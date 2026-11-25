// Copyright 2026 davenonymous
//
// ISMRunnerHost (runner/public/smrunner_host.h): what the command line asked
// of the extensions the runner ships, offered through the share system.
#ifndef SMRUNNER_CORE_RUNNER_HOST_H
#define SMRUNNER_CORE_RUNNER_HOST_H

#include <smrunner_host.h>

#include "options.h"

namespace smrunner {

class RunnerHost : public ISMRunnerHost {
 public:
	explicit RunnerHost(const SteamWorksOptions& steamworks) : steamworks_(steamworks) {}

	const char* SteamWorksFixtureDir() override { return steamworks_.fixtureDir.c_str(); }
	size_t SteamWorksPassthroughHostCount() override { return steamworks_.passthroughHosts.size(); }
	const char* SteamWorksPassthroughHost(size_t index) override;
	const char* SteamWorksRecordDir() override { return steamworks_.recordDir.c_str(); }
	int FrameNumber() override;
	void Note(const char* text) override;
	void Warn(const char* text) override;

 private:
	SteamWorksOptions steamworks_;
};

}  // namespace smrunner

#endif
