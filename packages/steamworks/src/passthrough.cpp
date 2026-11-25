// Copyright 2026 davenonymous
#include "passthrough.h"

#include <ctype.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <optional>

#include <curl/curl.h>

#include "url.h"

namespace steamworks {

// The CA bundles of the common distributions; Mbed TLS has none of its own.
static const char* FindCaBundle() {
	static const char* const kBundles[] = {
		"/etc/ssl/certs/ca-certificates.crt",   // Debian, Ubuntu, Alpine
		"/etc/pki/tls/certs/ca-bundle.crt",     // Fedora, RHEL
		"/etc/ssl/cert.pem",                    // Alpine, macOS-style
		"/etc/ssl/ca-bundle.pem",               // openSUSE
	};
	for (const char* bundle : kBundles) {
		if (access(bundle, R_OK) == 0) {
			return bundle;
		}
	}
	return nullptr;
}

// The most redirects one transfer follows.
static constexpr int kMaxRedirects = 10;

// One running transfer, the hop it is at and the libcurl state of that hop.
struct Running {
	std::shared_ptr<Transfer> transfer;
	std::string url;             // of the current hop
	Method method = Method::Get;
	std::string body;
	NameValues headers;
	int redirects = 0;
	CURL* easy = nullptr;
	curl_slist* headerList = nullptr;
};

static size_t OnBody(char* data, size_t size, size_t count, void* user) {
	Running* running = static_cast<Running*>(user);
	size_t bytes = size * count;
	running->transfer->result.body.append(data, bytes);
	running->transfer->received = static_cast<long long>(running->transfer->result.body.size());
	return bytes;
}

// Header lines of the last response only: a 100 Continue's headers are
// dropped when the next status line comes, and so is its Content-Length.
static size_t OnHeader(char* data, size_t size, size_t count, void* user) {
	Running* running = static_cast<Running*>(user);
	size_t bytes = size * count;
	std::string line(data, bytes);
	while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
		line.pop_back();
	}
	if (line.compare(0, 5, "HTTP/") == 0) {
		running->transfer->result.headers.clear();
		running->transfer->expected = 0;
		return bytes;
	}
	size_t colon = line.find(':');
	if (colon == std::string::npos || colon == 0) {
		return bytes;
	}
	std::string value = line.substr(colon + 1);
	value.erase(0, value.find_first_not_of(" \t"));
	running->transfer->result.headers.emplace_back(line.substr(0, colon), value);
	if (strcasecmp(line.substr(0, colon).c_str(), "Content-Length") == 0) {
		running->transfer->expected = atoll(value.c_str());
	}
	return bytes;
}

