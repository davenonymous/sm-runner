// Copyright 2026 davenonymous
//
// Ported from SourceMod's core/ConCmdManager.cpp, core/ConsoleDetours.cpp and
// core/smn_console.cpp (GPLv3, AlliedModders LLC).
#include "core/concmds.h"

#include <ctype.h>
#include <string.h>

#include <algorithm>

#include "core/core.h"
#include "core/players.h"
#include "core/sourcemod_api.h"
#include "testing/plugin_memory.h"

using namespace SourceMod;
using namespace SourcePawn;

namespace smrunner {

static std::string Lower(const char* name) {
	std::string lower = name;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return tolower(c); });
	return lower;
}

void ConCmdManager::OnSourceModAllInitialized() {
	scripts->AddPluginsListener(this);
	anyCommand_ = forwardsys->CreateForwardEx("OnAnyCommand", ET_Hook, 3, nullptr, Param_Cell, Param_String,
	                                          Param_Cell);
	rootmenu->AddRootConsoleCommand3("cmds", "List console commands", this);
	Core::Get().engine().console().SetCommandFilter(
		[this](int client, const CommandArgs& args) { return Dispatch(client, args); });
}

void ConCmdManager::OnSourceModShutdown() {
	Core::Get().engine().console().SetCommandFilter(nullptr);
	for (auto& entry : listeners_) {
		forwardsys->ReleaseForward(entry.second);
	}
	listeners_.clear();
	forwardsys->ReleaseForward(anyCommand_);
	anyCommand_ = nullptr;
	rootmenu->RemoveRootConsoleCommand("cmds", this);
	scripts->RemovePluginsListener(this);
}

ConCmdManager::Info* ConCmdManager::AddOrFindCommand(const char* name, const char* help, int flags, IPlugin* plugin) {
	std::string key = Lower(name);
	auto found = commands_.find(key);
	if (found != commands_.end()) {
		return found->second;
	}
	Console& console = Core::Get().engine().console();
	auto info = new Info();
	info->name = name;
	info->help = help ? help : "";
	if (ConsoleCommand* existing = console.FindCommand(name)) {
		info->help = existing->help;
	} else {
		// The command runs entirely through the filter; its handler does
		// nothing. Clients may run it, as the game's commands. As in SourceMod,
		// a cvar of the name keeps the command from reaching the console,
		// without an error.
		console.AddCommand({name, info->help, flags, [](int, const CommandArgs&) {}, true});
		info->sourceMod = true;
		info->plugin = plugin;
	}
	commands_.emplace(key, info);
	auto position = std::find_if(list_.begin(), list_.end(),
	                             [name](Info* other) { return strcmp(name, other->name.c_str()) < 0; });
	list_.insert(position, info);
	return info;
}

void ConCmdManager::RemoveCommand(Info* info) {
	if (info->sourceMod) {
		Core::Get().engine().console().RemoveCommand(info->name);
	}
	commands_.erase(Lower(info->name.c_str()));
	list_.erase(std::remove(list_.begin(), list_.end(), info), list_.end());
	delete info;
}

void ConCmdManager::OnPluginDestroyed(IPlugin* plugin) {
	std::vector<Info*> emptied;
	for (Info* info : list_) {
		auto& hooks = info->hooks;
		for (auto it = hooks.begin(); it != hooks.end();) {
			if ((*it)->plugin != plugin) {
				++it;
				continue;
			}
			delete *it;
			it = hooks.erase(it);
		}
		if (hooks.empty()) {
			emptied.push_back(info);
		} else {
			info->plugin = hooks.front()->plugin;
		}
	}
	for (Info* info : emptied) {
		RemoveCommand(info);
	}
}

bool ConCmdManager::IsHooked(const char* command) const {
	return commands_.count(Lower(command)) != 0;
}

