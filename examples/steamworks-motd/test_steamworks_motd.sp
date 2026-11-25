/**
 * Copyright 2026 davenonymous
 *
 * Tests of steamworks-motd.smx with smrunner's SteamWorks stand-in, its
 * answers from the mapping file in steamworks/ and the test's routes:
 *
 *     smrunner test test_steamworks_motd.smx --plugin steamworks-motd.smx \
 *         --steamworks steamworks
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <SteamWorks>
#include <smrunner>
#include <smrunner-steamworks>

#define MOTD_URL "http://motd.test/motd.txt"

public void Test_MotdFromTheMappingFile() {
	Assert.Equal(RunnerHttp.CountRequests(MOTD_URL), 1, "fetched once when the configs ran");
	char accept[32];
	Assert.True(RunnerHttp.GetRequest(0).GetHeader("Accept", accept, sizeof(accept)));
	Assert.StrEqual(accept, "text/plain");
	int alice = Runner.ConnectClient("alice");
	Assert.ClientReceived(alice, RunnerMessage_Chat, "[MOTD] Welcome! Matches start at 20:00.");
}

public void Test_RouteChangesTheMotd() {
	RunnerHttp.Route(k_EHTTPMethodGET, MOTD_URL, k_EHTTPStatusCode200OK, "Server restart at midnight.");
	Runner.Command("sm_motd_refresh");
	Runner.Frames(1);
	int alice = Runner.ConnectClient("alice");
	Assert.ClientReceived(alice, RunnerMessage_Chat, "[MOTD] Server restart at midnight.");
}

public void Test_FailedRequestKeepsTheLastMotd() {
	RunnerHttp.Route(k_EHTTPMethodGET, MOTD_URL, k_EHTTPStatusCode503ServiceUnavailable);
	Runner.Command("sm_motd_refresh");
	Runner.Frames(1);
	Assert.ConsoleContains("The message of the day was not fetched (status 503)");
	int alice = Runner.ConnectClient("alice");
	Assert.ClientReceived(alice, RunnerMessage_Chat, "[MOTD] Welcome!");
}

public void Test_ServerInfoFromSteam() {
	char output[256];
	Runner.CommandOutput(output, sizeof(output), "sm_serverinfo");
	Assert.StrContains(output, "connect 127.0.0.1 (VAC secured)");
	RunnerSteam.SetPublicIP("203.0.113.7");
	RunnerSteam.SetVACEnabled(false);
	Runner.CommandOutput(output, sizeof(output), "sm_serverinfo");
	Assert.StrContains(output, "connect 203.0.113.7 (not VAC secured)");
	RunnerSteam.FireServersDisconnected(k_EResultNoConnection);
	Runner.CommandOutput(output, sizeof(output), "sm_serverinfo");
	Assert.StrContains(output, "not connected to Steam");
}