// A new easy handle for the running transfer's current hop; redirects are
// not followed by libcurl.
static void BeginHop(Running& running, const char* caBundle) {
	const TransferRequest& request = running.transfer->request;
	TransferResult& result = running.transfer->result;
	result.headers.clear();
	result.body.clear();
	running.transfer->received = 0;
	running.transfer->expected = 0;
	CURL* easy = curl_easy_init();
	running.easy = easy;
	curl_easy_setopt(easy, CURLOPT_URL, running.url.c_str());
	curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "http,https");
	curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 0L);
	curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, OnBody);
	curl_easy_setopt(easy, CURLOPT_WRITEDATA, &running);
	curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, OnHeader);
	curl_easy_setopt(easy, CURLOPT_HEADERDATA, &running);
	curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, request.verifyCertificate ? 1L : 0L);
	curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, request.verifyCertificate ? 2L : 0L);
	if (caBundle) {
		curl_easy_setopt(easy, CURLOPT_CAINFO, caBundle);
	}
	if (request.absoluteTimeoutMs > 0) {
		curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, static_cast<long>(request.absoluteTimeoutMs));
	}
	if (request.activityTimeoutSeconds > 0) {
		curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, 1L);
		curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME, static_cast<long>(request.activityTimeoutSeconds));
	}
	for (const auto& header : running.headers) {
		running.headerList = curl_slist_append(running.headerList, (header.first + ": " + header.second).c_str());
	}
	// libcurl's own Expect: 100-continue would only slow uploads down.
	running.headerList = curl_slist_append(running.headerList, "Expect:");
	bool sendsBody = running.method != Method::Get && running.method != Method::Head &&
	                 (SendsParametersInBody(running.method) || !running.body.empty());
	// A body makes libcurl add a form Content-Type the request did not set;
	// the request goes out with the headers the request log shows.
	if (sendsBody && !FindHeader(running.headers, "Content-Type")) {
		running.headerList = curl_slist_append(running.headerList, "Content-Type:");
	}
	curl_easy_setopt(easy, CURLOPT_HTTPHEADER, running.headerList);
	switch (running.method) {
		case Method::Get:
			break;
		case Method::Head:
			curl_easy_setopt(easy, CURLOPT_NOBODY, 1L);
			break;
		default:
			curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, MethodName(running.method));
			// POST, PUT and PATCH always send a body (Content-Length: 0
			// when empty), DELETE and OPTIONS only one they were given.
			if (sendsBody) {
				curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(running.body.size()));
				curl_easy_setopt(easy, CURLOPT_POSTFIELDS, running.body.data());
			}
			break;
	}
}

static void EndHop(Running& running) {
	curl_easy_cleanup(running.easy);
	curl_slist_free_all(running.headerList);
	running.easy = nullptr;
	running.headerList = nullptr;
}

static void Finish(Running& running, CURLcode code, const std::string& error = std::string()) {
	TransferResult& result = running.transfer->result;
	long status = 0;
	if (running.easy) {
		curl_easy_getinfo(running.easy, CURLINFO_RESPONSE_CODE, &status);
	}
	result.status = static_cast<int>(status);
	result.answered = code == CURLE_OK && status > 0 && error.empty();
	result.timedOut = code == CURLE_OPERATION_TIMEDOUT;
	if (!result.answered) {
		result.error = error.empty() ? curl_easy_strerror(code) : error;
		result.status = 0;
		result.headers.clear();
		result.body.clear();
	}
	if (running.easy) {
		EndHop(running);
	}
	running.transfer->done = true;
}