// ConCmdManager::OnRootConsoleCommand
void ConCmdManager::OnRootConsoleCommand(const char*, const ICommandArgs* args) {
	Core& core = Core::Get();
	if (args->ArgC() < 3) {
		core.ConsolePrint("[SM] Usage: sm cmds <plugin #>");
		return;
	}
	IPlugin* plugin = scripts->FindPluginByConsoleArg(args->Arg(2));
	if (!plugin) {
		core.ConsolePrint("[SM] Plugin \"%s\" was not found.", args->Arg(2));
		return;
	}
	const sm_plugininfo_t* info = plugin->GetPublicInfo();
	const char* name = info->name && info->name[0] ? info->name : plugin->GetFilename();
	std::vector<const Hook*> hooks;
	for (const Info* command : list_) {
		for (const Hook* hook : command->hooks) {
			if (hook->plugin == plugin) {
				hooks.push_back(hook);
			}
		}
	}
	if (hooks.empty()) {
		core.ConsolePrint("[SM] No commands found for: %s", name);
		return;
	}
	core.ConsolePrint("[SM] Listing commands for: %s", name);
	core.ConsolePrint("  %-17.16s %-8.7s %s", "[Name]", "[Type]", "[Help]");
	for (const Hook* hook : hooks) {
		const char* type = hook->type == Hook::Server ? "server" : hook->info->eflags == 0 ? "console" : "admin";
		const char* help = hook->help.empty() ? hook->info->help.c_str() : hook->help.c_str();
		core.ConsolePrint("  %-17.16s %-12.11s %s", hook->info->name.c_str(), type, help);
	}
}

bool ConCmdManager::AddServerCommand(IPluginFunction* function, const char* name, const char* help, int flags,
                                     IPlugin* plugin) {
	Info* info = AddOrFindCommand(name, help, flags, plugin);
	if (!info) {
		return false;
	}
	info->hooks.push_back(new Hook{Hook::Server, info, function, help ? help : "", plugin});
	return true;
}

bool ConCmdManager::AddAdminCommand(IPluginFunction* function, const char* name, const char* group, int adminFlags,
                                    const char* help, int flags, IPlugin* plugin) {
	Info* info = AddOrFindCommand(name, help, flags, plugin);
	if (!info) {
		return false;
	}
	auto hook = new Hook{Hook::Client, info, function, help ? help : "", plugin};
	hook->isAdmin = true;
	hook->group = group;
	hook->flags = adminFlags;
	FlagBits overridden = 0;
	bool hasOverride = adminsys->GetCommandOverride(group, Override_CommandGroup, &overridden);
	if (adminsys->GetCommandOverride(name, Override_Command, &overridden)) {
		hasOverride = true;
	}
	hook->eflags = hasOverride ? overridden : hook->flags;
	info->eflags = hook->eflags;
	info->hooks.push_back(hook);
	return true;
}

bool ConCmdManager::AddListener(IPluginFunction* function, const char* command) {
	if (command == nullptr) {
		return anyCommand_->AddFunction(function);
	}
	std::string key = Lower(command);
	auto found = listeners_.find(key);
	if (found == listeners_.end()) {
		IChangeableForward* forward =
			forwardsys->CreateForwardEx(nullptr, ET_Hook, 3, nullptr, Param_Cell, Param_String, Param_Cell);
		found = listeners_.emplace(key, forward).first;
	}
	return found->second->AddFunction(function);
}

bool ConCmdManager::RemoveListener(IPluginFunction* function, const char* command) {
	if (command == nullptr) {
		return anyCommand_->RemoveFunction(function);
	}
	auto found = listeners_.find(Lower(command));
	if (found == listeners_.end()) {
		return false;
	}
	return found->second->RemoveFunction(function);
}

// ConsoleDetours::InternalDispatch
cell_t ConCmdManager::RunListeners(int client, const CommandArgs& args) {
	std::string name = Lower(args.Arg(0));
	cell_t result = Pl_Continue;
	anyCommand_->PushCell(client);
	anyCommand_->PushString(name.c_str());
	anyCommand_->PushCell(args.ArgC() - 1);
	anyCommand_->Execute(&result, nullptr);
	if (name == "sm") {
		result = Pl_Continue;
	}
	if (result >= Pl_Handled) {
		return result;
	}
	auto found = listeners_.find(name);
	if (found == listeners_.end() || found->second->GetFunctionCount() == 0) {
		return result;
	}
	cell_t second = Pl_Continue;
	found->second->PushCell(client);
	found->second->PushString(name.c_str());
	found->second->PushCell(args.ArgC() - 1);
	found->second->Execute(&second, nullptr);
	return second > result ? second : result;
}

