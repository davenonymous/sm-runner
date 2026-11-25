// Copyright 2026 davenonymous
//
// Ported from SourceMod's core/ConVarManager.cpp and core/smn_console.cpp
// (GPLv3, AlliedModders LLC).
#include "core/convars.h"

#include <ctype.h>
#include <string.h>

#include <algorithm>

#include "core/core.h"
#include "core/players.h"
#include "testing/plugin_memory.h"

using namespace SourceMod;
using namespace SourcePawn;

namespace smrunner {

static std::string Lower(const char* name) {
	std::string lower = name;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return tolower(c); });
	return lower;
}

void ConVarManager::OnSourceModStartup(bool) {
	HandleAccess security;
	security.access[HandleAccess_Read] = 0;
	security.access[HandleAccess_Delete] = HANDLE_RESTRICT_IDENTITY | HANDLE_RESTRICT_OWNER;
	security.access[HandleAccess_Clone] = HANDLE_RESTRICT_IDENTITY | HANDLE_RESTRICT_OWNER;
	type_ = handlesys->CreateType("ConVar", this, 0, nullptr, &security, g_pCoreIdent, nullptr);
	ConVar::SetGlobalChangeCallback(
		[this](ConVar* cvar, const char* oldValue, float oldFloat) { OnConVarChanged(cvar, oldValue, oldFloat); });
}

void ConVarManager::OnSourceModAllInitialized() {
	scripts->AddPluginsListener(this);
	rootmenu->AddRootConsoleCommand3("cvars", "View convars created by a plugin", this);
}

static const char* PluginName(IPlugin* plugin) {
	const sm_plugininfo_t* info = plugin->GetPublicInfo();
	return info->name && info->name[0] ? info->name : plugin->GetFilename();
}

// ConVarManager::OnRootConsoleCommand
void ConVarManager::OnRootConsoleCommand(const char*, const ICommandArgs* args) {
	Core& core = Core::Get();
	if (args->ArgC() < 3) {
		core.ConsolePrint("[SM] Usage: sm cvars [reset] <plugin #>");
		return;
	}
	bool reset = false;
	const char* wanted = args->Arg(2);
	if (args->ArgC() >= 4 && strcmp(wanted, "reset") == 0) {
		reset = true;
		wanted = args->Arg(3);
	}
	IPlugin* plugin = scripts->FindPluginByConsoleArg(wanted);
	if (!plugin) {
		core.ConsolePrint("[SM] Plugin \"%s\" was not found.", wanted);
		return;
	}
	PluginConVarList* list;
	if (!plugin->GetProperty("ConVarList", reinterpret_cast<void**>(&list))) {
		core.ConsolePrint("[SM] No convars found for: %s", PluginName(plugin));
		return;
	}
	if (reset) {
		for (ConVar* cvar : *list) {
			cvar->Revert();
		}
		core.ConsolePrint("[SM] Reset %d convars for: %s", static_cast<int>(list->size()), PluginName(plugin));
		return;
	}
	core.ConsolePrint("[SM] Listing %d convars for: %s", static_cast<int>(list->size()), PluginName(plugin));
	core.ConsolePrint("  %-32.31s %s", "[Name]", "[Value]");
	for (ConVar* cvar : *list) {
		core.ConsolePrint("  %-32.31s %s", cvar->GetName(), cvar->GetString());
	}
}

void ConVarManager::OnSourceModShutdown() {
	ConVar::SetGlobalChangeCallback(nullptr);
	HandleSecurity security(nullptr, g_pCoreIdent);
	for (auto& entry : infos_) {
		Info* info = entry.second;
		handlesys->FreeHandle(info->handle, &security);
		if (info->changeForward) {
			forwardsys->ReleaseForward(info->changeForward);
		}
		delete info;
	}
	infos_.clear();
	scripts->RemovePluginsListener(this);
	rootmenu->RemoveRootConsoleCommand("cvars", this);
	handlesys->RemoveType(type_, g_pCoreIdent);
}

void ConVarManager::OnHandleDestroy(HandleType_t, void*) {
	// The infos belong to the manager; their handles only die at shutdown.
}

void ConVarManager::OnPluginUnloaded(IPlugin* plugin) {
	PluginConVarList* list;
	if (plugin->GetProperty("ConVarList", reinterpret_cast<void**>(&list), true)) {
		delete list;
	}
	for (auto& entry : infos_) {
		if (entry.second->plugin == plugin) {
			entry.second->plugin = nullptr;
		}
	}
}

