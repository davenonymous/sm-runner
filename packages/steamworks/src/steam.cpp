// Copyright 2026 davenonymous
//
// The natives follow SteamWorks 1.2.167's gsnatives.cpp and swforwards.cpp:
// the same checks of client indexes, return values and forward parameters,
// with ISteamGameServer's answers taken from ServerState. Unlike SteamWorks,
// every span of plugin memory a native reads or writes is checked first
// (testing/plugin_memory.h); one that is not the plugin's is a native error.
#include "steam.h"

#include <stdlib.h>
#include <string.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "extension.h"
#include "testing/plugin_memory.h"

namespace steamworks {

using smrunner::PluginBytesArg;
using smrunner::PluginCellsArg;
using smrunner::PluginStringArg;

// SteamWorks' EUniverse and EAccountType as CSteamID packs them.
static constexpr uint64_t kUniversePublic = 1;
static constexpr uint64_t kAccountTypeIndividual = 1;
static constexpr uint64_t kInstanceDesktop = 1;
static constexpr int kTf2AppId = 440;

struct PendingGroupStatus {
	int frame;
	unsigned int account;
	unsigned int group;
	GroupStatus status;
};

static ServerState sServer;
static bool sConnectedFired = false;
static std::map<std::pair<unsigned int, unsigned int>, GroupStatus> sGroupStatus;
static std::map<std::pair<unsigned int, int>, License> sLicenses;
static std::vector<PendingGroupStatus> sPendingGroupStatus;

static IForward* sValidateClientOld = nullptr;
static IForward* sValidateClient = nullptr;
static IForward* sServersConnected = nullptr;
static IForward* sServersConnectFailure = nullptr;
static IForward* sServersDisconnected = nullptr;
static IForward* sRestartRequested = nullptr;
static IForward* sTokenRequested = nullptr;
static IForward* sClientGroupStatus = nullptr;
static IForward* sGCSendMessage = nullptr;
static IForward* sGCMsgAvailable = nullptr;
static IForward* sGCRetrieveMessage = nullptr;
static IForward* sUserStatsReceived = nullptr;
static IForward* sUserStatsUnloaded = nullptr;
static IForward* sUserStatsStored = nullptr;

ServerState& Server() {
	return sServer;
}

// --------------------------------------------------------------- forwards

void FireServersConnected() {
	sServer.connected = true;
	sServersConnected->Execute(nullptr);
}

void FireServersConnectFailure(int result) {
	sServer.connected = false;
	sServersConnectFailure->PushCell(result);
	sServersConnectFailure->Execute(nullptr);
}

void FireServersDisconnected(int result) {
	sServer.connected = false;
	sServersDisconnected->PushCell(result);
	sServersDisconnected->Execute(nullptr);
}

void FireValidateClient(unsigned int owner, unsigned int account) {
	for (IForward* forward : {sValidateClientOld, sValidateClient}) {
		forward->PushCell(static_cast<cell_t>(owner));
		forward->PushCell(static_cast<cell_t>(account));
		forward->Execute(nullptr);
	}
}

void FireClientGroupStatus(unsigned int account, unsigned int group, GroupStatus status) {
	sClientGroupStatus->PushCell(static_cast<cell_t>(account));
	sClientGroupStatus->PushCell(static_cast<cell_t>(group));
	sClientGroupStatus->PushCell(status.member);
	sClientGroupStatus->PushCell(status.officer);
	sClientGroupStatus->Execute(nullptr);
}

std::string FireTokenRequested() {
	char token[256] = "";
	sTokenRequested->PushStringEx(token, sizeof(token), SM_PARAM_STRING_UTF8 | SM_PARAM_STRING_COPY, SM_PARAM_COPYBACK);
	sTokenRequested->PushCell(sizeof(token));
	sTokenRequested->Execute(nullptr);
	token[sizeof(token) - 1] = '\0';
	return token;
}

bool FireRestartRequested() {
	cell_t result = Pl_Continue;
	sRestartRequested->Execute(&result);
	return result >= Pl_Handled;
}

void SetGroupStatus(unsigned int account, unsigned int group, GroupStatus status) {
	sGroupStatus[{account, group}] = status;
}

void SetLicense(unsigned int account, int app, License license) {
	sLicenses[{account, app}] = license;
}

void SteamFrame() {
	if (!sConnectedFired && playerhelpers->IsServerActivated()) {
		sConnectedFired = true;
		FireServersConnected();
	}
	std::vector<PendingGroupStatus> due;
	for (auto it = sPendingGroupStatus.begin(); it != sPendingGroupStatus.end();) {
		if (it->frame < CurrentFrame()) {
			due.push_back(*it);
			it = sPendingGroupStatus.erase(it);
		} else {
			++it;
		}
	}
	for (const PendingGroupStatus& pending : due) {
		FireClientGroupStatus(pending.account, pending.group, pending.status);
	}
}

// ---------------------------------------------------------- server natives

static cell_t IsVACEnabled(IPluginContext*, const cell_t*) {
	return sServer.vacSecured ? 1 : 0;
}

// SteamTools' order: the first byte of the address in the first cell.
static cell_t GetPublicIP(IPluginContext* context, const cell_t* params) {
	if (sServer.publicIp == 0) {
		return 0;
	}
	cell_t* address = PluginCellsArg(context, params[1], 4);
	if (!address) {
		return 0;
	}
	for (int byte = 0; byte < 4; byte++) {
		address[byte] = static_cast<cell_t>((sServer.publicIp >> (24 - byte * 8)) & 0xFF);
	}
	return 1;
}

static cell_t GetPublicIPCell(IPluginContext*, const cell_t*) {
	return static_cast<cell_t>(sServer.publicIp);
}

static cell_t IsLoaded(IPluginContext*, const cell_t*) {
	return 1;
}

// Sets *field to the string argument; 0 after a native error.
static cell_t SetStringField(IPluginContext* context, cell_t address, std::string* field) {
	std::optional<std::string> text = PluginStringArg(context, address);
	if (!text) {
		return 0;
	}
	*field = *text;
	return 1;
}

static cell_t SetGameData(IPluginContext* context, const cell_t* params) {
	return SetStringField(context, params[1], &sServer.gameData);
}

static cell_t SetGameDescription(IPluginContext* context, const cell_t* params) {
	return SetStringField(context, params[1], &sServer.gameDescription);
}

static cell_t SetMapName(IPluginContext* context, const cell_t* params) {
	return SetStringField(context, params[1], &sServer.mapName);
}

static cell_t IsConnected(IPluginContext*, const cell_t*) {
	return sServer.connected ? 1 : 0;
}

static cell_t SetRule(IPluginContext* context, const cell_t* params) {
	std::optional<std::string> key = PluginStringArg(context, params[1]);
	std::optional<std::string> value = key ? PluginStringArg(context, params[2]) : std::nullopt;
	if (!value) {
		return 0;
	}
	sServer.rules[*key] = *value;
	return 1;
}

static cell_t ClearRules(IPluginContext*, const cell_t*) {
	sServer.rules.clear();
	return 1;
}

static cell_t SetAdvertiseServerActive(IPluginContext*, const cell_t* params) {
	sServer.advertising = params[1] != 0;
	return 1;
}

// Deprecated in the SDK; SteamWorks answers 0.
static cell_t ForceHeartbeat(IPluginContext*, const cell_t*) {
	return 0;
}

static cell_t GetFakeIP(IPluginContext* context, const cell_t* params) {
	if (sServer.fakeIp == 0) {
		return 0;
	}
	cell_t* ip = PluginCellsArg(context, params[1], 1);
	cell_t* port = ip ? PluginCellsArg(context, params[2], 1) : nullptr;
	if (!port) {
		return 0;
	}
	*ip = static_cast<cell_t>(sServer.fakeIp);
	*port = sServer.fakePort;
	return 1;
}

// ---------------------------------------------------------- client natives

static IGamePlayer* ConnectedPlayer(IPluginContext* context, cell_t client) {
	if (client < 1 || client > playerhelpers->GetMaxClients()) {
		context->ThrowNativeError("Client index %d is invalid", client);
		return nullptr;
	}
	IGamePlayer* player = playerhelpers->GetGamePlayer(client);
	if (!player || !player->IsConnected()) {
		context->ThrowNativeError("Client index %d is not connected", client);
		return nullptr;
	}
	return player;
}

// SteamWorks::CreateCommonCSteamID: the universe from the auth string.
static uint64_t SteamId64(IGamePlayer* player) {
	uint64_t universe = kUniversePublic;
	const char* auth = player->GetAuthString(false);
	if (auth && strlen(auth) >= 7 && auth[6] != 'I') {
		int given = atoi(auth[0] == '[' ? &auth[3] : &auth[6]);
		universe = given == 0 ? kUniversePublic : static_cast<uint64_t>(given);
	}
	return universe << 56 | kAccountTypeIndividual << 52 | kInstanceDesktop << 32 | player->GetSteamAccountID(false);
}

static bool IsConnectedAccount(unsigned int account) {
	for (int client = 1; client <= playerhelpers->GetMaxClients(); client++) {
		IGamePlayer* player = playerhelpers->GetGamePlayer(client);
		if (player && player->IsConnected() && !player->IsFakeClient() && player->GetSteamAccountID(false) == account) {
			return true;
		}
	}
	return false;
}

// A license a test set, else the defaults: TF2 owned, nothing else.
static License LicenseOf(unsigned int account, int app, bool known) {
	auto set = sLicenses.find({account, app});
	if (set != sLicenses.end()) {
		return set->second;
	}
	if (account == 0 || !known) {
		return License::NoAuth;
	}
	return app == kTf2AppId ? License::Has : License::DoesNotHave;
}

static cell_t HasLicenseForApp(IPluginContext* context, const cell_t* params) {
	IGamePlayer* player = ConnectedPlayer(context, params[1]);
	if (!player) {
		return 0;
	}
	unsigned int account = player->IsFakeClient() ? 0 : player->GetSteamAccountID(false);
	return static_cast<cell_t>(LicenseOf(account, params[2], true));
}

static cell_t HasLicenseForAppId(IPluginContext*, const cell_t* params) {
	unsigned int account = static_cast<unsigned int>(params[1]);
	return static_cast<cell_t>(LicenseOf(account, params[2], IsConnectedAccount(account)));
}

static cell_t GetClientSteamID(IPluginContext* context, const cell_t* params) {
	IGamePlayer* player = ConnectedPlayer(context, params[1]);
	if (!player) {
		return 0;
	}
	char* buffer = PluginBytesArg(context, params[2], params[3]);
	if (!buffer) {
		return 0;
	}
	size_t written = smutils->Format(buffer, static_cast<size_t>(params[3]), "%llu",
	                                 static_cast<unsigned long long>(SteamId64(player)));
	return static_cast<cell_t>(written + 1);
}

// The answer comes as SteamWorks_OnClientGroupStatus on the next frame.
static bool RequestGroupStatus(unsigned int account, unsigned int group) {
	if (account == 0) {
		return false;
	}
	auto set = sGroupStatus.find({account, group});
	GroupStatus status = set == sGroupStatus.end() ? GroupStatus{} : set->second;
	sPendingGroupStatus.push_back({CurrentFrame(), account, group, status});
	return true;
}

static cell_t GetUserGroupStatus(IPluginContext* context, const cell_t* params) {
	IGamePlayer* player = ConnectedPlayer(context, params[1]);
	if (!player) {
		return 0;
	}
	unsigned int account = player->IsFakeClient() ? 0 : player->GetSteamAccountID(false);
	return RequestGroupStatus(account, static_cast<unsigned int>(params[2])) ? 1 : 0;
}

static cell_t GetUserGroupStatusAuthID(IPluginContext*, const cell_t* params) {
	return RequestGroupStatus(static_cast<unsigned int>(params[1]), static_cast<unsigned int>(params[2])) ? 1 : 0;
}

// ------------------------------------------------------- not faked natives

// One stub per native, so that the error names it.
#define NOT_FAKED(native)                                                                                 \
	static cell_t NotFaked_##native(IPluginContext* context, const cell_t*) {                             \
		return context->ThrowNativeError(#native " is not faked by smrunner's SteamWorks stand-in "       \
		                                          "(GC messages and user stats are not simulated)");      \
	}
