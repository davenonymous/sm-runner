// Copyright 2026 davenonymous
//
// smrunner's SteamWorks stand-in: an extension named and filed like
// SteamWorks 1.2.167 that offers its natives and forwards on the simulated
// server. HTTP requests are answered from a test's routes, the mapping file
// and, for allowed hosts, the network; the game server's and its clients'
// Steam state is faked. It loads only inside smrunner (ISMRunnerHost).
#ifndef SMRUNNER_STEAMWORKS_EXTENSION_H
#define SMRUNNER_STEAMWORKS_EXTENSION_H

#include <smsdk_ext.h>
#include <smrunner_host.h>

#include <string>

namespace steamworks {

class StandIn : public SDKExtension {
 public:
	bool SDK_OnLoad(char* error, size_t maxlength, bool late) override;
	void SDK_OnUnload() override;
};

// What the runner offers the stand-in.
smrunner::ISMRunnerHost* Host();

// A line on the server console, "[SteamWorks] " in front.
void ConsoleLine(const std::string& text);

// The runner's frame number: frames started since SourceMod started, during
// a frame its own number.
int CurrentFrame();

}  // namespace steamworks

#endif
