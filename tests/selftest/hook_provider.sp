/**
 * Copyright 2026 davenonymous
 *
 * A plugin that provides a native and a library, for the self tests of
 * hooks on natives whose provider is reloaded (test_hooks.sp).
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>

public APLRes AskPluginLoad2(Handle myself, bool late, char[] error, int err_max) {
	CreateNative("HookProvider_Value", Native_Value);
	RegPluginLibrary("hook_provider");
	return APLRes_Success;
}

any Native_Value(Handle plugin, int numParams) {
	return 7;
}
