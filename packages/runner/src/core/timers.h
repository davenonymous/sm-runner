// Copyright 2026 davenonymous
//
// SourceMod's timer system, ported from core/TimerSys.cpp of SourceMod 1.12
// (GPLv3, AlliedModders LLC) onto the simulated clock: the same lists of
// single and repeating timers, the same 0.1 second accuracy, the same order
// of a game frame (timers, frame hooks, OnGameFrame) and the same universal
// time, which only the frames advance.
#ifndef SMRUNNER_CORE_TIMERS_H
#define SMRUNNER_CORE_TIMERS_H

#include <ITimerSystem.h>

#include <list>
#include <vector>

#include "sm_globals.h"

namespace SourceMod {

class ITimer {
 public:
	void Initialize(ITimedEvent* callbacks, float interval, double toExec, void* data, int flags);

	ITimedEvent* listener = nullptr;
	void* data = nullptr;
	float interval = 0.0f;
	double toExec = 0.0;
	int flags = 0;
	bool inExec = false;
	bool killMe = false;
};

}  // namespace SourceMod

namespace smrunner {

class TimerSystem : public SourceMod::ITimerSystem, public SMGlobalClass {
 public:
	TimerSystem();
	~TimerSystem();

	// SMGlobalClass
	void OnSourceModAllInitialized() override;
	void OnSourceModLevelChange(const char* mapName) override;
	void OnSourceModLevelEnd() override;
	void OnSourceModGameInitialized() override;
	void OnSourceModShutdown() override;

	// ITimerSystem
	SourceMod::ITimer* CreateTimer(SourceMod::ITimedEvent* callbacks, float interval, void* data, int flags) override;
	void KillTimer(SourceMod::ITimer* timer) override;
	void FireTimerOnce(SourceMod::ITimer* timer, bool delayExec = false) override;
	SourceMod::IMapTimer* SetMapTimer(SourceMod::IMapTimer* timer) override;
	void MapTimeLeftChanged() override;
	float GetTickedTime() override;
	void NotifyOfGameStart(float offset = 0.0f) override;
	bool GetMapTimeLeft(float* timeLeft) override;
	SourceMod::IMapTimer* GetMapTimer() override;

	// One game frame: universal time, timers, frame hooks, OnGameFrame.
	void GameFrame(bool simulating);
	void RemoveMapChangeTimers();

	const double* UniversalTime() const { return &universalTime_; }

 private:
	void RunFrame();
	double CalcNextThink(double last, float interval) const;
	void Release(SourceMod::ITimer* timer);

	std::list<SourceMod::ITimer*> single_;
	std::list<SourceMod::ITimer*> loop_;
	std::vector<SourceMod::ITimer*> free_;
	SourceMod::IMapTimer* mapTimer_ = nullptr;
	bool hasMapTickedYet_ = false;
	bool hasMapSimulatedYet_ = false;
	float lastTickedTime_ = 0.0f;
	double universalTime_ = 0.0;
	double timerThink_ = 0.0;
	float gameStartTime_ = 0.0f;
	SourceMod::IForward* onGameFrame_ = nullptr;
	SourceMod::IForward* onMapTimeLeftChanged_ = nullptr;
};

}  // namespace smrunner

#endif