NOT_FAKED(SteamWorks_SendMessageToGC)
NOT_FAKED(SteamWorks_RequestStats)
NOT_FAKED(SteamWorks_RequestStatsAuthID)
NOT_FAKED(SteamWorks_GetStatCell)
NOT_FAKED(SteamWorks_GetStatAuthIDCell)
NOT_FAKED(SteamWorks_GetStatFloat)
NOT_FAKED(SteamWorks_GetStatAuthIDFloat)
#undef NOT_FAKED

static const sp_nativeinfo_t kSteamNatives[] = {
	{"SteamWorks_IsVACEnabled", IsVACEnabled},
	{"SteamWorks_GetPublicIP", GetPublicIP},
	{"SteamWorks_GetPublicIPCell", GetPublicIPCell},
	{"SteamWorks_IsLoaded", IsLoaded},
	{"SteamWorks_SetGameData", SetGameData},
	{"SteamWorks_SetGameDescription", SetGameDescription},
	{"SteamWorks_SetMapName", SetMapName},
	{"SteamWorks_IsConnected", IsConnected},
	{"SteamWorks_SetRule", SetRule},
	{"SteamWorks_ClearRules", ClearRules},
	{"SteamWorks_SetAdvertiseServerActive", SetAdvertiseServerActive},
	{"SteamWorks_GetFakeIP", GetFakeIP},
	{"SteamWorks_ForceHeartbeat", ForceHeartbeat},
	{"SteamWorks_HasLicenseForApp", HasLicenseForApp},
	{"SteamWorks_HasLicenseForAppId", HasLicenseForAppId},
	{"SteamWorks_GetClientSteamID", GetClientSteamID},
	{"SteamWorks_GetUserGroupStatus", GetUserGroupStatus},
	{"SteamWorks_GetUserGroupStatusAuthID", GetUserGroupStatusAuthID},
	{"SteamWorks_SendMessageToGC", NotFaked_SteamWorks_SendMessageToGC},
	{"SteamWorks_RequestStats", NotFaked_SteamWorks_RequestStats},
	{"SteamWorks_RequestStatsAuthID", NotFaked_SteamWorks_RequestStatsAuthID},
	{"SteamWorks_GetStatCell", NotFaked_SteamWorks_GetStatCell},
	{"SteamWorks_GetStatAuthIDCell", NotFaked_SteamWorks_GetStatAuthIDCell},
	{"SteamWorks_GetStatFloat", NotFaked_SteamWorks_GetStatFloat},
	{"SteamWorks_GetStatAuthIDFloat", NotFaked_SteamWorks_GetStatAuthIDFloat},
	{nullptr, nullptr},
};

