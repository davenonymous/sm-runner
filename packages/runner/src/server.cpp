// Copyright 2026 davenonymous
#include "server.h"

#include <dirent.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>

#include "core/core.h"
#include "diagnostics.h"

using namespace SourceMod;

namespace smrunner {

std::string Install::libDir() const {
#if defined(__x86_64__)
	return home + "/lib/x64";
#else
	return home + "/lib/x86";
#endif
}

Install Install::Locate(const std::string& override) {
	if (!override.empty()) {
		return Install{AbsolutePath(override)};
	}
	char exe[PATH_MAX];
	ssize_t length = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (length <= 0) {
		Fatal("could not find the runner's own path");
	}
	exe[length] = '\0';
	std::string bin = exe;
	bin = bin.substr(0, bin.rfind('/'));
	return Install{bin.substr(0, bin.rfind('/'))};
}

Server::Server(ServerSpec spec, const Install& install) : spec_(std::move(spec)), install_(install) {
	SandboxSpec sandbox;
	sandbox.shareDir = install_.shareDir();
	sandbox.libDir = install_.libDir();
	sandbox.root = spec_.root;
	sandbox.extensionDirs = spec_.extensionDirs;
	sandbox.extensionFiles = spec_.extensionFiles;
	sandbox.steamworks = spec_.steamworks.enabled;
	sandbox.plugins = spec_.plugins;
	sandbox.files = spec_.files;
	sandbox_ = std::make_unique<Sandbox>(sandbox);
	if (spec_.root.IsKept()) {
		Warn("the server folder is kept in %s", sandbox_->Root().c_str());
	}
	// srcds runs in the folder that holds the game folder.
	if (chdir(sandbox_->Root().c_str()) != 0) {
		Fatal("could not enter %s", sandbox_->Root().c_str());
	}

	EngineConfig config;
	config.gameDir = sandbox_->GameDir();
	config.tickInterval = spec_.tickInterval;
	config.maxClients = spec_.maxPlayers;
	engine_ = std::make_unique<Engine>(config);
	engine_->output().SetEcho(spec_.echo);
	engine_->DeclareMap(spec_.map);
	for (const std::string& map : spec_.maps) {
		engine_->DeclareMap(map);
	}
	for (const auto& cvar : spec_.cvars) {
		engine_->console().SetConVarValue(cvar.first, cvar.second);
	}
}

Server::~Server() {
	Stop();
}

void Server::Start(TestSession& session) {
	CoreOptions core;
	core.libDir = install_.libDir();
	core.smDir = sandbox_->SourceModDir();
	core.gameDir = sandbox_->GameDir();
	core.coreConfig = {{"DisableAutoUpdate", "yes"}};
	core.coreConfig.insert(core.coreConfig.end(), spec_.coreConfig.begin(), spec_.coreConfig.end());
	core.plugins = sandbox_->PluginNames();
	core.jit = spec_.jit;
	core.steamworks = spec_.steamworks;
	Core& sourcemod = Core::Get();
	sourcemod.Boot(core, *engine_);
	session_ = &session;
	session_->Attach();
	std::string error = sourcemod.ChangeMap(spec_.map);
	if (!error.empty()) {
		Fatal("%s", error.c_str());
	}
	for (const std::string& name : engine_->console().PendingConVarNames()) {
		Warn("--cvar %s: no cvar of that name was made by the time the configs were executed", name.c_str());
	}
}

void Server::RunFrames(int count) {
	Core::Get().RunFrames(count);
}

void Server::Stop() {
	if (!session_) {
		return;
	}
	Core::Get().Shutdown();
	session_->Detach();
	session_ = nullptr;
}

// The build turns -Wswitch off; here every status must have its name.
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wswitch"
static const char* StatusName(PluginStatus status) {
	switch (status) {
		case Plugin_Running: return "running";
		case Plugin_Paused: return "paused";
		case Plugin_Error: return "error";
		case Plugin_Loaded: return "loaded";
		case Plugin_Failed: return "failed";
		case Plugin_Created: return "created";
		case Plugin_Uncompiled: return "uncompiled";
		case Plugin_BadLoad: return "bad load";
		case Plugin_Evicted: return "evicted";
	}
	Fatal("SourceMod reported the plugin status %d, which smrunner does not know", static_cast<int>(status));
}
#pragma GCC diagnostic pop

// Every plugin SourceMod lists that does not run, and the given plugins that
// failed in the current map's ordered pass and are not listed (SourceMod
// lists no plugin whose first load failed).
std::vector<std::string> Server::PluginProblems() const {
	std::vector<std::string> problems;
	std::vector<std::string> listed;
	IPluginIterator* iterator = scripts->GetPluginIterator();
	for (; iterator->MorePlugins(); iterator->NextPlugin()) {
		IPlugin* plugin = iterator->GetPlugin();
		listed.push_back(plugin->GetFilename());
		if (plugin->GetStatus() == Plugin_Running) {
			continue;
		}
		problems.push_back(std::string(plugin->GetFilename()) + " (" + StatusName(plugin->GetStatus()) + ")");
	}
	iterator->Release();
	for (const Core::PluginLoadFailure& failure : Core::Get().OrderedLoadFailures()) {
		if (std::find(listed.begin(), listed.end(), failure.file) == listed.end()) {
			problems.push_back(failure.file + " (failed to load: " + failure.error + ")");
		}
	}
	return problems;
}

std::string Server::ErrorLog() const {
	std::string logs = sandbox_->SourceModDir() + "/logs";
	std::string text;
	DIR* dir = opendir(logs.c_str());
	if (!dir) {
		return text;
	}
	while (dirent* entry = readdir(dir)) {
		if (strncmp(entry->d_name, "errors_", 7) != 0) {
			continue;
		}
		std::ifstream in(logs + "/" + entry->d_name);
		std::stringstream content;
		content << in.rdbuf();
		text += content.str();
	}
	closedir(dir);
	return text;
}

}  // namespace smrunner
