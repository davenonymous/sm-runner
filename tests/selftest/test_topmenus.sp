/**
 * Copyright 2026 davenonymous
 *
 * Self tests of SourceMod's Top Menus extension, which the runner ships,
 * with SourceMod's stock adminmenu plugin (--plugin adminmenu).
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <adminmenu>
#include <smrunner>

int g_selected;

void Handler_Category(TopMenu menu, TopMenuAction action, TopMenuObject topObject, int client, char[] buffer,
                      int maxlength) {
	if (action == TopMenuAction_DisplayOption || action == TopMenuAction_DisplayTitle) {
		strcopy(buffer, maxlength, "Runner tests");
	}
}

void Handler_Item(TopMenu menu, TopMenuAction action, TopMenuObject topObject, int client, char[] buffer,
                  int maxlength) {
	if (action == TopMenuAction_DisplayOption) {
		strcopy(buffer, maxlength, "Run the item");
	} else if (action == TopMenuAction_SelectOption) {
		g_selected = client;
	}
}

int ConnectAdmin() {
	int client = Runner.ConnectClient("admin");
	SetUserFlagBits(client, ADMFLAG_ROOT);
	return client;
}

void AddTestCategory() {
	TopMenu menu = GetAdminTopMenu();
	Assert.True(menu != null, "adminmenu made its top menu");
	TopMenuObject category = menu.AddCategory("runner_tests", Handler_Category);
	menu.AddItem("runner_item", Handler_Item, category, "sm_runner_item", ADMFLAG_GENERIC);
}

public void Test_AnItemOfACategoryIsSelected() {
	AddTestCategory();
	int client = ConnectAdmin();
	Runner.ClientCommand(client, "sm_admin");
	Assert.ClientMenu(client, "Runner tests");
	Runner.SelectMenuItem(client, "Runner tests");
	Assert.ClientMenu(client, "Run the item");
	Runner.SelectMenuItem(client, "Run the item");
	Assert.Equal(g_selected, client);
}

public void Test_BackLeadsToTheRootMenu() {
	AddTestCategory();
	int client = ConnectAdmin();
	Runner.ClientCommand(client, "sm_admin");
	Runner.SelectMenuItem(client, "Runner tests");
	Runner.SelectMenuItem(client, "Back");
	Assert.ClientMenu(client, "->1. Runner tests");
}
