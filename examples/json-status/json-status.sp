/**
 * Copyright 2026 davenonymous
 *
 * An example plugin for smrunner: it only uses SMJansson and printing.
 * sm_status_json prints the server's status as JSON (map, host name and
 * the players with their Steam IDs), and writes it to
 * addons/sourcemod/data/status.json at every map start.
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <smjansson>

public Plugin myinfo = {
	name = "json-status",
	author = "davenonymous",
	description = "The server's status as JSON (an smrunner example)",
	version = "1.0.0",
	url = ""
};

char g_statusFile[PLATFORM_MAX_PATH];

public void OnPluginStart() {
	BuildPath(Path_SM, g_statusFile, sizeof(g_statusFile), "data/status.json");
	RegConsoleCmd("sm_status_json", Command_StatusJson, "Prints the server's status as JSON");
}

public void OnMapStart() {
	Handle status = BuildStatus();
	if (!json_dump_file(status, g_statusFile, 2)) {
		LogError("could not write %s", g_statusFile);
	}
	delete status;
}

Action Command_StatusJson(int client, int args) {
	Handle status = BuildStatus();
	char text[4096];
	json_dump(status, text, sizeof(text), 0, false, false, true);
	delete status;
	ReplyToCommand(client, "%s", text);
	return Plugin_Handled;
}

Handle BuildStatus() {
	Handle status = json_object();
	char map[PLATFORM_MAX_PATH];
	GetCurrentMap(map, sizeof(map));
	json_object_set_new(status, "map", json_string(map));
	char hostname[128];
	FindConVar("hostname").GetString(hostname, sizeof(hostname));
	json_object_set_new(status, "hostname", json_string(hostname));
	json_object_set_new(status, "maxplayers", json_integer(MaxClients));
	json_object_set_new(status, "players", BuildPlayers());
	return status;
}

Handle BuildPlayers() {
	Handle players = json_array();
	for (int client = 1; client <= MaxClients; client++) {
		if (!IsClientInGame(client) || IsFakeClient(client)) {
			continue;
		}
		json_array_append_new(players, BuildPlayer(client));
	}
	return players;
}

Handle BuildPlayer(int client) {
	Handle player = json_object();
	char name[MAX_NAME_LENGTH];
	GetClientName(client, name, sizeof(name));
	json_object_set_new(player, "name", json_string(name));
	json_object_set_new(player, "userid", json_integer(GetClientUserId(client)));
	char steamId[32];
	if (GetClientAuthId(client, AuthId_SteamID64, steamId, sizeof(steamId))) {
		json_object_set_new(player, "steamid", json_string(steamId));
	} else {
		json_object_set_new(player, "steamid", json_null());
	}
	return player;
}
