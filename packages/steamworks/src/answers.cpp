// Copyright 2026 davenonymous
#include "answers.h"

#include <stdlib.h>
#include <strings.h>

#include <fstream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace steamworks {

using nlohmann::json;

static const char* const kMethodNames[kMethodCount] = {"", "GET", "HEAD", "POST", "PUT", "DELETE", "OPTIONS", "PATCH"};

std::optional<Method> MethodFromName(const std::string& name) {
	for (int i = 1; i < kMethodCount; i++) {
		if (strcasecmp(name.c_str(), kMethodNames[i]) == 0) {
			return static_cast<Method>(i);
		}
	}
	return std::nullopt;
}

const char* MethodName(Method method) {
	int index = static_cast<int>(method);
	return index > 0 && index < kMethodCount ? kMethodNames[index] : "INVALID";
}

bool SendsParametersInBody(Method method) {
	return method == Method::Post || method == Method::Put || method == Method::Patch;
}

const std::string* FindHeader(const NameValues& headers, const std::string& name) {
	for (const auto& header : headers) {
		if (strcasecmp(header.first.c_str(), name.c_str()) == 0) {
			return &header.second;
		}
	}
	return nullptr;
}

void SetHeader(NameValues& headers, const std::string& name, const std::string& value) {
	for (auto& header : headers) {
		if (strcasecmp(header.first.c_str(), name.c_str()) == 0) {
			header.second = value;
			return;
		}
	}
	headers.emplace_back(name, value);
}

std::string ContentTypeFor(const std::string& fileName) {
	static const std::pair<const char*, const char*> kTypes[] = {
		{".json", "application/json"}, {".txt", "text/plain"}, {".html", "text/html"}, {".htm", "text/html"},
		{".xml", "application/xml"},   {".cfg", "text/plain"}, {".vdf", "text/plain"},
	};
	size_t slash = fileName.rfind('/');
	size_t dot = fileName.rfind('.');
	if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
		std::string extension = fileName.substr(dot);
		for (const auto& type : kTypes) {
			if (strcasecmp(extension.c_str(), type.first) == 0) {
				return type.second;
			}
		}
	}
	return "application/octet-stream";
}

static std::string ReadWholeFile(const std::string& path) {
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		throw std::runtime_error("cannot read " + path);
	}
	std::ostringstream content;
	content << in.rdbuf();
	return content.str();
}

// One entry of the mapping file; throws with what is wrong.
static AnswerRule ParseEntry(const json& entry, const std::string& dir) {
	if (!entry.is_object()) {
		throw std::runtime_error("is not an object");
	}
	static const char* const kKeys[] = {"method", "url", "status", "headers", "body_file", "body", "delay_frames", "fail"};
	for (const auto& item : entry.items()) {
		bool known = false;
		for (const char* key : kKeys) {
			known = known || item.key() == key;
		}
		if (!known) {
			throw std::runtime_error("has the unknown key \"" + item.key() + "\"");
		}
	}
	AnswerRule rule;
	if (!entry.contains("url") || !entry["url"].is_string() || entry["url"].get<std::string>().empty()) {
		throw std::runtime_error("needs a \"url\" string");
	}
	rule.url = entry["url"].get<std::string>();
	if (entry.contains("method")) {
		if (!entry["method"].is_string()) {
			throw std::runtime_error("\"method\" is not a string");
		}
		std::optional<Method> method = MethodFromName(entry["method"].get<std::string>());
		if (!method) {
			throw std::runtime_error("has the unknown method \"" + entry["method"].get<std::string>() + "\"");
		}
		rule.method = *method;
	}
	if (entry.contains("status")) {
		if (!entry["status"].is_number_integer() || entry["status"].get<int>() < 100 || entry["status"].get<int>() > 599) {
			throw std::runtime_error("\"status\" is not an HTTP status (100 to 599)");
		}
		rule.answer.status = entry["status"].get<int>();
	}
	if (entry.contains("delay_frames")) {
		if (!entry["delay_frames"].is_number_integer() || entry["delay_frames"].get<int>() < 0) {
			throw std::runtime_error("\"delay_frames\" is not a whole number of 0 or more");
		}
		rule.answer.delayFrames = entry["delay_frames"].get<int>();
	}
	if (entry.contains("fail")) {
		std::string fail = entry["fail"].is_string() ? entry["fail"].get<std::string>() : "";
		if (fail != "io" && fail != "timeout") {
			throw std::runtime_error("\"fail\" is neither \"io\" nor \"timeout\"");
		}
		rule.answer.failure = fail == "io" ? Failure::Io : Failure::Timeout;
	}
	if (entry.contains("body") && entry.contains("body_file")) {
		throw std::runtime_error("has both \"body\" and \"body_file\"");
	}
	std::string contentType = "application/octet-stream";
	if (entry.contains("body")) {
		if (!entry["body"].is_string()) {
			throw std::runtime_error("\"body\" is not a string");
		}
		rule.answer.body = entry["body"].get<std::string>();
	}
	if (entry.contains("body_file")) {
		if (!entry["body_file"].is_string() || entry["body_file"].get<std::string>().empty()) {
			throw std::runtime_error("\"body_file\" is not a file name");
		}
		std::string file = entry["body_file"].get<std::string>();
		rule.answer.body = ReadWholeFile(file[0] == '/' ? file : dir + "/" + file);
		contentType = ContentTypeFor(file);
	}
	if (entry.contains("headers")) {
		if (!entry["headers"].is_object()) {
			throw std::runtime_error("\"headers\" is not an object of names and strings");
		}
		for (const auto& header : entry["headers"].items()) {
			if (!header.value().is_string()) {
				throw std::runtime_error("the header \"" + header.key() + "\" is not a string");
			}
			rule.answer.headers.emplace_back(header.key(), header.value().get<std::string>());
		}
	}
	if (!FindHeader(rule.answer.headers, "Content-Type") && (entry.contains("body") || entry.contains("body_file"))) {
		rule.answer.headers.emplace_back("Content-Type", contentType);
	}
	return rule;
}

