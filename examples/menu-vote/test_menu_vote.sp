/**
 * Copyright 2026 davenonymous
 *
 * Tests of menu-vote.smx: what each client is shown, votes by the text of
 * the items, the time a vote runs and the map change after it.
 *
 *     smrunner test test_menu_vote.smx --plugin menu-vote.smx \
 *         --maps cp_process_final,koth_viaduct
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <smrunner>

int g_alice;
int g_bob;
int g_carol;

void ConnectVoters() {
	g_alice = Runner.ConnectClient("alice");
	g_bob = Runner.ConnectClient("bob");
	g_carol = Runner.ConnectClient("carol");
	SetUserFlagBits(g_alice, ADMFLAG_VOTE);
}

public void Test_EveryoneVotesAndTheWinnerIsNext() {
	ConnectVoters();
	Runner.ClientCommand(g_alice, "sm_mapvote");
	Assert.ClientMenu(g_carol, "Choose the next map");
	Assert.ClientMenu(g_carol, "->3. koth_viaduct");

	Runner.SelectMenuItem(g_alice, "koth_viaduct");
	Runner.SelectMenuItem(g_bob, "koth_viaduct");
	Runner.SelectMenuItem(g_carol, "cp_badlands");
	Assert.ClientReceived(g_bob, RunnerMessage_Chat, "[Vote] Next map: koth_viaduct", "without the colors");

	Runner.Seconds(6.0);
	char map[64];
	GetCurrentMap(map, sizeof(map));
	Assert.StrEqual(map, "koth_viaduct");
}

public void Test_AVoteWithoutVotesEndsAfterItsTime() {
	ConnectVoters();
	Runner.ClientCommand(g_alice, "sm_mapvote");
	Runner.Seconds(19.0);
	Assert.True(IsVoteInProgress(), "the vote runs for 20 seconds");
	Runner.Seconds(2.0);
	Assert.False(IsVoteInProgress());
	Assert.ClientReceived(g_alice, RunnerMessage_Chat, "Nobody voted.");
}

public void Test_OnlyOneVoteRunsAtATime() {
	ConnectVoters();
	Runner.ClientCommand(g_alice, "sm_mapvote");
	Runner.ClientCommand(g_alice, "sm_mapvote");
	Assert.ClientReceived(g_alice, RunnerMessage_Console, "A vote is already running.");
}

public void Test_PlayersWithoutTheVoteFlagCannotStartOne() {
	ConnectVoters();
	Runner.ClientCommand(g_bob, "sm_mapvote");
	Assert.False(IsVoteInProgress());
}

public void Test_MapInfoShowsTheMapAndCloses() {
	ConnectVoters();
	Runner.ClientCommand(g_bob, "sm_mapinfo");
	Assert.ClientMenu(g_bob, "Current map: cp_badlands\nPlayers: 3\n->1. Close");
	Runner.SelectMenuItem(g_bob, "Close");
	char text[256];
	Assert.False(Runner.GetClientMenu(g_bob, text, sizeof(text)));
}