bool ConCmdManager::CheckAccess(int client, const char* command, const Hook& hook) {
	if (adminsys->CheckClientCommandAccess(client, command, hook.eflags)) {
		return true;
	}
	char buffer[128];
	if (!logicore.CoreTranslate(buffer, sizeof(buffer), "%T", 2, nullptr, "No Access", &client)) {
		ke::SafeStrcpy(buffer, sizeof(buffer), "You do not have access to this command");
	}
	char full[192];
	if (replyTo_ == SM_REPLY_CHAT) {
		ke::SafeSprintf(full, sizeof(full), "[SM] %s.", buffer);
		Core::Get().players().PrintToChat(client, full);
	} else {
		ke::SafeSprintf(full, sizeof(full), "[SM] %s.\n", buffer);
		Core::Get().players().PrintToConsole(client, full);
	}
	return false;
}

// ConCmdManager::InternalDispatch; true when the command is blocked.
bool ConCmdManager::InternalDispatch(int client, const CommandArgs& args, Info* info) {
	if (client != 0 && !Core::Get().players().IsConnected(client)) {
		return false;
	}
	cell_t result = Pl_Continue;
	int argc = args.ArgC() - 1;
	// Hooks may unhook themselves; run over a copy.
	std::vector<Hook*> hooks = info->hooks;
	for (Hook* hook : hooks) {
		if (!hook->function->IsRunnable()) {
			continue;
		}
		if (hook->type == Hook::Server) {
			if (client != 0) {
				continue;
			}
		} else {
			if (client != 0 && hook->isAdmin && !CheckAccess(client, info->name.c_str(), *hook)) {
				if (result < Pl_Handled) {
					result = Pl_Handled;
				}
				continue;
			}
			hook->function->PushCell(client);
		}
		hook->function->PushCell(argc);
		cell_t hookResult = result;
		if (hook->function->Execute(&hookResult) == SP_ERROR_NONE) {
			if (hookResult > result) {
				result = hookResult;
			}
			if (result == Pl_Stop) {
				break;
			}
		}
	}
	return result >= Pl_Handled && !info->sourceMod;
}

bool ConCmdManager::Dispatch(int client, const CommandArgs& args) {
	Console& console = Core::Get().engine().console();
	if (!console.FindCommand(args.Arg(0))) {
		return false;
	}
	if (RunListeners(client, args) >= Pl_Handled) {
		return true;
	}
	auto found = commands_.find(Lower(args.Arg(0)));
	if (found == commands_.end()) {
		return false;
	}
	return InternalDispatch(client, args, found->second);
}

// ConCmdManager::DispatchClientCommand
ResultType ConCmdManager::DispatchClientCommand(int client, const char* command, int args, ResultType type) {
	auto found = commands_.find(Lower(command));
	if (found == commands_.end()) {
		return type;
	}
	cell_t result = type;
	std::vector<Hook*> hooks = found->second->hooks;
	for (Hook* hook : hooks) {
		if (hook->type == Hook::Server || !hook->function->IsRunnable()) {
			continue;
		}
		if (hook->isAdmin && !CheckAccess(client, command, *hook)) {
			if (result < Pl_Handled) {
				result = Pl_Handled;
			}
			continue;
		}
		hook->function->PushCell(client);
		hook->function->PushCell(args);
		cell_t hookResult = result;
		if (hook->function->Execute(&hookResult) == SP_ERROR_NONE) {
			if (hookResult > result) {
				result = hookResult;
			}
			if (result == Pl_Stop) {
				break;
			}
		}
	}
	return static_cast<ResultType>(result);
}

void ConCmdManager::UpdateAdminCmdFlags(const char* command, OverrideType type, FlagBits bits, bool remove) {
	auto apply = [bits, remove](Hook* hook) {
		hook->eflags = remove ? hook->flags : bits;
		hook->info->eflags = hook->eflags;
	};
	if (type == Override_Command) {
		auto found = commands_.find(Lower(command));
		if (found == commands_.end()) {
			return;
		}
		for (Hook* hook : found->second->hooks) {
			if (hook->isAdmin) {
				apply(hook);
			}
		}
		return;
	}
	if (type == Override_CommandGroup) {
		for (Info* info : list_) {
			for (Hook* hook : info->hooks) {
				if (hook->isAdmin && hook->group == command) {
					apply(hook);
				}
			}
		}
	}
}