void SteamStart() {
	sValidateClientOld = forwards->CreateForward("SW_OnValidateClient", ET_Ignore, 2, nullptr, Param_Cell, Param_Cell);
	sValidateClient = forwards->CreateForward("SteamWorks_OnValidateClient", ET_Ignore, 2, nullptr, Param_Cell, Param_Cell);
	sServersConnected = forwards->CreateForward("SteamWorks_SteamServersConnected", ET_Ignore, 0, nullptr);
	sServersConnectFailure = forwards->CreateForward("SteamWorks_SteamServersConnectFailure", ET_Ignore, 1, nullptr,
	                                                 Param_Cell);
	sServersDisconnected = forwards->CreateForward("SteamWorks_SteamServersDisconnected", ET_Ignore, 1, nullptr,
	                                               Param_Cell);
	sRestartRequested = forwards->CreateForward("SteamWorks_RestartRequested", ET_Hook, 0, nullptr);
	sTokenRequested = forwards->CreateForward("SteamWorks_TokenRequested", ET_Ignore, 2, nullptr, Param_String,
	                                          Param_Cell);
	sClientGroupStatus = forwards->CreateForward("SteamWorks_OnClientGroupStatus", ET_Ignore, 4, nullptr, Param_Cell,
	                                             Param_Cell, Param_Cell, Param_Cell);
	// Never fired: GC messages and user stats are not faked.
	sGCSendMessage = forwards->CreateForward("SteamWorks_GCSendMessage", ET_Event, 3, nullptr, Param_Cell,
	                                         Param_String, Param_Cell);
	sGCMsgAvailable = forwards->CreateForward("SteamWorks_GCMsgAvailable", ET_Ignore, 1, nullptr, Param_Cell);
	sGCRetrieveMessage = forwards->CreateForward("SteamWorks_GCRetrieveMessage", ET_Event, 4, nullptr, Param_Cell,
	                                             Param_String, Param_Cell, Param_Cell);
	sUserStatsReceived = forwards->CreateForward("SteamWorks_OnUserStatsReceived", ET_Ignore, 2, nullptr, Param_Cell,
	                                             Param_Cell);
	sUserStatsUnloaded = forwards->CreateForward("SteamWorks_OnUserStatsUnloaded", ET_Ignore, 1, nullptr, Param_Cell);
	sUserStatsStored = forwards->CreateForward("SteamWorks_OnUserStatsStored", ET_Ignore, 2, nullptr, Param_Cell,
	                                           Param_Cell);
	sharesys->AddNatives(myself, kSteamNatives);
}

void SteamStop() {
	for (IForward* forward :
	     {sValidateClientOld, sValidateClient, sServersConnected, sServersConnectFailure, sServersDisconnected,
	      sRestartRequested, sTokenRequested, sClientGroupStatus, sGCSendMessage, sGCMsgAvailable, sGCRetrieveMessage,
	      sUserStatsReceived, sUserStatsUnloaded, sUserStatsStored}) {
		forwards->ReleaseForward(forward);
	}
}

}  // namespace steamworks
