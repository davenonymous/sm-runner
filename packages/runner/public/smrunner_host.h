// Copyright 2026 davenonymous
//
// The interface smrunner offers the extensions it ships (the SteamWorks
// stand-in) through SourceMod's share system: what the command line asked
// of them and the runner's notes. Extensions request it by name and version
// and fail to load without it, so they run only inside smrunner. Only plain
// C types cross it: the runner and each extension link their own libstdc++.
#ifndef SMRUNNER_PUBLIC_SMRUNNER_HOST_H
#define SMRUNNER_PUBLIC_SMRUNNER_HOST_H

#include <stddef.h>

#include <IShareSys.h>

#define SMINTERFACE_SMRUNNERHOST_NAME "ISMRunnerHost"
#define SMINTERFACE_SMRUNNERHOST_VERSION 1

namespace smrunner {

class ISMRunnerHost : public SourceMod::SMInterface {
 public:
	const char* GetInterfaceName() override { return SMINTERFACE_SMRUNNERHOST_NAME; }
	unsigned int GetInterfaceVersion() override { return SMINTERFACE_SMRUNNERHOST_VERSION; }

	// --steamworks [DIR]: the absolute folder of steamworks.json and its body
	// files, or "" when none was given.
	virtual const char* SteamWorksFixtureDir() = 0;
	// --steamworks-passthrough HOST, in the order given.
	virtual size_t SteamWorksPassthroughHostCount() = 0;
	virtual const char* SteamWorksPassthroughHost(size_t index) = 0;
	// --steamworks-record DIR: the absolute folder, or "" without the option.
	virtual const char* SteamWorksRecordDir() = 0;

	// Frames started so far; during a frame, its own number.
	virtual int FrameNumber() = 0;

	// A runner note (printed with --verbose), one line without its break.
	virtual void Note(const char* text) = 0;
	// A runner warning (printed unless --quiet), one line without its break.
	virtual void Warn(const char* text) = 0;
};

}  // namespace smrunner

#endif
