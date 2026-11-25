// Copyright 2026 davenonymous
//
// Ported from SourceMod's core/PlayerManager.cpp (GPLv3, AlliedModders LLC).
#include "core/players.h"

#include <inttypes.h>
#include <sourcemod_version.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <algorithm>

#include "core/concmds.h"
#include "core/core.h"
#include "core/menu_radio.h"
#include "core/timers.h"
#include "core/usermsgs.h"
#include "diagnostics.h"

using namespace SourceMod;
using namespace SourcePawn;

namespace smrunner {

static constexpr uint64_t kSteamIdBase = 76561197960265728ULL;   // individual account, public universe

// Kicks a client whose name is reserved for an admin, by user id, a moment
// after the admin checks found it: the checks may run inside SourceMod's
// rebuild of the admin cache and must not disconnect anyone there.
class KickPlayerTimer : public ITimedEvent {
 public:
	ResultType OnTimer(ITimer*, void* data) override {
		int userid = static_cast<int>(reinterpret_cast<intptr_t>(data));
		int client = Core::Get().players().GetClientOfUserId(userid);
		if (client) {
			Core::Get().players().Player(client)->Kick(
				"Your name is reserved by SourceMod; set your password to use it.");
		}
		return Pl_Stop;
	}
	void OnTimerEnd(ITimer*, void*) override {}
};
static KickPlayerTimer sKickPlayerTimer;

// ----------------------------------------------------------------- SimPlayer

const char* SimPlayer::GetAuthString(bool validated) {
	if (!connected_ || (validated && !authValidated_ && !fake_)) {
		return nullptr;
	}
	return engineAuth_.c_str();
}

uint64_t SimPlayer::GetSteamId64(bool validated) {
	if (fake_ || (validated && !authValidated_)) {
		return 0;
	}
	return steamId64_;
}

unsigned int SimPlayer::GetSteamAccountID(bool validated) {
	uint64_t id = GetSteamId64(validated);
	return id == 0 ? 0 : static_cast<unsigned int>(id - kSteamIdBase);
}

// CPlayer::UpdateAuthIds: bots are "BOT".
const char* SimPlayer::GetSteam2Id(bool validated) {
	if (steam2_.empty() || (validated && !authValidated_)) {
		return nullptr;
	}
	return steam2_.c_str();
}

const char* SimPlayer::GetSteam3Id(bool validated) {
	if (steam3_.empty() || (validated && !authValidated_)) {
		return nullptr;
	}
	return steam3_.c_str();
}

void SimPlayer::SetAdminId(AdminId id, bool temporary) {
	if (!connected_) {
		return;
	}
	DumpAdmin(false);
	admin_ = id;
	tempAdmin_ = temporary;
}

void SimPlayer::DumpAdmin(bool deleting) {
	if (admin_ == INVALID_ADMIN_ID) {
		return;
	}
	if (tempAdmin_ && !deleting) {
		adminsys->InvalidateAdmin(admin_);
	}
	admin_ = INVALID_ADMIN_ID;
	tempAdmin_ = false;
}

void SimPlayer::Kick(const char* message) {
	MarkAsBeingKicked();
	Core::Get().engine().clients().SendKick(index_, message ? message : "");
	Core::Get().players().Disconnect(index_, message);
}

void SimPlayer::SetLanguageId(unsigned int id) {
	if (language_ == id) {
		return;
	}
	language_ = id;
	PlayerManager& players = Core::Get().players();
	players.clLanguage_->PushCell(index_);
	players.clLanguage_->PushCell(id);
	players.clLanguage_->Execute(nullptr);
}

// CPlayer::SetName: the engine cuts a long name at a byte, which can leave
// part of a UTF-8 character at its end; that part goes.
void SimPlayer::SetName(const std::string& name) {
	constexpr size_t kLongest = kMaxPlayerNameLength - 1;
	std::string cut = name.substr(0, kLongest);
	size_t i = 0;
	while (i < cut.size()) {
		size_t next = i + _GetUTF8CharBytes(&cut[i]);
		if (next > kLongest) {
			cut.resize(i);
			break;
		}
		i = next;
	}
	name_ = cut;
}

void SimPlayer::PrintToConsole(const char* message) {
	if (!connected_ || fake_) {
		return;
	}
	Core::Get().engine().clients().Print(index_, message);
}

bool SimPlayer::RunAdminCacheChecks() {
	AdminId old = admin_;
	DoBasicAdminChecks();
	return admin_ != old;
}

void SimPlayer::NotifyPostAdminChecks() {
	if (adminCheckSignalled_) {
		return;
	}
	adminCheckSignalled_ = true;
	PlayerManager& players = Core::Get().players();
	for (IClientListener* listener : players.listeners_) {
		listener->OnClientPostAdminCheck(index_);
	}
	players.postAdminFilter_->PushCell(index_);
	players.postAdminFilter_->Execute(nullptr);
	players.postAdminCheck_->PushCell(index_);
	players.postAdminCheck_->Execute(nullptr);
}

void SimPlayer::DoBasicAdminChecks() {
	if (admin_ != INVALID_ADMIN_ID) {
		return;
	}
	PlayerManager& players = Core::Get().players();
	AdminId id;
	if ((id = adminsys->FindAdminByIdentity("name", name_.c_str())) != INVALID_ADMIN_ID) {
		if (!players.CheckSetAdminName(this, id)) {
			void* userid = reinterpret_cast<void*>(static_cast<intptr_t>(GetUserId()));
			Core::Get().timers().CreateTimer(&sKickPlayerTimer, 0.1f, userid, 0);
		}
		return;
	}
	if ((id = adminsys->FindAdminByIdentity("ip", ipNoPort_.c_str())) != INVALID_ADMIN_ID) {
		if (players.CheckSetAdmin(this, id)) {
			return;
		}
	}
	if ((id = adminsys->FindAdminByIdentity("steam", engineAuth_.c_str())) != INVALID_ADMIN_ID) {
		players.CheckSetAdmin(this, id);
	}
}

void SimPlayer::DoPostConnectAuthorization() {
	PlayerManager& players = Core::Get().players();
	bool delay = false;
	for (IClientListener* listener : players.listeners_) {
		if (!listener->OnClientPreAdminCheck(index_)) {
			delay = true;
		}
	}
	cell_t result = 0;
	players.preAdminCheck_->PushCell(index_);
	players.preAdminCheck_->Execute(&result);
	// Defer, for better or worse.
	if (delay || static_cast<ResultType>(result) >= Pl_Handled) {
		return;
	}
	if (!connected_) {
		return;
	}
	DoBasicAdminChecks();
	NotifyPostAdminChecks();
}

void SimPlayer::Reset() {
	DumpAdmin(false);
	int index = index_;
	SimulatedClient* client = client_;
	*this = SimPlayer();
	index_ = index;
	client_ = client;
}

// ------------------------------------------------------------- PlayerManager

PlayerManager::PlayerManager(ClientTable& clients) : clients_(clients), maxClients_(clients.MaxClients()) {
	static_assert(ClientTable::kMaxClients == SM_MAXPLAYERS, "the engine has as many slots as SourceMod takes");
	for (int i = 0; i <= SM_MAXPLAYERS; i++) {
		players_[i].index_ = i;
		players_[i].client_ = clients.Slot(i);
	}
}

void PlayerManager::OnSourceModAllInitialized() {
	sharesys->AddInterface(nullptr, this);
	ParamType p1[] = {Param_Cell, Param_String, Param_Cell};
	ParamType p2[] = {Param_Cell};
	clConnect_ = forwardsys->CreateForward("OnClientConnect", ET_LowEvent, 3, p1);
	clConnectPost_ = forwardsys->CreateForward("OnClientConnected", ET_Ignore, 1, p2);
	clPutInServer_ = forwardsys->CreateForward("OnClientPutInServer", ET_Ignore, 1, p2);
	clDisconnect_ = forwardsys->CreateForward("OnClientDisconnect", ET_Ignore, 1, p2);
	clDisconnectPost_ = forwardsys->CreateForward("OnClientDisconnect_Post", ET_Ignore, 1, p2);
	clCommand_ = forwardsys->CreateForward("OnClientCommand", ET_Hook, 2, nullptr, Param_Cell, Param_Cell);
	clSettingsChanged_ = forwardsys->CreateForward("OnClientSettingsChanged", ET_Ignore, 1, p2);
	clAuth_ = forwardsys->CreateForward("OnClientAuthorized", ET_Ignore, 2, nullptr, Param_Cell, Param_String);
	clLanguage_ = forwardsys->CreateForward("OnClientLanguageChanged", ET_Ignore, 2, nullptr, Param_Cell, Param_Cell);
	onServerLoad_ = forwardsys->CreateForward("OnServerLoad", ET_Ignore, 0, nullptr);
	onMapStart_ = forwardsys->CreateForward("OnMapStart", ET_Ignore, 0, nullptr);
	preAdminCheck_ = forwardsys->CreateForward("OnClientPreAdminCheck", ET_Event, 1, p1);
	postAdminCheck_ = forwardsys->CreateForward("OnClientPostAdminCheck", ET_Ignore, 1, p1);
	postAdminFilter_ = forwardsys->CreateForward("OnClientPostAdminFilter", ET_Ignore, 1, p1);
	Core::Get().engine().SetUserSettingsObserver([this](int client) { OnClientSettingsChanged(client); });
}

void PlayerManager::OnSourceModShutdown() {
	Core::Get().engine().SetUserSettingsObserver(nullptr);
	for (IForward** forward : {&clConnect_, &clConnectPost_, &clPutInServer_, &clDisconnect_, &clDisconnectPost_,
	                           &clCommand_, &clSettingsChanged_, &clAuth_, &clLanguage_, &onServerLoad_, &onMapStart_,
	                           &preAdminCheck_, &postAdminCheck_, &postAdminFilter_}) {
		forwardsys->ReleaseForward(*forward);
		*forward = nullptr;
	}
}

ConfigResult PlayerManager::OnSourceModConfigChanged(const char* key, const char* value, ConfigSource, char* error,
                                                     size_t maxlength) {
	if (strcmp(key, "PassInfoVar") == 0) {
		passInfoVar_ = value;
		return ConfigResult_Accept;
	}
	if (strcmp(key, "AllowClLanguageVar") == 0) {
		if (strcasecmp(value, "on") == 0) {
			queryLanguage_ = true;
		} else if (strcasecmp(value, "off") == 0) {
			queryLanguage_ = false;
		} else {
			ke::SafeStrcpy(error, maxlength, "Invalid value: must be \"on\" or \"off\"");
			return ConfigResult_Reject;
		}
		return ConfigResult_Accept;
	}
	return ConfigResult_Ignore;
}

void PlayerManager::OnServerActivate() {
	serverActivated_ = true;
	Core::Get().SetMapStarted(true);
	extsys->CallOnCoreMapStart(nullptr, 0, maxClients_);
	onServerLoad_->Execute(nullptr);
	onMapStart_->Execute(nullptr);
	for (IClientListener* listener : listeners_) {
		if (listener->GetClientListenerVersion() >= 5) {
			listener->OnServerActivated(maxClients_);
		}
	}
}

void PlayerManager::OnSourceModLevelEnd() {
	for (int i = 1; i <= maxClients_; i++) {
		if (players_[i].connected_) {
			Disconnect(i, "Server shutting down");
		}
	}
	playerCount_ = 0;
}

void PlayerManager::AddClientListener(IClientListener* listener) {
	listeners_.push_back(listener);
}

void PlayerManager::RemoveClientListener(IClientListener* listener) {
	listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), listener), listeners_.end());
}

