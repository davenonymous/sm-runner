// Copyright 2026 davenonymous
#include "pending_request.h"

#include <algorithm>

namespace steamworks {

// Chunks of a streamed answer, one per frame.
static constexpr size_t kStreamChunk = 64 * 1024;

PendingRequest::PendingRequest(Answer answer, int dueFrame, RequestTiming timing)
	: timing_(timing), answer_(std::move(answer)), dueFrame_(dueFrame) {}

PendingRequest::PendingRequest(Passthrough& passthrough, std::shared_ptr<Transfer> transfer, RequestTiming timing)
	: timing_(timing), passthrough_(&passthrough), transfer_(std::move(transfer)) {}

PendingRequest::~PendingRequest() {
	Cancel();
}

void PendingRequest::Cancel() {
	if (transfer_ && !transfer_->done) {
		passthrough_->Cancel(*transfer_);
	}
}

// A faked request times out on the simulated clock: the absolute timeout
// always, the activity timeout while no data flows.
bool PendingRequest::IsPastDeadline(float now, bool dataFlowing) const {
	float deadline = -1.0f;
	auto earlier = [&deadline](float candidate) {
		deadline = deadline < 0.0f ? candidate : std::min(deadline, candidate);
	};
	if (timing_.absoluteTimeoutMs > 0) {
		earlier(timing_.sentAt + timing_.absoluteTimeoutMs / 1000.0f);
	}
	if (timing_.activityTimeoutSeconds > 0 && !dataFlowing) {
		earlier(timing_.sentAt + static_cast<float>(timing_.activityTimeoutSeconds));
	}
	return deadline >= 0.0f && now >= deadline;
}

PendingEvent PendingRequest::Complete(bool failure, bool successful) {
	phase_ = Phase::Done;
	PendingEvent event;
	event.kind = PendingEvent::Kind::Completed;
	event.failure = failure;
	event.successful = successful;
	return event;
}

PendingEvent PendingRequest::CompleteTimedOut() {
	timedOut_ = true;
	hasResponse_ = false;
	return Complete(false, false);
}

std::optional<PendingEvent> PendingRequest::Deliver(const Answer& answer, bool fromNetwork) {
	if (answer.failure == Failure::Io) {
		return Complete(true, false);
	}
	if (answer.failure == Failure::Timeout) {
		return CompleteTimedOut();
	}
	hasResponse_ = true;
	status_ = answer.status;
	responseHeaders_ = answer.headers;
	body_ = answer.body;
	phase_ = Phase::Answered;
	PendingEvent event;
	event.kind = PendingEvent::Kind::Headers;
	event.fromNetwork = fromNetwork;
	return event;
}

std::optional<PendingEvent> PendingRequest::DeliverNetwork() {
	const TransferResult& result = transfer_->result;
	if (!result.answered) {
		PendingEvent event = result.timedOut ? CompleteTimedOut() : Complete(false, false);
		event.error = result.error;
		return event;
	}
	// The headers as the server sent them, as Steam passes them on: a HEAD
	// answer keeps the length of the body it did not send, and an answer
	// without Content-Length (chunked) gets none.
	Answer answer;
	answer.status = result.status;
	answer.headers = result.headers;
	answer.body = result.body;
	return Deliver(answer, true);
}

// One chunk per frame; the completion follows the last chunk on its frame.
std::optional<PendingEvent> PendingRequest::NextChunk(int frame) {
	if (chunkFrame_ != frame) {
		chunkFrame_ = frame;
		size_t bytes = std::min(kStreamChunk, body_.size() - delivered_);
		if (bytes > 0) {
			PendingEvent event;
			event.kind = PendingEvent::Kind::Data;
			event.offset = delivered_;
			event.bytes = bytes;
			lastChunk_ = StreamChunk{delivered_, bytes};
			delivered_ += bytes;
			return event;
		}
	}
	if (delivered_ == body_.size()) {
		return Complete(false, true);
	}
	return std::nullopt;
}

std::optional<PendingEvent> PendingRequest::Advance(float now, int frame) {
	switch (phase_) {
		case Phase::Waiting:
			if (transfer_) {
				return transfer_->done ? DeliverNetwork() : std::nullopt;
			}
			if (IsPastDeadline(now, false)) {
				return CompleteTimedOut();
			}
			return frame >= dueFrame_ ? Deliver(answer_, false) : std::nullopt;
		case Phase::Answered:
			if (!timing_.streaming) {
				delivered_ = body_.size();
				return Complete(false, true);
			}
			// The first chunk comes with the headers, before any timeout.
			phase_ = Phase::Streaming;
			chunkFrame_ = frame - 1;
			return NextChunk(frame);
		case Phase::Streaming:
			if (chunkFrame_ != frame && !transfer_ && IsPastDeadline(now, true)) {
				return CompleteTimedOut();
			}
			return NextChunk(frame);
		case Phase::Done:
			return std::nullopt;
	}
	return std::nullopt;
}

float PendingRequest::ProgressPercent() const {
	if (IsWaiting() && transfer_) {
		long long expected = transfer_->expected, received = transfer_->received;
		return expected > 0 ? std::min(100.0f, 100.0f * received / expected) : 0.0f;
	}
	if (IsWaiting() || !hasResponse_) {
		return 0.0f;
	}
	return body_.empty() ? 100.0f : 100.0f * delivered_ / body_.size();
}

}  // namespace steamworks