static bool IsRedirect(long status) {
	return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

static bool IsAllowedHost(const TransferRequest& request, const std::string& host) {
	return std::any_of(request.allowedHosts.begin(), request.allowedHosts.end(),
	                   [&host](const std::string& pattern) { return HostMatches(pattern, host); });
}

// Where a finished hop leads: empty when it is the answer, else the next
// URL; error says why the transfer cannot go on.
static std::string NextHop(Running& running, std::string* error) {
	long status = 0;
	curl_easy_getinfo(running.easy, CURLINFO_RESPONSE_CODE, &status);
	char* location = nullptr;
	curl_easy_getinfo(running.easy, CURLINFO_REDIRECT_URL, &location);
	if (!IsRedirect(status) || !location) {
		return std::string();
	}
	std::string next = location;
	std::optional<Url> parsed = ParseUrl(next);
	if (!parsed) {
		*error = "redirected to " + next + ", which is no http or https URL";
		return std::string();
	}
	if (!IsAllowedHost(running.transfer->request, parsed->host)) {
		*error = "redirected to " + next + ", whose host " + parsed->host + " is no allowed passthrough host";
		return std::string();
	}
	if (running.redirects >= kMaxRedirects) {
		*error = "more than " + std::to_string(kMaxRedirects) + " redirects";
		return std::string();
	}
	// As libcurl and browsers do: 303 turns everything but HEAD into GET,
	// 301 and 302 turn POST into GET; the body and its type go with it.
	bool toGet = (status == 303 && running.method != Method::Head) ||
	             ((status == 301 || status == 302) && running.method == Method::Post);
	if (toGet) {
		running.method = Method::Get;
		running.body.clear();
		running.headers.erase(std::remove_if(running.headers.begin(), running.headers.end(),
		                                     [](const std::pair<std::string, std::string>& header) {
			                                     return strcasecmp(header.first.c_str(), "Content-Type") == 0;
		                                     }),
		                      running.headers.end());
	}
	running.redirects++;
	return next;
}

Passthrough::Passthrough() : multi_(curl_multi_init()) {}

std::shared_ptr<Transfer> Passthrough::Start(TransferRequest request) {
	auto transfer = std::make_shared<Transfer>();
	transfer->request = std::move(request);
	std::lock_guard<std::mutex> lock(mutex_);
	if (!worker_.joinable()) {
		worker_ = std::thread(&Passthrough::Run, this);
	}
	queued_.push_back(transfer);
	curl_multi_wakeup(multi_);
	return transfer;
}

void Passthrough::Cancel(Transfer& transfer) {
	transfer.cancelled = true;
	curl_multi_wakeup(multi_);
}

void Passthrough::Run() {
	const char* caBundle = FindCaBundle();
	std::map<CURL*, std::unique_ptr<Running>> running;
	auto begin = [&](std::unique_ptr<Running> entry) {
		BeginHop(*entry, caBundle);
		curl_multi_add_handle(multi_, entry->easy);
		CURL* easy = entry->easy;
		running[easy] = std::move(entry);
	};
	for (;;) {
		std::vector<std::shared_ptr<Transfer>> starting;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (stopping_) {
				break;
			}
			starting.swap(queued_);
		}
		for (auto& transfer : starting) {
			auto entry = std::make_unique<Running>();
			entry->transfer = transfer;
			entry->url = transfer->request.url;
			entry->method = transfer->request.method;
			entry->body = transfer->request.body;
			entry->headers = transfer->request.headers;
			if (!caBundle && transfer->request.verifyCertificate && entry->url.compare(0, 8, "https://") == 0) {
				Finish(*entry, CURLE_OK, "no CA bundle found to verify the certificate");
				continue;
			}
			begin(std::move(entry));
		}
		int active = 0;
		curl_multi_perform(multi_, &active);
		int left = 0;
		while (CURLMsg* message = curl_multi_info_read(multi_, &left)) {
			if (message->msg != CURLMSG_DONE) {
				continue;
			}
			auto found = running.find(message->easy_handle);
			std::unique_ptr<Running> entry = std::move(found->second);
			running.erase(found);
			curl_multi_remove_handle(multi_, entry->easy);
			std::string error;
			std::string next = message->data.result == CURLE_OK ? NextHop(*entry, &error) : std::string();
			if (!error.empty() || next.empty()) {
				Finish(*entry, message->data.result, error);
				continue;
			}
			EndHop(*entry);
			entry->url = next;
			begin(std::move(entry));
		}
		for (auto it = running.begin(); it != running.end();) {
			if (!it->second->transfer->cancelled) {
				++it;
				continue;
			}
			curl_multi_remove_handle(multi_, it->first);
			Finish(*it->second, CURLE_ABORTED_BY_CALLBACK);
			it = running.erase(it);
		}
		// Woken early by curl_multi_wakeup; libcurl's own timers shorten it.
		curl_multi_poll(multi_, nullptr, 0, 1000, nullptr);
	}
	for (auto& entry : running) {
		curl_multi_remove_handle(multi_, entry.first);
		Finish(*entry.second, CURLE_ABORTED_BY_CALLBACK);
	}
}

void Passthrough::Stop() {
	{
		std::lock_guard<std::mutex> lock(mutex_);
		stopping_ = true;
	}
	curl_multi_wakeup(multi_);
	if (worker_.joinable()) {
		worker_.join();
	}
}

Passthrough::~Passthrough() {
	Stop();
	curl_multi_cleanup(multi_);
}

}  // namespace steamworks