SimPlayer* PlayerManager::Player(int client) {
	if (client < 1 || client > maxClients_) {
		return nullptr;
	}
	return &players_[client];
}

bool PlayerManager::IsConnected(int client) {
	SimPlayer* player = Player(client);
	return player && player->connected_;
}

IGamePlayer* PlayerManager::GetGamePlayer(int client) {
	return Player(client);
}

IGamePlayer* PlayerManager::GetGamePlayer(edict_t* edict) {
	return edict ? Player(edict->index) : nullptr;
}

int PlayerManager::GetClientOfUserId(int userid) {
	for (int i = 1; i <= maxClients_; i++) {
		if (players_[i].connected_ && players_[i].GetUserId() == userid) {
			return i;
		}
	}
	return 0;
}

unsigned int PlayerManager::GetReplyTo() {
	return Core::Get().commands().ReplyTo();
}

unsigned int PlayerManager::SetReplyTo(unsigned int reply) {
	return Core::Get().commands().SetReplyTo(reply);
}

int PlayerManager::GetClientFromSerial(unsigned int serial) {
	for (int i = 1; i <= maxClients_; i++) {
		if (players_[i].connected_ && players_[i].serial_ == serial) {
			return i;
		}
	}
	return 0;
}

void PlayerManager::ClearAdminId(AdminId id) {
	for (int i = 1; i <= maxClients_; i++) {
		if (players_[i].admin_ == id) {
			players_[i].DumpAdmin(true);
		}
	}
}

