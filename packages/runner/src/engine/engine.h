// Copyright 2026 davenonymous
//
// The simulated game server below SourceMod: the console, the engine's
// globals (gpGlobals), the simulated clock, the current map, the client
// slots and what was printed. Nothing in it is real time: a frame advances the clock by exactly
// one tick interval and nothing else moves it, so runs are deterministic.
// The engine alone writes its clock and its map state.
#ifndef SMRUNNER_ENGINE_ENGINE_H
#define SMRUNNER_ENGINE_ENGINE_H

#include <functional>
#include <string>
#include <vector>

#include "console.h"
#include "engine/clients.h"

namespace smrunner {

// The engine's CGlobalVars as far as SourceMod reads them.
struct EngineGlobals {
	float curtime = 0.0f;
	float frametime = 0.0f;
	float interval_per_tick = 0.015f;
	int tickcount = 0;
};

// Everything that reached the server console, kept for the tests.
class OutputLog {
 public:
	void Append(const char* text);
	const std::string& Text() const { return text_; }
	void Clear() { text_.clear(); }
	void SetEcho(bool echo) { echo_ = echo; }

	// While a capture is pushed, text also goes into its string
	// (ServerCommandEx); captures nest.
	void PushCapture(std::string* capture) { captures_.push_back(capture); }
	void PopCapture() { captures_.pop_back(); }

 private:
	std::string text_;
	bool echo_ = true;
	std::vector<std::string*> captures_;
};

// The engine's precache string tables.
enum class PrecacheTable { Model, Sound, Decal, Generic };

struct EngineConfig {
	std::string gameDir;   // absolute path of the mod folder (tf/)
	std::string gameFolderName = "tf";
	std::string gameDescription = "Team Fortress";
	float tickInterval = 0.015f;
	int maxClients = 24;
};

class Engine {
 public:
	explicit Engine(const EngineConfig& config);

	Console& console() { return console_; }
	const EngineGlobals& globals() const { return globals_; }
	ClientTable& clients() { return clients_; }
	OutputLog& output() { return output_; }
	const EngineConfig& config() const { return config_; }

	// Plat_FloatTime: seconds since the runner started, in ticks.
	double EngineTime() const { return engineTime_; }

	// One frame as the engine runs it: the command buffer, the clients'
	// changed userinfo (to the user settings observer), then the game's
	// frame (simulating while a map runs) with curtime at the frame's tick,
	// then the clock moves on by one tick interval.
	using GameFrame = std::function<void(bool simulating)>;
	void RunFrame(const GameFrame& gameFrame);

	// A map loads: the game clock starts again at tick 0, the precache
	// tables are made anew (empty), the map does not run yet.
	void BeginMap(const std::string& map);

	// Precaches a name for the map: its index in the table, from 1; a name
	// precached before keeps its index. Precaching lasts until the map ends.
	int Precache(PrecacheTable table, const std::string& name);
	bool IsPrecached(PrecacheTable table, const std::string& name) const;
	// The loaded map runs (ServerActivate): frames simulate.
	void ActivateMap() { mapRunning_ = true; }
	// The map ends (LevelShutdown): frames no longer simulate.
	void EndMap() { mapRunning_ = false; }
	const std::string& MapName() const { return map_; }
	bool IsMapRunning() const { return mapRunning_; }

	// IVEngineServer::ChangeLevel: a changelevel command, buffered.
	void ChangeLevel(const std::string& map) { console_.AppendToBuffer("changelevel " + map + "\n"); }
	// The map a changelevel asked for, empty when none is pending.
	const std::string& PendingMap() const { return pendingMap_; }
	void RequestMap(const std::string& map) { pendingMap_ = map; }
	void ClearPendingMap() { pendingMap_.clear(); }

	bool QuitRequested() const { return quit_; }
	void RequestQuit() { quit_ = true; }

	// Whether a map name has a .bsp below maps/ of the game folder, or was
	// declared with --map, --maps or Runner.ChangeMap.
	bool MapExists(const std::string& map) const;
	void DeclareMap(const std::string& map) { declaredMaps_.push_back(map); }

	// Resolves a path relative to the game folder.
	std::string GamePath(const std::string& relative) const;

	// Called after each exec command with its argument (SourceMod watches
	// for the exec of server.cfg).
	using ExecObserver = std::function<void(const char* file)>;
	void SetExecObserver(ExecObserver observer) { execObserver_ = std::move(observer); }

	// Called in each frame for every client whose userinfo changed, before
	// the game's frame (IServerGameClients::ClientSettingsChanged from
	// CBaseServer::RunFrame's UpdateUserSettings).
	using UserSettingsObserver = std::function<void(int client)>;
	void SetUserSettingsObserver(UserSettingsObserver observer) { userSettingsObserver_ = std::move(observer); }

 private:
	void Exec(const CommandArgs& args);
	void AddEngineCvars();
	void AddEngineCommands();

	EngineConfig config_;
	OutputLog output_;
	Console console_;
	EngineGlobals globals_;
	ClientTable clients_;
	double engineTime_ = 0.0;
	std::string map_;
	bool mapRunning_ = false;
	std::string pendingMap_;
	bool quit_ = false;
	std::vector<std::string> declaredMaps_;
	std::vector<std::string> precacheTables_[4];   // by PrecacheTable
	ExecObserver execObserver_;
	UserSettingsObserver userSettingsObserver_;
};

}  // namespace smrunner

#endif