std::vector<AnswerRule> ReadMappingFile(const std::string& dir) {
	std::string path = dir + "/steamworks.json";
	std::ifstream probe(path);
	if (!probe) {
		return {};
	}
	json document = json::parse(ReadWholeFile(path), nullptr, false);
	if (document.is_discarded() || !document.is_array()) {
		throw std::runtime_error(path + " is not a JSON list of entries");
	}
	std::vector<AnswerRule> rules;
	for (size_t i = 0; i < document.size(); i++) {
		try {
			rules.push_back(ParseEntry(document[i], dir));
		} catch (const std::exception& error) {
			throw std::runtime_error(path + ": entry " + std::to_string(i + 1) + " " + error.what());
		}
	}
	return rules;
}

const AnswerRule* FindRule(const std::vector<AnswerRule>& rules, Method method, const std::string& url) {
	for (const AnswerRule& rule : rules) {
		if (rule.method == method && UrlMatches(rule.url, url)) {
			return &rule;
		}
	}
	return nullptr;
}

// "bytes=first-last", "bytes=first-" or "bytes=-suffix" against a body of
// size; false for anything else (several ranges included), which is then
// answered whole. satisfiable tells whether the range lies in the body.
static bool ParseRange(const std::string& header, size_t size, size_t* first, size_t* last, bool* satisfiable) {
	if (header.compare(0, 6, "bytes=") != 0 || header.find(',') != std::string::npos) {
		return false;
	}
	std::string spec = header.substr(6);
	size_t dash = spec.find('-');
	if (dash == std::string::npos) {
		return false;
	}
	std::string from = spec.substr(0, dash), to = spec.substr(dash + 1);
	auto number = [](const std::string& text, unsigned long long* value) {
		if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) {
			return false;
		}
		*value = strtoull(text.c_str(), nullptr, 10);
		return true;
	};
	unsigned long long a = 0, b = 0;
	if (from.empty()) {
		if (!number(to, &b) || b == 0) {
			return false;
		}
		*satisfiable = size > 0;
		*first = b >= size ? 0 : size - b;
		*last = size - 1;
		return true;
	}
	if (!number(from, &a) || (!to.empty() && (!number(to, &b) || b < a))) {
		return false;
	}
	*satisfiable = a < size;
	*first = a;
	*last = to.empty() || b >= size ? size - 1 : b;
	return true;
}

Answer AnswerForRequest(const Answer& answer, Method method, const std::string* rangeHeader) {
	Answer result = answer;
	size_t first = 0, last = 0;
	bool satisfiable = false;
	if (rangeHeader && answer.failure == Failure::None && answer.status == 200 && !answer.body.empty() &&
	    ParseRange(*rangeHeader, answer.body.size(), &first, &last, &satisfiable)) {
		std::string total = std::to_string(answer.body.size());
		if (satisfiable) {
			result.status = 206;
			result.body = answer.body.substr(first, last - first + 1);
			SetHeader(result.headers, "Content-Range",
			          "bytes " + std::to_string(first) + "-" + std::to_string(last) + "/" + total);
		} else {
			result.status = 416;
			result.body.clear();
			SetHeader(result.headers, "Content-Range", "bytes */" + total);
		}
	}
	// A HEAD rule may give the length of a body it has no file of (as a
	// recorded HEAD answer does); that length is the answer's.
	bool headLengthGiven = method == Method::Head && FindHeader(answer.headers, "Content-Length");
	if (!headLengthGiven) {
		SetHeader(result.headers, "Content-Length", std::to_string(result.body.size()));
	}
	if (method == Method::Head) {
		result.body.clear();
	}
	return result;
}

}  // namespace steamworks