void PlayerManager::RecheckAnyAdmins() {
	for (int i = 1; i <= maxClients_; i++) {
		if (players_[i].inGame_ && players_[i].authorized_) {
			players_[i].DoBasicAdminChecks();
		}
	}
}

bool PlayerManager::CheckSetAdmin(SimPlayer* player, AdminId id) {
	const char* password = adminsys->GetAdminPassword(id);
	if (password != nullptr) {
		if (passInfoVar_.empty()) {
			return false;
		}
		if (strcmp(player->client_->GetConVar(passInfoVar_), password) != 0) {
			return false;
		}
	}
	player->SetAdminId(id, false);
	return true;
}

bool PlayerManager::CheckSetAdminName(SimPlayer* player, AdminId id) {
	const char* password = adminsys->GetAdminPassword(id);
	if (password == nullptr || passInfoVar_.empty()) {
		return false;
	}
	if (strcmp(player->client_->GetConVar(passInfoVar_), password) != 0) {
		return false;
	}
	player->SetAdminId(id, false);
	return true;
}

void PlayerManager::Authorize(SimPlayer* player) {
	player->authorized_ = true;
	const char* steam2 = player->GetSteam2Id();
	const char* auth = steam2 ? steam2 : player->engineAuth_.c_str();
	for (IClientListener* listener : listeners_) {
		listener->OnClientAuthorized(player->index_, auth);
		if (!player->connected_) {
			return;
		}
	}
	if (clAuth_->GetFunctionCount()) {
		clAuth_->PushCell(player->index_);
		clAuth_->PushString(auth);
		clAuth_->Execute(nullptr);
	}
	if (player->connected_ && player->inGame_) {
		player->DoPostConnectAuthorization();
	}
}

// PlayerManager::RunAuthChecks: Steam validates a client's ticket before
// its auth string counts; a simulated client is validated when it connects.
void PlayerManager::RunAuthChecks() {
	std::vector<int> pending = authQueue_;
	for (int client : pending) {
		SimPlayer* player = &players_[client];
		if (!player->authValidated_) {
			continue;
		}
		authQueue_.erase(std::remove(authQueue_.begin(), authQueue_.end(), client), authQueue_.end());
		Authorize(player);
	}
}

