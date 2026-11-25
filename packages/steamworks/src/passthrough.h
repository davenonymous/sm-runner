// Copyright 2026 davenonymous
//
// Real HTTP for the requests the stand-in passes through: libcurl with
// Mbed TLS on a worker thread of its own, so the game frame never waits.
// The frame starts transfers and collects the finished ones; a transfer's
// progress is readable while it runs. Redirects are followed by the worker
// itself, each hop only to an allowed host: a redirect to another host fails
// the transfer, naming it. The worker sleeps in libcurl's poll and is woken
// at once when a transfer starts or is cancelled, or the worker stops.
#ifndef SMRUNNER_STEAMWORKS_PASSTHROUGH_H
#define SMRUNNER_STEAMWORKS_PASSTHROUGH_H

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <curl/curl.h>

#include "answers.h"

namespace steamworks {

struct TransferRequest {
	Method method = Method::Get;
	std::string url;
	NameValues headers;          // User-Agent and Content-Type included
	std::string body;            // sent for every method but GET and HEAD
	bool verifyCertificate = true;
	unsigned int absoluteTimeoutMs = 0;      // 0: none
	unsigned int activityTimeoutSeconds = 0; // 0: none
	std::vector<std::string> allowedHosts;   // host patterns every hop must match
};

// What the last hop answered.
struct TransferResult {
	bool answered = false;       // the server answered (any status)
	bool timedOut = false;
	std::string error;           // why not, when not answered
	int status = 0;
	NameValues headers;
	std::string body;
};

// Shared between the frame and the worker: progress while it runs, the
// result once done.
struct Transfer {
	TransferRequest request;
	std::atomic<bool> cancelled{false};
	std::atomic<bool> done{false};
	std::atomic<long long> received{0};   // of the current hop
	std::atomic<long long> expected{0};   // of the current hop; 0 while unknown
	TransferResult result;                // the worker's until done
};

class Passthrough {
 public:
	// Needs libcurl started (curl_global_init).
	Passthrough();
	~Passthrough();
	Passthrough(const Passthrough&) = delete;
	Passthrough& operator=(const Passthrough&) = delete;

	// Queues a transfer; the worker starts with the first one.
	std::shared_ptr<Transfer> Start(TransferRequest request);
	// The transfer is dropped by the worker, which is woken for it.
	void Cancel(Transfer& transfer);
	// Stops the worker; running transfers are abandoned.
	void Stop();

 private:
	void Run();

	// curl_multi_wakeup is the one call of it from other threads.
	CURLM* multi_;
	std::mutex mutex_;
	std::vector<std::shared_ptr<Transfer>> queued_;
	bool stopping_ = false;
	std::thread worker_;
};

}  // namespace steamworks

#endif
