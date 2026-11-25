// Copyright 2026 davenonymous
//
// The simulated clients, in the simulated engine's client slots (1 to max
// players): who connected (the connection's identity, user id and time),
// the client's own cvars, the game's player info, and what the engine
// delivered to the client, recorded at the delivery as the client shows it:
// console text, commands, the reason of a kick, and user messages decoded as
// TF2's client decodes them (text messages as their text, the radio menu
// style's ShowMenu as the menu display it draws, every other message by its
// name). A slot keeps what its client received after the client left, until
// the next client connects into it. SourceMod's player (core/players.h) is a
// view of a slot, as CPlayer is of an edict; the tests read the slots.
#ifndef SMRUNNER_ENGINE_CLIENTS_H
#define SMRUNNER_ENGINE_CLIENTS_H

#include <stdint.h>
#include <strings.h>

#include <map>
#include <string>
#include <vector>

#include "engine/bitbuf.h"
#include "engine/menu_display.h"

// Opaque to SourceMod: the engine's entity of a client and the game's player
// info, which the bridge reads.
struct edict_t {
	int index;
};
class IPlayerInfo {
 public:
	int team = 0;
	bool observer = false;
	bool alive = true;
	int health = 100;
	int armor = 0;
	int frags = 0;
	int deaths = 0;
	float origin[3] = {0.0f, 0.0f, 0.0f};
	float angles[3] = {0.0f, 0.0f, 0.0f};
	float mins[3] = {-24.0f, -24.0f, 0.0f};
	float maxs[3] = {24.0f, 24.0f, 82.0f};
	std::string weapon;
	std::string model;
};

namespace smrunner {

struct EngineGlobals;

// The order of a client's userinfo: KeyValues names, case-insensitive; the
// first spelling of a name stays.
struct UserInfoNameLess {
	bool operator()(const std::string& a, const std::string& b) const { return strcasecmp(a.c_str(), b.c_str()) < 0; }
};
using UserInfo = std::map<std::string, std::string, UserInfoNameLess>;

// What a simulated client connects with.
struct ClientIdentity {
	std::string name = "Player";
	std::string ip = "127.0.0.1:27005";
	uint64_t steamId64 = 0;   // 0: a generated one
	bool fake = false;        // a bot: no network, receives nothing
	bool sourceTV = false;
	bool replay = false;
	std::string language = "english";   // cl_language, as TF2's client names it
	UserInfo cvars;   // the client's own cvars
};

// One thing the client received, with its kind: Menu is a menu display,
// UserMessage the name of a user message that carries no text.
struct ClientMessage {
	// The values of RunnerMessage in smrunner.inc, which tests pass and get.
	enum Kind {
		Console = 0,
		Chat = 1,
		Center = 2,
		Hint = 3,
		Command = 4,
		Kick = 5,
		Menu = 6,
		UserMessage = 7,
	} kind;
	std::string text;
};

// What the radio menu style hands the engine with each ShowMenu message it
// sends: the parts of its display a client cannot read from the message.
// The title, which keys are the menu's navigation, the hold time (the
// message's byte for it wraps above 127 seconds) and whether the message
// refreshes the display shown rather than starting one.
struct ShowMenuInfo {
	std::string title;
	int navigationKeys = 0;
	unsigned int holdTime = 0;   // 0: until replaced or a key
	bool refresh = false;
};

// The engine's MAX_PLAYER_NAME_LENGTH: a client's name has at most one byte
// less, cut at a byte (CBaseClient::SetName copies it with Q_strncpy).
constexpr size_t kMaxPlayerNameLength = 32;

// HUD_PRINT* destinations of TextMsg; every other one is the console.
constexpr int kHudPrintConsole = 2;
constexpr int kHudPrintTalk = 3;
constexpr int kHudPrintCenter = 4;

class SimulatedClient {
 public:
	int Index() const { return edict_.index; }
	edict_t* Edict() { return &edict_; }
	// Whether a client is in the slot; it is active once it is in the game.
	bool IsConnected() const { return connected_; }
	bool IsActive() const { return active_; }
	const ClientIdentity& Identity() const { return identity_; }
	int UserId() const { return userId_; }
	// Plat_FloatTime when it connected.
	double ConnectedAt() const { return connectedAt_; }
	IPlayerInfo& Info() { return info_; }

