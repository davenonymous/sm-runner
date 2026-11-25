/**
 * Copyright 2026 davenonymous
 *
 * Self tests of smrunner's SteamWorks stand-in against steamworks_target.smx
 * and the mapping file in steamworks/:
 *
 *     smrunner test test_steamworks.smx --plugin steamworks_target.smx \
 *         --steamworks steamworks
 *
 * HTTP from the mapping file and from routes, ranges, timeouts on the
 * simulated clock, streaming, the request log, the server's state and
 * forwards, the clients' side and the natives that are not faked.
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <SteamWorks>
#include <smrunner>
#include <smrunner-steamworks>

Handle Target() {
	Handle plugin = FindPluginByFile("steamworks_target.smx");
	Assert.True(plugin != null, "steamworks_target.smx is loaded");
	return plugin;
}

any Value(const char[] name) {
	return Runner.GetPluginValue(Target(), name);
}

void TargetString(const char[] name, char[] buffer, int maxlength) {
	Runner.GetPluginString(Target(), name, buffer, maxlength);
}

// Sends a GET from the target and runs the frame that answers it.
void GetAndAnswer(const char[] url) {
	Runner.Command("sw_get \"%s\"", url);
	Runner.Frames(1);
}

void AssertBody(const char[] expected) {
	char body[4096];
	TargetString("g_body", body, sizeof(body));
	Assert.StrEqual(body, expected);
}

// ------------------------------------------------------------------ loading

public void Test_PluginRequiringSteamWorksLoads() {
	Assert.True(GetPluginStatus(Target()) == Plugin_Running, "steamworks_target.smx runs");
	Assert.True(LibraryExists("SteamWorks"), "the SteamWorks library is registered");
	Assert.True(SteamWorks_IsLoaded(), "SteamWorks_IsLoaded");
}

public void Test_ServersConnectedFiresOnceAtStart() {
	Assert.Equal(Value("g_connected"), 1, "SteamWorks_SteamServersConnected fired once");
	Runner.Frames(10);
	Assert.Equal(Value("g_connected"), 1, "and not again");
}

// ------------------------------------------------------------- mapping file

public void Test_MappingFileAnswersOnTheNextFrame() {
	Runner.Command("sw_get \"http://api.test/team/4242\"");
	Assert.Equal(Value("g_completed"), 0, "no answer before a frame");
	Runner.Frames(1);
	Assert.Equal(Value("g_completed"), 1, "answered on the next frame");
	Assert.False(Value("g_failure"));
	Assert.True(Value("g_successful"));
	Assert.Equal(Value("g_status"), 200);
	AssertBody("{\"team\": \"Example\", \"id\": 4242}\n");
	char type[128];
	TargetString("g_contentType", type, sizeof(type));
	Assert.StrEqual(type, "application/json", "Content-Type from the body file's extension");
	Assert.Equal(Value("g_contextHigh"), 11);
	Assert.Equal(Value("g_contextLow"), 22);
	Assert.Equal(Value("g_headersReceived"), 0, "no headers callback for a request sent with Send");
}

public void Test_QueryParametersMatchInAnyOrder() {
	GetAndAnswer("http://api.test/search?a=1&b=2");
	AssertBody("sorted");
}

public void Test_WildcardMatchesAnyCharacters() {
	GetAndAnswer("http://api.test/wild/some/thing/end");
	AssertBody("hello");
}

public void Test_FirstMatchingEntryWins() {
	GetAndAnswer("http://api.test/first");
	AssertBody("first");
}

public void Test_EntryStatusHeadersAndMethod() {
	Runner.Command("sw_post \"http://api.test/created\" name value");
	Runner.Frames(1);
	Assert.Equal(Value("g_status"), 201);
	AssertBody("made");
	char type[128];
	TargetString("g_contentType", type, sizeof(type));
	Assert.StrEqual(type, "text/x-custom", "the entry's own Content-Type");
	GetAndAnswer("http://api.test/created");
	Assert.Equal(Value("g_status"), 404, "a GET does not match a POST entry");
}

public void Test_DelayFrames() {
	Runner.Command("sw_get \"http://api.test/slow\"");
	Runner.Frames(5);
	Assert.Equal(Value("g_completed"), 0, "not within 1 + 4 frames");
	Runner.Frames(1);
	Assert.Equal(Value("g_completed"), 1, "on the sixth frame");
	AssertBody("late");
}

public void Test_FailIo() {
	GetAndAnswer("http://api.test/broken");
	Assert.Equal(Value("g_completed"), 1);
	Assert.True(Value("g_failure"), "bFailure");
	Assert.False(Value("g_successful"));
	Assert.Equal(Value("g_status"), 0);
	Assert.Equal(Value("g_bodySize"), -1, "no body");
}

public void Test_FailTimeout() {
	GetAndAnswer("http://api.test/hangs");
	Assert.False(Value("g_failure"));
	Assert.False(Value("g_successful"));
	Assert.True(Value("g_timedOut"), "GetWasTimedOut");
	Assert.Equal(Value("g_headersReceived"), 0, "no headers");
}

public void Test_MissingAnswerIs404WithAConsoleLine() {
	GetAndAnswer("http://api.test/nothing/here");
	Assert.True(Value("g_successful"));
	Assert.Equal(Value("g_status"), 404);
	Assert.Equal(Value("g_bodySize"), 0);
	Assert.ConsoleContains("[SteamWorks] no answer for GET http://api.test/nothing/here: 404");
	Assert.Equal(view_as<int>(RunnerHttp.GetRequest(0).Source), view_as<int>(RunnerHttpSource_Missing));
}

// -------------------------------------------------------------------- range

public void Test_RangeIsAnsweredWith206() {
	Runner.Command("sw_range \"http://api.test/digits\" bytes=2-5");
	Runner.Frames(1);
	Assert.Equal(Value("g_status"), 206);
	AssertBody("2345");
	Runner.Command("sw_range \"http://api.test/digits\" bytes=-3");
	Runner.Frames(1);
	AssertBody("hij");
	Runner.Command("sw_range \"http://api.test/digits\" bytes=18-");
	Runner.Frames(1);
	AssertBody("ij");
}

public void Test_RangePastTheEndIs416() {
	Runner.Command("sw_range \"http://api.test/digits\" bytes=20-30");
	Runner.Frames(1);
	Assert.Equal(Value("g_status"), 416);
	Assert.Equal(Value("g_bodySize"), 0);
}

public void Test_ContentRangeAndLengthHeaders() {
	SteamWorksHTTPRequest request = new SteamWorksHTTPRequest(k_EHTTPMethodGET, "http://api.test/digits");
	request.SetHeaderValue("Range", "bytes=0-0");
	request.Send();
	Runner.Frames(1);
	char value[64];
	Assert.True(request.GetResponseHeaderValue("content-range", value, sizeof(value)), "Content-Range, any case");
	Assert.StrEqual(value, "bytes 0-0/20");
	Assert.True(request.GetResponseHeaderValue("Content-Length", value, sizeof(value)));
	Assert.StrEqual(value, "1");
	int size;
	Assert.True(request.GetResponseHeaderSize("Content-Length", size));
	Assert.Equal(size, 2, "the size includes the terminator");
	Assert.False(request.GetResponseHeaderValue("Content-Length", value, 1), "a buffer too small fails");
	delete request;
}

public void Test_HeadRequestHasNoBody() {
	Runner.Command("sw_head \"http://api.test/digits\"");
	Runner.Frames(1);
	Assert.Equal(Value("g_status"), 200);
	Assert.Equal(Value("g_bodySize"), 0);
	char length[32];
	TargetString("g_contentLength", length, sizeof(length));
	Assert.StrEqual(length, "20", "the length of the body a GET gets");
}

public void Test_HeadRuleKeepsItsContentLength() {
	Runner.Command("sw_head \"http://api.test/sized\"");
	Runner.Frames(1);
	Assert.Equal(Value("g_status"), 200);
	Assert.Equal(Value("g_bodySize"), 0);
	char length[32];
	TargetString("g_contentLength", length, sizeof(length));
	Assert.StrEqual(length, "12345678", "the rule's Content-Length, not the empty body's");
}

// ------------------------------------------------------------------- routes

public void Test_RouteAnswersBeforeTheMappingFile() {
	RunnerHttpRoute route = RunnerHttp.Route(k_EHTTPMethodGET, "http://api.test/team/4242", k_EHTTPStatusCode200OK, "routed");
	GetAndAnswer("http://api.test/team/4242");
	AssertBody("routed");
	Assert.Equal(route.Hits, 1);
	Assert.Equal(view_as<int>(RunnerHttp.GetRequest(0).Source), view_as<int>(RunnerHttpSource_Route));
}

public void Test_NewestRouteWinsAndRemovingBringsTheOlderBack() {
	RunnerHttp.Route(k_EHTTPMethodGET, "http://api.test/r", k_EHTTPStatusCode200OK, "older");
	RunnerHttpRoute newer = RunnerHttp.Route(k_EHTTPMethodGET, "http://api.test/*", k_EHTTPStatusCode200OK, "newer");
	GetAndAnswer("http://api.test/r");
	AssertBody("newer");
	newer.Remove();
	GetAndAnswer("http://api.test/r");
	AssertBody("older");
	RunnerHttp.ClearRoutes();
	GetAndAnswer("http://api.test/r");
	Assert.Equal(Value("g_status"), 404);
}

public void Test_RouteStatusHeaderAndDelay() {
	RunnerHttpRoute route = RunnerHttp.Route(k_EHTTPMethodGET, "http://api.test/limited", k_EHTTPStatusCode429TooManyRequests, "", 2);
	route.SetHeader("Retry-After", "30");
	SteamWorksHTTPRequest request = new SteamWorksHTTPRequest(k_EHTTPMethodGET, "http://api.test/limited");
	request.Send();
	Runner.Frames(2);
	char value[16];
	Assert.False(request.GetResponseHeaderValue("Retry-After", value, sizeof(value)), "not before the third frame");
	Runner.Frames(1);
	Assert.True(request.GetResponseHeaderValue("Retry-After", value, sizeof(value)));
	Assert.StrEqual(value, "30");
	delete request;
}

public void Test_RouteFile() {
	File file = OpenFile("route-body.json", "wb");
	file.WriteString("{\"from\": \"file\"}", false);
	delete file;
	RunnerHttp.RouteFile(k_EHTTPMethodGET, "http://api.test/file", "route-body.json");
	GetAndAnswer("http://api.test/file");
	AssertBody("{\"from\": \"file\"}");
	char type[128];
	TargetString("g_contentType", type, sizeof(type));
	Assert.StrEqual(type, "application/json");
}

public void Test_RouteFailure() {
	RunnerHttp.RouteFailure(k_EHTTPMethodGET, "http://api.test/team/4242", RunnerHttpFailure_Io);
	GetAndAnswer("http://api.test/team/4242");
	Assert.True(Value("g_failure"));
	RunnerHttp.RouteFailure(k_EHTTPMethodGET, "http://api.test/team/4242", RunnerHttpFailure_Timeout, 3);
	Runner.Command("sw_get \"http://api.test/team/4242\"");
	Runner.Frames(3);
	Assert.Equal(Value("g_completed"), 0, "the failure waits its delay");
	Runner.Frames(1);
	Assert.True(Value("g_timedOut"));
}

// ---------------------------------------------------------------- timeouts

public void Test_AbsoluteTimeoutFollowsTheSimulatedClock() {
	RunnerHttp.Route(k_EHTTPMethodGET, "http://api.test/late", k_EHTTPStatusCode200OK, "late", 200);
	Runner.Command("sw_get \"http://api.test/late\" 1000");
	Runner.Seconds(0.9);
	Assert.Equal(Value("g_completed"), 0, "not before a second");
	Runner.Seconds(0.2);
	Assert.Equal(Value("g_completed"), 1, "timed out after a second");
	Assert.True(Value("g_timedOut"));
	Assert.False(Value("g_successful"));
}

public void Test_NetworkActivityTimeout() {
	RunnerHttp.Route(k_EHTTPMethodGET, "http://api.test/late", k_EHTTPStatusCode200OK, "late", 1000);
	Runner.Command("sw_get \"http://api.test/late\" 0 2");
	Runner.Seconds(1.9);
	Assert.Equal(Value("g_completed"), 0);
	Runner.Seconds(0.2);
	Assert.True(Value("g_timedOut"), "no data for two seconds");
}

public void Test_DelayWithinTheTimeoutIsAnswered() {
	RunnerHttp.Route(k_EHTTPMethodGET, "http://api.test/late", k_EHTTPStatusCode200OK, "late", 10);
	Runner.Command("sw_get \"http://api.test/late\" 1000 1");
	Runner.Frames(11);
	Assert.True(Value("g_successful"));
	AssertBody("late");
}

// ---------------------------------------------------------------- streaming

char g_callbackBody[64];
int g_callbackValue;
bool g_bodyCallbackTaken;
bool g_written;
int g_ownCompleted;

public void OnBodyString(const char[] data, any value) {
	strcopy(g_callbackBody, sizeof(g_callbackBody), data);
	g_callbackValue = value;
}

public void Test_StreamingInChunksWithProgress() {
	File file = OpenFile("big.bin", "wb");
	char line[1025];
	for (int i = 0; i < 1024; i++) {
		line[i] = 'a' + i % 26;
	}
	line[1024] = '\0';
	for (int i = 0; i < 150; i++) {
		file.WriteString(line, false);
	}
	delete file;
	RunnerHttp.RouteFile(k_EHTTPMethodGET, "http://api.test/big", "big.bin");
	Runner.Command("sw_stream \"http://api.test/big\"");
	Runner.Frames(1);
	Assert.Equal(Value("g_headersReceived"), 1, "the headers callback of a streamed request");
	Assert.Equal(Value("g_dataCallbacks"), 1, "the first 64 KiB on the answer's frame");
	Assert.Equal(Value("g_completed"), 0);
	char first[16];
	TargetString("g_firstStreamedBytes", first, sizeof(first));
	Assert.StrEqual(first, "abcdefghijklmno");
	Assert.False(Value("g_partialChunkRead"), "a chunk is read only by its offset and length");
	Assert.FloatEqual(Value("g_progress"), 100.0 * 65536.0 / 153600.0, 0.01);
	Runner.Frames(2);
	Assert.Equal(Value("g_dataCallbacks"), 3);
	Assert.Equal(Value("g_streamedBytes"), 153600);
	Assert.Equal(Value("g_chunksRead"), 3, "every chunk read in its data callback");
	Assert.Equal(Value("g_completed"), 1, "completed with the last chunk");
	Assert.FloatEqual(Value("g_progress"), 100.0, 0.01);
	Assert.Equal(Value("g_bodySize"), 153600);
}

public void Test_StreamedBodyIsNotReadWhole() {
	Runner.Command("sw_stream \"http://api.test/team/4242\"");
	Runner.Frames(1);
	Assert.Equal(Value("g_headersReceived"), 1);
	char type[128];
	TargetString("g_headersContentType", type, sizeof(type));
	Assert.StrEqual(type, "application/json", "the headers are there in the headers callback");
	Assert.Equal(Value("g_chunksRead"), 1);
	Assert.Equal(Value("g_completed"), 1);
	Assert.Equal(Value("g_bodySize"), 32);
	Assert.False(Value("g_bodyRead"), "GetResponseBodyData refuses a streamed response");
}

public void OnStreamedCompleted(Handle hRequest, bool failure, bool successful, EHTTPStatusCode status) {
	g_ownCompleted++;
	SteamWorksHTTPRequest request = view_as<SteamWorksHTTPRequest>(hRequest);
	g_bodyCallbackTaken = request.GetResponseBodyCallback(OnBodyString, 7);
	g_written = request.WriteResponseBodyToFile("streamed.txt");
	delete request;
}

public void Test_StreamedBodyHasNoBodyCallbackOrFile() {
	SteamWorksHTTPRequest request = new SteamWorksHTTPRequest(k_EHTTPMethodGET, "http://api.test/wild/x/end");
	request.SetCallbacks(OnStreamedCompleted);
	request.SendAndStreamResponse();
	Runner.Frames(1);
	Assert.Equal(g_ownCompleted, 1);
	Assert.False(g_bodyCallbackTaken, "GetResponseBodyCallback refuses a streamed response");
	Assert.False(g_written, "WriteResponseBodyToFile too");
	Assert.False(FileExists("streamed.txt"));
}

// ------------------------------------------------- the request's other natives

public void OnOwnCompleted(Handle hRequest, bool failure, bool successful, EHTTPStatusCode status) {
	g_ownCompleted++;
	SteamWorksHTTPRequest request = view_as<SteamWorksHTTPRequest>(hRequest);
	Assert.True(request.GetResponseBodyCallback(OnBodyString, 7), "GetResponseBodyCallback");
	g_written = request.WriteResponseBodyToFile("written.txt");
	delete request;
}

public void Test_BodyCallbackAndWriteToFile() {
	SteamWorksHTTPRequest request = new SteamWorksHTTPRequest(k_EHTTPMethodGET, "http://api.test/wild/x/end");
	request.SetCallbacks(OnOwnCompleted);
	request.Send();
	Runner.Frames(1);
	Assert.Equal(g_ownCompleted, 1);
	Assert.StrEqual(g_callbackBody, "hello");
	Assert.Equal(g_callbackValue, 7);
	Assert.True(g_written, "WriteResponseBodyToFile");
	File file = OpenFile("written.txt", "r");
	char content[16];
	file.ReadString(content, sizeof(content));
	delete file;
	Assert.StrEqual(content, "hello");
}

public void Test_NoResponseBeforeTheAnswer() {
	SteamWorksHTTPRequest request = new SteamWorksHTTPRequest(k_EHTTPMethodGET, "http://api.test/team/4242");
	float percent;
	Assert.False(request.GetDownloadProgressPct(percent), "no progress before Send");
	request.Send();
	int size;
	Assert.False(request.GetResponseBodySize(size), "no body before the answer");
	Assert.True(request.GetDownloadProgressPct(percent));
	Assert.FloatEqual(percent, 0.0, 0.001);
	Assert.False(request.SetHeaderValue("X-Late", "1"), "no setter after Send");
	Assert.False(request.Send(), "no second Send");
	Assert.True(request.Defer(), "Defer of a waiting request");
	Runner.Frames(1);
	Assert.True(request.GetResponseBodySize(size));
	Assert.Equal(size, 32);
	char tooSmall[8];
	Assert.False(request.GetResponseBodyData(tooSmall, sizeof(tooSmall)), "a buffer smaller than the body fails");
	char larger[64];
	Assert.False(request.GetResponseBodyData(larger, sizeof(larger)), "a buffer larger than the body fails too");
	Assert.True(request.GetResponseBodyData(larger, size), "the body's own size is read");
	delete request;
}

public void Test_DeletedRequestIsNotAnswered() {
	SteamWorksHTTPRequest request = new SteamWorksHTTPRequest(k_EHTTPMethodGET, "http://api.test/team/4242");
	request.SetCallbacks(OnOwnCompleted);
	request.Send();
	Assert.Equal(RunnerHttp.PendingRequests(), 1);
	delete request;
	Runner.Frames(2);
	Assert.Equal(g_ownCompleted, 0, "no callback for a deleted request");
	Assert.Equal(RunnerHttp.PendingRequests(), 0);
}

char g_bigBody[20001];

public void Test_BuffersOutsideThePluginsMemoryAreRefused() {
	for (int i = 0; i < sizeof(g_bigBody) - 1; i++) {
		g_bigBody[i] = 'x';
	}
	RunnerHttp.Route(k_EHTTPMethodGET, "http://api.test/big", k_EHTTPStatusCode200OK, g_bigBody);
	Runner.ExpectError("a buffer of 20000 bytes at");
	Runner.Command("sw_overread \"http://api.test/big\"");
	Runner.Frames(1);
}

public void Test_InvalidRequestsAreNull() {
	Assert.True(new SteamWorksHTTPRequest(k_EHTTPMethodGET, "ftp://api.test/") == null, "only http and https");
	Assert.True(new SteamWorksHTTPRequest(k_EHTTPMethodInvalid, "http://api.test/") == null, "no invalid method");
	Assert.True(SteamWorks_CreateHTTPRequest(k_EHTTPMethodGET, "not a url") == null);
}

public void Test_RawPostBodyFromFile() {
	File file = OpenFile("upload.txt", "wb");
	file.WriteString("file content", false);
	delete file;
	RunnerHttp.Route(k_EHTTPMethodPOST, "http://api.test/upload", k_EHTTPStatusCode200OK, "ok");
	SteamWorksHTTPRequest request = new SteamWorksHTTPRequest(k_EHTTPMethodPOST, "http://api.test/upload");
	Assert.True(request.SetRawPostBodyFromFile("text/plain", "upload.txt"));
	Assert.False(request.SetGetOrPostParameter("a", "b"), "no POST parameter after a raw body");
	request.Send();
	char body[64], type[64];
	RunnerHttpRequest sent = RunnerHttp.GetRequest(0);
	Assert.Equal(sent.GetBody(body, sizeof(body)), 12);
	Assert.StrEqual(body, "file content");
	Assert.True(sent.GetHeader("content-type", type, sizeof(type)));
	Assert.StrEqual(type, "text/plain");
	delete request;
	SteamWorksHTTPRequest get = new SteamWorksHTTPRequest(k_EHTTPMethodGET, "http://api.test/upload");
	Assert.False(get.SetRawPostBody("text/plain", "x", 1), "no raw body on a GET");
	delete get;
}

// -------------------------------------------------------------- request log

public void Test_RequestLog() {
	Runner.Command("sw_post \"http://api.test/created\" name \"a value\"");
	SteamWorksHTTPRequest request = new SteamWorksHTTPRequest(k_EHTTPMethodGET, "http://api.test/search");
	request.SetGetOrPostParameter("b", "2");
	request.SetGetOrPostParameter("a", "1");
	request.SetHeaderValue("X-Api-Key", "secret");
	request.SetUserAgentInfo("tests");
	request.Send();
	Assert.Equal(RunnerHttp.RequestCount(), 2);
	Assert.Equal(RunnerHttp.PendingRequests(), 2);

	RunnerHttpRequest post = RunnerHttp.GetRequest(0);
	Assert.Equal(view_as<int>(post.Method), view_as<int>(k_EHTTPMethodPOST));
	char text[256];
	Assert.True(post.GetParameter("name", text, sizeof(text)));
	Assert.StrEqual(text, "a value");
	post.GetBody(text, sizeof(text));
	Assert.StrEqual(text, "name=a%20value");
	Assert.Equal(post.BodyLength, 14);
	post.GetHeader("Content-Type", text, sizeof(text));
	Assert.StrEqual(text, "application/x-www-form-urlencoded");
	Assert.False(post.Answered);

	RunnerHttpRequest get = RunnerHttp.GetRequest(1);
	get.GetUrl(text, sizeof(text));
	Assert.StrEqual(text, "http://api.test/search?b=2&a=1", "GET parameters in the query");
	Assert.True(get.GetHeader("x-api-key", text, sizeof(text)));
	Assert.StrEqual(text, "secret");
	get.GetHeader("User-Agent", text, sizeof(text));
	Assert.StrEqual(text, "Valve/Steam HTTP Client 1.0 (440) tests");

	Runner.Frames(1);
	Assert.True(get.Answered);
	Assert.Equal(view_as<int>(get.Status), 200);
	Assert.Equal(view_as<int>(get.Source), view_as<int>(RunnerHttpSource_MappingFile));
	Assert.Equal(RunnerHttp.CountRequests("http://api.test/*"), 2);
	Assert.Equal(RunnerHttp.CountRequests("http://api.test/*", k_EHTTPMethodPOST), 1);
	Assert.Equal(RunnerHttp.CountRequests("http://api.test/search?a=1&b=2"), 1, "the query in any order");
	Assert.Equal(RunnerHttp.FindRequest("http://api.test/search*"), 1);
	Assert.Equal(RunnerHttp.FindRequest("http://api.test/nothing"), -1);
	RunnerHttp.ClearRequests();
	Assert.Equal(RunnerHttp.RequestCount(), 0);
	delete request;
}

// ---------------------------------------------------------------- the server

public void Test_ServerDefaults() {
	Assert.True(SteamWorks_IsConnected());
	Assert.True(SteamWorks_IsVACEnabled());
	int ip[4];
	Assert.True(SteamWorks_GetPublicIP(ip));
	Assert.Equal(ip[0], 127);
	Assert.Equal(ip[1], 0);
	Assert.Equal(ip[2], 0);
	Assert.Equal(ip[3], 1);
	Assert.Equal(SteamWorks_GetPublicIPCell(), 0x7F000001);
	int fakeIp, fakePort;
	Assert.False(SteamWorks_GetFakeIP(fakeIp, fakePort), "no fake IP");
	Assert.True(RunnerSteam.IsAdvertising());
	Assert.False(SteamWorks_ForceHeartbeat(), "deprecated, answers false");
}

public void Test_ServerStateFromTheTest() {
	RunnerSteam.SetVACEnabled(false);
	Assert.False(SteamWorks_IsVACEnabled());
	RunnerSteam.SetPublicIP("203.0.113.7");
	Assert.Equal(SteamWorks_GetPublicIPCell(), 0xCB007107);
	RunnerSteam.SetPublicIP("");
	int ip[4];
	Assert.False(SteamWorks_GetPublicIP(ip));
	Assert.Equal(SteamWorks_GetPublicIPCell(), 0);
	RunnerSteam.SetFakeIP("169.254.1.2", 27015);
	int fakeIp, fakePort;
	Assert.True(SteamWorks_GetFakeIP(fakeIp, fakePort));
	Assert.Equal(fakeIp, 0xA9FE0102);
	Assert.Equal(fakePort, 27015);
	RunnerSteam.SetConnected(false);
	Assert.False(SteamWorks_IsConnected());
	Assert.Equal(Value("g_disconnected"), 0, "SetConnected fires nothing");
}

public void Test_ServerForwards() {
	RunnerSteam.FireServersDisconnected(k_EResultNoConnection);
	Assert.Equal(Value("g_disconnected"), 1);
	Assert.Equal(Value("g_lastResult"), view_as<int>(k_EResultNoConnection));
	Assert.False(SteamWorks_IsConnected());
	RunnerSteam.FireServersConnectFailure(k_EResultTimeout);
	Assert.Equal(Value("g_connectFailures"), 1);
	Assert.Equal(Value("g_lastResult"), view_as<int>(k_EResultTimeout));
	RunnerSteam.FireServersConnected();
	Assert.Equal(Value("g_connected"), 2);
	Assert.True(SteamWorks_IsConnected());
}

public void Test_WhatPluginsSetIsKept() {
	SteamWorks_SetRule("league", "etf2l");
	SteamWorks_SetRule("mode", "6v6");
	SteamWorks_SetGameDescription("Team Fortress");
	SteamWorks_SetMapName("cp_process_final");
	SteamWorks_SetGameData("tags");
	SteamWorks_SetAdvertiseServerActive(false);
	char text[64];
	Assert.True(RunnerSteam.GetRule("league", text, sizeof(text)));
	Assert.StrEqual(text, "etf2l");
	Assert.Equal(RunnerSteam.RuleCount(), 2);
	RunnerSteam.GetGameDescription(text, sizeof(text));
	Assert.StrEqual(text, "Team Fortress");
	RunnerSteam.GetMapName(text, sizeof(text));
	Assert.StrEqual(text, "cp_process_final");
	RunnerSteam.GetGameData(text, sizeof(text));
	Assert.StrEqual(text, "tags");
	Assert.False(RunnerSteam.IsAdvertising());
	SteamWorks_ClearRules();
	Assert.Equal(RunnerSteam.RuleCount(), 0);
	Assert.False(RunnerSteam.GetRule("league", text, sizeof(text)));
}

public void Test_TokenAndRestartForwards() {
	Runner.SetPluginString(Target(), "g_token", "secret-token");
	char token[64];
	RunnerSteam.FireTokenRequested(token, sizeof(token));
	Assert.StrEqual(token, "secret-token");
	Assert.False(RunnerSteam.FireRestartRequested(), "Plugin_Continue does not handle it");
	Runner.SetPluginValue(Target(), "g_restartAnswer", Plugin_Handled);
	Assert.True(RunnerSteam.FireRestartRequested(), "Plugin_Handled does");
}

// --------------------------------------------------------------- the clients

public void Test_ClientSteamIDAndLicenses() {
	int alice = Runner.ConnectClient("alice", "STEAM_0:1:23");
	char steamId[32];
	Assert.Equal(SteamWorks_GetClientSteamID(alice, steamId, sizeof(steamId)), 18);
	Assert.StrEqual(steamId, "76561197960265775");
	Assert.Equal(view_as<int>(SteamWorks_HasLicenseForApp(alice, 440)), view_as<int>(k_EUserHasLicenseResultHasLicense));
	Assert.Equal(view_as<int>(SteamWorks_HasLicenseForApp(alice, 730)),
		view_as<int>(k_EUserHasLicenseResultDoesNotHaveLicense));
	Assert.Equal(view_as<int>(SteamWorks_HasLicenseForAppId(47, 440)), view_as<int>(k_EUserHasLicenseResultHasLicense));
	Assert.Equal(view_as<int>(SteamWorks_HasLicenseForAppId(48, 440)), view_as<int>(k_EUserHasLicenseResultNoAuth),
		"an account not on the server");
	RunnerSteam.SetLicense(47, 730, k_EUserHasLicenseResultHasLicense);
	Assert.Equal(view_as<int>(SteamWorks_HasLicenseForApp(alice, 730)), view_as<int>(k_EUserHasLicenseResultHasLicense));
	int bot = Runner.ConnectBot("bot");
	Assert.Equal(view_as<int>(SteamWorks_HasLicenseForApp(bot, 440)), view_as<int>(k_EUserHasLicenseResultNoAuth));
}

public void Test_GroupStatusOnTheNextFrame() {
	int alice = Runner.ConnectClient("alice", "STEAM_0:1:23");
	Assert.True(SteamWorks_GetUserGroupStatus(alice, 4321));
	Assert.Equal(Value("g_groupStatus"), 0, "not before a frame");
	Runner.Frames(1);
	Assert.Equal(Value("g_groupStatus"), 1);
	Assert.Equal(Value("g_groupAccount"), 47);
	Assert.False(Value("g_groupMember"));
	RunnerSteam.SetGroupStatus(47, 1234, true, true);
	Assert.True(SteamWorks_GetUserGroupStatusAuthID(47, 1234));
	Runner.Frames(1);
	Assert.Equal(Value("g_groupId"), 1234);
	Assert.True(Value("g_groupMember"));
	Assert.True(Value("g_groupOfficer"));
	int bot = Runner.ConnectBot("bot");
	Assert.False(SteamWorks_GetUserGroupStatus(bot, 1234), "no group status of a bot");
	RunnerSteam.FireClientGroupStatus(99, 5, true, false);
	Assert.Equal(Value("g_groupAccount"), 99, "fired at once");
}

public void Test_ValidateClientOnlyOnTheTestsCall() {
	Runner.ConnectClient("alice", "STEAM_0:1:23");
	Runner.Frames(5);
	Assert.Equal(Value("g_validated"), 0, "not at authorization");
	RunnerSteam.FireValidateClient(100, 47);
	Assert.Equal(Value("g_validated"), 2, "both forwards");
	Assert.Equal(Value("g_validatedOwner"), 100);
	Assert.Equal(Value("g_validatedAccount"), 47);
}

public void Test_ClientSteamIDChecksTheBuffer() {
	int alice = Runner.ConnectClient("alice", "STEAM_0:1:23");
	char small[8];
	Runner.ExpectError("a buffer of 100000 bytes at");
	SteamWorks_GetClientSteamID(alice, small, 100000);
}

public void Test_ClientNativesCheckTheIndex() {
	Runner.ExpectError("Client index 5 is not connected");
	char steamId[32];
	SteamWorks_GetClientSteamID(5, steamId, sizeof(steamId));
}

// ------------------------------------------------------------- not faked

public void Test_StatsAreNotFaked() {
	Runner.ExpectError("SteamWorks_GetStatCell is not faked by smrunner's SteamWorks stand-in");
	Runner.Command("sw_stats");
}
