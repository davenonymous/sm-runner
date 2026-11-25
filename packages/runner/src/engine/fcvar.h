// Copyright 2026 davenonymous
//
// The engine's FCVAR_ flags (tier1/iconvar.h of the TF2 SDK), as plugins see
// them in console.inc.
#ifndef SMRUNNER_ENGINE_FCVAR_H
#define SMRUNNER_ENGINE_FCVAR_H

namespace smrunner {

constexpr int FCVAR_NONE = 0;
constexpr int FCVAR_UNREGISTERED = (1 << 0);
constexpr int FCVAR_DEVELOPMENTONLY = (1 << 1);
constexpr int FCVAR_GAMEDLL = (1 << 2);
constexpr int FCVAR_CLIENTDLL = (1 << 3);
constexpr int FCVAR_HIDDEN = (1 << 4);
constexpr int FCVAR_PROTECTED = (1 << 5);
constexpr int FCVAR_SPONLY = (1 << 6);
constexpr int FCVAR_ARCHIVE = (1 << 7);
constexpr int FCVAR_NOTIFY = (1 << 8);
constexpr int FCVAR_USERINFO = (1 << 9);
constexpr int FCVAR_PRINTABLEONLY = (1 << 10);
constexpr int FCVAR_UNLOGGED = (1 << 11);
constexpr int FCVAR_NEVER_AS_STRING = (1 << 12);
constexpr int FCVAR_REPLICATED = (1 << 13);
constexpr int FCVAR_CHEAT = (1 << 14);
constexpr int FCVAR_DEMO = (1 << 16);
constexpr int FCVAR_DONTRECORD = (1 << 17);
constexpr int FCVAR_NOT_CONNECTED = (1 << 22);

}  // namespace smrunner

#endif