bool ConCmdManager::LookForCommandAdminFlags(const char* command, FlagBits* flags) {
	auto found = commands_.find(Lower(command));
	if (found == commands_.end()) {
		return false;
	}
	*flags = found->second->eflags;
	return true;
}

// ------------------------------------------------------------------ natives

static IPluginFunction* CallbackOrError(IPluginContext* context, cell_t id) {
	IPluginFunction* function = context->GetFunctionById(id);
	if (!function) {
		context->ThrowNativeError("Invalid function id (%X)", id);
	}
	return function;
}

static cell_t sm_RegServerCmd(IPluginContext* context, const cell_t* params) {
	char* name = PluginCharsArg(context, params[1]);
	if (!name) {
		return 0;
	}
	if (strcasecmp(name, "sm") == 0) {
		return context->ThrowNativeError("Cannot register \"sm\" command");
	}
	char* help = PluginCharsArg(context, params[3]);
	if (!help) {
		return 0;
	}
	IPluginFunction* function = CallbackOrError(context, params[2]);
	if (!function) {
		return 0;
	}
	IPlugin* plugin = scripts->FindPluginByContext(context->GetContext());
	if (!Core::Get().commands().AddServerCommand(function, name, help, params[4], plugin)) {
		return context->ThrowNativeError(
			"Command \"%s\" could not be created. A convar with the same name already exists.", name);
	}
	return 1;
}

static cell_t sm_RegConsoleCmd(IPluginContext* context, const cell_t* params) {
	char* name = PluginCharsArg(context, params[1]);
	if (!name) {
		return 0;
	}
	if (strcasecmp(name, "sm") == 0) {
		return context->ThrowNativeError("Cannot register \"sm\" command");
	}
	char* help = PluginCharsArg(context, params[3]);
	if (!help) {
		return 0;
	}
	IPluginFunction* function = CallbackOrError(context, params[2]);
	if (!function) {
		return 0;
	}
	IPlugin* plugin = scripts->FindPluginByContext(context->GetContext());
	if (!Core::Get().commands().AddAdminCommand(function, name, plugin->GetFilename(), 0, help, params[4], plugin)) {
		return context->ThrowNativeError(
			"Command \"%s\" could not be created. A convar with the same name already exists.", name);
	}
	return 1;
}

static cell_t sm_RegAdminCmd(IPluginContext* context, const cell_t* params) {
	char* name = PluginCharsArg(context, params[1]);
	if (!name) {
		return 0;
	}
	if (strcasecmp(name, "sm") == 0) {
		return context->ThrowNativeError("Cannot register \"sm\" command");
	}
	char* help = PluginCharsArg(context, params[4]);
	char* group = help ? PluginCharsArg(context, params[5]) : nullptr;
	if (!group) {
		return 0;
	}
	IPluginFunction* function = CallbackOrError(context, params[2]);
	if (!function) {
		return 0;
	}
	IPlugin* plugin = scripts->FindPluginByContext(context->GetContext());
	const char* groupName = group[0] == '\0' ? plugin->GetFilename() : group;
	if (!Core::Get().commands().AddAdminCommand(function, name, groupName, params[3], help, params[6], plugin)) {
		return context->ThrowNativeError(
			"Command \"%s\" could not be created. A convar with the same name already exists.", name);
	}
	return 1;
}

static const CommandArgs* CurrentCommandOrError(IPluginContext* context) {
	const CommandArgs* args = Core::Get().engine().console().CurrentCommand();
	if (!args) {
		context->ThrowNativeError("No command callback available");
	}
	return args;
}

static cell_t sm_GetCmdArgs(IPluginContext* context, const cell_t*) {
	const CommandArgs* args = CurrentCommandOrError(context);
	return args ? args->ArgC() - 1 : 0;
}

