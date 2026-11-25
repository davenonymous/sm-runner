/**
 * Copyright 2026 davenonymous
 *
 * Self tests of the SteamWorks stand-in without a mapping file:
 *
 *     smrunner test test_steamworks_bare.smx --plugin steamworks_target.smx --steamworks
 *
 * Every request is 404 unless a route answers it, and nothing goes over the
 * network without an allowed host.
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <SteamWorks>
#include <smrunner>
#include <smrunner-steamworks>

any Value(const char[] name) {
	return Runner.GetPluginValue(FindPluginByFile("steamworks_target.smx"), name);
}

public void Test_EverythingIs404WithoutAMappingFile() {
	Runner.Command("sw_get \"http://api.test/team/4242\"");
	Runner.Frames(1);
	Assert.Equal(Value("g_status"), 404);
	Assert.ConsoleContains("no answer for GET http://api.test/team/4242: 404");
}

public void Test_RoutesAnswerWithoutAMappingFile() {
	RunnerHttp.Route(k_EHTTPMethodGET, "http://api.test/*", k_EHTTPStatusCode200OK, "routed");
	Runner.Command("sw_get \"http://api.test/team/4242\"");
	Runner.Frames(1);
	Assert.Equal(Value("g_status"), 200);
}

public void Test_NoNetworkWithoutAnAllowedHost() {
	Runner.Command("sw_get \"http://127.0.0.1:1/\"");
	Runner.Frames(1);
	Assert.Equal(Value("g_status"), 404, "answered with 404, not sent");
	Assert.Equal(view_as<int>(RunnerHttp.GetRequest(0).Source), view_as<int>(RunnerHttpSource_Missing));
}
