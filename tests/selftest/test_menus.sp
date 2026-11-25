/**
 * Copyright 2026 davenonymous
 *
 * Self tests of menus, panels and votes, against menu_target.smx.
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <smrunner>

// The bit of a key in a menu display's keys: bit 0 is key 1, bit 9 key 0.
int Key(int key) {
	return 1 << (key == 0 ? 9 : key - 1);
}

int g_menuselects;

public void Test_AMenuShowsItsLinesAndKeys() {
	int client = Runner.ConnectClient("alice");
	Runner.ClientCommand(client, "sm_mt_fruit");
	Assert.ConsoleContains("menu_target: shown 1");
	Assert.ClientMenu(client, "Pick a fruit");
	Assert.ClientMenu(client, "->1. Apple\n->2. Banana\n3. Cherry\n", "a disabled item has no arrow");
	Assert.ClientMenu(client, "->0. Exit");
	Assert.Equal(Runner.ClientMenuKeys(client), Key(1) | Key(2) | Key(0));
	Assert.Equal(Runner.ClientMessageCount(client, RunnerMessage_Menu), 1);
}

public void Test_SelectingAnItemCallsTheHandlerAndClosesTheMenu() {
	int client = Runner.ConnectClient("alice");
	Runner.ClientCommand(client, "sm_mt_fruit");
	Runner.SelectMenuItem(client, "Banana");
	Assert.ConsoleContains("menu_target: select banana by 1");
	Assert.ConsoleContains("menu_target: end 0", "MenuEnd_Selected");
	char text[512];
	Assert.False(Runner.GetClientMenu(client, text, sizeof(text)), "the client closed the menu");
	Assert.StrEqual(text, "");
	Assert.Equal(Runner.ClientMenuKeys(client), 0);
}

public void Test_ExitCancelsTheMenu() {
	int client = Runner.ConnectClient("alice");
	Runner.ClientCommand(client, "sm_mt_fruit");
	Runner.SelectMenu(client, 10);
	Assert.ConsoleContains("menu_target: cancel -3 for 1", "MenuCancel_Exit");
}

public void Test_PagesGoForwardAndBack() {
	int client = Runner.ConnectClient("alice");
	Runner.ClientCommand(client, "sm_mt_long");
	Assert.ClientMenu(client, "->7. Item 7");
	Runner.SelectMenuItem(client, "Next");
	Assert.ClientMenu(client, "->1. Item 8");
	Assert.ClientMenu(client, "->5. Item 12");
	Runner.SelectMenuItem(client, "Previous");
	Assert.ClientMenu(client, "->1. Item 1");
	Assert.Equal(Runner.ClientMessageCount(client, RunnerMessage_Menu), 3, "every page is a display");
	Runner.SelectMenuItem(client, "Item 3");
	Assert.ConsoleContains("menu_target: select item3 by 1");
}

public void Test_AMenuTimesOut() {
	int client = Runner.ConnectClient("alice");
	Runner.ClientCommand(client, "sm_mt_fruit 2");
	Runner.Seconds(1.0);
	Assert.ClientMenu(client, "Pick a fruit", "still shown after a second");
	Runner.Seconds(2.5);
	Assert.ConsoleContains("menu_target: cancel -5 for 1", "MenuCancel_Timeout");
	char text[512];
	Assert.False(Runner.GetClientMenu(client, text, sizeof(text)), "the client no longer shows it");
}

public void Test_RefreshesAreNotRecorded() {
	int client = Runner.ConnectClient("alice");
	Runner.ClientCommand(client, "sm_mt_fruit");
	Runner.Seconds(10.0);
	Assert.Equal(Runner.ClientMessageCount(client, RunnerMessage_Menu), 1);
	Assert.Equal(Runner.ClientMessageCount(client, RunnerMessage_UserMessage), 0, "nor as ShowMenu messages");
	Assert.ClientMenu(client, "Pick a fruit", "a menu without a time stays");
}

Action Hook_OpenFruitMenu(UserMsg id, BfRead message, const int[] players, int playersNum, bool reliable,
                          bool init) {
	FakeClientCommand(players[0], "sm_mt_fruit 20");
	return Plugin_Continue;
}

public void Test_AMenuDisplayedInsideAUserMessageHookIsAnError() {
	int client = Runner.ConnectClient("alice");
	HookUserMessage(GetUserMessageId("TextMsg"), Hook_OpenFruitMenu);
	// The menu is displayed in the hook of another message, where its
	// ShowMenu cannot be started; SourceMod's radio style crashes the server
	// there.
	Runner.ExpectError("menu_target.smx: a menu was displayed to client 1 while a user message was being sent or hooked");
	PrintCenterText(client, "a message with a hook");
	Assert.Equal(Runner.ClientMessageCount(client, RunnerMessage_Menu), 0);
}

public void Test_APanelTakesItsFixedKeys() {
	int client = Runner.ConnectClient("alice");
	Runner.ClientCommand(client, "sm_mt_panel");
	Assert.ClientMenu(client, "Information\nRead this first\n->1. Okay\n2. Not now\n->0. Close\n");
	Assert.Equal(Runner.ClientMenuKeys(client), Key(1) | Key(0));
	Runner.SelectMenuItem(client, "Close");
	Assert.ConsoleContains("menu_target: panel key 10 by 1");
}

public void Test_OnlyTheRadioStylesItemsAreSelectable() {
	int client = Runner.ConnectClient("alice");
	Runner.ClientCommand(client, "sm_mt_lookalike");
	Assert.Equal(Runner.ClientMenuKeys(client), Key(1));
	// "->1.Red herring" is a text line, so "Red" is in one item only.
	Runner.SelectMenuItem(client, "Red");
	Assert.ConsoleContains("menu_target: panel key 1 by 1");
}

public void Test_AnotherMenuInterruptsTheFirst() {
	int client = Runner.ConnectClient("alice");
	Runner.ClientCommand(client, "sm_mt_fruit");
	Runner.ClientCommand(client, "sm_mt_panel");
	Assert.ConsoleContains("menu_target: cancel -2 for 1", "MenuCancel_Interrupted");
	Assert.ClientMenu(client, "Information");
}

public void Test_DisconnectingCancelsTheMenu() {
	int client = Runner.ConnectClient("alice");
	Runner.ClientCommand(client, "sm_mt_fruit");
	Runner.Disconnect(client);
	Assert.ConsoleContains("menu_target: cancel -1 for 1", "MenuCancel_Disconnected");
}

public void Test_AMapChangeCancelsTheMenu() {
	int client = Runner.ConnectClient("alice");
	Runner.ClientCommand(client, "sm_mt_fruit");
	Runner.ChangeMap("cp_badlands");
	Assert.ConsoleContains("menu_target: cancel -1 for 1");
}

public void Test_BotsGetNoMenu() {
	int bot = Runner.ConnectBot("bot");
	Runner.ClientCommand(bot, "sm_mt_fruit");
	Assert.ConsoleContains("menu_target: shown 0");
}

Action Listener_Menuselect(int client, const char[] command, int args) {
	g_menuselects++;
	return Plugin_Continue;
}

public void Test_AKeyWithoutAMenuIsAClientCommand() {
	int client = Runner.ConnectClient("alice");
	AddCommandListener(Listener_Menuselect, "menuselect");
	Runner.ClearClientMessages(client);
	Runner.SelectMenu(client, 1);
	Assert.Equal(g_menuselects, 1);
	Assert.ConsoleNotContains("menu_target: select");
	Assert.Equal(Runner.ClientMessageCount(client), 0, "TF2's game rules take menuselect without an answer");
}

public void Test_AVoteCountsTheVotes() {
	int alice = Runner.ConnectClient("alice");
	int bob = Runner.ConnectClient("bob");
	int carol = Runner.ConnectClient("carol");
	Runner.ClientCommand(alice, "sm_mt_vote");
	Assert.True(IsVoteInProgress());
	Assert.ClientMenu(carol, "Continue?");
	Runner.ClientCommand(bob, "sm_mt_vote");
	Assert.ClientReceived(bob, RunnerMessage_Console, "a vote is running");
	Runner.SelectMenuItem(alice, "Yes");
	Runner.SelectMenuItem(bob, "Yes");
	Runner.SelectMenuItem(carol, "No");
	Assert.ConsoleContains("menu_target: vote won yes with 2 of 3");
	Assert.False(IsVoteInProgress());
}

public void Test_TheValveStyleIsAnError() {
	int client = Runner.ConnectClient("alice");
	Runner.ExpectError("The menu style \"valve\" (MenuStyle_Valve) is not supported by smrunner");
	Runner.ClientCommand(client, "sm_mt_valve");
	Assert.ConsoleNotContains("menu_target: valve style");
}

public void Test_APrintedMenuIsABox() {
	int client = Runner.ConnectClient("alice");
	Runner.ClientCommand(client, "sm_mt_long");
	Runner.SelectMenuItem(client, "Next");
	Runner.PrintClientMenu(client);
	Runner.ClientCommand(client, "sm_mt_fruit 20");
	Runner.PrintClientMenu(client);
}