static void FormatAuthIds(SimPlayer& player, uint64_t steamId64, std::string& engine, std::string& steam2,
                          std::string& steam3) {
	uint32_t account = static_cast<uint32_t>(steamId64 - kSteamIdBase);
	char buffer[64];
	snprintf(buffer, sizeof(buffer), "[U:1:%u]", account);
	engine = buffer;
	steam3 = buffer;
	snprintf(buffer, sizeof(buffer), "STEAM_0:%u:%u", account & 1, account >> 1);
	steam2 = buffer;
}

int PlayerManager::Connect(const ClientIdentity& spec, std::string* rejectReason) {
	SimulatedClient* slot = clients_.Connect(spec, Core::Get().engine().EngineTime());
	if (!slot) {
		if (rejectReason) {
			*rejectReason = "Server is full";
		}
		return 0;
	}
	int client = slot->Index();
	SimPlayer* player = &players_[client];
	player->Reset();
	player->connected_ = true;
	player->SetName(slot->GetConVar("name"));
	player->ip_ = spec.fake ? "127.0.0.1" : spec.ip;
	player->ipNoPort_ = player->ip_.substr(0, player->ip_.find(':'));
	player->fake_ = spec.fake;
	player->sourceTV_ = spec.sourceTV;
	player->replay_ = spec.replay;
	player->serial_ = (nextSerial_++ << 7) | static_cast<unsigned int>(client);
	if (spec.fake) {
		player->engineAuth_ = "BOT";
		player->steam2_ = "BOT";
		player->steam3_ = "BOT";
		player->authValidated_ = true;
	} else {
		player->steamId64_ = spec.steamId64 != 0 ? spec.steamId64 : kSteamIdBase + 100000 + slot->UserId();
		FormatAuthIds(*player, player->steamId64_, player->engineAuth_, player->steam2_, player->steam3_);
		player->authValidated_ = true;
	}
	// CPlayer::Initialize, then the client's language from its cl_language
	// when core.cfg allows it.
	player->language_ = translator->GetServerLanguage();
	player->originalLanguage_ = player->language_;
	if (queryLanguage_) {
		if (!spec.fake) {
			unsigned int language;
			bool known = translator->GetLanguageByName(slot->GetConVar("cl_language"), &language);
			player->language_ = known ? language : translator->GetServerLanguage();
			clLanguage_->PushCell(client);
			clLanguage_->PushCell(player->language_);
			clLanguage_->Execute(nullptr);
		}
		player->originalLanguage_ = player->language_;
	}

	char reject[255] = "";
	for (IClientListener* listener : listeners_) {
		if (!listener->InterceptClientConnect(client, reject, sizeof(reject))) {
			if (rejectReason) {
				*rejectReason = reject;
			}
			Reject(player);
			return 0;
		}
	}
	cell_t allowed = 1;
	clConnect_->PushCell(client);
	clConnect_->PushStringEx(reject, sizeof(reject), SM_PARAM_STRING_UTF8 | SM_PARAM_STRING_COPY, SM_PARAM_COPYBACK);
	clConnect_->PushCell(sizeof(reject));
	clConnect_->Execute(&allowed);
	if (!allowed && !spec.fake) {
		if (rejectReason) {
			*rejectReason = reject;
		}
		Reject(player);
		return 0;
	}
	for (IClientListener* listener : listeners_) {
		listener->OnClientConnected(client);
		if (!player->connected_) {
			return 0;
		}
	}
	clConnectPost_->PushCell(client);
	clConnectPost_->Execute(nullptr);
	if (!player->connected_) {
		return 0;
	}
	// A human's signon: the engine takes the userinfo the client sent with
	// it on a frame before the client is put in the server. A bot is put in
	// at once, its userinfo change waits for the next frame.
	if (!spec.fake && clients_.TakeUserInfoChange(client)) {
		OnClientSettingsChanged(client);
		if (!player->connected_) {
			return 0;
		}
	}

	if (spec.fake) {
		// Bots are authorized at once (OnClientPutInServer's bot path).
		Authorize(player);
		if (!player->connected_) {
			return 0;
		}
	} else {
		authQueue_.push_back(client);
	}

	// ClientPutInServer (CPlayer::Connect)
	clients_.PutInServer(client);
	player->inGame_ = true;
	player->lastPassword_ = passInfoVar_.empty() ? "" : slot->GetConVar(passInfoVar_);
	player->countedInGame_ = true;
	playerCount_++;
	for (IClientListener* listener : listeners_) {
		listener->OnClientPutInServer(client);
	}
	clPutInServer_->PushCell(client);
	clPutInServer_->Execute(nullptr);
	if (player->connected_ && player->authorized_) {
		player->DoPostConnectAuthorization();
	}
	// CGameClient::ActivatePlayer: ClientPutInServer, ClientActive, then
	// ClientSettingsChanged.
	OnClientSettingsChanged(client);
	return player->connected_ ? client : 0;
}

// The engine drops a client SourceMod refused before it was in the server.
void PlayerManager::Reject(SimPlayer* player) {
	int client = player->index_;
	Invalidate(player);
	clients_.Disconnect(client);
}

