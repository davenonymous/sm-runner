// Copyright 2026 davenonymous
#include "engine.h"

#include <stdio.h>
#include <sys/stat.h>

#include <algorithm>
#include <fstream>
#include <sstream>

#include "fcvar.h"

namespace smrunner {

void OutputLog::Append(const char* text) {
	text_ += text;
	for (std::string* capture : captures_) {
		*capture += text;
	}
	if (echo_) {
		fputs(text, stdout);
		fflush(stdout);
	}
}

Engine::Engine(const EngineConfig& config)
	: config_(config),
	  console_([this](const char* text) { output_.Append(text); }),
	  clients_(config.maxClients, globals_) {
	globals_.interval_per_tick = config.tickInterval;
	AddEngineCvars();
	AddEngineCommands();
}

void Engine::RunFrame(const GameFrame& gameFrame) {
	console_.ExecuteBuffer();
	for (int client : clients_.TakeUserInfoChanges()) {
		if (userSettingsObserver_) {
			userSettingsObserver_(client);
		}
	}
	bool simulating = mapRunning_;
	globals_.curtime = static_cast<float>(globals_.tickcount) * globals_.interval_per_tick;
	globals_.frametime = simulating ? globals_.interval_per_tick : 0.0f;
	gameFrame(simulating);
	if (simulating) {
		globals_.tickcount++;
	}
	engineTime_ += globals_.interval_per_tick;
}

void Engine::BeginMap(const std::string& map) {
	map_ = map;
	mapRunning_ = false;
	for (std::vector<std::string>& table : precacheTables_) {
		table.clear();
	}
	globals_.tickcount = 0;
	globals_.curtime = 0.0f;
	globals_.frametime = 0.0f;
}

int Engine::Precache(PrecacheTable table, const std::string& name) {
	std::vector<std::string>& entries = precacheTables_[static_cast<int>(table)];
	for (size_t i = 0; i < entries.size(); i++) {
		if (entries[i] == name) {
			return static_cast<int>(i + 1);
		}
	}
	entries.push_back(name);
	return static_cast<int>(entries.size());
}

bool Engine::IsPrecached(PrecacheTable table, const std::string& name) const {
	const std::vector<std::string>& entries = precacheTables_[static_cast<int>(table)];
	return std::find(entries.begin(), entries.end(), name) != entries.end();
}

std::string Engine::GamePath(const std::string& relative) const {
	if (relative.empty()) {
		return config_.gameDir;
	}
	return config_.gameDir + "/" + relative;
}

bool Engine::MapExists(const std::string& map) const {
	for (const std::string& declared : declaredMaps_) {
		if (declared == map) {
			return true;
		}
	}
	struct stat info;
	return stat(GamePath("maps/" + map + ".bsp").c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

// The engine and TF2 cvars plugins look up most often, with their defaults
// on a dedicated TF2 server.
void Engine::AddEngineCvars() {
	struct Default {
		const char* name;
		const char* value;
		int flags;
		const char* help;
	};
	static const Default kDefaults[] = {
		{"hostname", "smrunner", 0, "Hostname for server."},
		{"hostport", "27015", 0, "Host game server port"},
		{"hostip", "2130706433", 0, "Host game server ip"},
		{"ip", "127.0.0.1", 0, "Overrides IP for multihomed hosts"},
		{"sv_password", "", FCVAR_NOTIFY | FCVAR_PROTECTED | FCVAR_DONTRECORD, "Server password for entry into multiplayer games"},
		{"sv_cheats", "0", FCVAR_NOTIFY | FCVAR_REPLICATED, "Allow cheats on server"},
		{"sv_lan", "0", 0, "Server is a lan server ( no heartbeat, no authentication, no non-class C addresses )"},
		{"sv_tags", "", FCVAR_NOTIFY, "Server tags. Used to provide extra information to clients when they're browsing for servers. Separate tags with a comma."},
		{"sv_visiblemaxplayers", "-1", 0, "Overrides the max players reported to prospective clients"},
		{"sv_pausable", "0", FCVAR_NOTIFY, "Is the server pausable."},
		{"sv_allow_votes", "1", 0, "Allow voting?"},
		{"sv_alltalk", "0", FCVAR_NOTIFY | FCVAR_REPLICATED, "Players can hear all other players' voice communication, no team restrictions"},
		{"sv_gravity", "800", FCVAR_NOTIFY | FCVAR_REPLICATED, "World gravity."},
		{"sv_maxrate", "0", FCVAR_REPLICATED, "Max bandwidth rate allowed on server, 0 == unlimited"},
		{"sv_minrate", "3500", FCVAR_REPLICATED, "Min bandwidth rate allowed on server, 0 == unlimited"},
		{"rcon_password", "", FCVAR_DONTRECORD, "remote console password."},
		{"sv_allow_wait_command", "1", FCVAR_REPLICATED, "Allow or disallow the wait command on clients connected to this server."},
		{"servercfgfile", "server.cfg", 0, ""},
		{"mapcyclefile", "mapcycle.txt", 0, "Name of the .txt file used to cycle the maps on multiplayer servers "},
		{"developer", "0", 0, "Set developer message level"},
		{"tv_enable", "0", FCVAR_NOTIFY, "Activates SourceTV on server."},
		{"tv_name", "SourceTV", 0, "SourceTV host name"},
		{"tv_delay", "30", 0, "SourceTV broadcast delay in seconds"},
		{"replay_enable", "0", FCVAR_REPLICATED | FCVAR_DONTRECORD, "Enable Replay recording on server"},
		{"mp_timelimit", "0", FCVAR_NOTIFY | FCVAR_REPLICATED, "game time per map in minutes"},
		{"mp_maxrounds", "0", FCVAR_NOTIFY | FCVAR_REPLICATED, "max number of rounds to play before server changes maps"},
		{"mp_winlimit", "0", FCVAR_NOTIFY | FCVAR_REPLICATED, "Max score one team can reach before server changes maps"},
		{"mp_windifference", "0", FCVAR_NOTIFY, "Score difference between teams before server changes maps"},
		{"mp_tournament", "0", FCVAR_NOTIFY | FCVAR_REPLICATED, ""},
		{"mp_friendlyfire", "0", FCVAR_NOTIFY | FCVAR_REPLICATED, "Allows team members to injure other members of their team"},
		{"mp_autoteambalance", "1", FCVAR_NOTIFY, "Automatically balance the teams based on mp_teams_unbalance_limit."},
		{"mp_teams_unbalance_limit", "1", FCVAR_NOTIFY | FCVAR_REPLICATED, "Teams are unbalanced when one team has this many more players than the other team. (0 disables check)"},
		{"tf_bot_quota", "0", FCVAR_NOTIFY, "Determines the total number of tf bots in the game."},
		{"tf_bot_quota_mode", "normal", FCVAR_NOTIFY, "Determines the type of quota.\nAllowed values: 'normal', 'fill', and 'match'."},
		{"tf_tournament_classlimit_scout", "-1", FCVAR_NOTIFY | FCVAR_REPLICATED, "Tournament mode per-team class limit for Scouts.\n"},
		{"nextlevel", "", FCVAR_GAMEDLL | FCVAR_NOTIFY, "If set to a valid map name, will trigger a changelevel to the specified map at the end of the round"},
	};
	for (const Default& entry : kDefaults) {
		console_.CreateConVar(entry.name, entry.value, entry.flags, entry.help, false, 0.0f, false, 0.0f);
	}
}

void Engine::Exec(const CommandArgs& args) {
	if (args.ArgC() < 2) {
		console_.Print("exec <filename>: execute a script file\n");
		return;
	}
	std::string file = args.Arg(1);
	if (file.find('.') == std::string::npos) {
		file += ".cfg";
	}
	std::ifstream in(GamePath("cfg/" + file));
	if (!in) {
		console_.Printf("exec: couldn't exec %s\n", file.c_str());
	} else {
		std::stringstream text;
		text << in.rdbuf();
		console_.InsertIntoBuffer(text.str());
	}
	if (execObserver_) {
		execObserver_(args.Arg(1));
	}
}

void Engine::AddEngineCommands() {
	console_.AddCommand({"exec", "Execute script file.", 0, [this](int, const CommandArgs& args) { Exec(args); }});
	console_.AddCommand({"echo", "Echo text to console.", 0, [this](int, const CommandArgs& args) {
		                     console_.Printf("%s\n", args.ArgS());
	                     }});
	auto changeLevel = [this](int, const CommandArgs& args) {
		if (args.ArgC() < 2) {
			console_.Printf("%s <mapname>\n", args.Arg(0));
			return;
		}
		if (!MapExists(args.Arg(1))) {
			console_.Printf("%s failed: \"%s\" not found on server.\n", args.Arg(0), args.Arg(1));
			return;
		}
		RequestMap(args.Arg(1));
	};
	console_.AddCommand({"changelevel", "Change server to the specified map", 0, changeLevel});
	console_.AddCommand({"map", "Start playing on specified map.", 0, changeLevel});
	auto quit = [this](int, const CommandArgs&) { RequestQuit(); };
	console_.AddCommand({"quit", "Exit the engine.", 0, quit});
	console_.AddCommand({"exit", "Exit the engine.", 0, quit});
}

}  // namespace smrunner
