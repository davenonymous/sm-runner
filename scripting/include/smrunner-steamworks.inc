// Copyright 2026 davenonymous
//
// smrunner's test API for SteamWorks: the routes that answer a test's HTTP
// requests, the requests the plugins sent, and the game server's and its
// clients' Steam state, for test plugins run with --steamworks. Its natives
// come from smrunner's SteamWorks stand-in extension, which this include
// requires; include SteamWorks.inc (SteamWorks 1.2.167) first or let this
// include do it.
//
// Answers: a request is answered by the newest route of the test that
// matches it, else by the first entry of the mapping file
// (--steamworks DIR: DIR/steamworks.json), else over the network when its
// host is allowed (--steamworks-passthrough HOST or AllowPassthrough), else
// with 404 and a console line naming it. Over the network, a redirect is
// followed only to an allowed host; one to another host fails the request
// (not successful) with a console line naming it, and nothing is recorded.
// A URL pattern matches the request's URL exactly, query parameters in any
// order, '*' matching any characters. Faked answers arrive on the frame
// after Send(), plus their delay frames.

#if defined _smrunner_steamworks_included
#endinput
#endif
#define _smrunner_steamworks_included

#include <SteamWorks>

// How a route's request ends without an answer (the stand-in's Failure).
enum RunnerHttpFailure {
	RunnerHttpFailure_None = 0,
	RunnerHttpFailure_Io = 1,       // bFailure true: Steam's call failed
	RunnerHttpFailure_Timeout = 2,  // not successful, GetWasTimedOut true
};

// Where a request's answer comes from (the stand-in's AnswerSource), chosen
// when it is sent: a passthrough request still on the network already reads
// RunnerHttpSource_Passthrough.
enum RunnerHttpSource {
	RunnerHttpSource_Route = 1,
	RunnerHttpSource_MappingFile = 2,
	RunnerHttpSource_Passthrough = 3,
	RunnerHttpSource_Missing = 4,      // nothing answered it: 404
};

// A route of the test, by its id.
methodmap RunnerHttpRoute {
	// Sets a response header of the route's answer.
	public native void SetHeader(const char[] name, const char[] value);
	// The route answers nothing from now on.
	public native void Remove();
	// How many requests it answered.
	property int Hits {
		public native get();
	}
}

// A request the plugins sent, by its index in the log (0 is the oldest).
methodmap RunnerHttpRequest {
	property EHTTPMethod Method {
		public native get();
	}
	// The answer's status once the request completed; 0 before and when it
	// failed or timed out.
	property EHTTPStatusCode Status {
		public native get();
	}
	// The completed callback's time came.
	property bool Answered {
		public native get();
	}
	property RunnerHttpSource Source {
		public native get();
	}
	// The length of the body sent (a raw body or the form of POST parameters).
	property int BodyLength {
		public native get();
	}
	// The URL, with the GET parameters of SetGetOrPostParameter.
	public native void GetUrl(char[] buffer, int maxlength);
	// A request header (any case), User-Agent and Content-Type included.
	public native bool GetHeader(const char[] name, char[] buffer, int maxlength);
	// A GET or POST parameter of SetGetOrPostParameter, the first of that name.
	public native bool GetParameter(const char[] name, char[] buffer, int maxlength);
	// The body sent, NUL-terminated; returns the bytes written without it.
	public native int GetBody(char[] buffer, int maxlength);
}

methodmap RunnerHttp {
	// Answers requests with method and URL pattern from now on, before the
	// mapping file and before older routes.
	public static native RunnerHttpRoute Route(EHTTPMethod method, const char[] url,
		EHTTPStatusCode status = k_EHTTPStatusCode200OK, const char[] body = "", int delayFrames = 0);
	// The same with the body read from a file now: a path below the game
	// folder (tf/) or an absolute one; Content-Type from its extension.
	public static native RunnerHttpRoute RouteFile(EHTTPMethod method, const char[] url, const char[] file,
		EHTTPStatusCode status = k_EHTTPStatusCode200OK, int delayFrames = 0);
	// Requests with method and URL pattern fail after delayFrames.
	public static native RunnerHttpRoute RouteFailure(EHTTPMethod method, const char[] url,
		RunnerHttpFailure failure, int delayFrames = 0);
	// Removes every route.
	public static native void ClearRoutes();
	// Lets requests to hosts matching the pattern ('*' for any characters,
	// any case) that nothing else answers go over the network.
	public static native void AllowPassthrough(const char[] host);

	// The requests sent since the start or the last ClearRequests.
	public static native int RequestCount();
	public static native RunnerHttpRequest GetRequest(int index);
	// How many sent requests match the URL pattern and method
	// (k_EHTTPMethodInvalid: any method).
	public static native int CountRequests(const char[] url = "*", EHTTPMethod method = k_EHTTPMethodInvalid);
	// The index of the first matching request from start on, or -1.
	public static native int FindRequest(const char[] url, EHTTPMethod method = k_EHTTPMethodInvalid, int start = 0);
	// Requests sent and not completed yet.
	public static native int PendingRequests();
	public static native void ClearRequests();
}

methodmap RunnerSteam {
	// What SteamWorks_IsConnected answers, without a forward.
	public static native void SetConnected(bool connected);
	// SteamWorks_SteamServersConnected; the server is connected afterwards.
	public static native void FireServersConnected();
	// SteamWorks_SteamServersConnectFailure; not connected afterwards.
	public static native void FireServersConnectFailure(EResult result);
	// SteamWorks_SteamServersDisconnected; not connected afterwards.
	public static native void FireServersDisconnected(EResult result);
	public static native void SetVACEnabled(bool enabled);
	// "a.b.c.d"; "" for none (GetPublicIP fails, GetPublicIPCell is 0).
	public static native void SetPublicIP(const char[] address);
	// "a.b.c.d" and its port for SteamWorks_GetFakeIP; "" for none.
	public static native void SetFakeIP(const char[] address, int port = 0);

	// What plugins set: SetRule, SetGameDescription, SetMapName, SetGameData
	// and SetAdvertiseServerActive (true by default).
	public static native bool GetRule(const char[] key, char[] value, int maxlength);
	public static native int RuleCount();
	public static native void GetGameDescription(char[] buffer, int maxlength);
	public static native void GetMapName(char[] buffer, int maxlength);
	public static native void GetGameData(char[] buffer, int maxlength);
	public static native bool IsAdvertising();

	// SteamWorks_TokenRequested; returns the token plugins wrote.
	public static native void FireTokenRequested(char[] token, int maxlength);
	// SteamWorks_RestartRequested; true when a plugin handled it.
	public static native bool FireRestartRequested();

	// What GetUserGroupStatus answers for an account and group from now on,
	// in SteamWorks_OnClientGroupStatus on the next frame (by default not a
	// member, not an officer).
	public static native void SetGroupStatus(int accountId, int groupId, bool member, bool officer);
	// What HasLicenseForApp answers for an account and app (by default the
	// connected clients own TF2, app 440, and nothing else; bots and other
	// accounts get NoAuth).
	public static native void SetLicense(int accountId, int appId, EUserHasLicenseForAppResult result);
	// SteamWorks_OnValidateClient and SW_OnValidateClient.
	public static native void FireValidateClient(int ownerAccountId, int accountId);
	// SteamWorks_OnClientGroupStatus, now.
	public static native void FireClientGroupStatus(int accountId, int groupId, bool member, bool officer);
}

public Extension __ext_smrunner_steamworks = {
	name = "SteamWorks",
	file = "SteamWorks.ext",
	autoload = 1,
	required = 1,
};
