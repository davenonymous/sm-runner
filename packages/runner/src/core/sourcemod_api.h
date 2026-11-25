// Copyright 2026 davenonymous
//
// ISourceMod, as SourceModBase of core/sourcemod.cpp provides it to the
// logic library and to extensions (smutils), on the sandbox's paths.
#ifndef SMRUNNER_CORE_SOURCEMOD_API_H
#define SMRUNNER_CORE_SOURCEMOD_API_H

#include <string>
#include <vector>

#include "core/globals.h"

namespace smrunner {

class SourceModImpl : public SourceMod::ISourceMod {
 public:
	SourceModImpl(const std::string& gameDir, const std::string& smDir);

	const char* GetGamePath() const override { return gameDir_.c_str(); }
	const char* GetSourceModPath() const override { return smDir_.c_str(); }
	size_t BuildPath(SourceMod::PathType type, char* buffer, size_t maxlength, const char* format, ...) override;
	void LogMessage(SourceMod::IExtension* extension, const char* format, ...) override;
	void LogError(SourceMod::IExtension* extension, const char* format, ...) override;
	size_t FormatString(char* buffer, size_t maxlength, SourcePawn::IPluginContext* context, const cell_t* params,
	                    unsigned int param) override;
	void* CreateDataPack() override { return nullptr; }
	void FreeDataPack(void*) override {}
	SourceMod::HandleType_t GetDataPackHandleType(bool) override { return 0; }
	KeyValues* ReadKeyValuesHandle(SourceMod::Handle_t handle, SourceMod::HandleError* error, bool root) override;
	const char* GetGameFolderName() const override { return gameFolder_.c_str(); }
	SourcePawn::ISourcePawnEngine* GetScriptingEngine() override { return g_pSourcePawn; }
	SourcePawn::IVirtualMachine* GetScriptingVM() override { return nullptr; }
	time_t GetAdjustedTime() override;
	unsigned int SetGlobalTarget(unsigned int index) override;
	unsigned int GetGlobalTarget() const override { return target_; }
	void AddGameFrameHook(SourceMod::GAME_FRAME_HOOK hook) override;
	void RemoveGameFrameHook(SourceMod::GAME_FRAME_HOOK hook) override;
	size_t Format(char* buffer, size_t maxlength, const char* format, ...) override;
	size_t FormatArgs(char* buffer, size_t maxlength, const char* format, va_list ap) override;
	void AddFrameAction(SourceMod::FRAMEACTION action, void* data) override;
	const char* GetCoreConfigValue(const char* key) override;
	int GetPluginId() override { return 0; }
	int GetShApiVersion() override { return 5; }
	bool IsMapRunning() override;
	void* FromPseudoAddress(uint32_t address) override { return logicore.FromPseudoAddress(address); }
	uint32_t ToPseudoAddress(void* address) override { return logicore.ToPseudoAddress(address); }

	void ProcessGameFrameHooks(bool simulating);
	const char* RelativeSourceModPath() const { return "addons/sourcemod"; }

 private:
	std::string gameDir_;
	std::string smDir_;
	std::string gameFolder_;
	unsigned int target_ = 0;
	std::vector<SourceMod::GAME_FRAME_HOOK> frameHooks_;
};

}  // namespace smrunner

#endif
