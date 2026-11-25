// Copyright 2026 davenonymous
//
// Ported from SourceMod's core/CoreConfig.cpp (GPLv3, AlliedModders LLC).
#include "core/coreconfig.h"

#include <sourcemod_version.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <string>

#include "core/convars.h"
#include "core/core.h"
#include "core/sourcemod_api.h"
#include "diagnostics.h"
#include "engine/fcvar.h"

using namespace SourceMod;
using namespace SourcePawn;

namespace smrunner {

static bool IsFile(const std::string& path) {
	struct stat info;
	return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

static bool MakeDirectories(const std::string& path) {
	std::string partial;
	size_t start = 0;
	while (start <= path.size()) {
		size_t slash = path.find('/', start);
		if (slash == std::string::npos) {
			slash = path.size();
		}
		partial = path.substr(0, slash);
		if (!partial.empty()) {
			struct stat info;
			if (stat(partial.c_str(), &info) != 0 && mkdir(partial.c_str(), 0775) != 0) {
				return false;
			}
		}
		start = slash + 1;
	}
	return true;
}

void CoreConfig::OnSourceModAllInitialized() {
	onServerCfg_ = forwardsys->CreateForward("OnServerCfg", ET_Ignore, 0, nullptr);
	onConfigsExecuted_ = forwardsys->CreateForward("OnConfigsExecuted", ET_Ignore, 0, nullptr);
	onAutoConfigsBuffered_ = forwardsys->CreateForward("OnAutoConfigsBuffered", ET_Ignore, 0, nullptr);
	rootmenu->AddRootConsoleCommand3("config", "Set core configuration options", this);
	Engine& engine = Core::Get().engine();
	engine.SetExecObserver([this](const char* file) { OnExec(file); });
	engine.console().AddCommand({"sm_internal", "", 0, [this](int, const CommandArgs& args) {
		                             if (strcmp(args.Arg(1), "1") == 0) {
			                             ConfigsExecutedGlobal();
		                             } else if (strcmp(args.Arg(1), "2") == 0 && args.ArgC() >= 3) {
			                             ConfigsExecutedPlugin(static_cast<unsigned int>(atoi(args.Arg(2))));
		                             }
	                             }});
}

void CoreConfig::OnSourceModShutdown() {
	Engine& engine = Core::Get().engine();
	engine.SetExecObserver(nullptr);
	engine.console().RemoveCommand("sm_internal");
	rootmenu->RemoveRootConsoleCommand("config", this);
	forwardsys->ReleaseForward(onServerCfg_);
	forwardsys->ReleaseForward(onConfigsExecuted_);
	forwardsys->ReleaseForward(onAutoConfigsBuffered_);
}

void CoreConfig::OnSourceModLevelChange(const char*) {
	configsExecuted_ = false;
	serverExecuted_ = false;
	gotServerStart_ = false;
}

void CoreConfig::Initialize(const std::string& path,
                            const std::vector<std::pair<std::string, std::string>>& overrides) {
	values_.clear();
	SMCError error = textparsers->ParseFile_SMC(path.c_str(), this, nullptr);
	if (error != SMCError_Okay) {
		const char* text = textparsers->GetSMCErrorString(error);
		Fatal("error parsing the core config file %s: %s", path.c_str(), text ? text : "unknown error");
	}
	for (const auto& entry : overrides) {
		char message[255] = "";
		if (SetConfigOption(entry.first.c_str(), entry.second.c_str(), ConfigSource_File, message, sizeof(message)) ==
		    ConfigResult_Reject) {
			Fatal("core config option %s=%s was rejected: %s", entry.first.c_str(), entry.second.c_str(), message);
		}
	}
}

SMCResult CoreConfig::ReadSMC_KeyValue(const SMCStates*, const char* key, const char* value) {
	char error[255] = "";
	if (SetConfigOption(key, value, ConfigSource_File, error, sizeof(error)) == ConfigResult_Reject) {
		Fatal("core config error (key: %s) (value: %s) %s", key, value, error);
	}
	return SMCResult_Continue;
}

ConfigResult CoreConfig::SetConfigOption(const char* option, const char* value, ConfigSource source, char* error,
                                         size_t maxlength) {
	ConfigResult result = ConfigResult_Ignore;
	for (SMGlobalClass* global = SMGlobalClass::head; global; global = global->m_pGlobalClassNext) {
		if ((result = global->OnSourceModConfigChanged(option, value, source, error, maxlength)) !=
		    ConfigResult_Ignore) {
			break;
		}
	}
	values_[option] = value;
	return result;
}

// CoreConfig::OnRootConsoleCommand
void CoreConfig::OnRootConsoleCommand(const char*, const ICommandArgs* args) {
	Core& core = Core::Get();
	if (args->ArgC() >= 4) {
		const char* option = args->Arg(2);
		const char* value = args->Arg(3);
		char error[255] = "";
		ConfigResult result = SetConfigOption(option, value, ConfigSource_Console, error, sizeof(error));
		if (result == ConfigResult_Reject) {
			core.ConsolePrint("[SM] Could not set config option \"%s\" to \"%s\". (%s)", option, value, error);
			return;
		}
		if (result == ConfigResult_Ignore) {
			core.ConsolePrint("[SM] WARNING: Config option \"%s\" is not registered.", option);
		}
		core.ConsolePrint("[SM] Config option \"%s\" set to \"%s\".", option, value);
		return;
	}
	if (args->ArgC() >= 3) {
		const char* option = args->Arg(2);
		const char* value = GetCoreConfigValue(option);
		if (value == nullptr) {
			core.ConsolePrint("[SM] No such config option \"%s\" exists.", option);
		} else {
			core.ConsolePrint("[SM] Config option \"%s\" is set to \"%s\".", option, value);
		}
		return;
	}
	core.ConsolePrint("[SM] Usage: sm config <option> [value]");
}

const char* CoreConfig::GetCoreConfigValue(const char* key) const {
	auto found = values_.find(key);
	return found == values_.end() ? nullptr : found->second.c_str();
}

// SM_ExecuteConfig: writes a missing config from the plugin's cvars when the
// plugin asked for it, then buffers its exec.
bool CoreConfig::ExecuteConfig(SMPlugin* plugin, AutoConfig* config, bool canCreate) {
	SourceModImpl& sourcemod = Core::Get().sourcemod();
	bool willCreate = canCreate && config->create;
	if (willCreate) {
		char folder[PLATFORM_MAX_PATH];
		sourcemod.BuildPath(Path_Game, folder, sizeof(folder), "cfg/%s", config->folder.c_str());
		MakeDirectories(folder);
	}
	std::string local = config->folder.size() ? std::string(config->folder.c_str()) + "/" + config->autocfg.c_str() +
	                                                ".cfg"
	                                          : std::string(config->autocfg.c_str()) + ".cfg";
	char file[PLATFORM_MAX_PATH];
	sourcemod.BuildPath(Path_Game, file, sizeof(file), "cfg/%s", local.c_str());

	bool exists = IsFile(file);
	PluginConVarList* convars = nullptr;
	if (!exists && willCreate && plugin->GetProperty("ConVarList", reinterpret_cast<void**>(&convars), false) &&
	    convars) {
		FILE* out = fopen(file, "wt");
		if (!out) {
			logger->LogError(
				"Failed to auto generate config for %s, make sure the directory has write permission.",
				plugin->GetFilename());
			return canCreate;
		}
		fprintf(out, "// This file was auto-generated by SourceMod (v%s)\n", SOURCEMOD_VERSION);
		fprintf(out, "// ConVars for plugin \"%s\"\n", plugin->GetFilename());
		fprintf(out, "\n\n");
		for (ConVar* cvar : *convars) {
			if (cvar->IsFlagSet(FCVAR_DONTRECORD)) {
				continue;
			}
			std::string help = std::string(cvar->GetHelpText()).substr(0, 254);
			size_t start = 0;
			while (start < help.size()) {
				size_t end = help.find('\n', start);
				if (end == std::string::npos) {
					end = help.size();
				}
				fprintf(out, "// %s\n", help.substr(start, end - start).c_str());
				start = end + 1;
			}
			fprintf(out, "// -\n");
			fprintf(out, "// Default: \"%s\"\n", cvar->GetDefault());
			float bound;
			if (cvar->GetMin(bound)) {
				fprintf(out, "// Minimum: \"%02f\"\n", bound);
			}
			if (cvar->GetMax(bound)) {
				fprintf(out, "// Maximum: \"%02f\"\n", bound);
			}
			fprintf(out, "%s \"%s\"\n", cvar->GetName(), cvar->GetDefault());
			fprintf(out, "\n");
		}
		fprintf(out, "\n");
		fclose(out);
		exists = true;
		canCreate = false;
	}
	if (exists) {
		Core::Get().engine().console().AppendToBuffer("exec " + local + "\n");
	}
	return canCreate;
}

void CoreConfig::ExecuteAllConfigs() {
	if (gotServerStart_) {
		return;
	}
	Core::Get().engine().console().AppendToBuffer("exec sourcemod/sourcemod.cfg\n");
	AutoPluginList plugins(scripts);
	for (size_t i = 0; i < plugins->size(); i++) {
		SMPlugin* plugin = plugins->at(i);
		bool canCreate = true;
		for (unsigned int c = 0; c < plugin->GetConfigCount(); c++) {
			canCreate = ExecuteConfig(plugin, plugin->GetConfig(c), canCreate);
		}
	}
	gotServerStart_ = true;
	CheckAndFinalizeConfigs();
}

static void DoSingleExecForwards(IPluginContext* context) {
	if (IPluginFunction* function = context->GetFunctionByName("OnServerCfg")) {
		function->Execute(nullptr);
	}
	if (IPluginFunction* function = context->GetFunctionByName("OnConfigsExecuted")) {
		function->Execute(nullptr);
	}
}

// SM_ExecuteForPlugin: a plugin loaded after the configs ran.
void CoreConfig::ExecuteForPlugin(IPluginContext* context) {
	SMPlugin* plugin = scripts->FindPluginByContext(context->GetContext());
	unsigned int count = plugin->GetConfigCount();
	if (count == 0) {
		DoSingleExecForwards(context);
		return;
	}
	bool canCreate = true;
	for (unsigned int i = 0; i < count; i++) {
		canCreate = ExecuteConfig(plugin, plugin->GetConfig(i), canCreate);
	}
	char command[64];
	snprintf(command, sizeof(command), "sm_internal 2 %u\n", plugin->GetSerial());
	Core::Get().engine().console().AppendToBuffer(command);
}

void CoreConfig::ConfigsExecutedPlugin(unsigned int serial) {
	IPluginIterator* iterator = scripts->GetPluginIterator();
	while (iterator->MorePlugins()) {
		IPlugin* plugin = iterator->GetPlugin();
		if (plugin->GetSerial() == serial) {
			DoSingleExecForwards(plugin->GetBaseContext());
			break;
		}
		iterator->NextPlugin();
	}
	iterator->Release();
}

void CoreConfig::ConfigsExecutedGlobal() {
	if (configsExecuted_) {
		return;
	}
	configsExecuted_ = true;
	onServerCfg_->Execute(nullptr);
	onConfigsExecuted_->Execute(nullptr);
}

// Hook_ExecDispatchPre/Post: the exec of servercfgfile releases the configs.
void CoreConfig::OnExec(const char* file) {
	ConVar* serverCfg = Core::Get().engine().console().FindConVar("servercfgfile");
	if (serverExecuted_ || !serverCfg || strcmp(file, serverCfg->GetString()) != 0) {
		return;
	}
	serverExecuted_ = true;
	CheckAndFinalizeConfigs();
}

void CoreConfig::CheckAndFinalizeConfigs() {
	if (serverExecuted_ && gotServerStart_) {
		Core::Get().RequestInternalPush();
	}
}

void CoreConfig::InternalCmdTrigger() {
	onAutoConfigsBuffered_->Execute(nullptr);
	Core::Get().engine().console().AppendToBuffer("sm_internal 1\n");
}

}  // namespace smrunner
