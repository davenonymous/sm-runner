// Copyright 2026 davenonymous
//
// Ported from core/UserMessages.cpp and core/HalfLife2.cpp of SourceMod
// 1.12 (GPLv3, AlliedModders LLC), the bit buffer paths only.
#include "core/usermsgs.h"

#include <IGameConfigs.h>
#include <stdio.h>
#include <string.h>

#include "core/core.h"

using namespace SourceMod;

namespace smrunner {

// TF2's user messages in the order the game registers them, as a TF2
// server's GetUserMessageName lists them.
static const char* const kTf2MessageNames[] = {
	"Geiger", "Train", "HudText", "SayText", "SayText2", "TextMsg", "ResetHUD", "GameTitle", "ItemPickup",
	"ShowMenu", "Shake", "Fade", "VGUIMenu", "Rumble", "CloseCaption", "SendAudio", "VoiceMask", "RequestState",
	"Damage", "HintText", "KeyHintText", "HudMsg", "AmmoDenied", "AchievementEvent", "UpdateRadar",
	"VoiceSubtitle", "HudNotify", "HudNotifyCustom", "PlayerStatsUpdate", "MapStatsUpdate", "PlayerIgnited",
	"PlayerIgnitedInv", "HudArenaNotify", "UpdateAchievement", "TrainingMsg", "TrainingObjective",
	"DamageDodged", "PlayerJarated", "PlayerExtinguished", "PlayerJaratedFade", "PlayerShieldBlocked",
	"BreakModel", "CheapBreakModel", "BreakModel_Pumpkin", "BreakModelRocketDud", "CallVoteFailed", "VoteStart",
	"VotePass", "VoteFailed", "VoteSetup", "PlayerBonusPoints", "RDTeamPointsChanged", "SpawnFlyingBird",
	"PlayerGodRayEffect", "PlayerTeleportHomeEffect", "MVMStatsReset", "MVMPlayerEvent", "MVMResetPlayerStats",
	"MVMWaveFailed", "MVMAnnouncement", "MVMPlayerUpgradedEvent", "MVMVictory", "MVMWaveChange",
	"MVMLocalPlayerUpgradesClear", "MVMLocalPlayerUpgradesValue", "MVMResetPlayerWaveSpendingStats",
	"MVMLocalPlayerWaveSpendingValue", "MVMResetPlayerUpgradeSpending", "MVMServerKickTimeUpdate",
	"PlayerLoadoutUpdated", "PlayerTauntSoundLoopStart", "PlayerTauntSoundLoopEnd", "ForcePlayerViewAngles",
	"BonusDucks", "EOTLDuckEvent", "PlayerPickupWeapon", "QuestObjectiveCompleted", "SdkRequestEquipment",
	"BuiltObject", "SPHapWeapEvent", "HapDmg", "HapPunch", "HapSetDrag", "HapSetConst", "HapMeleeContact",
	"SavedConvar",
};
static const int kTf2MessageCount = sizeof(kTf2MessageNames) / sizeof(kTf2MessageNames[0]);

static bool GameConfigSaysYes(const char* key) {
	IGameConfig* config = logicore.GetCoreGameConfig();
	const char* value = config ? config->GetKeyValue(key) : nullptr;
	return value && strcmp(value, "yes") == 0;
}

UserMessages::UserMessages()
	: engineBuffer_(kMaxUserMessageBytes), interceptBuffer_(kMaxUserMessageBytes) {}

int UserMessages::GetMessageIndex(const char* name) const {
	for (int id = 0; id < kTf2MessageCount; id++) {
		if (strcmp(kTf2MessageNames[id], name) == 0) {
			return id;
		}
	}
	return kInvalidMessageId;
}

const char* UserMessages::GetMessageName(int id) const {
	if (id < 0 || id >= kTf2MessageCount) {
		return nullptr;
	}
	return kTf2MessageNames[id];
}

BitWriter* UserMessages::StartMessage(int id, const cell_t players[], unsigned int count, int flags,
                                      const ShowMenuInfo* menu) {
	if (inExec_ || inHook_ || id < 0 || id >= 255) {
		return nullptr;
	}
	hasMenu_ = menu != nullptr;
	menu_ = hasMenu_ ? *menu : ShowMenuInfo();
	recipients_.clients.assign(players, players + count);
	recipients_.reliable = (flags & USERMSG_RELIABLE) != 0;
	recipients_.init = (flags & USERMSG_INITMSG) != 0;
	curFlags_ = flags;
	inExec_ = true;
	overflowed_ = false;
	if (flags & USERMSG_BLOCKHOOKS) {
		return EngineBegin(id);
	}
	return HookedBegin(id);
}

UserMessages::EndResult UserMessages::EndMessage() {
	if (!inExec_) {
		return EndResult::NotStarted;
	}
	if (curFlags_ & USERMSG_BLOCKHOOKS) {
		EngineEnd();
	} else {
		HookedEnd();
	}
	inExec_ = false;
	curFlags_ = 0;
	recipients_ = MessageRecipients();
	hasMenu_ = false;
	return overflowed_ ? EndResult::Overflowed : EndResult::Ended;
}

bool UserMessages::Hook(int id, UserMessageListener* listener, bool intercept, bool isNew) {
	if (id < 0 || id >= 255) {
		return false;
	}
	ListenerList& list = intercept ? intercepts_[id] : hooks_[id];
	list.push_back({listener, false, false, isNew});
	return true;
}

bool UserMessages::Unhook(int id, UserMessageListener* listener, bool intercept, bool isNew) {
	if (id < 0 || id >= 255) {
		return false;
	}
	ListenerList& list = intercept ? intercepts_[id] : hooks_[id];
	for (auto it = list.begin(); it != list.end(); ++it) {
		if (it->callback != listener || it->isNew != isNew) {
			continue;
		}
		// A listener being called is removed once its call returned.
		if (it->isHooked) {
			it->killMe = true;
			return true;
		}
		list.erase(it);
		return true;
	}
	return false;
}

// OnStartMessage_Pre and _Post: with intercept hooks the message is written
// into SourceMod's own buffer instead of the engine's.
BitWriter* UserMessages::HookedBegin(int id) {
	if (intercepts_[id].empty() && hooks_[id].empty()) {
		inHook_ = false;
		return EngineBegin(id);
	}
	curId_ = id;
	inHook_ = true;
	blockEndPost_ = false;
	if (!intercepts_[id].empty()) {
		interceptBuffer_.Reset();
		origBuffer_ = &interceptBuffer_;
		return &interceptBuffer_;
	}
	BitWriter* buffer = EngineBegin(id);
	origBuffer_ = buffer;
	return buffer;
}

void UserMessages::HookedEnd() {
	if (!inHook_) {
		EngineEnd();
		return;
	}
	bool superseded = RunEndPre();
	if (!superseded) {
		EngineEnd();
	}
	RunEndPost();
}

bool UserMessages::RunEndPre() {
	bool intercepted = false;
	bool handled = false;
	ListenerList& intercepts = intercepts_[curId_];
	for (auto it = intercepts.begin(); it != intercepts.end();) {
		it->isHooked = true;
		ResultType result = it->callback->InterceptUserMessage(curId_, interceptBuffer_, recipients_);
		intercepted = true;
		if (result == Pl_Stop) {
			if (it->killMe) {
				intercepts.erase(it);
			} else {
				it->isHooked = false;
			}
			blockEndPost_ = true;
			return true;
		}
		if (result == Pl_Handled) {
			handled = true;
		}
		if (it->killMe) {
			it = intercepts.erase(it);
			continue;
		}
		it->isHooked = false;
		++it;
	}
	// Intercepted and not handled: SourceMod sends its copy to the engine
	// past the hooks.
	if (intercepted && !handled) {
		if (interceptBuffer_.IsOverflowed()) {
			overflowed_ = true;
		} else {
			Deliver(curId_, interceptBuffer_);
		}
	}
	ListenerList& hooks = hooks_[curId_];
	for (auto it = hooks.begin(); it != hooks.end();) {
		it->isHooked = true;
		it->callback->OnUserMessage(curId_, *origBuffer_, recipients_);
		if (it->killMe) {
			it = hooks.erase(it);
			continue;
		}
		it->isHooked = false;
		++it;
	}
	return intercepted;
}

// OnMessageEnd_Post. A blocked message is not reported to the core's own
// listeners (SourceMod loops forever there instead of skipping them).
void UserMessages::RunEndPost() {
	inHook_ = false;
	for (ListenerList* list : {&intercepts_[curId_], &hooks_[curId_]}) {
		for (auto it = list->begin(); it != list->end();) {
			if (blockEndPost_ && !it->isNew) {
				++it;
				continue;
			}
			it->isHooked = true;
			it->callback->OnUserMessageSent(curId_);
			if (it->isNew) {
				it->callback->OnPostUserMessage(curId_, !blockEndPost_);
			}
			if (it->killMe) {
				it = list->erase(it);
				continue;
			}
			it->isHooked = false;
			++it;
		}
	}
}

BitWriter* UserMessages::EngineBegin(int id) {
	engineBuffer_.Reset();
	engineId_ = id;
	return &engineBuffer_;
}

void UserMessages::EngineEnd() {
	if (engineBuffer_.IsOverflowed()) {
		overflowed_ = true;
		return;
	}
	Deliver(engineId_, engineBuffer_);
}

// ------------------------------------------------------------- delivering

void UserMessages::Deliver(int id, const BitWriter& message) {
	const char* name = GetMessageName(id);
	if (!name) {
		return;
	}
	Core::Get().engine().clients().SendUserMessage(recipients_.clients, name, message, hasMenu_ ? &menu_ : nullptr);
}

// ------------------------------------------------------------ the core's

bool SendTextMsg(int client, int dest, const char* message) {
	UserMessages& messages = Core::Get().userMessages();
	cell_t players[] = {client};
	if (dest == kHudPrintTalk && GameConfigSaysYes("ChatSayText")) {
		char buffer[253];
		snprintf(buffer, sizeof(buffer), "%s\1\n", message);
		BitWriter* writer = messages.StartMessage(messages.GetMessageIndex("SayText"), players, 1, USERMSG_RELIABLE);
		if (!writer) {
			return false;
		}
		writer->WriteByte(0);
		writer->WriteString(buffer);
		writer->WriteByte(1);
		messages.EndMessage();
		return true;
	}
	BitWriter* writer = messages.StartMessage(messages.GetMessageIndex("TextMsg"), players, 1, USERMSG_RELIABLE);
	if (!writer) {
		return false;
	}
	// The client reads all four parameters.
	writer->WriteByte(dest);
	writer->WriteString(message);
	for (int i = 0; i < 4; i++) {
		writer->WriteString("");
	}
	messages.EndMessage();
	return true;
}

bool SendHintText(int client, const char* message) {
	UserMessages& messages = Core::Get().userMessages();
	cell_t players[] = {client};
	BitWriter* writer = messages.StartMessage(messages.GetMessageIndex("HintText"), players, 1, USERMSG_RELIABLE);
	if (!writer) {
		return false;
	}
	if (GameConfigSaysYes("HintTextPreByte")) {
		writer->WriteByte(1);
	}
	writer->WriteString(message);
	messages.EndMessage();
	return true;
}

}  // namespace smrunner