ConVarManager::Info* ConVarManager::Lookup(const char* name) {
	auto found = infos_.find(Lower(name));
	return found == infos_.end() ? nullptr : found->second;
}

ConVarManager::Info* ConVarManager::Track(ConVar* cvar, bool sourceMod, IPlugin* plugin) {
	auto info = new Info();
	info->sourceMod = sourceMod;
	info->cvar = cvar;
	info->plugin = plugin;
	info->handle = handlesys->CreateHandle(type_, info, nullptr, g_pCoreIdent, nullptr);
	if (info->handle == BAD_HANDLE) {
		delete info;
		return nullptr;
	}
	infos_.emplace(Lower(cvar->GetName()), info);
	return info;
}

void ConVarManager::AddToPluginList(IPlugin* plugin, ConVar* cvar) {
	if (!plugin) {
		return;
	}
	PluginConVarList* list;
	if (!plugin->GetProperty("ConVarList", reinterpret_cast<void**>(&list))) {
		list = new PluginConVarList();
		plugin->SetProperty("ConVarList", list);
	} else if (std::find(list->begin(), list->end(), cvar) != list->end()) {
		return;
	}
	auto position = std::find_if(list->begin(), list->end(),
	                             [cvar](ConVar* other) { return strcmp(cvar->GetName(), other->GetName()) < 0; });
	list->insert(position, cvar);
}

Handle_t ConVarManager::CreateConVar(IPluginContext* context, const char* name, const char* defaultValue,
                                     const char* description, int flags, bool hasMin, float min, bool hasMax,
                                     float max) {
	IPlugin* plugin = scripts->FindPluginByContext(context->GetContext());
	Console& console = Core::Get().engine().console();
	if (ConVar* existing = console.FindConVar(name)) {
		AddToPluginList(plugin, existing);
		if (Info* info = Lookup(name)) {
			if (info->sourceMod && info->plugin == nullptr) {
				info->plugin = plugin;
			}
			return info->handle;
		}
		Info* info = Track(existing, false, nullptr);
		return info ? info->handle : BAD_HANDLE;
	}
	if (console.FindCommand(name)) {
		return BAD_HANDLE;
	}
	ConVar* created = console.CreateConVar(name, defaultValue, flags, description, hasMin, min, hasMax, max);
	if (!created) {
		return BAD_HANDLE;
	}
	Info* info = Track(created, true, plugin);
	if (!info) {
		return BAD_HANDLE;
	}
	AddToPluginList(plugin, created);
	return info->handle;
}

Handle_t ConVarManager::FindConVar(const char* name) {
	if (Info* info = Lookup(name)) {
		return info->handle;
	}
	ConVar* cvar = Core::Get().engine().console().FindConVar(name);
	if (!cvar) {
		return BAD_HANDLE;
	}
	Info* info = Track(cvar, false, nullptr);
	return info ? info->handle : BAD_HANDLE;
}

void ConVarManager::HookConVarChange(ConVar* cvar, IPluginFunction* function) {
	Info* info = Lookup(cvar->GetName());
	if (!info) {
		return;
	}
	if (!info->changeForward) {
		ParamType params[] = {Param_Cell, Param_String, Param_String};
		info->changeForward = forwardsys->CreateForwardEx(nullptr, ET_Ignore, 3, params);
	}
	info->changeForward->AddFunction(function);
}

void ConVarManager::UnhookConVarChange(ConVar* cvar, IPluginFunction* function) {
	Info* info = Lookup(cvar->GetName());
	if (!info) {
		return;
	}
	IPluginContext* context = function->GetParentContext();
	if (!info->changeForward) {
		context->ThrowNativeError("Convar \"%s\" has no active hook", cvar->GetName());
		return;
	}
	if (!info->changeForward->RemoveFunction(function)) {
		context->ThrowNativeError("Invalid hook callback specified for convar \"%s\"", cvar->GetName());
		return;
	}
	bool changing = std::find(reentrancy_.begin(), reentrancy_.end(), cvar) != reentrancy_.end();
	if (info->changeForward->GetFunctionCount() == 0 && !changing) {
		forwardsys->ReleaseForward(info->changeForward);
		info->changeForward = nullptr;
	}
}

HandleError ConVarManager::ReadConVarHandle(Handle_t handle, ConVar** cvar, IPlugin** plugin) {
	Info* info;
	HandleError error = handlesys->ReadHandle(handle, type_, nullptr, reinterpret_cast<void**>(&info));
	if (error != HandleError_None) {
		return error;
	}
	if (cvar) {
		*cvar = info->cvar;
	}
	if (plugin) {
		*plugin = info->plugin;
	}
	return HandleError_None;
}