static cell_t sm_GetCmdArg(IPluginContext* context, const cell_t* params) {
	const CommandArgs* args = CurrentCommandOrError(context);
	if (!args) {
		return 0;
	}
	std::optional<size_t> length = WritePluginString(context, params[2], params[3], args->Arg(params[1]));
	return length ? static_cast<cell_t>(*length) : 0;
}

static cell_t sm_GetCmdArgString(IPluginContext* context, const cell_t* params) {
	const CommandArgs* args = CurrentCommandOrError(context);
	if (!args) {
		return 0;
	}
	std::optional<size_t> length = WritePluginString(context, params[1], params[2], args->ArgS());
	return length ? static_cast<cell_t>(*length) : 0;
}

static cell_t sm_ServerCommandEx(IPluginContext* context, const cell_t* params) {
	// The output buffer is checked before the command runs.
	if (params[2] < 0) {
		return context->ThrowNativeError("a buffer cannot have %d bytes", params[2]);
	}
	char* buffer = params[2] > 0 ? PluginBytesArg(context, params[1], params[2]) : nullptr;
	if (params[2] > 0 && !buffer) {
		return 0;
	}
	SourceModImpl& sourcemod = Core::Get().sourcemod();
	sourcemod.SetGlobalTarget(SOURCEMOD_SERVER_LANGUAGE);
	char command[1024];
	size_t length;
	{
		DetectExceptions exceptions(context);
		length = sourcemod.FormatString(command, sizeof(command) - 2, context, params, 3);
		if (exceptions.HasException()) {
			return 0;
		}
	}
	command[length++] = '\n';
	command[length] = '\0';

	Engine& engine = Core::Get().engine();
	engine.console().ExecuteBuffer();
	std::string captured;
	engine.output().PushCapture(&captured);
	engine.console().AppendToBuffer(command);
	engine.console().ExecuteBuffer();
	engine.output().PopCapture();

	if (buffer) {
		ke::SafeStrcpy(buffer, params[2], captured.c_str());
	}
	return 1;
}

static cell_t FakeClientCommandEx(IPluginContext* context, const cell_t* params) {
	PlayerManager& players = Core::Get().players();
	int client = params[1];
	if (client < 1 || client > players.GetMaxClients()) {
		return context->ThrowNativeError("Client index %d is invalid", client);
	}
	if (!players.IsConnected(client)) {
		return context->ThrowNativeError("Client %d is not connected", client);
	}
	SourceModImpl& sourcemod = Core::Get().sourcemod();
	sourcemod.SetGlobalTarget(client);
	char buffer[256];
	{
		DetectExceptions exceptions(context);
		sourcemod.FormatString(buffer, sizeof(buffer), context, params, 2);
		if (exceptions.HasException()) {
			return 0;
		}
	}
	players.QueueFakeClientCommand(client, buffer);
	return 1;
}

static cell_t AddCommandListener(IPluginContext* context, const cell_t* params) {
	char* name = PluginCharsArg(context, params[2]);
	if (!name) {
		return 0;
	}
	if (strcasecmp(name, "sm") == 0) {
		logger->LogError("Request to register \"sm\" command denied.");
		return 0;
	}
	IPluginFunction* function = CallbackOrError(context, params[1]);
	if (!function) {
		return 0;
	}
	Core::Get().commands().AddListener(function, name[0] == '\0' ? nullptr : name);
	return 1;
}

static cell_t RemoveCommandListener(IPluginContext* context, const cell_t* params) {
	char* name = PluginCharsArg(context, params[2]);
	if (!name) {
		return 0;
	}
	IPluginFunction* function = CallbackOrError(context, params[1]);
	if (!function) {
		return 0;
	}
	if (!Core::Get().commands().RemoveListener(function, name[0] == '\0' ? nullptr : name)) {
		return context->ThrowNativeError("No matching callback was registered");
	}
	return 1;
}

static cell_t SetCommandFlags(IPluginContext* context, const cell_t* params) {
	char* name = PluginCharsArg(context, params[1]);
	if (!name) {
		return 0;
	}
	Console& console = Core::Get().engine().console();
	if (ConsoleCommand* command = console.FindCommand(name)) {
		command->flags = params[2];
		return 1;
	}
	if (ConVar* cvar = console.FindConVar(name)) {
		cvar->SetFlags(params[2]);
		return 1;
	}
	return 0;
}

