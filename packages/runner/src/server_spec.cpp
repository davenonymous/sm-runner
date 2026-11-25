// Copyright 2026 davenonymous
#include "server_spec.h"

namespace smrunner {

// The stand-in reads its folders after the server entered the sandbox, so
// they are made absolute here, where relative paths still mean what the user
// meant.
static SteamWorksOptions WithAbsoluteFolders(SteamWorksOptions steamworks) {
	if (!steamworks.fixtureDir.empty()) {
		steamworks.fixtureDir = AbsolutePath(steamworks.fixtureDir);
	}
	if (!steamworks.recordDir.empty()) {
		MakeDirectories(steamworks.recordDir);
		steamworks.recordDir = AbsolutePath(steamworks.recordDir);
	}
	return steamworks;
}

static ServerSpec FromOptions(const Options& options, SandboxRoot root) {
	ServerSpec spec;
	spec.root = std::move(root);
	spec.extensionDirs = options.extensionDirs;
	spec.extensionFiles = options.extensionFiles;
	spec.files = options.files;
	spec.map = options.map;
	spec.maps = options.maps;
	spec.cvars = options.cvars;
	spec.coreConfig = options.coreConfig;
	spec.jit = options.jit;
	spec.tickInterval = options.tickInterval;
	spec.maxPlayers = options.maxPlayers;
	spec.steamworks = WithAbsoluteFolders(options.steamworks);
	return spec;
}

ServerSpec ForRun(const Options& options, SandboxRoot root) {
	ServerSpec spec = FromOptions(options, std::move(root));
	spec.plugins = options.plugins;
	spec.echo = options.verbosity != Verbosity::Quiet;
	return spec;
}

ServerSpec ForTest(const Options& options, const Test& test, SandboxRoot root) {
	ServerSpec spec = FromOptions(options, std::move(root));
	spec.plugins.push_back(test.plugin);
	spec.plugins.insert(spec.plugins.end(), options.plugins.begin(), options.plugins.end());
	spec.echo = options.echo;
	return spec;
}

}  // namespace smrunner