void PlayerManager::Invalidate(SimPlayer* player) {
	authQueue_.erase(std::remove(authQueue_.begin(), authQueue_.end(), player->index_), authQueue_.end());
	player->Reset();
}

void PlayerManager::Disconnect(int client, const char*) {
	SimPlayer* player = Player(client);
	if (!player || !player->connected_) {
		return;
	}
	clDisconnect_->PushCell(client);
	clDisconnect_->Execute(nullptr);
	if (player->countedInGame_) {
		playerCount_--;
	}
	for (IClientListener* listener : listeners_) {
		listener->OnClientDisconnecting(client);
	}
	Invalidate(player);
	clients_.Disconnect(client);
	clDisconnectPost_->PushCell(client);
	clDisconnectPost_->Execute(nullptr);
	for (IClientListener* listener : listeners_) {
		listener->OnClientDisconnected(client);
	}
}

// PlayerManager::OnClientSettingsChanged: the forward, a name change (a
// name reserved for another admin needs its password, a name that made the
// client an admin is that admin's no more), a new password (the admin checks
// again), then the client listeners.
void PlayerManager::OnClientSettingsChanged(int client) {
	SimPlayer* player = Player(client);
	if (!player || !player->connected_) {
		return;
	}
	clSettingsChanged_->PushCell(client);
	clSettingsChanged_->Execute(nullptr);
	if (!player->connected_) {
		return;
	}
	SimulatedClient* slot = player->client_;
	std::string newName = slot->GetConVar("name");
	if (newName != player->name_) {
		if (!player->fake_) {
			AdminId id = adminsys->FindAdminByIdentity("name", newName.c_str());
			if (id != INVALID_ADMIN_ID && player->admin_ != id) {
				if (!CheckSetAdminName(player, id)) {
					char kickMessage[128];
					logicore.CoreTranslate(kickMessage, sizeof(kickMessage), "%T", 2, nullptr, "Name Reserved", &client);
					player->Kick(kickMessage);
					return;
				}
			} else if ((id = adminsys->FindAdminByIdentity("name", player->name_.c_str())) != INVALID_ADMIN_ID &&
			           id == player->admin_) {
				player->SetAdminId(INVALID_ADMIN_ID, false);
			}
		}
		player->SetName(newName);
	}
	if (!player->fake_ && !passInfoVar_.empty()) {
		std::string newPassword = slot->GetConVar(passInfoVar_);
		if (newPassword != player->lastPassword_) {
			player->lastPassword_ = newPassword;
			if (player->inGame_ && player->authorized_) {
				// With an admin already assigned, this returns at once.
				player->DoBasicAdminChecks();
			}
		}
	}
	for (IClientListener* listener : listeners_) {
		if (listener->GetClientListenerVersion() >= 13) {
			listener->OnClientSettingsChanged(client);
		}
	}
}

// A client's command line, split as the game's client sends it.
void PlayerManager::ExecuteClientCommand(int client, const std::string& line) {
	for (const std::string& command : Console::SplitCommandLine(line)) {
		SimPlayer* player = Player(client);
		if (!player || !player->connected_) {
			return;
		}
		ExecuteSingleClientCommand(player, CommandArgs(command));
	}
}

// PlayerManager::OnClientCommand for the commands the console leaves to the
// game.
void PlayerManager::ExecuteSingleClientCommand(SimPlayer* player, const CommandArgs& args) {
	if (args.Overflowed()) {
		// CGameClient::ExecuteStringCommand: CCommand::Tokenize refused it.
		Core::Get().engine().console().Print(CommandArgs::kOverflowWarning);
		return;
	}
	if (args.Clamped()) {
		Core::Get().engine().console().Print(CommandArgs::kClampedWarning);
	}
	if (args.ArgC() == 0) {
		return;
	}
	int client = player->index_;
	if (strcmp(args.Arg(0), "sm") == 0) {
		ClientSourceModCommand(player, args);
		return;
	}
	// The client closes its menu on a key the menu takes, then sends the
	// key; the radio menu style takes it before anyone else.
	if (strcmp(args.Arg(0), "menuselect") == 0) {
		player->client_->PressMenuKey(atoi(args.Arg(1)));
	}
	bool menuKey = g_RadioMenuStyle.OnClientCommand(client, args);
	Console& console = Core::Get().engine().console();
	if (console.ExecuteClientCommand(client, args)) {
		return;
	}
	ConCmdManager& commands = Core::Get().commands();
	// The command stack of the console holds the args for GetCmdArg.
	cell_t result = menuKey ? Pl_Handled : Pl_Continue;
	console.ExecuteNowWith(client, args, [&]() {
		cell_t listeners = commands.RunListeners(client, args);
		if (listeners >= Pl_Handled) {
			result = Pl_Stop;
			return;
		}
		if (listeners > result) {
			result = listeners;
		}
		cell_t forward = Pl_Continue;
		if (player->inGame_) {
			clCommand_->PushCell(client);
			clCommand_->PushCell(args.ArgC() - 1);
			clCommand_->Execute(&forward, nullptr);
		}
		if (forward > result) {
			result = forward;
		}
		if (result >= Pl_Stop) {
			return;
		}
		result = commands.DispatchClientCommand(client, args.Arg(0), args.ArgC() - 1,
		                                        static_cast<ResultType>(result));
	});
	if (result < Pl_Handled && player->connected_) {
		GameClientCommand(player, args);
	}
}

