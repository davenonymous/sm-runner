/**
 * Copyright 2026 davenonymous
 *
 * Self tests of cvars set on the command line before the plugin or
 * SourceMod made them (tests/run.sh):
 *
 *     smrunner test test_cvars.smx \
 *         --cvar smrunner_test_limit=42 --cvar smrunner_test_greeting=hi --cvar sm_maphistory_size=3
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <smrunner>

ConVar g_limit;
ConVar g_greeting;

public void OnPluginStart() {
	g_limit = CreateConVar("smrunner_test_limit", "5", "A bounded cvar", _, true, 0.0, true, 10.0);
	g_greeting = CreateConVar("smrunner_test_greeting", "hello", "A string cvar");
}

public void Test_APluginsCvarKeepsItsDefinition() {
	Assert.Equal(g_limit.IntValue, 10, "the value from the command line, clamped to the plugin's bound");
	float max;
	Assert.True(g_limit.GetBounds(ConVarBound_Upper, max), "the plugin's upper bound");
	Assert.FloatEqual(max, 10.0);
	char value[64];
	g_limit.GetDefault(value, sizeof(value));
	Assert.StrEqual(value, "5", "the plugin's default");
	g_limit.RestoreDefault();
	Assert.Equal(g_limit.IntValue, 5, "a reset goes back to the plugin's default");
	g_greeting.GetString(value, sizeof(value));
	Assert.StrEqual(value, "hi");
}

public void Test_APluginsCvarIsItsPlugins() {
	char output[1024];
	Runner.CommandOutput(output, sizeof(output), "sm cvars test_cvars.smx");
	Assert.StrContains(output, "[SM] Listing 2 convars for: test_cvars.smx");
	Assert.StrContains(output, "smrunner_test_limit");
}

public void Test_ACoreCvarKeepsItsDefinition() {
	ConVar size = FindConVar("sm_maphistory_size");
	Assert.Equal(size.IntValue, 3);
	char value[64];
	size.GetDefault(value, sizeof(value));
	Assert.StrEqual(value, "20", "SourceMod's default");
}
