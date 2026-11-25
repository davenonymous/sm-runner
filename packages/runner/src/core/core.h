// Copyright 2026 davenonymous
//
// The runner's SourceMod core: what SourceMod's engine-dependent core
// (sourcemod.2.tf2.so) does on a server, done against the simulated engine.
// It loads the SourcePawn VM and SourceMod's own logic library, hands the
// logic library the bridge it expects (CoreProvider), and drives the same
// sequence the real core drives from the engine's callbacks: startup, map
// start (LevelInit, ServerActivate, the configs), game frames, map end and
// shutdown.
#ifndef SMRUNNER_CORE_CORE_H
#define SMRUNNER_CORE_CORE_H

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/globals.h"
#include "engine/engine.h"
#include "options.h"

namespace smrunner {

class TimerSystem;
class PlayerManager;
class GameHelpers;
class UserMessages;
class ConVarManager;
class ConCmdManager;
class CoreConfig;
class NextMapManager;
class SourceModImpl;
class CoreProviderImpl;
class Debugger;
class RunnerHost;

struct CoreOptions {
	std::string libDir;       // holds sourcemod.logic.so and libsourcepawn.so
	std::string smDir;        // absolute path of addons/sourcemod in the sandbox
	std::string gameDir;      // absolute path of the mod folder (tf/)
	std::vector<std::pair<std::string, std::string>> coreConfig;   // core.cfg overrides
	std::vector<std::string> plugins;   // below plugins/, loaded first and in this order on the first map
	bool jit = true;
	int watchdogSeconds = 0;  // 0: no watchdog
	SteamWorksOptions steamworks;   // for the stand-in, through ISMRunnerHost; absolute folders
};

class Core {
 public:
	static Core& Get();

	// Loads the VM and the logic library and starts SourceMod the way
	// SourceModBase::StartSourceMod does. Fails loud.
	void Boot(const CoreOptions& options, Engine& engine);

	// The one way a map starts: the running map ends (LevelShutdown:
	// OnMapEnd, map change timers are killed), the new one loads (LevelInit:
	// plugins load (LoadPlugins), OnMapInit) and runs (ServerActivate:
	// OnMapStart, the configs buffered), and frames run until its configs
	// were executed (OnConfigsExecuted). Returns the error when they were not
	// within kConfigFrames frames, empty otherwise; a quit ends the wait
	// without one. Used for the first map, Runner.ChangeMap and changelevel.
	std::string ChangeMap(const std::string& map);
	static constexpr int kConfigFrames = 100;

	// One server frame: the engine's frame with IServerGameDLL::GameFrame
	// and Think, then the map change a changelevel asked for. Every frame
	// runs here; false, and nothing runs, once the server quit.
	bool RunFrame();
	// count frames, fewer when the server quits.
	void RunFrames(int count);
	// Frames until done() holds, at most limit of them; whether it held.
	bool RunFramesUntil(const std::function<bool()>& done, int limit);
	// Unloads plugins and extensions (SourceModBase::CloseSourceMod).
	void Shutdown();

	bool AreConfigsExecuted() const;

	// SourceMod's DoGlobalPluginLoads, at every map's LevelInit: on the first
	// map the given plugins (CoreOptions::plugins) load first, in their order,
	// then SourceMod's LoadAll loads every other plugin it finds and runs the
	// second pass; on later maps LoadAll alone loads what is not loaded, as on
	// a server. A given plugin whose load failed is not tried again by that
	// first LoadAll; it is not in SourceMod's plugin list on the first map
	// (OrderedLoadFailures has it) and LoadAll tries it again on the next.
	void LoadPlugins();
	struct PluginLoadFailure {
		std::string file;    // below plugins/
		std::string error;
	};
	// The given plugins that failed to load in the current map's ordered pass.
	const std::vector<PluginLoadFailure>& OrderedLoadFailures() const { return orderedLoadFailures_; }

