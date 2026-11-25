// Copyright 2026 davenonymous
//
// SourceMod's players and player manager, ported from
// core/PlayerManager.cpp of SourceMod 1.12 (GPLv3, AlliedModders LLC): the
// same forwards in the same order (OnClientLanguageChanged, OnClientConnect,
// OnClientConnected, OnClientPutInServer, OnClientAuthorized every 0.7
// seconds of auth checks, OnClientPreAdminCheck, OnClientPostAdminFilter,
// OnClientPostAdminCheck, OnClientSettingsChanged when the engine reports a
// userinfo change, OnClientDisconnect, OnClientDisconnect_Post), the same admin checks by
// name, IP and Steam ID, the same target patterns (#userid, #STEAM_..., @all,
// partial names) and the same client command path. A player is SourceMod's
// view of a simulated client, the engine's slot of the same index
// (engine/clients.h), as CPlayer is of an edict: the client's identity, its
// cvars and what it is sent are the engine's.
#ifndef SMRUNNER_CORE_PLAYERS_H
#define SMRUNNER_CORE_PLAYERS_H

#include <IPlayerHelpers.h>

#include <string>
#include <vector>

#include "core/globals.h"
#include "engine/clients.h"
#include "engine/console.h"

namespace smrunner {

class SimPlayer : public SourceMod::IGamePlayer {
 public:
	// IGamePlayer
	const char* GetName() override { return name_.c_str(); }
	const char* GetIPAddress() override { return ip_.c_str(); }
	const char* GetAuthString(bool validated = true) override;
	edict_t* GetEdict() override { return connected_ ? client_->Edict() : nullptr; }
	bool IsInGame() override { return inGame_; }
	bool IsConnected() override { return connected_; }
	bool IsFakeClient() override { return fake_; }
	SourceMod::AdminId GetAdminId() override { return admin_; }
	void SetAdminId(SourceMod::AdminId id, bool temporary) override;
	int GetUserId() override { return connected_ ? client_->UserId() : -1; }
	unsigned int GetLanguageId() override { return language_; }
	IPlayerInfo* GetPlayerInfo() override { return inGame_ ? &client_->Info() : nullptr; }
	bool RunAdminCacheChecks() override;
	void NotifyPostAdminChecks() override;
	unsigned int GetSerial() override { return serial_; }
	bool IsAuthorized() override { return authorized_; }
	void Kick(const char* message) override;
	bool IsInKickQueue() override { return inKickQueue_; }
	void MarkAsBeingKicked() override { inKickQueue_ = true; }
	void SetLanguageId(unsigned int id) override;
	bool IsSourceTV() const override { return sourceTV_; }
	bool IsReplay() const override { return replay_; }
	unsigned int GetSteamAccountID(bool validated = true) override;
	int GetIndex() const override { return index_; }
	void PrintToConsole(const char* message) override;
	void ClearAdmin() override { DumpAdmin(true); }
	uint64_t GetSteamId64(bool validated = true) override;
	const char* GetSteam2Id(bool validated = true) override;
	const char* GetSteam3Id(bool validated = true) override;
	unsigned int GetOriginalLanguageId() override { return originalLanguage_; }

 private:
	friend class PlayerManager;
	void SetName(const std::string& name);
	void DumpAdmin(bool deleting);
	void DoBasicAdminChecks();
	void DoPostConnectAuthorization();
	void Reset();

	int index_ = 0;
	SimulatedClient* client_ = nullptr;   // the engine's slot of index_
	bool connected_ = false;
	bool inGame_ = false;
	bool authorized_ = false;
	bool authValidated_ = false;
	bool fake_ = false;
	bool sourceTV_ = false;
	bool replay_ = false;
	bool adminCheckSignalled_ = false;
	bool inKickQueue_ = false;
	bool countedInGame_ = false;
	std::string name_;
	std::string ip_;         // with the port, as the engine gave it
	std::string ipNoPort_;   // for the "ip" admin identity
	std::string lastPassword_;   // the password userinfo the admin checks last saw
	uint64_t steamId64_ = 0;
	std::string engineAuth_;
	std::string steam2_;
	std::string steam3_;
	unsigned int serial_ = 0;
	unsigned int language_ = 0;
	unsigned int originalLanguage_ = 0;
	SourceMod::AdminId admin_ = INVALID_ADMIN_ID;
	bool tempAdmin_ = false;
};

class PlayerManager : public SourceMod::IPlayerManager, public SMGlobalClass {
 public:
	explicit PlayerManager(ClientTable& clients);

	// SMGlobalClass
	void OnSourceModAllInitialized() override;
	void OnSourceModShutdown() override;
	void OnSourceModLevelEnd() override;
	ConfigResult OnSourceModConfigChanged(const char* key, const char* value, ConfigSource source, char* error,
	                                      size_t maxlength) override;