static cell_t GetCommandFlags(IPluginContext* context, const cell_t* params) {
	char* name = PluginCharsArg(context, params[1]);
	if (!name) {
		return 0;
	}
	Console& console = Core::Get().engine().console();
	if (ConsoleCommand* command = console.FindCommand(name)) {
		return command->flags;
	}
	if (ConVar* cvar = console.FindConVar(name)) {
		return cvar->GetFlags();
	}
	return -1;
}

static cell_t IsChatTrigger(IPluginContext*, const cell_t*) {
	return Core::Get().players().IsChatTrigger() ? 1 : 0;
}

static cell_t GetPublicChatTriggers(IPluginContext* context, const cell_t* params) {
	const char* triggers = Core::Get().players().PublicChatTrigger();
	std::optional<size_t> length = WritePluginString(context, params[1], params[2], triggers);
	return length ? static_cast<cell_t>(*length) : 0;
}

static cell_t GetSilentChatTriggers(IPluginContext* context, const cell_t* params) {
	const char* triggers = Core::Get().players().SilentChatTrigger();
	std::optional<size_t> length = WritePluginString(context, params[1], params[2], triggers);
	return length ? static_cast<cell_t>(*length) : 0;
}

// -------------------------------------------------------- command iterators

// GlobCmdIter: SourceMod's own commands, by position in the command list.
struct SourceModCommandIterator {
	bool started = false;
	size_t position = 0;
};

// ConCommandBaseIterator: the console's commands and cvars as they were
// when the search began.
struct ConsoleIterator {
	std::vector<Console::Entry> entries;
	size_t position = 0;
};

static HandleType_t sSourceModIteratorType = NO_HANDLE_TYPE;
static HandleType_t sConsoleIteratorType = NO_HANDLE_TYPE;

class CommandIteratorTypes : public SMGlobalClass, public IHandleTypeDispatch {
 public:
	void OnSourceModAllInitialized() override {
		HandleAccess access;
		handlesys->InitAccessDefaults(nullptr, &access);
		sConsoleIteratorType = handlesys->CreateType("ConCmdIter", this, 0, nullptr, &access, g_pCoreIdent, nullptr);
		access.access[HandleAccess_Clone] = HANDLE_RESTRICT_OWNER | HANDLE_RESTRICT_IDENTITY;
		sSourceModIteratorType = handlesys->CreateType("CmdIter", this, 0, nullptr, &access, g_pCoreIdent, nullptr);
	}
	void OnSourceModShutdown() override {
		handlesys->RemoveType(sSourceModIteratorType, g_pCoreIdent);
		handlesys->RemoveType(sConsoleIteratorType, g_pCoreIdent);
	}
	void OnHandleDestroy(HandleType_t type, void* object) override {
		if (type == sSourceModIteratorType) {
			delete static_cast<SourceModCommandIterator*>(object);
		} else if (type == sConsoleIteratorType) {
			delete static_cast<ConsoleIterator*>(object);
		}
	}
	bool GetHandleApproxSize(HandleType_t type, void*, unsigned int* size) override {
		*size = type == sSourceModIteratorType ? sizeof(SourceModCommandIterator) : sizeof(ConsoleIterator);
		return true;
	}
};

static CommandIteratorTypes sCommandIteratorTypes;

static const std::vector<ConCmdManager::Info*>& SourceModCommands() {
	return Core::Get().commands().CommandList();
}

static SourceModCommandIterator* ReadSourceModIterator(IPluginContext* context, cell_t handle, const char* kind) {
	SourceModCommandIterator* iterator = nullptr;
	HandleSecurity security(context->GetIdentity(), g_pCoreIdent);
	if (handlesys->ReadHandle(handle, sSourceModIteratorType, &security, reinterpret_cast<void**>(&iterator)) !=
	    HandleError_None) {
		context->ThrowNativeError("Invalid %s Handle %x", kind, handle);
		return nullptr;
	}
	return iterator;
}