// The commands TF2's game rules take whatever their arguments and whatever
// the game's state, without an answer (CTFGameRules, CTeamplayRules,
// CMultiplayRules and CVoiceGameMgr::ClientCommand); FStrEq ignores case.
// The game's other commands (the player's, such as jointeam and joinclass,
// and the tournament ones) need a game and are not simulated.
static bool IsSilentGameCommand(const CommandArgs& args) {
	const char* command = args.Arg(0);
	for (const char* silent : {"menuselect", "voicemenu", "objcmd", "mvm_allupgrades"}) {
		if (strcasecmp(command, silent) == 0) {
			return true;
		}
	}
	bool voiceCommand = strcasecmp(command, "vban") == 0 || strcasecmp(command, "VModEnable") == 0;
	return voiceCommand && args.ArgC() >= 2;
}

// TF2's ClientCommand (game/server/client.cpp): what the game rules do not
// take gets "Unknown command: X" as a TextMsg to the client's console, the
// game's ClientPrint, which user message hooks see.
void PlayerManager::GameClientCommand(SimPlayer* player, const CommandArgs& args) {
	if (IsSilentGameCommand(args)) {
		return;
	}
	const char* command = args.Arg(0);
	std::string reply = strlen(command) > 128 ? std::string("Console command too long.\n")
	                                          : std::string("Unknown command: ") + command + "\n";
	if (!SendTextMsg(player->index_, kHudPrintConsole, reply.c_str())) {
		Fatal("client %d ran \"%s\" while a user message was being sent or hooked: the game's answer would start a "
		      "user message inside it, where the engine stops the server",
		      player->index_, command);
	}
}

// PlayerManager::OnClientCommand's answer to a client's "sm".
void PlayerManager::ClientSourceModCommand(SimPlayer* player, const CommandArgs& args) {
	auto print = [player](const std::string& text) { player->PrintToConsole((text + "\n").c_str()); };
	if (args.ArgC() > 1 && strcmp(args.Arg(1), "plugins") == 0) {
		AutoPluginList plugins(scripts);
		if (plugins->size() == 0) {
			print("[SM] No plugins found.");
			return;
		}
		for (size_t i = 0; i < plugins->size(); i++) {
			SMPlugin* plugin = plugins->at(i);
			if (plugin->GetStatus() != Plugin_Running) {
				continue;
			}
			const sm_plugininfo_t* info = plugin->GetPublicInfo();
			std::string line = std::string(" \"") + (info->name && info->name[0] ? info->name : plugin->GetFilename()) + "\"";
			if (info->version && info->version[0]) {
				line += std::string(" (") + info->version + ")";
			}
			line += info->author && info->author[0] ? std::string(" by ") + info->author
			                                        : std::string(" ") + plugin->GetFilename();
			print(line);
		}
		return;
	}
	if (args.ArgC() > 1 && strcmp(args.Arg(1), "exts") == 0) {
		AutoExtensionList extensions(extsys);
		if (extensions->size() == 0) {
			print("[SM] No extensions found.");
			return;
		}
		for (size_t i = 0; i < extensions->size(); i++) {
			IExtension* extension = extensions->at(i);
			char error[255];
			if (!extension->IsRunning(error, sizeof(error))) {
				continue;
			}
			IExtensionInterface* api = extension->GetAPI();
			auto filled = [](const char* text) { return text != nullptr && text[0] != '\0'; };
			std::string line = std::string(" \"") + api->GetExtensionName() + "\"";
			if (filled(api->GetExtensionVerString())) {
				line += std::string(" (") + api->GetExtensionVerString() + ")";
			}
			if (filled(api->GetExtensionAuthor())) {
				line += std::string(" by ") + api->GetExtensionAuthor();
			}
			if (filled(api->GetExtensionDescription())) {
				line += std::string(": ") + api->GetExtensionDescription();
			}
			print(line);
		}
		return;
	}
	if (args.ArgC() > 1 && strcmp(args.Arg(1), "credits") == 0) {
		for (const char* line : {"SourceMod would not be possible without:",
		                         " David \"BAILOPAN\" Anderson, Matt \"pRED\" Woodrow",
		                         " Scott \"DS\" Ehlert, Fyren",
		                         " Nicholas \"psychonic\" Hastings, Asher \"asherkin\" Baker",
		                         " Ruben \"Dr!fter\" Gonzalez, Josh \"KyleS\" Allard",
		                         " Michael \"Headline\" Flaherty, Jannik \"Peace-Maker\" Hartung",
		                         " Borja \"faluco\" Ferrer, Pavol \"PM OnoTo\" Marko",
		                         "SourceMod is open source under the GNU General Public License."}) {
			print(line);
		}
		return;
	}
	print(std::string("SourceMod ") + SOURCEMOD_VERSION + ", by AlliedModders LLC");
	print("To see running plugins, type \"sm plugins\"");
	print("To see credits, type \"sm credits\"");
	print("Visit https://www.sourcemod.net/");
}

