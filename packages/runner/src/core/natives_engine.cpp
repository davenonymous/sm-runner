// Copyright 2026 davenonymous
//
// The natives SourceMod's core implements on the engine, on the simulated
// engine: core/smn_halflife.cpp (time, maps, random numbers, text to
// clients as user messages), core/smn_vector.cpp (the mathlib functions it calls),
// core/smn_commandline.cpp and the network statistics
// of core/smn_player.cpp, from SourceMod 1.12 (GPLv3, AlliedModders LLC).
// The core natives the runner does not simulate are bound to a stub that
// fails with the native's name, so a plugin that uses them still loads.
#include <math.h>
#include <string.h>

#include <random>
#include <string>
#include <vector>

#include "core/core.h"
#include "core/players.h"
#include "core/sourcemod_api.h"
#include "core/timers.h"
#include "core/usermsgs.h"
#include "testing/plugin_memory.h"

using namespace SourceMod;
using namespace SourcePawn;

namespace smrunner {

// ------------------------------------------------------------------- random

// The engine's uniform random stream; seeded with 0 so runs repeat.
static std::mt19937& RandomStream() {
	static std::mt19937 stream(0);
	return stream;
}

static cell_t SetRandomSeed(IPluginContext*, const cell_t* params) {
	RandomStream().seed(static_cast<uint32_t>(params[1]));
	return 1;
}

static cell_t GetRandomFloat(IPluginContext*, const cell_t* params) {
	float low = sp_ctof(params[1]);
	float high = sp_ctof(params[2]);
	float unit = std::uniform_real_distribution<float>(0.0f, 1.0f)(RandomStream());
	return sp_ftoc(low + unit * (high - low));
}

static cell_t GetRandomInt(IPluginContext*, const cell_t* params) {
	int low = params[1];
	int high = params[2];
	if (high <= low) {
		return low;
	}
	return std::uniform_int_distribution<int>(low, high)(RandomStream());
}

// --------------------------------------------------------------- the server

static cell_t IsMapValid(IPluginContext* context, const cell_t* params) {
	char* map = PluginCharsArg(context, params[1]);
	return map && Core::Get().engine().MapExists(map);
}

// FindMapResult: Found 0, NotFound 1.
static cell_t FindMap(IPluginContext* context, const cell_t* params) {
	char* map = PluginCharsArg(context, params[1]);
	if (!map) {
		return 0;
	}
	bool found = Core::Get().engine().MapExists(map);
	if (params[0] >= 3 && !WritePluginString(context, params[2], params[3], std::string(map))) {
		return 0;
	}
	return found ? 0 : 1;
}

static cell_t GetMapDisplayName(IPluginContext* context, const cell_t* params) {
	char* map = PluginCharsArg(context, params[1]);
	if (!map || !Core::Get().engine().MapExists(map)) {
		return 0;
	}
	std::string name = map;
	size_t slash = name.rfind('/');
	if (slash != std::string::npos) {
		name = name.substr(slash + 1);
	}
	return WritePluginString(context, params[2], params[3], name) ? 1 : 0;
}

static cell_t IsDedicatedServer(IPluginContext*, const cell_t*) {
	return 1;
}

static cell_t GetEngineTime(IPluginContext*, const cell_t*) {
	return sp_ftoc(static_cast<float>(Core::Get().engine().EngineTime()));
}

static cell_t GetGameTime(IPluginContext*, const cell_t*) {
	return sp_ftoc(Core::Get().engine().globals().curtime);
}

static cell_t GetGameTickCount(IPluginContext*, const cell_t*) {
	return Core::Get().engine().globals().tickcount;
}

static cell_t GetGameFrameTime(IPluginContext*, const cell_t*) {
	return sp_ftoc(Core::Get().engine().globals().frametime);
}

static cell_t CreateFakeClient(IPluginContext* context, const cell_t* params) {
	if (!Core::Get().IsMapStarted()) {
		return context->ThrowNativeError("Cannot create fakeclient when no map is active");
	}
	char* name = PluginCharsArg(context, params[1]);
	if (!name) {
		return 0;
	}
	ClientIdentity identity;
	identity.name = name;
	identity.fake = true;
	return Core::Get().players().Connect(identity, nullptr);
}

static SimPlayer* ConnectedPlayer(IPluginContext* context, int client) {
	SimPlayer* player = Core::Get().players().Player(client);
	if (!player) {
		context->ThrowNativeError("Client index %d is invalid", client);
		return nullptr;
	}
	if (!player->IsConnected()) {
		context->ThrowNativeError("Client %d is not connected", client);
		return nullptr;
	}
	return player;
}

static SimPlayer* InGamePlayer(IPluginContext* context, int client) {
	SimPlayer* player = Core::Get().players().Player(client);
	if (!player) {
		context->ThrowNativeError("Client index %d is invalid", client);
		return nullptr;
	}
	if (!player->IsInGame()) {
		context->ThrowNativeError("Client %d is not in game", client);
		return nullptr;
	}
	return player;
}

static cell_t SetFakeClientConVar(IPluginContext* context, const cell_t* params) {
	SimPlayer* player = ConnectedPlayer(context, params[1]);
	if (!player) {
		return 0;
	}
	if (!player->IsFakeClient()) {
		return context->ThrowNativeError("Client %d is not a fake client", params[1]);
	}
	char* name = PluginCharsArg(context, params[2]);
	char* value = name ? PluginCharsArg(context, params[3]) : nullptr;
	if (!value) {
		return 0;
	}
	Core::Get().engine().clients().Slot(params[1])->SetConVar(name, value);
	return 1;
}

static cell_t GetGameDescription(IPluginContext* context, const cell_t* params) {
	return WritePluginString(context, params[1], params[2], Core::Get().engine().config().gameDescription) ? 1 : 0;
}

static cell_t GetGameFolderName(IPluginContext* context, const cell_t* params) {
	return WritePluginString(context, params[1], params[2], Core::Get().sourcemod().GetGameFolderName()) ? 1 : 0;
}

static cell_t GetCurrentMap(IPluginContext* context, const cell_t* params) {
	std::optional<size_t> bytes = WritePluginString(context, params[1], params[2], Core::Get().engine().MapName());
	return bytes ? static_cast<cell_t>(*bytes) : 0;
}

// Precaching: the engine's tables of the current map (engine/engine.h).
static cell_t Precache(IPluginContext* context, const cell_t* params, PrecacheTable table) {
	char* name = PluginCharsArg(context, params[1]);
	return name ? Core::Get().engine().Precache(table, name) : 0;
}

static cell_t IsPrecached(IPluginContext* context, const cell_t* params, PrecacheTable table) {
	char* name = PluginCharsArg(context, params[1]);
	return name && Core::Get().engine().IsPrecached(table, name) ? 1 : 0;
}

static cell_t PrecacheModel(IPluginContext* c, const cell_t* p) { return Precache(c, p, PrecacheTable::Model); }
static cell_t IsModelPrecached(IPluginContext* c, const cell_t* p) { return IsPrecached(c, p, PrecacheTable::Model); }
static cell_t PrecacheSound(IPluginContext* c, const cell_t* p) { return Precache(c, p, PrecacheTable::Sound) != 0; }
static cell_t IsSoundPrecached(IPluginContext* c, const cell_t* p) { return IsPrecached(c, p, PrecacheTable::Sound); }
static cell_t PrecacheDecal(IPluginContext* c, const cell_t* p) { return Precache(c, p, PrecacheTable::Decal); }
static cell_t IsDecalPrecached(IPluginContext* c, const cell_t* p) { return IsPrecached(c, p, PrecacheTable::Decal); }
static cell_t PrecacheGeneric(IPluginContext* c, const cell_t* p) { return Precache(c, p, PrecacheTable::Generic); }
static cell_t IsGenericPrecached(IPluginContext* c, const cell_t* p) {
	return IsPrecached(c, p, PrecacheTable::Generic);
}
static cell_t PrecacheSentenceFile(IPluginContext* c, const cell_t* p) { return Precache(c, p, PrecacheTable::Generic); }

// PrintToChat and PrintCenterText send a TextMsg, PrintHintText a HintText.
enum class TextChannel { Chat, Center, Hint };

static bool SendTextOn(TextChannel channel, int client, const char* text) {
	if (channel == TextChannel::Hint) {
		return SendHintText(client, text);
	}
	return SendTextMsg(client, channel == TextChannel::Chat ? kHudPrintTalk : kHudPrintCenter, text);
}

static cell_t SendText(IPluginContext* context, const cell_t* params, TextChannel channel) {
	SimPlayer* player = InGamePlayer(context, params[1]);
	if (!player) {
		return 0;
	}
	SourceModImpl& sourcemod = Core::Get().sourcemod();
	sourcemod.SetGlobalTarget(params[1]);
	char buffer[254];
	{
		DetectExceptions exceptions(context);
		sourcemod.FormatString(buffer, sizeof(buffer), context, params, 2);
		if (exceptions.HasException()) {
			return 0;
		}
	}
	if (!SendTextOn(channel, params[1], buffer)) {
		return context->ThrowNativeError("Could not send a usermessage");
	}
	return 1;
}

static cell_t PrintToChat(IPluginContext* c, const cell_t* p) { return SendText(c, p, TextChannel::Chat); }
static cell_t PrintCenterText(IPluginContext* c, const cell_t* p) { return SendText(c, p, TextChannel::Center); }
static cell_t PrintHintText(IPluginContext* c, const cell_t* p) { return SendText(c, p, TextChannel::Hint); }

static cell_t IsPlayerAlive(IPluginContext* context, const cell_t* params) {
	SimPlayer* player = InGamePlayer(context, params[1]);
	return player ? player->GetPlayerInfo()->alive : 0;
}

// SOURCE_SDK_EPISODE2VALVE, as SourceMod reports TF2.
static cell_t GuessSDKVersion(IPluginContext*, const cell_t*) {
	return 35;
}

static cell_t GetEngineVersion(IPluginContext*, const cell_t*) {
	return 17;   // Engine_TF2
}

static cell_t EntIndexToEntRef(IPluginContext*, const cell_t* params) {
	return params[1];
}

static cell_t EntRefToEntIndex(IPluginContext*, const cell_t* params) {
	cell_t reference = params[1];
	if (reference == -1) {
		return -1;
	}
	return (reference & (1 << 31)) ? (reference & 0xFFF) : reference;
}

static cell_t MakeCompatEntRef(IPluginContext*, const cell_t* params) {
	return params[1];
}

// -------------------------------------------------------------- vector math

// A vector argument's 3 cells, read or written; false after the native
// error.
static bool ReadVector(IPluginContext* context, cell_t address, float out[3]) {
	cell_t* cells = PluginCellsArg(context, address, 3);
	if (!cells) {
		return false;
	}
	for (int i = 0; i < 3; i++) {
		out[i] = sp_ctof(cells[i]);
	}
	return true;
}

static bool WriteVector(IPluginContext* context, cell_t address, const float in[3]) {
	cell_t* cells = PluginCellsArg(context, address, 3);
	if (!cells) {
		return false;
	}
	for (int i = 0; i < 3; i++) {
		cells[i] = sp_ftoc(in[i]);
	}
	return true;
}

static float Dot(const float a[3], const float b[3]) {
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void Cross(const float a[3], const float b[3], float out[3]) {
	out[0] = a[1] * b[2] - a[2] * b[1];
	out[1] = a[2] * b[0] - a[0] * b[2];
	out[2] = a[0] * b[1] - a[1] * b[0];
}

// mathlib's VectorNormalize
static float Normalize(float v[3]) {
	float radius = sqrtf(Dot(v, v));
	float inverse = 1.0f / (radius + 1.192092896e-07f);
	for (int i = 0; i < 3; i++) {
		v[i] *= inverse;
	}
	return radius;
}

static cell_t GetVectorLength(IPluginContext* context, const cell_t* params) {
	float v[3];
	if (!ReadVector(context, params[1], v)) {
		return 0;
	}
	float squared = Dot(v, v);
	return sp_ftoc(params[2] ? squared : sqrtf(squared));
}

static cell_t GetVectorDistance(IPluginContext* context, const cell_t* params) {
	float a[3], b[3];
	if (!ReadVector(context, params[1], a) || !ReadVector(context, params[2], b)) {
		return 0;
	}
	float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
	float squared = Dot(d, d);
	return sp_ftoc(params[3] ? squared : sqrtf(squared));
}

static cell_t GetVectorDotProduct(IPluginContext* context, const cell_t* params) {
	float a[3], b[3];
	if (!ReadVector(context, params[1], a) || !ReadVector(context, params[2], b)) {
		return 0;
	}
	return sp_ftoc(Dot(a, b));
}

static cell_t GetVectorCrossProduct(IPluginContext* context, const cell_t* params) {
	float a[3], b[3], out[3];
	if (!ReadVector(context, params[1], a) || !ReadVector(context, params[2], b)) {
		return 0;
	}
	Cross(a, b, out);
	return WriteVector(context, params[3], out) ? 1 : 0;
}

static constexpr float kDegrees = 3.14159265358979323846f / 180.0f;

// mathlib's AngleVectors
static cell_t GetAngleVectors(IPluginContext* context, const cell_t* params) {
	float angles[3];
	if (!ReadVector(context, params[1], angles)) {
		return 0;
	}
	float sp = sinf(angles[0] * kDegrees), cp = cosf(angles[0] * kDegrees);
	float sy = sinf(angles[1] * kDegrees), cy = cosf(angles[1] * kDegrees);
	float sr = sinf(angles[2] * kDegrees), cr = cosf(angles[2] * kDegrees);
	float forward[3] = {cp * cy, cp * sy, -sp};
	float right[3] = {-1 * sr * sp * cy + -1 * cr * -sy, -1 * sr * sp * sy + -1 * cr * cy, -1 * sr * cp};
	float up[3] = {cr * sp * cy + -sr * -sy, cr * sp * sy + -sr * cy, cr * cp};
	return WriteVector(context, params[2], forward) && WriteVector(context, params[3], right) &&
	       WriteVector(context, params[4], up);
}

// mathlib's VectorAngles
static cell_t GetVectorAngles(IPluginContext* context, const cell_t* params) {
	float forward[3];
	if (!ReadVector(context, params[1], forward)) {
		return 0;
	}
	float angles[3] = {0.0f, 0.0f, 0.0f};
	if (forward[1] == 0.0f && forward[0] == 0.0f) {
		angles[0] = forward[2] > 0.0f ? 270.0f : 90.0f;
	} else {
		angles[1] = atan2f(forward[1], forward[0]) / kDegrees;
		if (angles[1] < 0.0f) {
			angles[1] += 360.0f;
		}
		float flat = sqrtf(forward[0] * forward[0] + forward[1] * forward[1]);
		angles[0] = atan2f(-forward[2], flat) / kDegrees;
		if (angles[0] < 0.0f) {
			angles[0] += 360.0f;
		}
	}
	return WriteVector(context, params[2], angles) ? 1 : 0;
}

// mathlib's VectorVectors
static cell_t GetVectorVectors(IPluginContext* context, const cell_t* params) {
	float forward[3];
	if (!ReadVector(context, params[1], forward)) {
		return 0;
	}
	float right[3], up[3];
	if (forward[0] == 0.0f && forward[1] == 0.0f) {
		right[0] = 0.0f;
		right[1] = -1.0f;
		right[2] = 0.0f;
		up[0] = -forward[2];
		up[1] = 0.0f;
		up[2] = 0.0f;
	} else {
		float z[3] = {0.0f, 0.0f, 1.0f};
		Cross(forward, z, right);
		Normalize(right);
		Cross(right, forward, up);
		Normalize(up);
	}
	return WriteVector(context, params[2], right) && WriteVector(context, params[3], up);
}

static cell_t NormalizeVector(IPluginContext* context, const cell_t* params) {
	float v[3];
	if (!ReadVector(context, params[1], v)) {
		return 0;
	}
	float length = Normalize(v);
	return WriteVector(context, params[2], v) ? sp_ftoc(length) : 0;
}

// ------------------------------------------------------------- command line

// The command line of a TF2 server started by hand.
static const char kCommandLine[] = "./srcds_linux -game tf";

static std::vector<std::string> CommandLineWords() {
	std::vector<std::string> words;
	CommandArgs args(kCommandLine);
	for (int i = 0; i < args.ArgC(); i++) {
		words.push_back(args.Arg(i));
	}
	return words;
}

static bool FindParam(const char* name, std::string* value) {
	std::vector<std::string> words = CommandLineWords();
	for (size_t i = 0; i < words.size(); i++) {
		if (words[i] != name) {
			continue;
		}
		if (value) {
			*value = (i + 1 < words.size() && words[i + 1][0] != '-' && words[i + 1][0] != '+') ? words[i + 1] : "";
		}
		return true;
	}
	return false;
}

static cell_t GetCommandLine(IPluginContext* context, const cell_t* params) {
	return WritePluginString(context, params[1], params[2], kCommandLine) ? 1 : 0;
}

static cell_t GetCommandLineParam(IPluginContext* context, const cell_t* params) {
	char* name = PluginCharsArg(context, params[1]);
	char* fallback = name ? PluginCharsArg(context, params[4]) : nullptr;
	if (!fallback) {
		return 0;
	}
	std::string value;
	if (!FindParam(name, &value)) {
		value = fallback;
	}
	return WritePluginString(context, params[2], params[3], value) ? 1 : 0;
}

static cell_t GetCommandLineParamInt(IPluginContext* context, const cell_t* params) {
	char* name = PluginCharsArg(context, params[1]);
	std::string value;
	if (!name) {
		return 0;
	}
	return FindParam(name, &value) ? atoi(value.c_str()) : params[2];
}

static cell_t GetCommandLineParamFloat(IPluginContext* context, const cell_t* params) {
	char* name = PluginCharsArg(context, params[1]);
	std::string value;
	if (!name) {
		return 0;
	}
	return FindParam(name, &value) ? sp_ftoc(static_cast<float>(atof(value.c_str()))) : params[2];
}

static cell_t FindCommandLineParam(IPluginContext* context, const cell_t* params) {
	char* name = PluginCharsArg(context, params[1]);
	return name && FindParam(name, nullptr) ? 1 : 0;
}

// ------------------------------------------------------- client net stats

static cell_t GetMaxHumanPlayers(IPluginContext*, const cell_t*) {
	return Core::Get().players().GetMaxClients();
}

// core/smn_player.cpp: the network natives want a connected human.
static SimPlayer* HumanOrError(IPluginContext* context, int client) {
	SimPlayer* player = ConnectedPlayer(context, client);
	if (player && player->IsFakeClient()) {
		context->ThrowNativeError("Client %d is a bot", client);
		return nullptr;
	}
	return player;
}

static cell_t GetClientTime(IPluginContext* context, const cell_t* params) {
	SimPlayer* player = HumanOrError(context, params[1]);
	if (!player) {
		return 0;
	}
	double connectedAt = Core::Get().engine().clients().Slot(params[1])->ConnectedAt();
	return sp_ftoc(static_cast<float>(Core::Get().engine().EngineTime() - connectedAt));
}

// The simulated clients have a perfect network.
static cell_t ZeroNetStat(IPluginContext* context, const cell_t* params) {
	return HumanOrError(context, params[1]) ? sp_ftoc(0.0f) : 0;
}

static cell_t IsClientTimingOut(IPluginContext* context, const cell_t* params) {
	HumanOrError(context, params[1]);
	return 0;
}

static cell_t GetClientDataRate(IPluginContext* context, const cell_t* params) {
	return HumanOrError(context, params[1]) ? 80000 : 0;
}

static cell_t RunAdminCacheChecks(IPluginContext* context, const cell_t* params) {
	SimPlayer* player = InGamePlayer(context, params[1]);
	if (!player) {
		return 0;
	}
	if (!player->IsAuthorized()) {
		return context->ThrowNativeError("Client %d is not authorized", params[1]);
	}
	return player->RunAdminCacheChecks();
}

REGISTER_NATIVES(engineNatives) {
	{"SetRandomSeed", SetRandomSeed},
	{"GetRandomFloat", GetRandomFloat},
	{"GetRandomInt", GetRandomInt},
	{"IsMapValid", IsMapValid},
	{"FindMap", FindMap},
	{"GetMapDisplayName", GetMapDisplayName},
	{"IsDedicatedServer", IsDedicatedServer},
	{"GetEngineTime", GetEngineTime},
	{"GetGameTime", GetGameTime},
	{"GetGameTickCount", GetGameTickCount},
	{"GetGameFrameTime", GetGameFrameTime},
	{"CreateFakeClient", CreateFakeClient},
	{"SetFakeClientConVar", SetFakeClientConVar},
	{"GetGameDescription", GetGameDescription},
	{"GetGameFolderName", GetGameFolderName},
	{"GetCurrentMap", GetCurrentMap},
	{"PrecacheModel", PrecacheModel},
	{"PrecacheSentenceFile", PrecacheSentenceFile},
	{"PrecacheDecal", PrecacheDecal},
	{"PrecacheGeneric", PrecacheGeneric},
	{"IsModelPrecached", IsModelPrecached},
	{"IsDecalPrecached", IsDecalPrecached},
	{"IsGenericPrecached", IsGenericPrecached},
	{"PrecacheSound", PrecacheSound},
	{"IsSoundPrecached", IsSoundPrecached},
	{"PrintToChat", PrintToChat},
	{"PrintCenterText", PrintCenterText},
	{"PrintHintText", PrintHintText},
	{"IsPlayerAlive", IsPlayerAlive},
	{"GuessSDKVersion", GuessSDKVersion},
	{"GetEngineVersion", GetEngineVersion},
	{"EntIndexToEntRef", EntIndexToEntRef},
	{"EntRefToEntIndex", EntRefToEntIndex},
	{"MakeCompatEntRef", MakeCompatEntRef},
	{"GetAngleVectors", GetAngleVectors},
	{"GetVectorAngles", GetVectorAngles},
	{"GetVectorCrossProduct", GetVectorCrossProduct},
	{"GetVectorDistance", GetVectorDistance},
	{"GetVectorDotProduct", GetVectorDotProduct},
	{"GetVectorLength", GetVectorLength},
	{"GetVectorVectors", GetVectorVectors},
	{"NormalizeVector", NormalizeVector},
	{"GetCommandLine", GetCommandLine},
	{"GetCommandLineParam", GetCommandLineParam},
	{"GetCommandLineParamInt", GetCommandLineParamInt},
	{"GetCommandLineParamFloat", GetCommandLineParamFloat},
	{"FindCommandLineParam", FindCommandLineParam},
	{"GetMaxHumanPlayers", GetMaxHumanPlayers},
	{"GetClientTime", GetClientTime},
	{"GetClientDataRate", GetClientDataRate},
	{"IsClientTimingOut", IsClientTimingOut},
	{"GetClientLatency", ZeroNetStat},
	{"GetClientAvgLatency", ZeroNetStat},
	{"GetClientAvgLoss", ZeroNetStat},
	{"GetClientAvgChoke", ZeroNetStat},
	{"GetClientAvgData", ZeroNetStat},
	{"GetClientAvgPackets", ZeroNetStat},
	{"RunAdminCacheChecks", RunAdminCacheChecks},
	{nullptr, nullptr},
};

// ------------------------------------------------------ unsupported natives

// The native being called is the innermost frame.
static cell_t Unsupported(IPluginContext* context, const cell_t*) {
	IFrameIterator* frames = context->CreateFrameIterator();
	const char* name = nullptr;
	for (; !frames->Done(); frames->Next()) {
		if (frames->IsNativeFrame()) {
			name = frames->FunctionName();
			break;
		}
	}
	std::string native = name ? name : "?";
	context->DestroyFrameIterator(frames);
	return context->ThrowNativeError("Native \"%s\" is not supported by smrunner (the simulated server has no %s)",
	                                 native.c_str(), "engine for it");
}

#define UNSUPPORTED(name) {name, Unsupported}

REGISTER_NATIVES(unsupportedNatives) {
	// core/smn_halflife.cpp
	UNSUPPORTED("CreateDialog"), UNSUPPORTED("ShowVGUIPanel"), UNSUPPORTED("GetClientsInRange"),
	// core/smn_console.cpp
	UNSUPPORTED("QueryClientConVar"), UNSUPPORTED("FakeClientCommandKeyValues"),
	// core/smn_entities.cpp
	UNSUPPORTED("ChangeEdictState"), UNSUPPORTED("CreateEdict"), UNSUPPORTED("FindDataMapOffs"),
	UNSUPPORTED("FindSendPropInfo"), UNSUPPORTED("FindSendPropOffs"), UNSUPPORTED("GetEdictClassname"),
	UNSUPPORTED("GetEdictFlags"), UNSUPPORTED("GetEntData"), UNSUPPORTED("GetEntDataEnt"),
	UNSUPPORTED("GetEntDataEnt2"), UNSUPPORTED("GetEntDataFloat"), UNSUPPORTED("GetEntDataString"),
	UNSUPPORTED("GetEntDataVector"), UNSUPPORTED("GetEntProp"), UNSUPPORTED("GetEntPropArraySize"),
	UNSUPPORTED("GetEntPropEnt"), UNSUPPORTED("GetEntPropFloat"), UNSUPPORTED("GetEntPropString"),
	UNSUPPORTED("GetEntPropVector"), UNSUPPORTED("GetEntityCount"), UNSUPPORTED("GetEntityFlags"),
	UNSUPPORTED("GetEntityNetClass"), UNSUPPORTED("GetMaxEntities"), UNSUPPORTED("IsEntNetworkable"),
	UNSUPPORTED("IsValidEdict"), UNSUPPORTED("IsValidEntity"), UNSUPPORTED("RemoveEntity"),
	UNSUPPORTED("RemoveEdict"), UNSUPPORTED("SetEdictFlags"), UNSUPPORTED("SetEntData"),
	UNSUPPORTED("SetEntDataEnt"), UNSUPPORTED("SetEntDataEnt2"), UNSUPPORTED("SetEntDataFloat"),
	UNSUPPORTED("SetEntDataVector"), UNSUPPORTED("SetEntDataString"), UNSUPPORTED("SetEntityFlags"),
	UNSUPPORTED("SetEntProp"), UNSUPPORTED("SetEntPropEnt"), UNSUPPORTED("SetEntPropFloat"),
	UNSUPPORTED("SetEntPropString"), UNSUPPORTED("SetEntPropVector"), UNSUPPORTED("GetEntityAddress"),
	UNSUPPORTED("FindDataMapInfo"), UNSUPPORTED("LoadEntityFromHandleAddress"),
	UNSUPPORTED("StoreEntityToHandleAddress"),
	// core/smn_events.cpp
	UNSUPPORTED("HookEvent"), UNSUPPORTED("HookEventEx"), UNSUPPORTED("UnhookEvent"), UNSUPPORTED("CreateEvent"),
	UNSUPPORTED("FireEvent"), UNSUPPORTED("CancelCreatedEvent"), UNSUPPORTED("GetEventName"),
	UNSUPPORTED("GetEventBool"), UNSUPPORTED("GetEventInt"), UNSUPPORTED("GetEventFloat"),
	UNSUPPORTED("GetEventString"), UNSUPPORTED("SetEventBool"), UNSUPPORTED("SetEventInt"),
	UNSUPPORTED("SetEventFloat"), UNSUPPORTED("SetEventString"), UNSUPPORTED("SetEventBroadcast"),
	UNSUPPORTED("Event.Fire"), UNSUPPORTED("Event.FireToClient"), UNSUPPORTED("Event.Cancel"),
	UNSUPPORTED("Event.GetName"), UNSUPPORTED("Event.GetBool"), UNSUPPORTED("Event.GetInt"),
	UNSUPPORTED("Event.GetFloat"), UNSUPPORTED("Event.GetString"), UNSUPPORTED("Event.SetBool"),
	UNSUPPORTED("Event.SetInt"), UNSUPPORTED("Event.SetFloat"), UNSUPPORTED("Event.SetString"),
	UNSUPPORTED("Event.BroadcastDisabled.set"), UNSUPPORTED("Event.BroadcastDisabled.get"),
	// core/smn_hudtext.cpp (the protobuf natives are not bound, as SourceMod
	// does not build them for TF2)
	UNSUPPORTED("SetHudTextParams"), UNSUPPORTED("SetHudTextParamsEx"), UNSUPPORTED("ShowHudText"), UNSUPPORTED("CreateHudSynchronizer"),
	UNSUPPORTED("ShowSyncHudText"), UNSUPPORTED("ClearSyncHud"),
	{nullptr, nullptr},
};

}  // namespace smrunner