// Moves to the next of SourceMod's own commands at or after the position.
static void SkipToSourceModCommand(SourceModCommandIterator* iterator) {
	const auto& commands = SourceModCommands();
	while (iterator->position < commands.size() && !commands[iterator->position]->sourceMod) {
		iterator->position++;
	}
}

// The command the iterator is at, or nullptr with a native error.
static ConCmdManager::Info* CurrentCommand(IPluginContext* context, const cell_t* params) {
	SourceModCommandIterator* iterator = ReadSourceModIterator(context, params[1], "CommandIterator");
	if (!iterator) {
		return nullptr;
	}
	if (!iterator->started || iterator->position >= SourceModCommands().size()) {
		context->ThrowNativeError("Invalid CommandIterator position");
		return nullptr;
	}
	return SourceModCommands()[iterator->position];
}

static const char* HelpOf(const ConCmdManager::Info* info) {
	ConsoleCommand* command = Core::Get().engine().console().FindCommand(info->name);
	return command ? command->help.c_str() : info->help.c_str();
}

static cell_t NewSourceModIterator(IPluginContext* context, const cell_t*) {
	auto* iterator = new SourceModCommandIterator();
	Handle_t handle =
		handlesys->CreateHandle(sSourceModIteratorType, iterator, context->GetIdentity(), g_pCoreIdent, nullptr);
	if (handle == BAD_HANDLE) {
		delete iterator;
	}
	return handle;
}

static cell_t ReadCommandIterator(IPluginContext* context, const cell_t* params) {
	SourceModCommandIterator* iterator = ReadSourceModIterator(context, params[1], "GlobCmdIter");
	if (!iterator) {
		return 0;
	}
	if (!iterator->started) {
		iterator->position = 0;
		iterator->started = true;
	}
	SkipToSourceModCommand(iterator);
	if (iterator->position >= SourceModCommands().size()) {
		return 0;
	}
	ConCmdManager::Info* info = SourceModCommands()[iterator->position];
	cell_t* flags = PluginCellsArg(context, params[4], 1);
	if (!flags || !WritePluginString(context, params[2], params[3], info->name) ||
	    !WritePluginString(context, params[5], params[6], HelpOf(info))) {
		return 0;
	}
	*flags = info->eflags;
	iterator->position++;
	return 1;
}

static cell_t CommandIteratorNext(IPluginContext* context, const cell_t* params) {
	SourceModCommandIterator* iterator = ReadSourceModIterator(context, params[1], "CommandIterator");
	if (!iterator) {
		return 0;
	}
	if (!iterator->started) {
		iterator->position = 0;
		iterator->started = true;
	} else {
		iterator->position++;
	}
	SkipToSourceModCommand(iterator);
	return iterator->position < SourceModCommands().size();
}

static cell_t CommandIteratorGetDescription(IPluginContext* context, const cell_t* params) {
	ConCmdManager::Info* info = CurrentCommand(context, params);
	if (!info) {
		return 0;
	}
	return WritePluginString(context, params[2], params[3], HelpOf(info)) ? 1 : 0;
}

static cell_t CommandIteratorGetName(IPluginContext* context, const cell_t* params) {
	ConCmdManager::Info* info = CurrentCommand(context, params);
	if (!info) {
		return 0;
	}
	return WritePluginString(context, params[2], params[3], info->name) ? 1 : 0;
}

static cell_t CommandIteratorAdminFlags(IPluginContext* context, const cell_t* params) {
	ConCmdManager::Info* info = CurrentCommand(context, params);
	return info ? static_cast<cell_t>(info->eflags) : 0;
}

static cell_t CommandIteratorConVarFlags(IPluginContext* context, const cell_t* params) {
	ConCmdManager::Info* info = CurrentCommand(context, params);
	if (!info) {
		return 0;
	}
	ConsoleCommand* command = Core::Get().engine().console().FindCommand(info->name);
	return command ? command->flags : 0;
}

static cell_t CommandIteratorPlugin(IPluginContext* context, const cell_t* params) {
	ConCmdManager::Info* info = CurrentCommand(context, params);
	if (!info || !info->plugin) {
		return BAD_HANDLE;
	}
	return info->plugin->GetMyHandle();
}

