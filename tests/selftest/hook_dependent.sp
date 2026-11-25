/**
 * Copyright 2026 davenonymous
 *
 * A plugin that calls hook_provider's native, from a command and when the
 * provider's library is added, for the self tests of hooks on natives
 * whose provider is reloaded (test_hooks.sp).
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>

native int HookProvider_Value();

public void OnPluginStart() {
	RegServerCmd("hook_dependent_value", Command_Value, "Prints the provider's value");
}

public void OnLibraryAdded(const char[] name) {
	if (StrEqual(name, "hook_provider")) {
		PrintToServer("hook_dependent: provider added, value %d", HookProvider_Value());
	}
}

Action Command_Value(int args) {
	PrintToServer("hook_dependent: value %d", HookProvider_Value());
	return Plugin_Handled;
}