	Engine& engine() { return *engine_; }
	TimerSystem& timers() { return *timers_; }
	PlayerManager& players() { return *players_; }
	ConVarManager& convars() { return *convars_; }
	ConCmdManager& commands() { return *commands_; }
	CoreConfig& config() { return *config_; }
	NextMapManager& nextMap() { return *nextMap_; }
	UserMessages& userMessages() { return *userMessages_; }
	SourceModImpl& sourcemod() { return *sourcemod_; }
	SourcePawn::ISourcePawnEnvironment* pawnEnvironment() { return pawnEnv_; }
	// The patched VM (patches/sourcepawn-mutable-natives.patch) calls the
	// listener after every change of a native's binding, by anyone; one
	// listener, null for none.
	using NativeBindingListener = void (*)(SourcePawn::IPluginRuntime* runtime, uint32_t index);
	void SetNativeBindingListener(NativeBindingListener listener) { setNativeBindingListener_(listener); }
	const CoreOptions& options() const { return options_; }
	// Frames started since SourceMod started; during a frame, its own number.
	int FrameNumber() const { return frameNumber_; }

	// A plugin error where the core has no plugin context at hand: the VM's
	// errors are its environment's, reported for the innermost plugin frame
	// running (an exception there when a native is being called), so any
	// plugin's context reports it. Fatal when no plugin is loaded.
	void ReportPluginError(const char* format, ...) __attribute__((format(printf, 2, 3)));

	// UTIL_ConsolePrint: one line on the server console.
	void ConsolePrint(const char* format, ...) __attribute__((format(printf, 2, 3)));

	// Whether a frame is being run; frames cannot be run inside one.
	bool IsInFrame() const { return frameDepth_ > 0; }

	// core/frame_hooks.cpp
	void AddFrameAction(SourceMod::FRAMEACTION action, void* data);
	void RunFrameHooks(bool simulating);
	void RequestInternalPush() { pendingInternalPush_ = true; }

	void SetMapStarted(bool started) { mapStarted_ = started; }
	bool IsMapStarted() const { return mapStarted_; }
	bool IsMapLoading() const { return mapLoading_; }

 private:
	Core();
	void LoadVm();
	void LoadLogic();
	// The ordered pass: the given plugins that failed, whose files are kept
	// from LoadAll's scan until it ran.
	std::vector<std::string> LoadGivenPluginsInOrder();
	void StartMap(const std::string& map);
	void EndMap();
	// Whether a map was loaded and has not ended (LevelInit came, LevelShutdown
	// did not).
	bool IsLevelLoaded() const { return engine_->IsMapRunning() || levelEndBarrier_; }
	void NotifyGlobalClasses(const std::function<void(SMGlobalClass*)>& call);

	CoreOptions options_;
	int frameNumber_ = 0;
	Engine* engine_ = nullptr;
	std::unique_ptr<TimerSystem> timers_;
	std::unique_ptr<PlayerManager> players_;
	std::unique_ptr<GameHelpers> gameHelpers_;
	std::unique_ptr<UserMessages> userMessages_;
	std::unique_ptr<ConVarManager> convars_;
	std::unique_ptr<ConCmdManager> commands_;
	std::unique_ptr<CoreConfig> config_;
	std::unique_ptr<NextMapManager> nextMap_;
	std::unique_ptr<SourceModImpl> sourcemod_;
	std::unique_ptr<CoreProviderImpl> provider_;
	std::unique_ptr<RunnerHost> runnerHost_;
	void* vmLibrary_ = nullptr;
	void (*setNativeBindingListener_)(NativeBindingListener listener) = nullptr;
	void* logicLibrary_ = nullptr;
	SourcePawn::ISourcePawnEnvironment* pawnEnv_ = nullptr;
	bool booted_ = false;
	bool givenPluginsLoaded_ = false;
	std::vector<PluginLoadFailure> orderedLoadFailures_;
	bool mapStarted_ = false;
	bool mapLoading_ = false;
	bool levelEndBarrier_ = false;
	int frameDepth_ = 0;
	bool pendingInternalPush_ = false;
	SourceMod::IForward* onMapInit_ = nullptr;
	SourceMod::IForward* onMapEnd_ = nullptr;
	std::vector<std::pair<SourceMod::FRAMEACTION, void*>> frameActions_;
};

}  // namespace smrunner

#endif