// Writes an entry of the console into the plugin's buffers: the name, then
// at the given parameter positions whether it is a command, its flags and,
// with a buffer, its description. False after the native error.
static bool WriteConsoleEntry(IPluginContext* context, const cell_t* params, int first,
                              const Console::Entry& entry) {
	cell_t* isCommand = PluginCellsArg(context, params[first + 2], 1);
	cell_t* flags = isCommand ? PluginCellsArg(context, params[first + 3], 1) : nullptr;
	if (!flags || !WritePluginString(context, params[first], params[first + 1], entry.name)) {
		return false;
	}
	*isCommand = entry.isCommand ? 1 : 0;
	*flags = entry.flags;
	return !params[first + 5] || WritePluginString(context, params[first + 4], params[first + 5], entry.help);
}

static cell_t FindFirstConCommand(IPluginContext* context, const cell_t* params) {
	auto* iterator = new ConsoleIterator{Core::Get().engine().console().Entries(), 0};
	if (iterator->entries.empty()) {
		delete iterator;
		return BAD_HANDLE;
	}
	if (!WriteConsoleEntry(context, params, 1, iterator->entries[0])) {
		delete iterator;
		return BAD_HANDLE;
	}
	Handle_t handle =
		handlesys->CreateHandle(sConsoleIteratorType, iterator, context->GetIdentity(), g_pCoreIdent, nullptr);
	if (handle == BAD_HANDLE) {
		delete iterator;
	}
	return handle;
}

static cell_t FindNextConCommand(IPluginContext* context, const cell_t* params) {
	ConsoleIterator* iterator = nullptr;
	HandleSecurity security(context->GetIdentity(), g_pCoreIdent);
	HandleError error =
		handlesys->ReadHandle(params[1], sConsoleIteratorType, &security, reinterpret_cast<void**>(&iterator));
	if (error != HandleError_None) {
		return context->ThrowNativeError("Invalid Handle %x (error %d)", params[1], error);
	}
	if (iterator->position + 1 >= iterator->entries.size()) {
		iterator->position = iterator->entries.size();
		return 0;
	}
	iterator->position++;
	return WriteConsoleEntry(context, params, 2, iterator->entries[iterator->position]) ? 1 : 0;
}

REGISTER_NATIVES(commandNatives) {
	{"RegServerCmd", sm_RegServerCmd},
	{"RegConsoleCmd", sm_RegConsoleCmd},
	{"RegAdminCmd", sm_RegAdminCmd},
	{"GetCmdArgs", sm_GetCmdArgs},
	{"GetCmdArg", sm_GetCmdArg},
	{"GetCmdArgString", sm_GetCmdArgString},
	{"ServerCommandEx", sm_ServerCommandEx},
	{"FakeClientCommandEx", FakeClientCommandEx},
	{"AddCommandListener", AddCommandListener},
	{"RemoveCommandListener", RemoveCommandListener},
	{"SetCommandFlags", SetCommandFlags},
	{"GetCommandFlags", GetCommandFlags},
	{"IsChatTrigger", IsChatTrigger},
	{"GetPublicChatTriggers", GetPublicChatTriggers},
	{"GetSilentChatTriggers", GetSilentChatTriggers},
	{"GetCommandIterator", NewSourceModIterator},
	{"ReadCommandIterator", ReadCommandIterator},
	{"FindFirstConCommand", FindFirstConCommand},
	{"FindNextConCommand", FindNextConCommand},
	{"CommandIterator.CommandIterator", NewSourceModIterator},
	{"CommandIterator.Next", CommandIteratorNext},
	{"CommandIterator.GetDescription", CommandIteratorGetDescription},
	{"CommandIterator.GetName", CommandIteratorGetName},
	{"CommandIterator.Flags.get", CommandIteratorAdminFlags},
	{"CommandIterator.AdminFlags.get", CommandIteratorAdminFlags},
	{"CommandIterator.ConVarFlags.get", CommandIteratorConVarFlags},
	{"CommandIterator.Plugin.get", CommandIteratorPlugin},
	{nullptr, nullptr},
};

}  // namespace smrunner
