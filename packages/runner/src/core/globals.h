// Copyright 2026 davenonymous
//
// The interfaces of SourceMod's logic library, as core holds them once the
// bridge is up (core/logic_bridge.cpp does the same), and the globals that
// SourceMod's core headers (sm_globals.h) expect core to define.
#ifndef SMRUNNER_CORE_GLOBALS_H
#define SMRUNNER_CORE_GLOBALS_H

#include "sm_globals.h"

#include <IAdminSystem.h>
#include <IExtensionSys.h>
#include <IForwardSys.h>
#include <IHandleSys.h>
#include <IPlayerHelpers.h>
#include <IPluginSys.h>
#include <IRootConsoleMenu.h>
#include <IShareSys.h>
#include <ISourceMod.h>
#include <ITextParsers.h>
#include <IThreader.h>
#include <ITranslator.h>
#include <bridge/include/CoreProvider.h>
#include <bridge/include/IExtensionBridge.h>
#include <bridge/include/ILogger.h>
#include <bridge/include/IProviderCallbacks.h>
#include <bridge/include/IScriptManager.h>
#include <bridge/include/LogicProvider.h>

extern SourceMod::sm_logic_t logicore;
extern SourceMod::IShareSys* sharesys;
extern SourceMod::IHandleSys* handlesys;
extern SourceMod::IForwardManager* forwardsys;
extern SourceMod::IScriptManager* scripts;
extern SourceMod::IExtensionSys* extsys;
extern SourceMod::IAdminSystem* adminsys;
extern SourceMod::ILogger* logger;
extern SourceMod::IRootConsole* rootmenu;
extern SourceMod::ITranslator* translator;

#endif