void PlayerManager::QueueFakeClientCommand(int client, const std::string& line) {
	SimPlayer* player = Player(client);
	if (!player || !player->connected_) {
		return;
	}
	fakeCommands_.emplace_back(player->GetUserId(), line);
}

void PlayerManager::ProcessFakeClientCommands() {
	std::vector<std::pair<int, std::string>> queued;
	queued.swap(fakeCommands_);
	for (const auto& entry : queued) {
		int client = GetClientOfUserId(entry.first);
		if (client != 0) {
			ExecuteClientCommand(client, entry.second);
		}
	}
}

void PlayerManager::AddDelayedKick(int client, int userid, const char* message) {
	SimPlayer* player = Player(client);
	if (!player || !player->connected_ || player->GetUserId() != userid) {
		return;
	}
	player->MarkAsBeingKicked();
	delayedKicks_.emplace_back(userid, message ? message : "");
}

void PlayerManager::ProcessDelayedKicks() {
	std::vector<std::pair<int, std::string>> queued;
	queued.swap(delayedKicks_);
	for (const auto& entry : queued) {
		int client = GetClientOfUserId(entry.first);
		if (client != 0) {
			players_[client].Kick(entry.second.c_str());
		}
	}
}

void PlayerManager::PrintToConsole(int client, const char* text) {
	if (client == 0) {
		Core::Get().engine().console().Print(text);
		return;
	}
	if (SimPlayer* player = Player(client)) {
		player->PrintToConsole(text);
	}
}

void PlayerManager::PrintToChat(int client, const char* text) {
	if (client == 0) {
		Core::Get().engine().console().Printf("%s\n", text);
		return;
	}
	SendTextMsg(client, kHudPrintTalk, text);
}

int PlayerManager::ReplicateConVar(int client, const char* name, const char* value) {
	SimPlayer* player = Player(client);
	if (!player || !player->inGame_ || player->fake_) {
		return 0;
	}
	char line[512];
	snprintf(line, sizeof(line), "%s %s", name, value);
	clients_.SendCommand(client, line);
	return 1;
}

// ----------------------------------------------------------- command targets

int PlayerManager::FilterCommandTarget(IGamePlayer* admin, IGamePlayer* target, int flags) {
	return InternalFilterCommandTarget(static_cast<SimPlayer*>(admin), static_cast<SimPlayer*>(target), flags);
}

void PlayerManager::RegisterCommandTargetProcessor(ICommandTargetProcessor* handler) {
	targetProcessors_.push_back(handler);
}

void PlayerManager::UnregisterCommandTargetProcessor(ICommandTargetProcessor* handler) {
	targetProcessors_.erase(std::remove(targetProcessors_.begin(), targetProcessors_.end(), handler),
	                        targetProcessors_.end());
}

int PlayerManager::InternalFilterCommandTarget(SimPlayer* admin, SimPlayer* target, int flags) {
	if ((flags & COMMAND_FILTER_CONNECTED) == COMMAND_FILTER_CONNECTED && !target->IsConnected()) {
		return COMMAND_TARGET_NONE;
	}
	if ((flags & COMMAND_FILTER_CONNECTED) != COMMAND_FILTER_CONNECTED && !target->IsInGame()) {
		return COMMAND_TARGET_NOT_IN_GAME;
	}
	if ((flags & COMMAND_FILTER_NO_BOTS) == COMMAND_FILTER_NO_BOTS && target->IsFakeClient()) {
		return COMMAND_TARGET_NOT_HUMAN;
	}
	if (admin != nullptr && (flags & COMMAND_FILTER_NO_IMMUNITY) != COMMAND_FILTER_NO_IMMUNITY &&
	    !adminsys->CanAdminTarget(admin->GetAdminId(), target->GetAdminId())) {
		return COMMAND_TARGET_IMMUNE;
	}
	bool alive = target->client_->Info().alive;
	if ((flags & COMMAND_FILTER_ALIVE) == COMMAND_FILTER_ALIVE && !alive) {
		return COMMAND_TARGET_NOT_ALIVE;
	}
	if ((flags & COMMAND_FILTER_DEAD) == COMMAND_FILTER_DEAD && alive) {
		return COMMAND_TARGET_NOT_DEAD;
	}
	return COMMAND_TARGET_VALID;
}

static void SetSingleTarget(cmd_target_info_t* info, int client, SimPlayer* target) {
	info->targets[0] = client;
	info->num_targets = 1;
	ke::SafeStrcpy(info->target_name, info->target_name_maxlength, target->GetName());
	info->target_name_style = COMMAND_TARGETNAME_RAW;
}