void ConVarManager::AddChangeListener(const char* name, const void* owner, ChangeListener listener) {
	if (FindConVar(name) == BAD_HANDLE) {
		return;
	}
	if (Info* info = Lookup(name)) {
		info->listeners.emplace_back(owner, std::move(listener));
	}
}

void ConVarManager::RemoveChangeListener(const char* name, const void* owner) {
	Info* info = Lookup(name);
	if (!info) {
		return;
	}
	auto& listeners = info->listeners;
	listeners.erase(std::remove_if(listeners.begin(), listeners.end(),
	                               [owner](const std::pair<const void*, ChangeListener>& entry) {
		                               return entry.first == owner;
	                               }),
	                listeners.end());
}

void ConVarManager::OnConVarChanged(ConVar* cvar, const char* oldValue, float oldFloat) {
	if (strcmp(cvar->GetString(), oldValue) == 0) {
		return;
	}
	Info* info = Lookup(cvar->GetName());
	if (!info) {
		return;
	}
	// The old value lives in the cvar's caller; keep a copy for the hooks.
	std::string old = oldValue;
	auto listeners = info->listeners;
	for (auto& entry : listeners) {
		entry.second(cvar, old.c_str(), oldFloat);
	}
	if (info->changeForward) {
		reentrancy_.push_back(cvar);
		info->changeForward->PushCell(info->handle);
		info->changeForward->PushString(old.c_str());
		info->changeForward->PushString(cvar->GetString());
		info->changeForward->Execute(nullptr);
		reentrancy_.pop_back();
	}
}

// ------------------------------------------------------------------ natives

static ConVar* ReadConVar(IPluginContext* context, cell_t handle) {
	ConVar* cvar = nullptr;
	HandleError error = Core::Get().convars().ReadConVarHandle(static_cast<Handle_t>(handle), &cvar);
	if (error != HandleError_None) {
		context->ThrowNativeError("Invalid convar handle %x (error %d)", handle, error);
		return nullptr;
	}
	return cvar;
}

static cell_t sm_CreateConVar(IPluginContext* context, const cell_t* params) {
	char* name = PluginCharsArg(context, params[1]);
	if (!name) {
		return 0;
	}
	if (name[0] == '\0') {
		return context->ThrowNativeError("Convar with blank name is not permitted");
	}
	char* defaultValue = PluginCharsArg(context, params[2]);
	char* help = defaultValue ? PluginCharsArg(context, params[3]) : nullptr;
	if (!help) {
		return 0;
	}
	Handle_t handle = Core::Get().convars().CreateConVar(context, name, defaultValue, help, params[4], params[5] != 0,
	                                                    sp_ctof(params[6]), params[7] != 0, sp_ctof(params[8]));
	if (handle == BAD_HANDLE) {
		return context->ThrowNativeError(
			"Convar \"%s\" was not created. A console command with the same might already exist.", name);
	}
	return handle;
}

static cell_t sm_FindConVar(IPluginContext* context, const cell_t* params) {
	char* name = PluginCharsArg(context, params[1]);
	return name ? Core::Get().convars().FindConVar(name) : 0;
}

static cell_t sm_HookConVarChange(IPluginContext* context, const cell_t* params) {
	ConVar* cvar = ReadConVar(context, params[1]);
	if (!cvar) {
		return 0;
	}
	IPluginFunction* function = context->GetFunctionById(params[2]);
	if (!function) {
		return context->ThrowNativeError("Invalid function id (%X)", params[2]);
	}
	Core::Get().convars().HookConVarChange(cvar, function);
	return 1;
}

static cell_t sm_UnhookConVarChange(IPluginContext* context, const cell_t* params) {
	ConVar* cvar = ReadConVar(context, params[1]);
	if (!cvar) {
		return 0;
	}
	IPluginFunction* function = context->GetFunctionById(params[2]);
	if (!function) {
		return context->ThrowNativeError("Invalid function id (%X)", params[2]);
	}
	Core::Get().convars().UnhookConVarChange(cvar, function);
	return 1;
}

static cell_t sm_GetConVarBool(IPluginContext* context, const cell_t* params) {
	ConVar* cvar = ReadConVar(context, params[1]);
	return cvar ? cvar->GetBool() : 0;
}

static cell_t sm_GetConVarInt(IPluginContext* context, const cell_t* params) {
	ConVar* cvar = ReadConVar(context, params[1]);
	return cvar ? cvar->GetInt() : 0;
}

