// Copyright 2026 davenonymous
#include "core/runner_host.h"

#include "core/core.h"
#include "diagnostics.h"

namespace smrunner {

const char* RunnerHost::SteamWorksPassthroughHost(size_t index) {
	if (index >= steamworks_.passthroughHosts.size()) {
		Fatal("an extension asked for passthrough host %zu of %zu", index, steamworks_.passthroughHosts.size());
	}
	return steamworks_.passthroughHosts[index].c_str();
}

int RunnerHost::FrameNumber() {
	return Core::Get().FrameNumber();
}

void RunnerHost::Note(const char* text) {
	smrunner::Note("%s", text);
}

void RunnerHost::Warn(const char* text) {
	smrunner::Warn("%s", text);
}

}  // namespace smrunner
