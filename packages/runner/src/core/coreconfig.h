// Copyright 2026 davenonymous
//
// SourceMod's core configuration and config execution, ported from
// core/CoreConfig.cpp of SourceMod 1.12 (GPLv3, AlliedModders LLC):
// configs/core.cfg is read at startup and every option is offered to the
// global classes; at map start sourcemod.cfg and the plugins' AutoExecConfig
// files are buffered (missing ones written from the plugins' cvars), and
// once server.cfg was executed OnAutoConfigsBuffered, OnServerCfg and
// OnConfigsExecuted follow through "sm_internal".
#ifndef SMRUNNER_CORE_CORECONFIG_H
#define SMRUNNER_CORE_CORECONFIG_H

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "core/globals.h"

namespace smrunner {

class CoreConfig : public SMGlobalClass, public SourceMod::ITextListener_SMC, public SourceMod::IRootConsoleCommand {
 public:
	// SMGlobalClass
	void OnSourceModAllInitialized() override;
	void OnSourceModShutdown() override;
	void OnSourceModLevelChange(const char* mapName) override;

	// Reads core.cfg, then applies the overrides, as CoreConfig::Initialize.
	void Initialize(const std::string& path, const std::vector<std::pair<std::string, std::string>>& overrides);
	const char* GetCoreConfigValue(const char* key) const;

	// ITextListener_SMC
	SourceMod::SMCResult ReadSMC_KeyValue(const SourceMod::SMCStates* states, const char* key,
	                                      const char* value) override;

	// IRootConsoleCommand: sm config <option> [value]
	void OnRootConsoleCommand(const char* name, const SourceMod::ICommandArgs* args) override;

	void ExecuteAllConfigs();
	void ExecuteForPlugin(SourcePawn::IPluginContext* context);
	bool AreConfigsExecuted() const { return configsExecuted_; }
	void InternalCmdTrigger();

 private:
	ConfigResult SetConfigOption(const char* option, const char* value, ConfigSource source, char* error,
	                             size_t maxlength);
	bool ExecuteConfig(SourceMod::SMPlugin* plugin, SourceMod::AutoConfig* config, bool canCreate);
	void CheckAndFinalizeConfigs();
	void ConfigsExecutedGlobal();
	void ConfigsExecutedPlugin(unsigned int serial);
	void OnExec(const char* file);

	std::map<std::string, std::string> values_;
	bool configsExecuted_ = false;
	bool serverExecuted_ = false;
	bool gotServerStart_ = false;
	SourceMod::IForward* onServerCfg_ = nullptr;
	SourceMod::IForward* onConfigsExecuted_ = nullptr;
	SourceMod::IForward* onAutoConfigsBuffered_ = nullptr;
};

}  // namespace smrunner

#endif