	// IPlayerManager
	void AddClientListener(SourceMod::IClientListener* listener) override;
	void RemoveClientListener(SourceMod::IClientListener* listener) override;
	SourceMod::IGamePlayer* GetGamePlayer(int client) override;
	SourceMod::IGamePlayer* GetGamePlayer(edict_t* edict) override;
	int GetMaxClients() override { return maxClients_; }
	int GetNumPlayers() override { return playerCount_; }
	int GetClientOfUserId(int userid) override;
	bool IsServerActivated() override { return serverActivated_; }
	unsigned int GetReplyTo() override;
	unsigned int SetReplyTo(unsigned int reply) override;
	int FilterCommandTarget(SourceMod::IGamePlayer* admin, SourceMod::IGamePlayer* target, int flags) override;
	void RegisterCommandTargetProcessor(SourceMod::ICommandTargetProcessor* handler) override;
	void UnregisterCommandTargetProcessor(SourceMod::ICommandTargetProcessor* handler) override;
	int GetClientFromSerial(unsigned int serial) override;
	void ProcessCommandTarget(SourceMod::cmd_target_info_t* info) override;
	void ClearAdminId(SourceMod::AdminId id) override;
	void RecheckAnyAdmins() override;

	// The server side.
	void OnServerActivate();
	void RunAuthChecks();
	// Connects a client and puts it in the server; returns the client index,
	// or 0 when a plugin rejected it (rejectReason says why) or the server is
	// full.
	int Connect(const ClientIdentity& identity, std::string* rejectReason);
	void Disconnect(int client, const char* reason);
	// A client's command line: each of its commands runs through the console
	// when clients may run it, else through the game (OnClientCommand).
	void ExecuteClientCommand(int client, const std::string& line);
	// PlayerManager::OnClientSettingsChanged: the client's userinfo changed
	// (the engine's ClientSettingsChanged).
	void OnClientSettingsChanged(int client);
	void QueueFakeClientCommand(int client, const std::string& line);
	void ProcessFakeClientCommands();
	void AddDelayedKick(int client, int userid, const char* message);
	void ProcessDelayedKicks();

	SimPlayer* Player(int client);
	bool IsConnected(int client);
	void PrintToConsole(int client, const char* text);
	void PrintToChat(int client, const char* text);
	int ReplicateConVar(int client, const char* name, const char* value);
	bool IsChatTrigger() const { return false; }
	const char* PublicChatTrigger() const { return "!"; }
	const char* SilentChatTrigger() const { return "/"; }
	bool HasPendingAuth() const { return !authQueue_.empty(); }

 private:
	void ExecuteSingleClientCommand(SimPlayer* player, const CommandArgs& args);
	void ClientSourceModCommand(SimPlayer* player, const CommandArgs& args);
	// The game's ClientCommand for a command nothing else took.
	void GameClientCommand(SimPlayer* player, const CommandArgs& args);
	bool CheckSetAdmin(SimPlayer* player, SourceMod::AdminId id);
	bool CheckSetAdminName(SimPlayer* player, SourceMod::AdminId id);
	void Authorize(SimPlayer* player);
	void Reject(SimPlayer* player);
	void Invalidate(SimPlayer* player);
	int InternalFilterCommandTarget(SimPlayer* admin, SimPlayer* target, int flags);
	friend class SimPlayer;

	ClientTable& clients_;
	int maxClients_;
	int playerCount_ = 0;
	unsigned int nextSerial_ = 1;
	bool serverActivated_ = false;
	SimPlayer players_[SM_MAXPLAYERS + 1];
	std::vector<int> authQueue_;
	std::vector<SourceMod::IClientListener*> listeners_;
	std::vector<SourceMod::ICommandTargetProcessor*> targetProcessors_;
	std::vector<std::pair<int, std::string>> fakeCommands_;   // userid, command
	std::vector<std::pair<int, std::string>> delayedKicks_;   // userid, message
	std::string passInfoVar_ = "_password";
	bool queryLanguage_ = true;   // core.cfg AllowClLanguageVar
	SourceMod::IForward* clConnect_ = nullptr;
	SourceMod::IForward* clConnectPost_ = nullptr;
	SourceMod::IForward* clPutInServer_ = nullptr;
	SourceMod::IForward* clDisconnect_ = nullptr;
	SourceMod::IForward* clDisconnectPost_ = nullptr;
	SourceMod::IForward* clCommand_ = nullptr;
	SourceMod::IForward* clSettingsChanged_ = nullptr;
	SourceMod::IForward* clAuth_ = nullptr;
	SourceMod::IForward* clLanguage_ = nullptr;
	SourceMod::IForward* onServerLoad_ = nullptr;
	SourceMod::IForward* onMapStart_ = nullptr;
	SourceMod::IForward* preAdminCheck_ = nullptr;
	SourceMod::IForward* postAdminCheck_ = nullptr;
	SourceMod::IForward* postAdminFilter_ = nullptr;
	friend class Core;
};

}  // namespace smrunner

#endif