	// The client's own cvar, by a name in any case; "" when it has none of
	// the name, as CBaseClient::GetUserSetting answers.
	const char* GetConVar(const std::string& name) const;
	// Changes the client's userinfo; the engine tells the game on its next
	// frame (CBaseServer::UpdateUserSettings). The name is cut to fit the
	// engine's name buffer, as CBaseClient::SetName does.
	void SetConVar(const std::string& name, const std::string& value);

	const std::vector<ClientMessage>& Received() const { return received_; }
	void ClearReceived() { received_.clear(); }

	// The menu on the client's screen now, or nullptr once its hold time ran
	// out, a key it takes was pressed, the client left or it never had one.
	const MenuDisplay* CurrentMenu() const;
	// The client pressed a menu key (1 to 9, 0 or 10 for 0): a key its menu
	// takes closes the menu, as the game's client does.
	void PressMenuKey(int key);

 private:
	friend class ClientTable;
	bool NetworkReceives() const { return connected_ && !identity_.fake; }
	void Record(ClientMessage::Kind kind, const std::string& text) { received_.push_back({kind, text}); }
	void ReceiveShowMenu(const BitWriter& message, const ShowMenuInfo& info);

	const EngineGlobals* globals_ = nullptr;
	edict_t edict_{0};
	bool connected_ = false;
	bool active_ = false;
	ClientIdentity identity_;
	int userId_ = -1;
	double connectedAt_ = 0.0;
	IPlayerInfo info_;
	UserInfo cvars_;
	bool userInfoChanged_ = false;   // CBaseClient::m_bConVarsChanged
	std::vector<ClientMessage> received_;
	std::string menuChunks_;   // the ShowMenu text of a display still to be completed
	bool hasMenu_ = false;
	MenuDisplay menu_;
};

class ClientTable {
 public:
	// As many slots as SourceMod takes (SM_MAXPLAYERS).
	static constexpr int kMaxClients = 101;

	ClientTable(int maxClients, const EngineGlobals& globals);
	ClientTable(const ClientTable&) = delete;
	ClientTable& operator=(const ClientTable&) = delete;

	int MaxClients() const { return static_cast<int>(slots_.size()) - 1; }
	// The slot of a client index, nullptr outside 1 to MaxClients.
	SimulatedClient* Slot(int client);

	// A client connects into the first free slot, with the next user id and
	// cvars "name" (cut as SetConVar cuts it) and "cl_language" of its identity; nullptr when the
	// server is full. What the slot's last client received goes. Its
	// userinfo counts as changed, as the engine's CBaseClient::SetName marks
	// it at the connect.
	SimulatedClient* Connect(const ClientIdentity& identity, double now);
	// The connected clients whose userinfo changed since the last call, in
	// slot order; their change is taken (CBaseServer::UpdateUserSettings).
	std::vector<int> TakeUserInfoChanges();
	// Takes the change of one client's userinfo, as the update during its
	// signon does; whether there was one.
	bool TakeUserInfoChange(int client);
	// The client is in the game: it gets user messages.
	void PutInServer(int client);
	// The client leaves its slot; what it received stays readable.
	void Disconnect(int client);

	// What the engine delivers to a client, recorded here and nowhere else.
	// ClientPrintf: console text.
	void Print(int client, const std::string& text);
	// IVEngineServer::ClientCommand and a replicated cvar ("name value").
	void SendCommand(int client, const std::string& command);
	// The reason the server drops a client with.
	void SendKick(int client, const std::string& reason);
	// A user message by name to the clients in the game; ShowMenu with the
	// radio menu style's info is a menu display, without it a message like
	// any other.
	void SendUserMessage(const std::vector<int>& clients, const std::string& name, const BitWriter& message,
	                     const ShowMenuInfo* menu);

 private:
	std::vector<SimulatedClient> slots_;   // slot 0 is the server, never used
	int nextUserId_ = 2;
};

// The text a client shows for a text message's format and its parameters:
// %s1 to %s4 replaced, a #token kept with its parameters after it.
std::string ComposeClientText(const std::string& format, const std::vector<std::string>& parameters);

// Text without the chat color codes (\x01 to \x06, \x07RRGGBB, \x08RRGGBBAA).
std::string StripChatColors(const std::string& text);

}  // namespace smrunner

#endif
