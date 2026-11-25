/**
 * Copyright 2026 davenonymous
 *
 * The plugin under test of the SteamWorks stand-in's self tests: it requires
 * SteamWorks, as plugins on a server do, sends the requests the tests ask
 * for with sw_get, sw_head, sw_post and sw_stream and keeps how each ended
 * in its globals (read in the completed callback, as on a server, where the
 * headers callback runs for streamed requests only), and counts the
 * forwards it gets.
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <SteamWorks>

public Plugin myinfo = {
	name = "steamworks_target",
	author = "davenonymous",
	description = "smrunner's SteamWorks self test target",
	version = "1.0.0",
	url = ""
};

// How the last request ended.
int g_completed;
bool g_failure;
bool g_successful;
int g_status;
bool g_timedOut;
char g_body[4096];
int g_bodySize;
bool g_bodyRead;
char g_contentType[128];
char g_contentLength[32];
int g_contextHigh;
int g_contextLow;
// Streaming: the headers callback, data callbacks, bytes and the last
// progress; each chunk is read by its own offset and length, and the first
// one is also asked for in part, which SteamWorks refuses.
int g_headersReceived;
char g_headersContentType[128];
int g_dataCallbacks;
int g_streamedBytes;
int g_chunksRead;
bool g_partialChunkRead;
float g_progress;
char g_firstStreamedBytes[16];
char g_chunk[64 * 1024];

// Forwards.
int g_connected;
int g_connectFailures;
int g_disconnected;
int g_lastResult;
int g_validated;
int g_validatedOwner;
int g_validatedAccount;
int g_groupStatus;
int g_groupAccount;
int g_groupId;
bool g_groupMember;
bool g_groupOfficer;
char g_token[64] = "";
Action g_restartAnswer = Plugin_Continue;

public void OnPluginStart() {
	RegServerCmd("sw_get", Command_Get, "sw_get <url> [absolute timeout ms] [activity timeout s]");
	RegServerCmd("sw_head", Command_Head, "sw_head <url>");
	RegServerCmd("sw_post", Command_Post, "sw_post <url> <name> <value>");
	RegServerCmd("sw_stream", Command_Stream, "sw_stream <url>");
	RegServerCmd("sw_range", Command_Range, "sw_range <url> <range>");
	RegServerCmd("sw_stats", Command_Stats, "reads a user stat, which the stand-in does not fake");
	RegServerCmd("sw_overread", Command_Overread, "sw_overread <url>: copies the body into too small a buffer");
}

public Action Command_Stats(int args) {
	int value;
	SteamWorks_GetStatCell(0, "kills", value);
	return Plugin_Handled;
}

static void Reset() {
	g_completed = 0;
	g_failure = false;
	g_successful = false;
	g_status = 0;
	g_timedOut = false;
	g_body[0] = '\0';
	g_bodySize = -1;
	g_bodyRead = false;
	g_contentType[0] = '\0';
	g_contentLength[0] = '\0';
	g_headersReceived = 0;
	g_headersContentType[0] = '\0';
	g_dataCallbacks = 0;
	g_streamedBytes = 0;
	g_chunksRead = 0;
	g_partialChunkRead = false;
	g_progress = -1.0;
}

static SteamWorksHTTPRequest NewRequest(EHTTPMethod method, int urlArgument = 1) {
	char url[512];
	GetCmdArg(urlArgument, url, sizeof(url));
	Reset();
	SteamWorksHTTPRequest request = new SteamWorksHTTPRequest(method, url);
	request.SetContextValue(11, 22);
	request.SetCallbacks(OnCompleted, OnHeaders, OnData);
	return request;
}

public Action Command_Get(int args) {
	SteamWorksHTTPRequest request = NewRequest(k_EHTTPMethodGET);
	if (args >= 2) {
		request.SetAbsoluteTimeoutMS(GetCmdArgInt(2));
	}
	if (args >= 3) {
		request.SetNetworkActivityTimeout(GetCmdArgInt(3));
	}
	request.Send();
	return Plugin_Handled;
}

public Action Command_Head(int args) {
	NewRequest(k_EHTTPMethodHEAD).Send();
	return Plugin_Handled;
}

public Action Command_Post(int args) {
	SteamWorksHTTPRequest request = NewRequest(k_EHTTPMethodPOST);
	char name[64], value[256];
	GetCmdArg(2, name, sizeof(name));
	GetCmdArg(3, value, sizeof(value));
	request.SetGetOrPostParameter(name, value);
	request.Send();
	return Plugin_Handled;
}

public Action Command_Stream(int args) {
	NewRequest(k_EHTTPMethodGET).SendAndStreamResponse();
	return Plugin_Handled;
}

public Action Command_Range(int args) {
	SteamWorksHTTPRequest request = NewRequest(k_EHTTPMethodGET);
	char range[64];
	GetCmdArg(2, range, sizeof(range));
	request.SetHeaderValue("Range", range);
	request.Send();
	return Plugin_Handled;
}

public Action Command_Overread(int args) {
	char url[512];
	GetCmdArg(1, url, sizeof(url));
	SteamWorksHTTPRequest request = new SteamWorksHTTPRequest(k_EHTTPMethodGET, url);
	request.SetCallbacks(OnCompleted_Overread);
	request.Send();
	return Plugin_Handled;
}

// Claims room for the whole body in an 8-byte local, whose stack ends long
// before the body would.
public void OnCompleted_Overread(Handle hRequest, bool failure, bool successful, EHTTPStatusCode status, any data1,
                                 any data2) {
	SteamWorksHTTPRequest request = view_as<SteamWorksHTTPRequest>(hRequest);
	char small[8];
	int size;
	request.GetResponseBodySize(size);
	request.GetResponseBodyData(small, size);
	delete request;
}

public void OnHeaders(Handle hRequest, bool failure, any data1, any data2) {
	g_headersReceived++;
	SteamWorksHTTPRequest request = view_as<SteamWorksHTTPRequest>(hRequest);
	request.GetResponseHeaderValue("Content-Type", g_headersContentType, sizeof(g_headersContentType));
}

public void OnData(Handle hRequest, bool failure, int offset, int bytes, any data1, any data2) {
	SteamWorksHTTPRequest request = view_as<SteamWorksHTTPRequest>(hRequest);
	if (request.GetStreamingResponseBodyData(offset, g_chunk, bytes)) {
		g_chunksRead++;
	}
	if (g_dataCallbacks == 0) {
		char part[15];
		g_partialChunkRead = request.GetStreamingResponseBodyData(offset, part, sizeof(part));
		for (int i = 0; i < 15 && i < bytes; i++) {
			g_firstStreamedBytes[i] = g_chunk[i];
		}
		g_firstStreamedBytes[15] = '\0';
	}
	g_dataCallbacks++;
	g_streamedBytes += bytes;
	request.GetDownloadProgressPct(g_progress);
}

public void OnCompleted(Handle hRequest, bool failure, bool successful, EHTTPStatusCode status, any data1, any data2) {
	SteamWorksHTTPRequest request = view_as<SteamWorksHTTPRequest>(hRequest);
	g_completed++;
	g_failure = failure;
	g_successful = successful;
	g_status = view_as<int>(status);
	g_contextHigh = data1;
	g_contextLow = data2;
	request.GetWasTimedOut(g_timedOut);
	request.GetResponseHeaderValue("Content-Type", g_contentType, sizeof(g_contentType));
	request.GetResponseHeaderValue("Content-Length", g_contentLength, sizeof(g_contentLength));
	int size;
	if (request.GetResponseBodySize(size)) {
		g_bodySize = size;
		if (size < sizeof(g_body)) {
			g_bodyRead = request.GetResponseBodyData(g_body, size);
			g_body[g_bodyRead ? size : 0] = '\0';
		}
	}
	delete request;
}

public void SteamWorks_SteamServersConnected() {
	g_connected++;
}

public void SteamWorks_SteamServersConnectFailure(EResult result) {
	g_connectFailures++;
	g_lastResult = view_as<int>(result);
}

public void SteamWorks_SteamServersDisconnected(EResult result) {
	g_disconnected++;
	g_lastResult = view_as<int>(result);
}

public void SteamWorks_OnValidateClient(int ownerauthid, int authid) {
	g_validated++;
	g_validatedOwner = ownerauthid;
	g_validatedAccount = authid;
}

public void SW_OnValidateClient(int ownerauthid, int authid) {
	g_validated++;
}

public void SteamWorks_OnClientGroupStatus(int authid, int groupid, bool isMember, bool isOfficer) {
	g_groupStatus++;
	g_groupAccount = authid;
	g_groupId = groupid;
	g_groupMember = isMember;
	g_groupOfficer = isOfficer;
}

public void SteamWorks_TokenRequested(char[] sToken, int maxlen) {
	strcopy(sToken, maxlen, g_token);
}

public Action SteamWorks_RestartRequested() {
	return g_restartAnswer;
}