void PlayerManager::ProcessCommandTarget(cmd_target_info_t* info) {
	int total = 0;
	if (info->max_targets < 1) {
		info->reason = COMMAND_TARGET_NONE;
		info->num_targets = 0;
	}
	SimPlayer* admin = info->admin == 0 ? nullptr : Player(info->admin);
	auto single = [&](int client, SimPlayer* target) {
		if ((info->reason = InternalFilterCommandTarget(admin, target, info->flags)) == COMMAND_TARGET_VALID) {
			SetSingleTarget(info, client, target);
		} else {
			info->num_targets = 0;
		}
	};

	if (info->pattern[0] == '#') {
		int client = GetClientOfUserId(atoi(&info->pattern[1]));
		if (client > 0) {
			single(client, Player(client));
			return;
		}
		int steamIdType = 0;
		if (strncmp(&info->pattern[1], "STEAM_", 6) == 0) {
			steamIdType = 2;
		} else if (strncmp(&info->pattern[1], "[U:", 3) == 0) {
			steamIdType = 3;
		}
		if (steamIdType > 0) {
			std::string pattern = steamIdType == 2 ? std::string("STEAM_") + &info->pattern[7] : &info->pattern[1];
			if (steamIdType == 2) {
				std::replace(pattern.begin() + 6, pattern.end(), '_', ':');
			} else {
				std::replace(pattern.begin(), pattern.end(), '_', ':');
			}
			for (int i = 1; i <= maxClients_; i++) {
				SimPlayer* target = &players_[i];
				if (!target->connected_) {
					continue;
				}
				const char* steamId = steamIdType == 2 ? target->GetSteam2Id(false) : target->GetSteam3Id(false);
				if (steamId && pattern == steamId) {
					single(i, target);
					return;
				}
			}
		}
		for (int i = 1; i <= maxClients_; i++) {
			SimPlayer* target = &players_[i];
			if (target->connected_ && strcmp(target->GetName(), &info->pattern[1]) == 0) {
				single(i, target);
				return;
			}
		}
	}

	if (strcmp(info->pattern, "@me") == 0 && info->admin != 0) {
		info->reason = InternalFilterCommandTarget(admin, admin, info->flags);
		if (info->reason == COMMAND_TARGET_VALID) {
			SetSingleTarget(info, info->admin, admin);
		} else {
			info->num_targets = 0;
		}
		return;
	}

	if ((info->flags & COMMAND_FILTER_NO_MULTI) != COMMAND_FILTER_NO_MULTI) {
		bool isMulti = false;
		bool botsOnly = false;
		int skipClient = -1;
		auto multi = [&](const char* name) {
			isMulti = true;
			ke::SafeStrcpy(info->target_name, info->target_name_maxlength, name);
			info->target_name_style = COMMAND_TARGETNAME_ML;
		};
		if (strcmp(info->pattern, "@all") == 0) {
			multi("all players");
		} else if (strcmp(info->pattern, "@dead") == 0) {
			if ((info->flags & COMMAND_FILTER_ALIVE) == COMMAND_FILTER_ALIVE) {
				info->num_targets = 0;
				info->reason = COMMAND_TARGET_NOT_ALIVE;
				return;
			}
			info->flags |= COMMAND_FILTER_DEAD;
			multi("all dead players");
		} else if (strcmp(info->pattern, "@alive") == 0) {
			if ((info->flags & COMMAND_FILTER_DEAD) == COMMAND_FILTER_DEAD) {
				info->num_targets = 0;
				info->reason = COMMAND_TARGET_NOT_DEAD;
				return;
			}
			multi("all alive players");
			info->flags |= COMMAND_FILTER_ALIVE;
		} else if (strcmp(info->pattern, "@bots") == 0) {
			if ((info->flags & COMMAND_FILTER_NO_BOTS) == COMMAND_FILTER_NO_BOTS) {
				info->num_targets = 0;
				info->reason = COMMAND_TARGET_NOT_HUMAN;
				return;
			}
			multi("all bots");
			botsOnly = true;
		} else if (strcmp(info->pattern, "@humans") == 0) {
			multi("all humans");
			info->flags |= COMMAND_FILTER_NO_BOTS;
		} else if (strcmp(info->pattern, "@!me") == 0) {
			multi("all players");
			skipClient = info->admin;
		}
		if (isMulti) {
			for (int i = 1; i <= maxClients_ && total < info->max_targets; i++) {
				SimPlayer* target = &players_[i];
				if (InternalFilterCommandTarget(admin, target, info->flags) > 0 &&
				    (!botsOnly || target->IsFakeClient()) && skipClient != i) {
					info->targets[total++] = i;
				}
			}
			info->num_targets = total;
			info->reason = info->num_targets ? COMMAND_TARGET_VALID : COMMAND_TARGET_EMPTY_FILTER;
			return;
		}
	}

	for (ICommandTargetProcessor* processor : targetProcessors_) {
		if (processor->ProcessCommandTarget(info)) {
			return;
		}
	}

	int found = 0;
	for (int i = 1; i <= maxClients_; i++) {
		SimPlayer* target = &players_[i];
		if (!target->connected_) {
			continue;
		}
		if (logicore.stristr(target->GetName(), info->pattern) != nullptr) {
			if (found) {
				info->num_targets = 0;
				info->reason = COMMAND_TARGET_AMBIGUOUS;
				return;
			}
			found = i;
		}
	}
	if (found) {
		single(found, &players_[found]);
	} else {
		info->num_targets = 0;
		info->reason = COMMAND_TARGET_NONE;
	}
}

}  // namespace smrunner
