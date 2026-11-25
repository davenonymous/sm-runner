// Copyright 2026 davenonymous
//
// The command line, parsed once into Options; everything after this works
// on the parsed values.
#ifndef SMRUNNER_OPTIONS_H
#define SMRUNNER_OPTIONS_H

#include <string>
#include <utility>
#include <vector>

#include "diagnostics.h"
#include "terminal.h"

namespace smrunner {

enum class Mode { Run, Test, List };

// --steamworks [DIR], --steamworks-passthrough HOST, --steamworks-record DIR.
struct SteamWorksOptions {
	bool enabled = false;                    // the stand-in is in the sandbox
	std::string fixtureDir;                  // holds steamworks.json; empty: none
	std::vector<std::string> passthroughHosts;
	std::string recordDir;                   // empty: nothing is recorded
};

struct Options {
	Mode mode = Mode::Run;
	Verbosity verbosity = Verbosity::Normal;
	ColorMode color = ColorMode::Auto;
	std::string home;                                  // the install prefix (bin/, lib/, share/)
	std::vector<std::string> plugins;                  // in load order; test mode: the plugins under test
	std::vector<std::string> testPlugins;              // test and list mode: the plugins whose tests run
	std::vector<std::string> extensionDirs;
	std::vector<std::string> extensionFiles;
	std::vector<std::pair<std::string, std::string>> files;     // source, destination below tf/
	std::vector<std::pair<std::string, std::string>> cvars;
	std::vector<std::pair<std::string, std::string>> coreConfig;
	std::vector<std::string> commands;
	std::vector<std::string> maps;                     // maps that exist besides the current one
	std::string map = "cp_badlands";
	int maxPlayers = 24;
	float tickInterval = 0.015f;
	int frames = 66;
	std::string sandbox;
	bool keepSandbox = false;
	bool jit = true;
	std::string filter;
	int timeoutSeconds = 30;
	std::string junitFile;
	bool echo = false;                                 // test mode: print the console of every test
	SteamWorksOptions steamworks;
};

// Parses argv; prints the usage and exits on --help, --version or an error.
Options ParseOptions(int argc, char** argv);

}  // namespace smrunner

#endif
