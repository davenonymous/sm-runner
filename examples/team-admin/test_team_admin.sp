/**
 * Copyright 2026 davenonymous
 *
 * Tests of team-admin.smx with SourceMod's stock adminmenu plugin and the
 * Top Menus extension that smrunner ships:
 *
 *     smrunner test test_team_admin.smx --plugin team-admin.smx --plugin adminmenu
 *
 * They go through the admin menu by the text of its lines, read colored
 * chat with and without its colors, and press the panel's fixed key 0.
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <smrunner>

int ConnectAdmin() {
	int admin = Runner.ConnectClient("admin");
	SetUserFlagBits(admin, ADMFLAG_GENERIC | ADMFLAG_CHAT);
	return admin;
}

public void Test_AnnounceSendsColoredChatToEveryone() {
	int admin = ConnectAdmin();
	int player = Runner.ConnectClient("player");
	FindConVar("sm_teamadmin_announcement").SetString("Scrim at eight");

	Runner.ClientCommand(admin, "sm_admin");
	Runner.SelectMenuItem(admin, "Team tools");
	Assert.ClientMenu(admin, "->1. Announce");
	Runner.SelectMenuItem(admin, "Announce");

	Assert.ClientReceived(player, RunnerMessage_Chat, "[Team] Scrim at eight", "the text without its colors");
	Assert.ClientReceived(player, RunnerMessage_Chat, "\x07FFD700[Team]\x01 Scrim at eight", "the gold tag");
	Assert.ClientReceived(admin, RunnerMessage_Chat, "[Team] Scrim at eight");
}

public void Test_TeamInfoShowsDisabledLinesAndCloses() {
	int player = Runner.ConnectClient("player");
	Runner.ClientCommand(player, "sm_teaminfo");
	Assert.ClientMenu(player, "Team information\nPractice every Tuesday\n");
	Assert.ClientMenu(player, "\n2. Next scrim (none planned)\n", "a disabled line has no arrow");
	Assert.Equal(Runner.ClientMenuKeys(player), (1 << 0) | (1 << 9), "keys 1 and 0");
	Runner.PrintClientMenu(player);
	Runner.SelectMenu(player, 10);
	char text[256];
	Assert.False(Runner.GetClientMenu(player, text, sizeof(text)), "0 closed the panel");
}

public void Test_PlayersOnlineAreListedInChat() {
	int player = Runner.ConnectClient("player");
	Runner.ConnectClient("teammate");
	Runner.ClientCommand(player, "sm_teaminfo");
	Runner.SelectMenuItem(player, "Players online");
	Assert.ClientReceived(player, RunnerMessage_Chat, "[Team] teammate");
}
