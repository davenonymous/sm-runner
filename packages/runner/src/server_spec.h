// Copyright 2026 davenonymous
//
// What one simulated server is made of, decided from the command line before
// the server is built: the run's server, or one test's.
#ifndef SMRUNNER_SERVER_SPEC_H
#define SMRUNNER_SERVER_SPEC_H

#include <string>
#include <utility>
#include <vector>

#include "options.h"
#include "sandbox.h"
#include "testing/test.h"

namespace smrunner {

struct ServerSpec {
	SandboxRoot root;
	std::vector<std::string> plugins;                  // in load order; a test's plugin first
	std::vector<std::string> extensionDirs;
	std::vector<std::string> extensionFiles;
	std::vector<std::pair<std::string, std::string>> files;     // source, destination below tf/
	std::string map;
	std::vector<std::string> maps;                     // maps that exist besides the current one
	std::vector<std::pair<std::string, std::string>> cvars;
	std::vector<std::pair<std::string, std::string>> coreConfig;
	bool jit = true;
	float tickInterval = 0.015f;
	int maxPlayers = 24;
	bool echo = false;                                 // the console is printed as it is written
	SteamWorksOptions steamworks;                      // with absolute folders, the record folder made
};

// The run's server: every plugin in the order given, the console echoed
// unless --quiet.
ServerSpec ForRun(const Options& options, SandboxRoot root);

// One test's server: its test plugin first, so that its natives and mocks are
// there for the rest, then the plugins under test; the console echoed with
// --echo. The other test plugins are left out.
ServerSpec ForTest(const Options& options, const Test& test, SandboxRoot root);

}  // namespace smrunner

#endif
