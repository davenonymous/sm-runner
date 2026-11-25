// Copyright 2026 davenonymous
//
// A sent request on its way to completion: a faked answer due on a frame or a
// transfer on the network, then its headers, its body (at once, or in chunks
// of one per frame when streamed) and its completion, with the faked
// answers' timeouts on the simulated clock. Advance gives the events one at
// a time with the state they bring applied, so what an event's callbacks
// read is the request as it is right after that event; the caller fires
// them and stops when the request was released meanwhile.
#ifndef SMRUNNER_STEAMWORKS_PENDING_REQUEST_H
#define SMRUNNER_STEAMWORKS_PENDING_REQUEST_H

#include <stddef.h>

#include <memory>
#include <optional>
#include <string>

#include "answers.h"
#include "passthrough.h"

namespace steamworks {

struct PendingEvent {
	enum class Kind { Headers, Data, Completed };
	Kind kind = Kind::Completed;
	bool fromNetwork = false;   // Headers: the network's answer, which is recorded
	size_t offset = 0;          // Data: the chunk delivered
	size_t bytes = 0;
	bool failure = false;       // Completed: Steam's call failed (bFailure)
	bool successful = false;    // Completed: with a status (bRequestSuccessful)
	std::string error;          // Completed: why the network gave no answer
};

// A chunk of a streamed body, as a data callback reports it.
struct StreamChunk {
	size_t offset = 0;
	size_t bytes = 0;
};

// What a request was given before it was sent.
struct RequestTiming {
	float sentAt = 0.0f;                     // simulated time
	unsigned int absoluteTimeoutMs = 0;      // 0: none
	unsigned int activityTimeoutSeconds = 0; // 0: none
	bool streaming = false;
};

class PendingRequest {
 public:
	// A faked answer, due on dueFrame.
	PendingRequest(Answer answer, int dueFrame, RequestTiming timing);
	// A transfer of passthrough, cancelled when the request goes first.
	PendingRequest(Passthrough& passthrough, std::shared_ptr<Transfer> transfer, RequestTiming timing);
	~PendingRequest();
	PendingRequest(const PendingRequest&) = delete;
	PendingRequest& operator=(const PendingRequest&) = delete;

	// The next event due at the simulated time now on frame, or none until a
	// later call; call it until none on every frame.
	std::optional<PendingEvent> Advance(float now, int frame);
	// Drops a transfer still on the network.
	void Cancel();

	// No completion yet and no body delivered (Steam's queue holds it).
	bool IsWaiting() const { return phase_ == Phase::Waiting || phase_ == Phase::Answered; }
	bool IsDone() const { return phase_ == Phase::Done; }
	bool IsStreaming() const { return timing_.streaming; }
	bool HasResponse() const { return hasResponse_; }
	bool HasWholeBody() const { return hasResponse_ && phase_ == Phase::Done; }
	bool TimedOut() const { return timedOut_; }
	int Status() const { return status_; }
	const NameValues& ResponseHeaders() const { return responseHeaders_; }
	const std::string& Body() const { return body_; }
	size_t Delivered() const { return delivered_; }
	// The chunk of the latest data event, the only one a streamed body can
	// be read by; none before the first.
	std::optional<StreamChunk> LastChunk() const { return lastChunk_; }
	float ProgressPercent() const;
	// The network's answer, for recording; null for a faked request.
	const TransferResult* NetworkResult() const { return transfer_ ? &transfer_->result : nullptr; }

 private:
	enum class Phase { Waiting, Answered, Streaming, Done };

	std::optional<PendingEvent> Deliver(const Answer& answer, bool fromNetwork);
	std::optional<PendingEvent> DeliverNetwork();
	std::optional<PendingEvent> NextChunk(int frame);
	PendingEvent Complete(bool failure, bool successful);
	PendingEvent CompleteTimedOut();
	bool IsPastDeadline(float now, bool dataFlowing) const;

	RequestTiming timing_;
	Answer answer_;                          // a faked answer, until it is due
	int dueFrame_ = 0;
	Passthrough* passthrough_ = nullptr;
	std::shared_ptr<Transfer> transfer_;
	Phase phase_ = Phase::Waiting;
	int chunkFrame_ = 0;                     // the frame of the last chunk
	bool hasResponse_ = false;
	bool timedOut_ = false;
	int status_ = 0;
	NameValues responseHeaders_;
	std::string body_;
	size_t delivered_ = 0;
	std::optional<StreamChunk> lastChunk_;
};

}  // namespace steamworks

#endif
