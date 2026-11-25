/**
 * Copyright 2026 davenonymous
 *
 * Self tests of the SteamWorks stand-in's passthrough and record mode
 * against a local HTTP server (tests/run.sh starts it):
 *
 *     smrunner test test_steamworks_passthrough.smx --plugin steamworks_target.smx \
 *         --steamworks --steamworks-passthrough 127.0.0.1 --steamworks-record DIR \
 *         --cvar smrunner_http_base=http://127.0.0.1:PORT
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

any Value(const char[] name) {
	return Runner.GetPluginValue(FindPluginByFile("steamworks_target.smx"), name);
}

public bool Completed() {
	return Value("g_completed") > 0;
}

// The local server's base URL from the command line.
void Base(char[] buffer, int maxlength) {
	g_base.GetString(buffer, maxlength);
	Assert.True(buffer[0] != '\0', "--cvar smrunner_http_base is given");
}

public void Test_PassthroughGetsTheRealAnswer() {
	char base[128];
	Base(base, sizeof(base));
	Runner.Command("sw_get \"%s/hello.json\"", base);
	Assert.True(Runner.WaitUntil(Completed, 10.0), "the real answer came");
	Assert.True(Value("g_successful"));
	Assert.Equal(Value("g_status"), 200);
	char body[64];
	Runner.GetPluginString(FindPluginByFile("steamworks_target.smx"), "g_body", body, sizeof(body));
	Assert.StrEqual(body, "{\"real\": true}");
	Assert.Equal(view_as<int>(RunnerHttp.GetRequest(0).Source), view_as<int>(RunnerHttpSource_Passthrough));
}

public void Test_HostAllowedByTheTest() {
	char base[128];
	Base(base, sizeof(base));
	ReplaceString(base, sizeof(base), "127.0.0.1", "localhost");
	Runner.Command("sw_get \"%s/other.txt\"", base);
	Runner.Frames(1);
	Assert.Equal(Value("g_status"), 404, "localhost is not allowed yet");
	RunnerHttp.AllowPassthrough("LOCAL*");
	Runner.SetPluginValue(FindPluginByFile("steamworks_target.smx"), "g_completed", 0);
	Runner.Command("sw_get \"%s/other.txt\"", base);
	Assert.True(Runner.WaitUntil(Completed, 10.0));
	Assert.Equal(Value("g_status"), 200);
}

public void Test_RealConnectionFailure() {
	Runner.Command("sw_get \"http://127.0.0.1:1/\"");
	Assert.True(Runner.WaitUntil(Completed, 10.0));
	Assert.False(Value("g_failure"));
	Assert.False(Value("g_successful"));
	Assert.Equal(Value("g_status"), 0);
	Assert.ConsoleContains("GET http://127.0.0.1:1/ failed:");
}

public void Test_RoutesAnswerBeforeTheNetwork() {
	char base[128], url[160];
	Base(base, sizeof(base));
	Format(url, sizeof(url), "%s/hello.json", base);
	RunnerHttp.Route(k_EHTTPMethodGET, url, k_EHTTPStatusCode200OK, "faked");
	Runner.Command("sw_get \"%s\"", url);
	Runner.Frames(1);
	Assert.True(Completed(), "the route answered on the next frame");
	char body[64];
	Runner.GetPluginString(FindPluginByFile("steamworks_target.smx"), "g_body", body, sizeof(body));
	Assert.StrEqual(body, "faked");
	Assert.Equal(view_as<int>(RunnerHttp.GetRequest(0).Source), view_as<int>(RunnerHttpSource_Route));
}

public void Test_RedirectsOnAnAllowedHostAreFollowed() {
	char base[128];
	Base(base, sizeof(base));
	Runner.Command("sw_get \"%s/moved\"", base);
	Assert.True(Runner.WaitUntil(Completed, 10.0));
	Assert.Equal(Value("g_status"), 200);
	char body[64];
	Runner.GetPluginString(FindPluginByFile("steamworks_target.smx"), "g_body", body, sizeof(body));
	Assert.StrEqual(body, "{\"real\": true}");
}

public void Test_RedirectToAHostNotAllowedFails() {
	char base[128];
	Base(base, sizeof(base));
	Runner.Command("sw_get \"%s/elsewhere\"", base);
	Assert.True(Runner.WaitUntil(Completed, 10.0));
	Assert.False(Value("g_successful"));
	Assert.Equal(Value("g_status"), 0);
	Assert.ConsoleContains("/elsewhere failed: redirected to http://localhost:");
	Assert.ConsoleContains("whose host localhost is no allowed passthrough host");
}

public void Test_HeadKeepsTheServersContentLength() {
	char base[128];
	Base(base, sizeof(base));
	Runner.Command("sw_head \"%s/hello.json\"", base);
	Assert.True(Runner.WaitUntil(Completed, 10.0));
	Assert.Equal(Value("g_status"), 200);
	Assert.Equal(Value("g_bodySize"), 0, "no body");
	char length[32];
	Runner.GetPluginString(FindPluginByFile("steamworks_target.smx"), "g_contentLength", length, sizeof(length));
	Assert.StrEqual(length, "14", "the length of the file the server did not send");
}

public void Test_GetAndPostToOnePath() {
	char base[128], body[64];
	Base(base, sizeof(base));
	Handle target = FindPluginByFile("steamworks_target.smx");
	Runner.Command("sw_get \"%s/items.json\"", base);
	Assert.True(Runner.WaitUntil(Completed, 10.0));
	Assert.Equal(Value("g_status"), 200);
	Runner.GetPluginString(target, "g_body", body, sizeof(body));
	Assert.StrEqual(body, "[1, 2]");
	Runner.Command("sw_post \"%s/items.json\" name value", base);
	Assert.True(Runner.WaitUntil(Completed, 10.0));
	Assert.Equal(Value("g_status"), 201);
	Runner.GetPluginString(target, "g_body", body, sizeof(body));
	Assert.StrEqual(body, "{\"created\": true}");
}

char g_deleteAnswer[128];

public void OnDeleteCompleted(Handle hRequest, bool failure, bool successful, EHTTPStatusCode status) {
	SteamWorksHTTPRequest request = view_as<SteamWorksHTTPRequest>(hRequest);
	int size;
	if (request.GetResponseBodySize(size) && size < sizeof(g_deleteAnswer)) {
		request.GetResponseBodyData(g_deleteAnswer, size);
		g_deleteAnswer[size] = '\0';
	}
	delete request;
}

public bool DeleteAnswered() {
	return g_deleteAnswer[0] != '\0';
}

public void Test_DeleteWithoutABodySendsNoContentType() {
	char base[128], url[160];
	Base(base, sizeof(base));
	Format(url, sizeof(url), "%s/thing", base);
	SteamWorksHTTPRequest request = new SteamWorksHTTPRequest(k_EHTTPMethodDELETE, url);
	request.SetCallbacks(OnDeleteCompleted);
	request.Send();
	Assert.True(Runner.WaitUntil(DeleteAnswered, 10.0));
	Assert.StrEqual(g_deleteAnswer, "content-type: none; content-length: none", "no form Content-Type of libcurl's");
}

// Their body files would have the same names; record mode keeps them apart.
public void Test_AnswersWhoseBodyFileNamesWouldClash() {
	char base[128], body[64];
	Base(base, sizeof(base));
	Handle target = FindPluginByFile("steamworks_target.smx");
	char paths[3][32] = {"data", "data.bin", "items.post.json"};
	char bodies[3][32] = {"plain data", "binary data", "{\"get\": true}"};
	for (int i = 0; i < sizeof(paths); i++) {
		Runner.Command("sw_get \"%s/%s\"", base, paths[i]);
		Assert.True(Runner.WaitUntil(Completed, 10.0));
		Runner.GetPluginString(target, "g_body", body, sizeof(body));
		Assert.StrEqual(body, bodies[i]);
	}
}