// SetConVarBool and SetConVarInt; replicate and notify are no-ops on the
// Orange Box engine, as in SourceMod.
static cell_t sm_SetConVarNum(IPluginContext* context, const cell_t* params) {
	ConVar* cvar = ReadConVar(context, params[1]);
	if (!cvar) {
		return 0;
	}
	cvar->SetValue(static_cast<int>(params[2]));
	return 1;
}

static cell_t sm_GetConVarFloat(IPluginContext* context, const cell_t* params) {
	ConVar* cvar = ReadConVar(context, params[1]);
	return cvar ? sp_ftoc(cvar->GetFloat()) : 0;
}

static cell_t sm_SetConVarFloat(IPluginContext* context, const cell_t* params) {
	ConVar* cvar = ReadConVar(context, params[1]);
	if (!cvar) {
		return 0;
	}
	cvar->SetValue(sp_ctof(params[2]));
	return 1;
}

static cell_t sm_GetConVarString(IPluginContext* context, const cell_t* params) {
	ConVar* cvar = ReadConVar(context, params[1]);
	if (!cvar) {
		return 0;
	}
	return WritePluginString(context, params[2], params[3], cvar->GetString()) ? 1 : 0;
}

static cell_t sm_SetConVarString(IPluginContext* context, const cell_t* params) {
	ConVar* cvar = ReadConVar(context, params[1]);
	if (!cvar) {
		return 0;
	}
	char* value = PluginCharsArg(context, params[2]);
	if (!value) {
		return 0;
	}
	cvar->SetValue(value);
	return 1;
}

static cell_t sm_ResetConVar(IPluginContext* context, const cell_t* params) {
	ConVar* cvar = ReadConVar(context, params[1]);
	if (!cvar) {
		return 0;
	}
	cvar->Revert();
	return 1;
}

static cell_t GetConVarDefault(IPluginContext* context, const cell_t* params) {
	ConVar* cvar = ReadConVar(context, params[1]);
	if (!cvar) {
		return 0;
	}
	std::optional<size_t> bytes = WritePluginString(context, params[2], params[3], cvar->GetDefault());
	return bytes ? static_cast<cell_t>(*bytes) : 0;
}

static cell_t sm_GetConVarFlags(IPluginContext* context, const cell_t* params) {
	ConVar* cvar = ReadConVar(context, params[1]);
	return cvar ? cvar->GetFlags() : 0;
}

static cell_t sm_SetConVarFlags(IPluginContext* context, const cell_t* params) {
	ConVar* cvar = ReadConVar(context, params[1]);
	if (!cvar) {
		return 0;
	}
	cvar->SetFlags(params[2]);
	return 1;
}

static cell_t sm_GetConVarPlugin(IPluginContext* context, const cell_t* params) {
	IPlugin* plugin = nullptr;
	HandleError error = Core::Get().convars().ReadConVarHandle(static_cast<Handle_t>(params[1]), nullptr, &plugin);
	if (error != HandleError_None) {
		return context->ThrowNativeError("Invalid convar handle %x (error %d)", params[1], error);
	}
	return plugin ? plugin->GetMyHandle() : BAD_HANDLE;
}

enum ConVarBounds { ConVarBound_Upper = 0, ConVarBound_Lower };

static cell_t sm_GetConVarBounds(IPluginContext* context, const cell_t* params) {
	ConVar* cvar = ReadConVar(context, params[1]);
	if (!cvar) {
		return 0;
	}
	bool hasBound;
	float bound;
	switch (params[2]) {
		case ConVarBound_Upper:
			hasBound = cvar->GetMax(bound);
			break;
		case ConVarBound_Lower:
			hasBound = cvar->GetMin(bound);
			break;
		default:
			return context->ThrowNativeError("Invalid ConVarBounds value %d", params[2]);
	}
	cell_t* address = PluginCellsArg(context, params[3], 1);
	if (!address) {
		return 0;
	}
	*address = sp_ftoc(bound);
	return hasBound;
}

static cell_t sm_SetConVarBounds(IPluginContext* context, const cell_t* params) {
	ConVar* cvar = ReadConVar(context, params[1]);
	if (!cvar) {
		return 0;
	}
	switch (params[2]) {
		case ConVarBound_Upper:
			cvar->SetMax(params[3] != 0, sp_ctof(params[4]));
			break;
		case ConVarBound_Lower:
			cvar->SetMin(params[3] != 0, sp_ctof(params[4]));
			break;
		default:
			return context->ThrowNativeError("Invalid ConVarBounds value %d", params[2]);
	}
	return 1;
}

