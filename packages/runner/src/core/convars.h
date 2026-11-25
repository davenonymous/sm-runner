// Copyright 2026 davenonymous
//
// SourceMod's ConVar layer, ported from core/ConVarManager.cpp and the cvar
// natives of core/smn_console.cpp of SourceMod 1.12 (GPLv3, AlliedModders
// LLC): one handle per cvar, owned by core, which plugins can neither close
// nor clone; change hooks are a forward per cvar, called with the handle,
// the old and the new string; a plugin's cvars are listed on the plugin
// (property "ConVarList") for its auto-generated config file.
#ifndef SMRUNNER_CORE_CONVARS_H
#define SMRUNNER_CORE_CONVARS_H

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/globals.h"
#include "engine/console.h"

namespace smrunner {

// The plugin property "ConVarList": a plugin's cvars, sorted by name.
using PluginConVarList = std::vector<ConVar*>;

class ConVarManager : public SMGlobalClass,
                      public SourceMod::IHandleTypeDispatch,
                      public SourceMod::IPluginsListener,
                      public SourceMod::IRootConsoleCommand {
 public:
	using ChangeListener = std::function<void(ConVar* cvar, const char* oldValue, float oldFloat)>;

	// SMGlobalClass
	void OnSourceModStartup(bool late) override;
	void OnSourceModAllInitialized() override;
	void OnSourceModShutdown() override;

	// IHandleTypeDispatch
	void OnHandleDestroy(SourceMod::HandleType_t type, void* object) override;

	// IPluginsListener
	void OnPluginUnloaded(SourceMod::IPlugin* plugin) override;

	// IRootConsoleCommand: sm cvars [reset] <plugin>
	void OnRootConsoleCommand(const char* name, const SourceMod::ICommandArgs* args) override;

	SourceMod::Handle_t CreateConVar(SourcePawn::IPluginContext* context, const char* name, const char* defaultValue,
	                                 const char* description, int flags, bool hasMin, float min, bool hasMax,
	                                 float max);
	SourceMod::Handle_t FindConVar(const char* name);
	void HookConVarChange(ConVar* cvar, SourcePawn::IPluginFunction* function);
	void UnhookConVarChange(ConVar* cvar, SourcePawn::IPluginFunction* function);
	SourceMod::HandleError ReadConVarHandle(SourceMod::Handle_t handle, ConVar** cvar,
	                                        SourceMod::IPlugin** plugin = nullptr);

	// C++ listeners (IConVarChangeListener of the original), keyed by owner.
	void AddChangeListener(const char* name, const void* owner, ChangeListener listener);
	void RemoveChangeListener(const char* name, const void* owner);

	// Installed as the engine's global change callback.
	void OnConVarChanged(ConVar* cvar, const char* oldValue, float oldFloat);

 private:
	struct Info {
		SourceMod::Handle_t handle = 0;
		bool sourceMod = false;
		SourceMod::IChangeableForward* changeForward = nullptr;
		ConVar* cvar = nullptr;
		SourceMod::IPlugin* plugin = nullptr;
		std::vector<std::pair<const void*, ChangeListener>> listeners;
	};

	Info* Lookup(const char* name);
	Info* Track(ConVar* cvar, bool sourceMod, SourceMod::IPlugin* plugin);
	static void AddToPluginList(SourceMod::IPlugin* plugin, ConVar* cvar);

	SourceMod::HandleType_t type_ = 0;
	std::map<std::string, Info*> infos_;   // by lower-case name
	std::vector<ConVar*> reentrancy_;
};

}  // namespace smrunner

#endif
