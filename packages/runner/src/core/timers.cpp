// Copyright 2026 davenonymous
//
// Ported from SourceMod's core/TimerSys.cpp (GPLv3, AlliedModders LLC).
#include "core/timers.h"

#include <algorithm>

#include "core/convars.h"
#include "core/core.h"

using namespace SourceMod;

void SourceMod::ITimer::Initialize(ITimedEvent* callbacks, float interval_, double toExec_, void* data_, int flags_) {
	listener = callbacks;
	interval = interval_;
	toExec = toExec_;
	data = data_;
	flags = flags_;
	inExec = false;
	killMe = false;
}

namespace smrunner {

static constexpr double kTimerMinAccuracy = 0.1;

// The default map timer: mp_timelimit in minutes.
class DefaultMapTimer : public IMapTimer {
 public:
	int GetMapTimeLimit() override {
		ConVar* timelimit = Core::Get().engine().console().FindConVar("mp_timelimit");
		return timelimit ? timelimit->GetInt() : 0;
	}
	void SetMapTimerStatus(bool enabled) override {
		if (enabled == inUse_) {
			return;
		}
		inUse_ = enabled;
		if (enabled) {
			Core::Get().convars().AddChangeListener("mp_timelimit", this, [](ConVar*, const char*, float) {
				Core::Get().timers().MapTimeLeftChanged();
			});
		} else {
			Core::Get().convars().RemoveChangeListener("mp_timelimit", this);
		}
	}
	void ExtendMapTimeLimit(int extraTime) override {
		ConVar* timelimit = Core::Get().engine().console().FindConVar("mp_timelimit");
		if (!timelimit) {
			return;
		}
		if (extraTime == 0) {
			timelimit->SetValue(0);
			return;
		}
		timelimit->SetValue(timelimit->GetInt() + extraTime / 60);
	}

