// Copyright 2026 davenonymous
//
// User messages, ported from core/UserMessages.cpp and core/HalfLife2.cpp
// of SourceMod 1.12 (GPLv3, AlliedModders LLC) for TF2's bit buffer
// messages: the message table, starting and ending a message with
// SourceMod's intercept and post hooks around the engine, and the core's
// text messages (TextMsg, SayText, HintText) as the game config chooses
// them. A message that reaches the engine goes to the simulated clients it
// was sent to (engine/clients.h).
#ifndef SMRUNNER_CORE_USERMSGS_H
#define SMRUNNER_CORE_USERMSGS_H

#include <list>
#include <string>
#include <vector>

#include "core/globals.h"
#include "engine/bitbuf.h"
#include "engine/clients.h"

// IUserMessages.h needs the SDK; these are its flags.
#define USERMSG_RELIABLE (1 << 2)
#define USERMSG_INITMSG (1 << 3)
#define USERMSG_BLOCKHOOKS (1 << 7)

namespace smrunner {

// The engine's limit of a user message's data (MAX_USER_MSG_DATA).
constexpr size_t kMaxUserMessageBytes = 255;
constexpr int kInvalidMessageId = -1;

// CellRecipientFilter: the clients a message goes to.
struct MessageRecipients {
	std::vector<int> clients;
	bool reliable = false;
	bool init = false;
};

// IBitBufUserMessageListener. A hook reads the message as it was written.
class UserMessageListener {
 public:
	virtual ~UserMessageListener() = default;
	virtual void OnUserMessage(int id, const BitWriter& message, const MessageRecipients& recipients) {}
	virtual SourceMod::ResultType InterceptUserMessage(int id, const BitWriter& message,
	                                                   const MessageRecipients& recipients) {
		return SourceMod::Pl_Continue;
	}
	virtual void OnUserMessageSent(int id) {}
	virtual void OnPostUserMessage(int id, bool sent) {}
};

class UserMessages {
 public:
	UserMessages();

	// The message's id in TF2's table, or kInvalidMessageId.
	int GetMessageIndex(const char* name) const;
	// The name of an id, or nullptr.
	const char* GetMessageName(int id) const;

	// StartBitBufMessage: nullptr while a message is being sent or hooked.
	// The radio menu style's ShowMenu messages carry its info about the
	// display along to the engine.
	BitWriter* StartMessage(int id, const cell_t players[], unsigned int count, int flags,
	                        const ShowMenuInfo* menu = nullptr);
	enum class EndResult { NotStarted, Ended, Overflowed };
	// EndMessage; Overflowed when the message was longer than the engine
	// takes, which then delivers nothing.
	EndResult EndMessage();

	// HookUserMessage2 (isNew, the plugins' hooks with a post notification)
	// and HookUserMessage (the core's own).
	bool Hook(int id, UserMessageListener* listener, bool intercept, bool isNew);
	bool Unhook(int id, UserMessageListener* listener, bool intercept, bool isNew);

 private:
	struct ListenerInfo {
		UserMessageListener* callback;
		bool isHooked;
		bool killMe;
		bool isNew;
	};
	using ListenerList = std::list<ListenerInfo>;

	// The engine's UserMessageBegin/MessageEnd with SourceMod's hooks
	// around them (OnStartMessage_Pre/Post, OnMessageEnd_Pre/Post).
	BitWriter* HookedBegin(int id);
	void HookedEnd();
	// OnMessageEnd_Pre; true when the engine's MessageEnd is superseded.
	bool RunEndPre();
	void RunEndPost();
	// The engine itself.
	BitWriter* EngineBegin(int id);
	void EngineEnd();
	void Deliver(int id, const BitWriter& message);

	ListenerList hooks_[255];
	ListenerList intercepts_[255];
	MessageRecipients recipients_;
	BitWriter engineBuffer_;
	BitWriter interceptBuffer_;
	const BitWriter* origBuffer_ = nullptr;
	int engineId_ = kInvalidMessageId;
	int curId_ = kInvalidMessageId;
	int curFlags_ = 0;
	bool inExec_ = false;
	bool inHook_ = false;
	bool blockEndPost_ = false;
	bool overflowed_ = false;
	bool hasMenu_ = false;   // the message carries menu_
	ShowMenuInfo menu_;
};

// CHalfLife2::TextMsg and HintTextMsg: false when no message could be
// started (a message is being sent or hooked).
bool SendTextMsg(int client, int dest, const char* message);
bool SendHintText(int client, const char* message);

}  // namespace smrunner

#endif
