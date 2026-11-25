/**
 * Copyright 2026 davenonymous
 *
 * The plugin the self tests test: a little of everything a plugin does, so
 * that the tests can watch, hook, break into and change it.
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>

public Plugin myinfo = {
	name = "smrunner self test target",
	author = "davenonymous",
	description = "The plugin of smrunner's self tests",
	version = "1.0.0",
	url = ""
};

ConVar g_greeting;
int g_counter;
int g_ticks;
float g_lastTick;
char g_lastName[64];
int g_history[4];
int g_connected;
ConVar g_cvars[2];

public void OnPluginStart() {
	g_greeting = CreateConVar("sm_target_greeting", "hello", "What sm_target_greet says");
	g_cvars[0] = g_greeting;
	g_cvars[1] = CreateConVar("sm_target_limit", "5", "A bounded cvar", _, true, 0.0, true, 10.0);
	RegServerCmd("sm_target_count", Command_Count, "Counts up");
	RegConsoleCmd("sm_target_greet", Command_Greet, "Greets");
	RegAdminCmd("sm_target_admin", Command_Admin, ADMFLAG_GENERIC, "Needs the generic flag");
	CreateTimer(1.0, Timer_Tick, _, TIMER_REPEAT);
	AutoExecConfig(true);
	PrintToServer("target started");
}

public void OnConfigsExecuted() {
	PrintToServer("target configs executed");
}

Action Command_Count(int args) {
	int step = 1;
	if (args >= 1) {
		step = GetCmdArgInt(1);
	}
	g_counter = AddTo(g_counter, step);
	PrintToServer("counter is %d", g_counter);
	return Plugin_Handled;
}

int AddTo(int value, int step) {
	int sum = value + step;
	g_history[g_counter % sizeof(g_history)] = sum;
	return sum;
}

Action Command_Greet(int client, int args) {
	char greeting[64];
	g_greeting.GetString(greeting, sizeof(greeting));
	char name[64];
	if (client == 0) {
		strcopy(name, sizeof(name), "console");
	} else {
		GetClientName(client, name, sizeof(name));
	}
	strcopy(g_lastName, sizeof(g_lastName), name);
	ReplyToCommand(client, "%s, %s", greeting, name);
	return Plugin_Handled;
}

Action Command_Admin(int client, int args) {
	ReplyToCommand(client, "admin command ran");
	return Plugin_Handled;
}

Action Timer_Tick(Handle timer) {
	g_ticks++;
	g_lastTick = GetGameTime();
	return Plugin_Continue;
}

public void OnClientPostAdminCheck(int client) {
	g_connected++;
	char auth[32];
	GetClientAuthId(client, AuthId_Steam2, auth, sizeof(auth));
	PrintToServer("post admin check %N %s", client, auth);
}

public void OnClientDisconnect(int client) {
	g_connected--;
}

public float Target_LastTick() {
	return g_lastTick;
}

public int Target_Divide(int a, int b) {
	return a / b;
}

// Last in the file, so the lines the breakpoint tests stop at stay where
// they are.
bool g_lateLoad;

public APLRes AskPluginLoad2(Handle myself, bool late, char[] error, int err_max) {
	g_lateLoad = late;
	return APLRes_Success;
}