 private:
	bool inUse_ = false;
};

static DefaultMapTimer sDefaultMapTimer;

TimerSystem::TimerSystem() = default;

TimerSystem::~TimerSystem() {
	for (ITimer* timer : free_) {
		delete timer;
	}
}

void TimerSystem::OnSourceModAllInitialized() {
	sharesys->AddInterface(nullptr, this);
	onGameFrame_ = forwardsys->CreateForward("OnGameFrame", ET_Ignore, 0, nullptr);
	onMapTimeLeftChanged_ = forwardsys->CreateForward("OnMapTimeLeftChanged", ET_Ignore, 0, nullptr);
}

void TimerSystem::OnSourceModLevelChange(const char*) {
	gameStartTime_ = 0.0f;
}

void TimerSystem::OnSourceModGameInitialized() {
	if (mapTimer_ == nullptr && Core::Get().engine().console().FindConVar("mp_timelimit") != nullptr) {
		SetMapTimer(&sDefaultMapTimer);
	}
}

void TimerSystem::OnSourceModShutdown() {
	SetMapTimer(nullptr);
	forwardsys->ReleaseForward(onGameFrame_);
	forwardsys->ReleaseForward(onMapTimeLeftChanged_);
	onGameFrame_ = nullptr;
	onMapTimeLeftChanged_ = nullptr;
}

void TimerSystem::OnSourceModLevelEnd() {
	hasMapTickedYet_ = false;
	hasMapSimulatedYet_ = false;
}

double TimerSystem::CalcNextThink(double last, float interval) const {
	if (universalTime_ - last - interval <= kTimerMinAccuracy) {
		return last + interval;
	}
	return universalTime_ + interval;
}

void TimerSystem::GameFrame(bool simulating) {
	const EngineGlobals& globals = Core::Get().engine().globals();
	if (simulating && hasMapTickedYet_) {
		universalTime_ += globals.curtime - lastTickedTime_;
		if (!hasMapSimulatedYet_) {
			hasMapSimulatedYet_ = true;
			MapTimeLeftChanged();
		}
	} else {
		universalTime_ += globals.interval_per_tick;
	}
	lastTickedTime_ = globals.curtime;
	hasMapTickedYet_ = true;

	if (universalTime_ >= timerThink_) {
		RunFrame();
		timerThink_ = CalcNextThink(timerThink_, kTimerMinAccuracy);
	}

	Core::Get().RunFrameHooks(simulating);

	if (onGameFrame_ && onGameFrame_->GetFunctionCount()) {
		onGameFrame_->Execute(nullptr);
	}
}

void TimerSystem::Release(ITimer* timer) {
	free_.push_back(timer);
}

void TimerSystem::RunFrame() {
	double now = universalTime_;
	for (auto it = single_.begin(); it != single_.end();) {
		ITimer* timer = *it;
		if (now < timer->toExec) {
			break;
		}
		timer->inExec = true;
		timer->listener->OnTimer(timer, timer->data);
		timer->listener->OnTimerEnd(timer, timer->data);
		it = single_.erase(it);
		Release(timer);
	}
	for (auto it = loop_.begin(); it != loop_.end();) {
		ITimer* timer = *it;
		if (now < timer->toExec) {
			++it;
			continue;
		}
		timer->inExec = true;
		ResultType result = timer->listener->OnTimer(timer, timer->data);
		if (timer->killMe || result == Pl_Stop) {
			timer->listener->OnTimerEnd(timer, timer->data);
			it = loop_.erase(it);
			Release(timer);
			continue;
		}
		timer->inExec = false;
		timer->toExec = CalcNextThink(timer->toExec, timer->interval);
		++it;
	}
}

ITimer* TimerSystem::CreateTimer(ITimedEvent* callbacks, float interval, void* data, int flags) {
	// Like the original: the time to run is kept as a float.
	float toExec = static_cast<float>(universalTime_ + interval);
	ITimer* timer;
	if (free_.empty()) {
		timer = new ITimer;
	} else {
		timer = free_.back();
		free_.pop_back();
	}
	timer->Initialize(callbacks, interval, toExec, data, flags);
	if (flags & TIMER_FLAG_REPEAT) {
		loop_.push_back(timer);
		return timer;
	}
	if (!single_.empty() && single_.back()->toExec <= toExec) {
		single_.push_back(timer);
		return timer;
	}
	auto position = std::find_if(single_.begin(), single_.end(),
	                             [toExec](ITimer* other) { return other->toExec >= toExec; });
	single_.insert(position, timer);
	return timer;
}

void TimerSystem::FireTimerOnce(ITimer* timer, bool delayExec) {
	if (timer->inExec) {
		return;
	}
	timer->inExec = true;
	ResultType result = timer->listener->OnTimer(timer, timer->data);
	if (!(timer->flags & TIMER_FLAG_REPEAT)) {
		timer->listener->OnTimerEnd(timer, timer->data);
		single_.remove(timer);
		Release(timer);
		return;
	}
	if (result != Pl_Stop && !timer->killMe) {
		if (delayExec) {
			timer->toExec = universalTime_ + timer->interval;
		}
		timer->inExec = false;
		return;
	}
	timer->listener->OnTimerEnd(timer, timer->data);
	loop_.remove(timer);
	Release(timer);
}

void TimerSystem::KillTimer(ITimer* timer) {
	if (timer->killMe) {
		return;
	}
	if (timer->inExec) {
		timer->killMe = true;
		return;
	}
	timer->inExec = true;
	timer->listener->OnTimerEnd(timer, timer->data);
	if (timer->flags & TIMER_FLAG_REPEAT) {
		loop_.remove(timer);
	} else {
		single_.remove(timer);
	}
	Release(timer);
}

void TimerSystem::RemoveMapChangeTimers() {
	std::vector<ITimer*> doomed;
	for (ITimer* timer : single_) {
		if (timer->flags & TIMER_FLAG_NO_MAPCHANGE) {
			doomed.push_back(timer);
		}
	}
	for (ITimer* timer : loop_) {
		if (timer->flags & TIMER_FLAG_NO_MAPCHANGE) {
			doomed.push_back(timer);
		}
	}
	for (ITimer* timer : doomed) {
		KillTimer(timer);
	}
}

IMapTimer* TimerSystem::SetMapTimer(IMapTimer* timer) {
	IMapTimer* old = mapTimer_;
	mapTimer_ = timer;
	if (mapTimer_) {
		mapTimer_->SetMapTimerStatus(true);
	}
	if (old) {
		old->SetMapTimerStatus(false);
	}
	return old;
}

IMapTimer* TimerSystem::GetMapTimer() {
	return mapTimer_;
}

void TimerSystem::MapTimeLeftChanged() {
	if (onMapTimeLeftChanged_) {
		onMapTimeLeftChanged_->Execute(nullptr);
	}
}

void TimerSystem::NotifyOfGameStart(float offset) {
	gameStartTime_ = Core::Get().engine().globals().curtime + offset;
}

float TimerSystem::GetTickedTime() {
	return static_cast<float>(universalTime_);
}

bool TimerSystem::GetMapTimeLeft(float* timeLeft) {
	if (!mapTimer_) {
		return false;
	}
	int timeLimit;
	if (!hasMapSimulatedYet_ || (timeLimit = mapTimer_->GetMapTimeLimit()) < 1) {
		*timeLeft = -1.0f;
	} else {
		*timeLeft = (gameStartTime_ + timeLimit * 60.0f) - Core::Get().engine().globals().curtime;
	}
	return true;
}

}  // namespace smrunner
