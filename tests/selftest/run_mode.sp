/**
 * Copyright 2026 davenonymous
 *
 * The test API in run mode (smrunner run), driven by tests/run.sh with
 * --command: what needs no test works (runmode_menu passes the run), a
 * failed assertion fails the run (runmode_assert), and what needs a test
 * throws a native error that says so (runmode_hook).
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <smrunner>

public void OnPluginStart() {
	RegServerCmd("runmode_menu", Command_Menu);
	RegServerCmd("runmode_assert", Command_Assert);
	RegServerCmd("runmode_hook", Command_Hook);
}

public Action Command_Menu(int args) {
	int client = Runner.ConnectClient("alice", "", false);
	Runner.PrintClientMenu(client);
	return Plugin_Handled;
}

public Action Command_Assert(int args) {
	Assert.Equal(GetMaxHumanPlayers() + 1, 0, "asserted in run mode");
	return Plugin_Handled;
}

public Action Command_Hook(int args) {
	Runner.HookNative("GetMaxHumanPlayers", Hook_Count);
	return Plugin_Handled;
}

public Action Hook_Count(RunnerCall call) {
	return Plugin_Continue;
}
