/**
 * Copyright 2026 davenonymous
 *
 * An example plugin for smrunner: a map vote and a panel. sm_mapvote (vote
 * flag) lets everyone vote on the maps of sm_mapvote_maps; the map that won
 * is told in chat and changed to five seconds later. sm_mapinfo shows the
 * current map and the number of players in a panel.
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>

public Plugin myinfo = {
	name = "menu-vote",
	author = "davenonymous",
	description = "A map vote and a panel (an smrunner example)",
	version = "1.0.0",
	url = ""
};

#define VOTE_SECONDS 20
#define CHANGE_DELAY 5.0

ConVar g_maps;
char g_nextMap[PLATFORM_MAX_PATH];

public void OnPluginStart() {
	g_maps = CreateConVar("sm_mapvote_maps", "cp_badlands,cp_process_final,koth_viaduct",
	                      "The maps sm_mapvote offers, separated by commas");
	RegAdminCmd("sm_mapvote", Command_MapVote, ADMFLAG_VOTE, "Starts a vote on the next map");
	RegConsoleCmd("sm_mapinfo", Command_MapInfo, "Shows the current map");
}

Action Command_MapVote(int client, int args) {
	if (IsVoteInProgress()) {
		ReplyToCommand(client, "A vote is already running.");
		return Plugin_Handled;
	}
	char list[512];
	g_maps.GetString(list, sizeof(list));
	char maps[8][PLATFORM_MAX_PATH];
	int count = ExplodeString(list, ",", maps, sizeof(maps), sizeof(maps[]));
	Menu menu = new Menu(VoteHandler_Map, MENU_ACTIONS_DEFAULT | MenuAction_VoteCancel);
	menu.SetTitle("Choose the next map");
	for (int i = 0; i < count; i++) {
		TrimString(maps[i]);
		menu.AddItem(maps[i], maps[i]);
	}
	menu.ExitButton = false;
	menu.DisplayVoteToAll(VOTE_SECONDS);
	return Plugin_Handled;
}

int VoteHandler_Map(Menu menu, MenuAction action, int param1, int param2) {
	switch (action) {
		case MenuAction_VoteEnd: {
			menu.GetItem(param1, g_nextMap, sizeof(g_nextMap));
			PrintToChatAll("\x04[Vote]\x01 Next map: %s", g_nextMap);
			CreateTimer(CHANGE_DELAY, Timer_ChangeMap);
		}
		case MenuAction_VoteCancel: {
			if (param1 == VoteCancel_NoVotes) {
				PrintToChatAll("\x04[Vote]\x01 Nobody voted.");
			}
		}
		case MenuAction_End: {
			delete menu;
		}
	}
	return 0;
}

Action Timer_ChangeMap(Handle timer) {
	ForceChangeLevel(g_nextMap, "map vote");
	return Plugin_Stop;
}

Action Command_MapInfo(int client, int args) {
	char map[PLATFORM_MAX_PATH];
	GetCurrentMap(map, sizeof(map));
	char line[128];
	Panel panel = new Panel();
	panel.SetTitle("Map information");
	Format(line, sizeof(line), "Current map: %s", map);
	panel.DrawText(line);
	Format(line, sizeof(line), "Players: %d", GetClientCount());
	panel.DrawText(line);
	panel.DrawItem("Close");
	panel.Send(client, PanelHandler_Close, MENU_TIME_FOREVER);
	delete panel;
	return Plugin_Handled;
}

int PanelHandler_Close(Menu menu, MenuAction action, int param1, int param2) {
	return 0;
}
