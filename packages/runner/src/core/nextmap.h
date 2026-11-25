// Copyright 2026 davenonymous
//
// The next map manager, ported from core/NextMap.cpp and
// core/smn_nextmap.cpp of SourceMod 1.12 (GPLv3, AlliedModders LLC): the
// map history, recorded at every level change with the reason of the change
// and the time the map before it started, trimmed to sm_maphistory_size;
// the reason comes from ForceChangeLevel or from the changelevel command,
// which it hooks. The game's own level change at a map's end (the engine's
// ChangeLevel without ForceChangeLevel, where SourceMod puts in
// sm_nextmap) does not exist on the simulated server, so neither does that
// hook.
#ifndef SMRUNNER_CORE_NEXTMAP_H
#define SMRUNNER_CORE_NEXTMAP_H

#include <time.h>

#include <deque>
#include <string>

#include "core/globals.h"
#include "engine/console.h"

namespace smrunner {

// One entry of the map history: a map that ended, why, and when it started.
struct MapChangeData {
	std::string mapName;
	std::string changeReason;
	time_t startTime = 0;
};

class NextMapManager : public SMGlobalClass {
 public:
	// SMGlobalClass
	void OnSourceModAllInitialized_Post() override;
	void OnSourceModShutdown() override;
	void OnSourceModLevelChange(const char* mapName) override;

	const char* GetNextMap();
	bool SetNextMap(const char* map);
	void ForceChangeLevel(const char* mapName, const char* changeReason);

	// Oldest first.
	const std::deque<MapChangeData>& MapHistory() const { return mapHistory_; }

 private:
	// CmdChangeLevelCallback: runs before the engine's changelevel.
	void OnChangeLevelCommand(const CommandArgs& args);

	std::deque<MapChangeData> mapHistory_;
	MapChangeData tempChangeInfo_;
	std::string lastMap_;
	ConVar* nextMap_ = nullptr;
	ConVar* mapHistorySize_ = nullptr;
	CommandHandler engineChangeLevel_;   // the engine's handler while it is hooked
};

}  // namespace smrunner

#endif
