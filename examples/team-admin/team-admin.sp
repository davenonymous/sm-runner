/**
 * Copyright 2026 davenonymous
 *
 * An example plugin for smrunner: an admin menu category and colored chat.
 * "Team tools" in the admin menu (SourceMod's adminmenu plugin) has
 * "Announce", which sends sm_teamadmin_announcement to everyone as a chat
 * line of the admin's (SayText2) with a gold tag. sm_teaminfo shows a panel
 * with a disabled line, an item that lists the players in chat and Close on
 * the key 0.
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#undef REQUIRE_PLUGIN
#include <adminmenu>

public Plugin myinfo = {
	name = "team-admin",
	author = "davenonymous",
	description = "An admin menu category and colored chat (an smrunner example)",
	version = "1.0.0",
	url = ""
};

#define TAG "\x07FFD700[Team]\x01 "
#define KEY_ZERO 10

ConVar g_announcement;
TopMenu g_adminMenu;

public void OnPluginStart() {
	g_announcement = CreateConVar("sm_teamadmin_announcement", "Scrim starts in 5 minutes",
	                              "What Announce of the admin menu says");
	RegConsoleCmd("sm_teaminfo", Command_TeamInfo, "Shows the team information");
	TopMenu menu;
	if (LibraryExists("adminmenu") && (menu = GetAdminTopMenu()) != null) {
		OnAdminMenuReady(menu);
	}
}

public void OnAdminMenuReady(Handle topMenu) {
	TopMenu menu = TopMenu.FromHandle(topMenu);
	if (menu == g_adminMenu) {
		return;
	}
	g_adminMenu = menu;
	TopMenuObject category = menu.AddCategory("team_tools", Handler_Category);
	menu.AddItem("team_announce", Handler_Announce, category, "sm_teamadmin_announce", ADMFLAG_CHAT);
}

public void OnLibraryRemoved(const char[] name) {
	if (StrEqual(name, "adminmenu")) {
		g_adminMenu = null;
	}
}

void Handler_Category(TopMenu menu, TopMenuAction action, TopMenuObject topObject, int client, char[] buffer,
                      int maxlength) {
	if (action == TopMenuAction_DisplayOption || action == TopMenuAction_DisplayTitle) {
		strcopy(buffer, maxlength, "Team tools");
	}
}

void Handler_Announce(TopMenu menu, TopMenuAction action, TopMenuObject topObject, int client, char[] buffer,
                      int maxlength) {
	if (action == TopMenuAction_DisplayOption) {
		strcopy(buffer, maxlength, "Announce");
	} else if (action == TopMenuAction_SelectOption) {
		char text[192];
		g_announcement.GetString(text, sizeof(text));
		Announce(client, text);
	}
}

// One chat line of the author's to everyone in the game.
void Announce(int author, const char[] text) {
	char line[256];
	Format(line, sizeof(line), "%s%s", TAG, text);
	BfWrite message = UserMessageToBfWrite(StartMessageAll("SayText2", USERMSG_RELIABLE));
	message.WriteByte(author);
	message.WriteByte(true);
	message.WriteString(line);
	EndMessage();
}

Action Command_TeamInfo(int client, int args) {
	Panel panel = new Panel();
	panel.SetTitle("Team information");
	panel.DrawText("Practice every Tuesday");
	panel.DrawItem("Players online");
	panel.DrawItem("Next scrim (none planned)", ITEMDRAW_DISABLED);
	panel.CurrentKey = KEY_ZERO;
	panel.DrawItem("Close");
	panel.Send(client, PanelHandler_TeamInfo, MENU_TIME_FOREVER);
	delete panel;
	return Plugin_Handled;
}

int PanelHandler_TeamInfo(Menu menu, MenuAction action, int client, int key) {
	if (action != MenuAction_Select || key != 1) {
		return 0;
	}
	char name[MAX_NAME_LENGTH];
	for (int i = 1; i <= MaxClients; i++) {
		if (IsClientInGame(i) && !IsFakeClient(i)) {
			GetClientName(i, name, sizeof(name));
			PrintToChat(client, "%s%s", TAG, name);
		}
	}
	return 0;
}
