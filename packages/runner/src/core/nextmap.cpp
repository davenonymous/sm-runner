// Copyright 2026 davenonymous
//
// Ported from SourceMod's core/NextMap.cpp and core/smn_nextmap.cpp (GPLv3,
// AlliedModders LLC).
#include "core/nextmap.h"

#include <string.h>

#include "core/core.h"
#include "diagnostics.h"
#include "testing/plugin_memory.h"

using namespace SourceMod;
using namespace SourcePawn;

namespace smrunner {

// MapChangeData's char arrays: m_mapName[PLATFORM_MAX_PATH], m_changeReason[100].
static constexpr size_t kMapNameSize = PLATFORM_MAX_PATH;
static constexpr size_t kChangeReasonSize = 100;

static std::string Truncated(const char* text, size_t size) {
	return std::string(text, strnlen(text, size - 1));
}

static MapChangeData MakeChangeData(const char* mapName, const char* changeReason, time_t startTime) {
	return {Truncated(mapName, kMapNameSize), Truncated(changeReason, kChangeReasonSize), startTime};
}

void NextMapManager::OnSourceModAllInitialized_Post() {
	Console& console = Core::Get().engine().console();
	nextMap_ = console.FindConVar("sm_nextmap");
	mapHistorySize_ = console.FindConVar("sm_maphistory_size");
	ConsoleCommand* changeLevel = console.FindCommand("changelevel");
	if (!nextMap_ || !mapHistorySize_ || !changeLevel) {
		Fatal("the next map manager misses sm_nextmap, sm_maphistory_size or the engine's changelevel");
	}
	// SH_ADD_HOOK on the command's Dispatch: the hook runs first.
	engineChangeLevel_ = changeLevel->handler;
	CommandHandler engine = engineChangeLevel_;
	changeLevel->handler = [this, engine](int client, const CommandArgs& args) {
		OnChangeLevelCommand(args);
		engine(client, args);
	};
}

void NextMapManager::OnSourceModShutdown() {
	if (ConsoleCommand* changeLevel = Core::Get().engine().console().FindCommand("changelevel")) {
		changeLevel->handler = engineChangeLevel_;
	}
	engineChangeLevel_ = nullptr;
	mapHistory_.clear();
}

const char* NextMapManager::GetNextMap() {
	return nextMap_->GetString();
}

bool NextMapManager::SetNextMap(const char* map) {
	if (!Core::Get().engine().MapExists(map)) {
		return false;
	}
	nextMap_->SetValue(map);
	return true;
}

void NextMapManager::OnSourceModLevelChange(const char* mapName) {
	// Skip the first 'mapchange' when the server starts up.
	if (tempChangeInfo_.startTime != 0) {
		if (tempChangeInfo_.mapName == mapName) {
			// The map change was as we expected.
			mapHistory_.push_back(MakeChangeData(lastMap_.c_str(), tempChangeInfo_.changeReason.c_str(),
			                                     tempChangeInfo_.startTime));
		} else {
			// Something intercepted the mapchange.
			std::string newReason = Truncated((tempChangeInfo_.changeReason + " (Map overridden)").c_str(), 255);
			mapHistory_.push_back(MakeChangeData(lastMap_.c_str(), newReason.c_str(), tempChangeInfo_.startTime));
		}

		int historydiff = mapHistorySize_->GetInt();
		if (historydiff > 0) {
			historydiff -= static_cast<int>(mapHistory_.size());
		} else if (historydiff < 0) {
			historydiff = -static_cast<int>(mapHistory_.size());
		}
		for (; historydiff < 0; historydiff++) {
			mapHistory_.pop_front();
		}
	}

	tempChangeInfo_.mapName.clear();
	tempChangeInfo_.changeReason.clear();
	tempChangeInfo_.startTime = time(nullptr);
	lastMap_ = Truncated(mapName, kMapNameSize);
}

// The engine's ChangeLevel buffers a changelevel command; SourceMod's hook
// of it only logs a forced change.
void NextMapManager::ForceChangeLevel(const char* mapName, const char* changeReason) {
	// Store the mapname and reason.
	tempChangeInfo_.mapName = Truncated(mapName, kMapNameSize);
	tempChangeInfo_.changeReason = Truncated(changeReason, kChangeReasonSize);

	logger->LogMessage("[SM] Changed map to \"%s\"", mapName);
	Core::Get().engine().ChangeLevel(mapName);
}

void NextMapManager::OnChangeLevelCommand(const CommandArgs& args) {
	if (args.ArgC() < 2) {
		return;
	}
	if (tempChangeInfo_.mapName.empty()) {
		tempChangeInfo_.mapName = Truncated(args.Arg(1), kMapNameSize);
		tempChangeInfo_.changeReason = "changelevel Command";
	}
}

// --------------------------------------------------------------- natives

static cell_t sm_GetNextMap(IPluginContext* pCtx, const cell_t* params) {
	const char* map = Core::Get().nextMap().GetNextMap();
	if (map[0] == 0) {
		return 0;
	}
	return WritePluginString(pCtx, params[1], params[2], map, TextCut::AtByte) ? 1 : 0;
}

static cell_t sm_SetNextMap(IPluginContext* pCtx, const cell_t* params) {
	char* map = PluginCharsArg(pCtx, params[1]);
	return map ? Core::Get().nextMap().SetNextMap(map) : 0;
}

static cell_t sm_ForceChangeLevel(IPluginContext* pCtx, const cell_t* params) {
	char* map = PluginCharsArg(pCtx, params[1]);
	char* changeReason = map ? PluginCharsArg(pCtx, params[2]) : nullptr;
	if (!changeReason) {
		return 0;
	}
	Core::Get().nextMap().ForceChangeLevel(map, changeReason);
	return 0;
}

static cell_t sm_GetMapHistorySize(IPluginContext* pCtx, const cell_t* params) {
	return static_cast<cell_t>(Core::Get().nextMap().MapHistory().size());
}

static cell_t sm_GetMapHistory(IPluginContext* pCtx, const cell_t* params) {
	const std::deque<MapChangeData>& history = Core::Get().nextMap().MapHistory();
	if (params[1] < 0 || params[1] >= static_cast<cell_t>(history.size())) {
		return pCtx->ThrowNativeError("Invalid Map History Index");
	}
	// The newest first.
	const MapChangeData& data = history[history.size() - 1 - static_cast<size_t>(params[1])];
	cell_t* startTime = PluginCellsArg(pCtx, params[6], 1);
	if (!startTime || !WritePluginString(pCtx, params[2], params[3], data.mapName, TextCut::AtByte) ||
	    !WritePluginString(pCtx, params[4], params[5], data.changeReason, TextCut::AtByte)) {
		return 0;
	}
	*startTime = static_cast<cell_t>(data.startTime);
	return 0;
}

REGISTER_NATIVES(nextmapnatives) {
	{"GetNextMap", sm_GetNextMap},
	{"SetNextMap", sm_SetNextMap},
	{"ForceChangeLevel", sm_ForceChangeLevel},
	{"GetMapHistorySize", sm_GetMapHistorySize},
	{"GetMapHistory", sm_GetMapHistory},
	{nullptr, nullptr},
};

}  // namespace smrunner
