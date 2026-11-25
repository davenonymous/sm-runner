/**
 * Copyright 2026 davenonymous
 *
 * A plugin that refuses to load in AskPluginLoad2: "smrunner run" must
 * exit non-zero, having asked it once.
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>

public APLRes AskPluginLoad2(Handle myself, bool late, char[] error, int err_max) {
	PrintToServer("failing_load: AskPluginLoad2 ran");
	strcopy(error, err_max, "failing on purpose in AskPluginLoad2");
	return APLRes_Failure;
}
