/**
 * Copyright 2026 davenonymous
 *
 * Self test of the SteamWorks stand-in's record mode: what
 * test_steamworks_passthrough recorded answers again from the mapping file,
 * with the local server gone (tests/run.sh):
 *
 *     smrunner test test_steamworks_replay.smx --plugin steamworks_target.smx \
 *         --steamworks DIR --cvar smrunner_http_base=http://127.0.0.1:PORT
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <SteamWorks>
#include <smrunner>
#include <smrunner-steamworks>

ConVar g_base;

public void OnPluginStart() {
	g_base = CreateConVar("smrunner_http_base", "", "The local HTTP server's base URL (--cvar)");
}

public void Test_RecordedAnswerReplays() {
	char base[128];
	g_base.GetString(base, sizeof(base));
	Runner.Command("sw_get \"%s/hello.json\"", base);
	Runner.Frames(1);
	Handle target = FindPluginByFile("steamworks_target.smx");
	Assert.Equal(Runner.GetPluginValue(target, "g_status"), 200);
	char body[64], type[64];
	Runner.GetPluginString(target, "g_body", body, sizeof(body));
	Assert.StrEqual(body, "{\"real\": true}");
	Runner.GetPluginString(target, "g_contentType", type, sizeof(type));
	Assert.StrEqual(type, "application/json");
	Assert.Equal(view_as<int>(RunnerHttp.GetRequest(0).Source), view_as<int>(RunnerHttpSource_MappingFile));
}

public void Test_RecordedGetAndPostToOnePathReplayApart() {
	char base[128], body[64];
	g_base.GetString(base, sizeof(base));
	Handle target = FindPluginByFile("steamworks_target.smx");
	Runner.Command("sw_get \"%s/items.json\"", base);
	Runner.Frames(1);
	Assert.Equal(Runner.GetPluginValue(target, "g_status"), 200);
	Runner.GetPluginString(target, "g_body", body, sizeof(body));
	Assert.StrEqual(body, "[1, 2]", "the GET's own body file");
	Runner.Command("sw_post \"%s/items.json\" name value", base);
	Runner.Frames(1);
	Assert.Equal(Runner.GetPluginValue(target, "g_status"), 201);
	Runner.GetPluginString(target, "g_body", body, sizeof(body));
	Assert.StrEqual(body, "{\"created\": true}", "the POST's own body file");
}

public void Test_RecordedHeadKeepsItsContentLength() {
	char base[128], length[32];
	g_base.GetString(base, sizeof(base));
	Runner.Command("sw_head \"%s/hello.json\"", base);
	Runner.Frames(1);
	Handle target = FindPluginByFile("steamworks_target.smx");
	Assert.Equal(Runner.GetPluginValue(target, "g_status"), 200);
	Runner.GetPluginString(target, "g_contentLength", length, sizeof(length));
	Assert.StrEqual(length, "14");
}

public void Test_AnswersWhoseBodyFileNamesWouldClashReplayApart() {
	char base[128], body[64];
	g_base.GetString(base, sizeof(base));
	Handle target = FindPluginByFile("steamworks_target.smx");
	char paths[3][32] = {"data", "data.bin", "items.post.json"};
	char bodies[3][32] = {"plain data", "binary data", "{\"get\": true}"};
	for (int i = 0; i < sizeof(paths); i++) {
		Runner.Command("sw_get \"%s/%s\"", base, paths[i]);
		Runner.Frames(1);
		Runner.GetPluginString(target, "g_body", body, sizeof(body));
		Assert.StrEqual(body, bodies[i], "each answer from its own body file");
	}
}
