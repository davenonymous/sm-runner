// Copyright 2026 davenonymous
//
// The natives of smrunner.inc for simulated clients: connecting them, what
// they do and what they were sent.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <string>

#include "core/core.h"
#include "core/players.h"
#include "core/sourcemod_api.h"
#include "testing/natives_common.h"

using namespace SourceMod;
using namespace SourcePawn;

namespace smrunner {

static std::string sRejectReason;

// SourceMod checks for authorized clients every 0.7 seconds of game time;
// a client is authorized within this much of it.
static constexpr float kAuthorizeSeconds = 2.0f;

// A Steam ID as STEAM_0:1:23, [U:1:47] or 64-bit; "" is none (0). Every
// form must fill the whole text.
static bool ParseSteamId(const std::string& text, uint64_t* steamId64) {
	constexpr uint64_t kBase = 76561197960265728ULL;
	unsigned int universe, low, high;
	if (text.empty()) {
		*steamId64 = 0;
		return true;
	}
	// %n is only reached when everything before it matched.
	int consumed = -1;
	bool whole = sscanf(text.c_str(), "STEAM_%u:%u:%u%n", &universe, &low, &high, &consumed) == 3 &&
	             static_cast<size_t>(consumed) == text.size();
	if (whole && low <= 1) {
		*steamId64 = kBase + static_cast<uint64_t>(high) * 2 + low;
		return true;
	}
	consumed = -1;
	whole = sscanf(text.c_str(), "[U:1:%u]%n", &low, &consumed) == 1 && static_cast<size_t>(consumed) == text.size();
	if (whole) {
		*steamId64 = kBase + low;
		return true;
	}
	char* end;
	unsigned long long value = strtoull(text.c_str(), &end, 10);
	if (*end == '\0' && value > kBase) {
		*steamId64 = value;
		return true;
	}
	return false;
}

static cell_t Runner_ConnectClient(IPluginContext* context, const cell_t* params) {
	std::optional<std::string> name = StringArg(context, params[1]);
	std::optional<std::string> steamId = name ? StringArg(context, params[2]) : std::nullopt;
	std::optional<std::string> ip = steamId ? StringArg(context, params[4]) : std::nullopt;
	if (!ip) {
		return 0;
	}
	ClientIdentity identity;
	identity.name = *name;
	if (!ParseSteamId(*steamId, &identity.steamId64)) {
		return context->ThrowNativeError("\"%s\" is not a Steam ID (STEAM_0:1:23, [U:1:47] or 64-bit)",
		                                 steamId->c_str());
	}
	identity.ip = *ip;
	// SourceMod copies a client's address into 64 bytes (GetClientIP).
	if (identity.ip.size() >= 64) {
		return context->ThrowNativeError("the IP \"%s\" has %zu characters; an address has fewer than 64",
		                                 identity.ip.c_str(), identity.ip.size());
	}
	bool authorize = params[3] != 0;
	// Authorizing takes frames.
	if (authorize && !FramesAllowedOrError(context)) {
		return 0;
	}
	PlayerManager& players = Core::Get().players();
	sRejectReason.clear();
	int client = players.Connect(identity, &sRejectReason);
	if (client == 0 || !authorize) {
		return client;
	}
	// Authorization is SourceMod's.
	IGamePlayer* player = players.GetGamePlayer(client);
	int frames = static_cast<int>(ceilf(kAuthorizeSeconds / Core::Get().engine().globals().interval_per_tick));
	Core::Get().RunFramesUntil([player]() { return !player->IsConnected() || player->IsAuthorized(); }, frames);
	return player->IsConnected() ? client : 0;
}

static cell_t Runner_ConnectBot(IPluginContext* context, const cell_t* params) {
	std::optional<std::string> name = StringArg(context, params[1]);
	if (!name) {
		return 0;
	}
	ClientIdentity identity;
	identity.name = *name;
	identity.fake = true;
	sRejectReason.clear();
	return Core::Get().players().Connect(identity, &sRejectReason);
}

static cell_t Runner_GetRejectReason(IPluginContext* context, const cell_t* params) {
	return WriteStringArg(context, params[1], params[2], sRejectReason);
}

static cell_t Runner_Disconnect(IPluginContext* context, const cell_t* params) {
	if (!ConnectedClientOrError(context, params[1])) {
		return 0;
	}
	std::optional<std::string> reason = StringArg(context, params[2]);
	if (!reason) {
		return 0;
	}
	Core::Get().players().Disconnect(params[1], reason->c_str());
	return 1;
}

static cell_t Runner_ClientCommand(IPluginContext* context, const cell_t* params) {
	if (!ConnectedClientOrError(context, params[1])) {
		return 0;
	}
	Core::Get().sourcemod().SetGlobalTarget(params[1]);
	std::string command = FormattedArg(context, params, 2);
	// The game's client adds what is typed to its own command buffer: a
	// wait there delays its later commands by its own frames, which a
	// simulated client does not have, and a command too long is refused and
	// never reaches the server.
	Console& console = Core::Get().engine().console();
	for (const std::string& line : Console::SplitCommandLine(command)) {
		if (console.WaitsAllowed() && Console::IsWaitCommand(line)) {
			return context->ThrowNativeError(
				"\"%s\" holds a wait: a client would run the commands after it in later frames of its own, "
				"which simulated clients do not have; send them with another Runner.ClientCommand after "
				"Runner.Frames",
				command.c_str());
		}
		if (line.size() >= CommandArgs::kMaxLength - 1) {
			return context->ThrowNativeError(
				"a client cannot send a command of %zu characters; its command buffer takes at most %zu", line.size(),
				CommandArgs::kMaxLength - 2);
		}
	}
	Core::Get().players().ExecuteClientCommand(params[1], command);
	return 1;
}

// The client changes its userinfo; the server sees the change on the next
// frame (OnClientSettingsChanged).
static cell_t Runner_SetClientConVar(IPluginContext* context, const cell_t* params) {
	SimulatedClient* slot = ConnectedClientOrError(context, params[1]);
	if (!slot) {
		return 0;
	}
	std::optional<std::string> name = StringArg(context, params[2]);
	std::optional<std::string> value = name ? StringArg(context, params[3]) : std::nullopt;
	if (!value) {
		return 0;
	}
	slot->SetConVar(*name, *value);
	return 1;
}

static cell_t Runner_SetClientTeam(IPluginContext* context, const cell_t* params) {
	SimulatedClient* slot = ConnectedClientOrError(context, params[1]);
	if (slot) {
		slot->Info().team = params[2];
	}
	return 1;
}

static cell_t Runner_SetClientAlive(IPluginContext* context, const cell_t* params) {
	SimulatedClient* slot = ConnectedClientOrError(context, params[1]);
	if (slot) {
		slot->Info().alive = params[2] != 0;
	}
	return 1;
}

static cell_t Runner_SetClientHealth(IPluginContext* context, const cell_t* params) {
	SimulatedClient* slot = ConnectedClientOrError(context, params[1]);
	if (slot) {
		slot->Info().health = params[2];
	}
	return 1;
}

static cell_t Runner_ClientReceived(IPluginContext* context, const cell_t* params) {
	SimulatedClient* slot = ClientSlotOrError(context, params[1]);
	std::optional<std::string> text = slot ? StringArg(context, params[3]) : std::nullopt;
	return text && HasMessage(*slot, params[2], *text);
}

static cell_t Runner_ClientMessageCount(IPluginContext* context, const cell_t* params) {
	SimulatedClient* slot = ClientSlotOrError(context, params[1]);
	if (!slot) {
		return 0;
	}
	cell_t count = 0;
	for (const ClientMessage& message : slot->Received()) {
		if (params[2] < 0 || message.kind == params[2]) {
			count++;
		}
	}
	return count;
}

static cell_t Runner_GetClientMessage(IPluginContext* context, const cell_t* params) {
	SimulatedClient* slot = ClientSlotOrError(context, params[1]);
	if (!slot || params[2] < 0 || params[2] >= static_cast<cell_t>(slot->Received().size())) {
		return -1;
	}
	const ClientMessage& message = slot->Received()[static_cast<size_t>(params[2])];
	WriteStringArg(context, params[3], params[4], message.text);
	return message.kind;
}

static cell_t Runner_ClearClientMessages(IPluginContext* context, const cell_t* params) {
	SimulatedClient* slot = ClientSlotOrError(context, params[1]);
	if (slot) {
		slot->ClearReceived();
	}
	return 1;
}

REGISTER_NATIVES(clientNatives) {
	{"Runner.ConnectClient", Runner_ConnectClient},
	{"Runner.ConnectBot", Runner_ConnectBot},
	{"Runner.GetRejectReason", Runner_GetRejectReason},
	{"Runner.Disconnect", Runner_Disconnect},
	{"Runner.ClientCommand", Runner_ClientCommand},
	{"Runner.SetClientConVar", Runner_SetClientConVar},
	{"Runner.SetClientTeam", Runner_SetClientTeam},
	{"Runner.SetClientAlive", Runner_SetClientAlive},
	{"Runner.SetClientHealth", Runner_SetClientHealth},
	{"Runner.ClientReceived", Runner_ClientReceived},
	{"Runner.ClientMessageCount", Runner_ClientMessageCount},
	{"Runner.GetClientMessage", Runner_GetClientMessage},
	{"Runner.ClearClientMessages", Runner_ClearClientMessages},
	{nullptr, nullptr},
};

}  // namespace smrunner
