/**
 * Copyright 2026 davenonymous
 *
 * The plugin the menu self tests test: menus, a panel and a vote, opened by
 * console commands of the client, each telling on the server console what
 * its handler was called with.
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>

public Plugin myinfo = {
	name = "smrunner menu self test target",
	author = "davenonymous",
	description = "The menus of smrunner's self tests",
	version = "1.0.0",
	url = ""
};

public void OnPluginStart() {
	RegConsoleCmd("sm_mt_fruit", Command_Fruit, "sm_mt_fruit [seconds]: a menu of three fruits");
	RegConsoleCmd("sm_mt_long", Command_Long, "a menu of twelve items over two pages");
	RegConsoleCmd("sm_mt_panel", Command_Panel, "a panel with a fixed 0 key");
	RegConsoleCmd("sm_mt_lookalike", Command_Lookalike, "a panel with text lines that look like items");
	RegConsoleCmd("sm_mt_vote", Command_Vote, "a vote of everyone on yes or no");
	RegConsoleCmd("sm_mt_valve", Command_Valve, "asks for the valve menu style");
}

int MenuHandler_Report(Menu menu, MenuAction action, int param1, int param2) {
	switch (action) {
		case MenuAction_Select: {
			char info[32];
			menu.GetItem(param2, info, sizeof(info));
			PrintToServer("menu_target: select %s by %d", info, param1);
		}
		case MenuAction_Cancel: {
			PrintToServer("menu_target: cancel %d for %d", param2, param1);
		}
		case MenuAction_End: {
			PrintToServer("menu_target: end %d", param1);
			delete menu;
		}
	}
	return 0;
}

Action Command_Fruit(int client, int args) {
	Menu menu = new Menu(MenuHandler_Report);
	menu.SetTitle("Pick a fruit");
	menu.AddItem("apple", "Apple");
	menu.AddItem("banana", "Banana");
	menu.AddItem("cherry", "Cherry", ITEMDRAW_DISABLED);
	int time = args > 0 ? GetCmdArgInt(1) : MENU_TIME_FOREVER;
	PrintToServer("menu_target: shown %d", menu.Display(client, time));
	return Plugin_Handled;
}

Action Command_Long(int client, int args) {
	Menu menu = new Menu(MenuHandler_Report);
	menu.SetTitle("Twelve items");
	char info[16];
	char display[16];
	for (int i = 1; i <= 12; i++) {
		Format(info, sizeof(info), "item%d", i);
		Format(display, sizeof(display), "Item %d", i);
		menu.AddItem(info, display);
	}
	menu.Display(client, MENU_TIME_FOREVER);
	return Plugin_Handled;
}

int PanelHandler_Report(Menu menu, MenuAction action, int param1, int param2) {
	if (action == MenuAction_Select) {
		PrintToServer("menu_target: panel key %d by %d", param2, param1);
	} else if (action == MenuAction_Cancel) {
		PrintToServer("menu_target: panel cancel %d for %d", param2, param1);
	}
	return 0;
}

Action Command_Panel(int client, int args) {
	Panel panel = new Panel();
	panel.SetTitle("Information");
	panel.DrawText("Read this first");
	panel.DrawItem("Okay");
	panel.DrawItem("Not now", ITEMDRAW_DISABLED);
	panel.CurrentKey = 10;
	panel.DrawItem("Close");
	panel.Send(client, PanelHandler_Report, MENU_TIME_FOREVER);
	delete panel;
	return Plugin_Handled;
}

// Text lines a radio menu would never draw as items: "->1.Red herring"
// lacks the space after the key's dot, "2. Blue" lacks the arrow of a key
// the panel takes.
Action Command_Lookalike(int client, int args) {
	Panel panel = new Panel();
	panel.DrawText("->1.Red herring");
	panel.DrawItem("Red");
	panel.DrawText("2. Blue");
	panel.Send(client, PanelHandler_Report, MENU_TIME_FOREVER);
	delete panel;
	return Plugin_Handled;
}

int VoteHandler_Report(Menu menu, MenuAction action, int param1, int param2) {
	if (action == MenuAction_VoteEnd) {
		char info[32];
		menu.GetItem(param1, info, sizeof(info));
		int votes;
		int total;
		GetMenuVoteInfo(param2, votes, total);
		PrintToServer("menu_target: vote won %s with %d of %d", info, votes, total);
	} else if (action == MenuAction_End) {
		delete menu;
	}
	return 0;
}

Action Command_Vote(int client, int args) {
	if (IsVoteInProgress()) {
		ReplyToCommand(client, "a vote is running");
		return Plugin_Handled;
	}
	Menu menu = new Menu(VoteHandler_Report);
	menu.SetTitle("Continue?");
	menu.AddItem("yes", "Yes");
	menu.AddItem("no", "No");
	menu.ExitButton = false;
	menu.DisplayVoteToAll(10);
	return Plugin_Handled;
}

Action Command_Valve(int client, int args) {
	Handle style = GetMenuStyleHandle(MenuStyle_Valve);
	PrintToServer("menu_target: valve style %x", style);
	return Plugin_Handled;
}
