/**
 * Copyright 2026 davenonymous
 *
 * A plugin whose OnPluginStart fails: "smrunner run" must exit non-zero.
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>

public void OnPluginStart() {
	SetFailState("failing on purpose");
}
