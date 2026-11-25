/**
 * Copyright 2026 davenonymous
 *
 * Tests of json-status.smx with the real SMJansson extension:
 *
 *     smrunner test test_json_status.smx --plugin json-status.smx \
 *         --extensions <SMJansson's addons/sourcemod/extensions>
 *
 * They read the plugin's JSON back with SMJansson too, connect clients,
 * mock a native to make a Steam ID pending and use a breakpoint to look at
 * the plugin while it builds a player.
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <smjansson>
#include <smrunner>

// Runs sm_status_json and parses what it printed.
Handle StatusFromCommand() {
	Runner.ClearConsole();
	Runner.Command("sm_status_json");
	char output[4096];
	Runner.GetConsole(output, sizeof(output));
	TrimString(output);
	Handle status = json_load(output);
	Assert.True(status != null, "sm_status_json printed JSON: %s", output);
	return status;
}

public void Test_StatusOfAnEmptyServer() {
	Handle status = StatusFromCommand();
	char map[64];
	json_object_get_string(status, "map", map, sizeof(map));
	Assert.StrEqual(map, "cp_badlands");
	Assert.Equal(json_object_get_int(status, "maxplayers"), 24);
	Handle players = json_object_get(status, "players");
	Assert.Equal(json_array_size(players), 0, "no players yet");
	delete players;
	delete status;
}

public void Test_HostnameComesFromTheCvar() {
	FindConVar("hostname").SetString("smrunner example");
	Handle status = StatusFromCommand();
	char hostname[64];
	json_object_get_string(status, "hostname", hostname, sizeof(hostname));
	Assert.StrEqual(hostname, "smrunner example");
	delete status;
}

public void Test_PlayersAreListedButNotBots() {
	Runner.ConnectClient("Alice", "[U:1:1001]");
	Runner.ConnectBot("Bot01");
	Runner.ConnectClient("Bob", "76561197960266730");
	Handle status = StatusFromCommand();
	Handle players = json_object_get(status, "players");
	Assert.Equal(json_array_size(players), 2, "two humans");
	Handle alice = json_array_get(players, 0);
	char name[64];
	json_object_get_string(alice, "name", name, sizeof(name));
	Assert.StrEqual(name, "Alice");
	char steamId[32];
	json_object_get_string(alice, "steamid", steamId, sizeof(steamId));
	Assert.StrEqual(steamId, "76561197960266729", "[U:1:1001] as a 64-bit Steam ID");
	delete alice;
	delete players;
	delete status;
}

public void Test_RepliesGoToTheClientThatAsked() {
	int client = Runner.ConnectClient("Carol");
	Runner.ClientCommand(client, "sm_status_json");
	Assert.ClientReceived(client, RunnerMessage_Console, "\"name\": \"Carol\"");
}

public void Test_StatusFileIsWrittenAtMapStart() {
	char path[PLATFORM_MAX_PATH];
	BuildPath(Path_SM, path, sizeof(path), "data/status.json");
	Handle status = json_load_file(path);
	Assert.True(status != null, "%s holds JSON", path);
	char map[64];
	json_object_get_string(status, "map", map, sizeof(map));
	Assert.StrEqual(map, "cp_badlands");
	delete status;

	Runner.ChangeMap("koth_viaduct");
	status = json_load_file(path);
	json_object_get_string(status, "map", map, sizeof(map));
	Assert.StrEqual(map, "koth_viaduct", "the file follows the map");
	delete status;
}

// GetClientAuthId fails while Steam has not validated the client yet.
any Mock_PendingSteamId(RunnerCall call) {
	return false;
}

public void Test_PendingSteamIdsAreNull() {
	Runner.ConnectClient("Dave");
	Runner.MockNative("GetClientAuthId", Mock_PendingSteamId);
	Handle status = StatusFromCommand();
	Handle players = json_object_get(status, "players");
	Handle dave = json_array_get(players, 0);
	Handle steamId = json_object_get(dave, "steamid");
	Assert.Equal(json_typeof(steamId), JSON_NULL, "a pending Steam ID is null");
	Assert.Equal(Runner.NativeCalls("GetClientAuthId"), 1);
	delete steamId;
	delete dave;
	delete players;
	delete status;
}

char g_seenName[64];
int g_seenClient;

void OnBuildPlayer(Breakpoint frame) {
	g_seenClient = frame.GetValue("client");
	// BuildPlayer's first statement; name is declared but not read yet.
	Assert.True(frame.HasVariable("client"));
}

void OnPlayerNamed(Breakpoint frame) {
	frame.GetString("name", g_seenName, sizeof(g_seenName));
	frame.SetString("name", "renamed in a breakpoint");
}

public void Test_BreakpointsLookIntoThePlugin() {
	int client = Runner.ConnectClient("Erin");
	Runner.BreakAtFunction("BuildPlayer", OnBuildPlayer);
	// json-status.sp:75, the line after GetClientName: name holds the name.
	Runner.BreakAtLine("json-status.sp", 75, OnPlayerNamed);
	Handle status = StatusFromCommand();
	Assert.Equal(g_seenClient, client);
	Assert.StrEqual(g_seenName, "Erin");
	Handle players = json_object_get(status, "players");
	Handle erin = json_array_get(players, 0);
	char name[64];
	json_object_get_string(erin, "name", name, sizeof(name));
	Assert.StrEqual(name, "renamed in a breakpoint");
	delete erin;
	delete players;
	delete status;
}