static cell_t sm_GetConVarName(IPluginContext* context, const cell_t* params) {
	ConVar* cvar = ReadConVar(context, params[1]);
	if (!cvar) {
		return 0;
	}
	return WritePluginString(context, params[2], params[3], cvar->GetName()) ? 1 : 0;
}

static cell_t sm_GetConVarDescription(IPluginContext* context, const cell_t* params) {
	ConVar* cvar = ReadConVar(context, params[1]);
	if (!cvar) {
		return 0;
	}
	return WritePluginString(context, params[2], params[3], cvar->GetHelpText()) ? 1 : 0;
}

// SendConVarValue: the client records the value as a command it was sent.
static cell_t SendValue(IPluginContext* context, cell_t client, cell_t handle, cell_t valueAddress) {
	ConVar* cvar = ReadConVar(context, handle);
	if (!cvar) {
		return 0;
	}
	SimPlayer* player = Core::Get().players().Player(client);
	if (!player) {
		return context->ThrowNativeError("Client index %d is invalid", client);
	}
	if (!player->IsConnected()) {
		return context->ThrowNativeError("Client %d is not connected", client);
	}
	if (player->IsFakeClient()) {
		return context->ThrowNativeError("Client %d is fake and cannot be targeted", client);
	}
	char* value = PluginCharsArg(context, valueAddress);
	if (!value) {
		return 0;
	}
	return Core::Get().players().ReplicateConVar(client, cvar->GetName(), value);
}

static cell_t ConVar_ReplicateToClient(IPluginContext* context, const cell_t* params) {
	return SendValue(context, params[2], params[1], params[3]);
}

static cell_t SendConVarValue(IPluginContext* context, const cell_t* params) {
	return SendValue(context, params[1], params[2], params[3]);
}

REGISTER_NATIVES(convarNatives) {
	{"CreateConVar", sm_CreateConVar},
	{"FindConVar", sm_FindConVar},
	{"HookConVarChange", sm_HookConVarChange},
	{"UnhookConVarChange", sm_UnhookConVarChange},
	{"GetConVarBool", sm_GetConVarBool},
	{"SetConVarBool", sm_SetConVarNum},
	{"GetConVarInt", sm_GetConVarInt},
	{"SetConVarInt", sm_SetConVarNum},
	{"GetConVarFloat", sm_GetConVarFloat},
	{"SetConVarFloat", sm_SetConVarFloat},
	{"GetConVarString", sm_GetConVarString},
	{"SetConVarString", sm_SetConVarString},
	{"GetConVarFlags", sm_GetConVarFlags},
	{"SetConVarFlags", sm_SetConVarFlags},
	{"ResetConVar", sm_ResetConVar},
	{"GetConVarName", sm_GetConVarName},
	{"GetConVarBounds", sm_GetConVarBounds},
	{"SetConVarBounds", sm_SetConVarBounds},
	{"GetConVarDefault", GetConVarDefault},
	{"SendConVarValue", SendConVarValue},
	{"ConVar.BoolValue.get", sm_GetConVarBool},
	{"ConVar.BoolValue.set", sm_SetConVarNum},
	{"ConVar.FloatValue.get", sm_GetConVarFloat},
	{"ConVar.FloatValue.set", sm_SetConVarFloat},
	{"ConVar.IntValue.get", sm_GetConVarInt},
	{"ConVar.IntValue.set", sm_SetConVarNum},
	{"ConVar.Flags.get", sm_GetConVarFlags},
	{"ConVar.Flags.set", sm_SetConVarFlags},
	{"ConVar.Plugin.get", sm_GetConVarPlugin},
	{"ConVar.SetBool", sm_SetConVarNum},
	{"ConVar.SetInt", sm_SetConVarNum},
	{"ConVar.SetFloat", sm_SetConVarFloat},
	{"ConVar.GetString", sm_GetConVarString},
	{"ConVar.SetString", sm_SetConVarString},
	{"ConVar.RestoreDefault", sm_ResetConVar},
	{"ConVar.GetDefault", GetConVarDefault},
	{"ConVar.GetBounds", sm_GetConVarBounds},
	{"ConVar.SetBounds", sm_SetConVarBounds},
	{"ConVar.GetName", sm_GetConVarName},
	{"ConVar.GetDescription", sm_GetConVarDescription},
	{"ConVar.ReplicateToClient", ConVar_ReplicateToClient},
	{"ConVar.AddChangeHook", sm_HookConVarChange},
	{"ConVar.RemoveChangeHook", sm_UnhookConVarChange},
	{nullptr, nullptr},
};

}  // namespace smrunner
