# API reference: smrunner-steamworks.inc

Every type and native of `smrunner-steamworks.inc`, the test API of the SteamWorks stand-in. Its
natives come from the stand-in, so a plugin that includes it needs `--steamworks`. The include
also includes `SteamWorks.inc` (SteamWorks 1.2.167), which you need on the include path.

How the stand-in answers requests, and what the natives are for, is explained in
[SteamWorks](steamworks.md).

Contents:

- [Types](#types): `RunnerHttpFailure`, `RunnerHttpSource`
- [RunnerHttp: routes](#runnerhttp-routes): `Route`, `RouteFile`, `RouteFailure`, `ClearRoutes`,
  `AllowPassthrough`
- [RunnerHttpRoute methodmap](#runnerhttproute-methodmap): `SetHeader`, `Remove`, `Hits`
- [RunnerHttp: the request log](#runnerhttp-the-request-log): `RequestCount`, `GetRequest`,
  `CountRequests`, `FindRequest`, `PendingRequests`, `ClearRequests`
- [RunnerHttpRequest](#runnerhttprequest): `Method`, `Status`, `Answered`, `Source`,
  `BodyLength`, `GetUrl`, `GetHeader`, `GetParameter`, `GetBody`
- [RunnerSteam: the server](#runnersteam-the-server): `SetConnected`, `SetVACEnabled`,
  `SetPublicIP`, `SetFakeIP`
- [RunnerSteam: what plugins set](#runnersteam-what-plugins-set): `GetRule`, `RuleCount`,
  `GetGameDescription`, `GetMapName`, `GetGameData`, `IsAdvertising`
- [RunnerSteam: forwards](#runnersteam-forwards): `FireServersConnected`,
  `FireServersConnectFailure`, `FireServersDisconnected`, `FireTokenRequested`,
  `FireRestartRequested`
- [RunnerSteam: clients](#runnersteam-clients): `SetGroupStatus`, `SetLicense`,
  `FireValidateClient`, `FireClientGroupStatus`

URL patterns match the whole URL, with `*` for any characters and query parameters in any order.
See [URL patterns](steamworks.md#url-patterns).

Every native checks its arguments and throws a native error for a wrong one: an index or id that
does not exist, a status outside 100 to 599, a negative delay, an unreadable file, an address that
is not IPv4. The entries name the checks that are not obvious.

## Types

### RunnerHttpFailure

How a route's request ends without an answer.

| Value | Effect |
|---|---|
| `RunnerHttpFailure_None` (0) | no failure |
| `RunnerHttpFailure_Io` (1) | `bFailure` true: Steam's call failed |
| `RunnerHttpFailure_Timeout` (2) | not successful, `GetWasTimedOut` true |

### RunnerHttpSource

Where a request's answer comes from. Chosen when the request is sent, so a passthrough request
still on the network already reads `RunnerHttpSource_Passthrough`.

| Value | Source |
|---|---|
| `RunnerHttpSource_Route` (1) | a route of the test |
| `RunnerHttpSource_MappingFile` (2) | an entry of `steamworks.json` |
| `RunnerHttpSource_Passthrough` (3) | the network |
| `RunnerHttpSource_Missing` (4) | nothing: answered with 404 |

## RunnerHttp: routes

### RunnerHttp.Route

```sourcepawn
RunnerHttpRoute RunnerHttp.Route(EHTTPMethod method, const char[] url,
    EHTTPStatusCode status = k_EHTTPStatusCode200OK, const char[] body = "", int delayFrames = 0)
```

From now on, answers requests with `method` and the URL pattern `url` with `status` and `body`,
`delayFrames` frames after the frame following `Send()`. Wins over the mapping file and over older
routes. A non-empty `body` is served as `application/octet-stream` unless the route sets a
Content-Type with `SetHeader`.

```sourcepawn
RunnerHttp.Route(k_EHTTPMethodGET, "http://motd.test/motd.txt", k_EHTTPStatusCode200OK, "Hello");
```

### RunnerHttp.RouteFile

```sourcepawn
RunnerHttpRoute RunnerHttp.RouteFile(EHTTPMethod method, const char[] url, const char[] file,
    EHTTPStatusCode status = k_EHTTPStatusCode200OK, int delayFrames = 0)
```

Like [`RunnerHttp.Route`](#runnerhttproute), with the body read from `file` now: a path relative
to the game folder (`tf/`), or an absolute path. The Content-Type comes from the file's extension.
A file that cannot be read is a native error.

```sourcepawn
RunnerHttp.RouteFile(k_EHTTPMethodGET, "http://scores.test/top", "addons/sourcemod/data/top.json");
```

### RunnerHttp.RouteFailure

```sourcepawn
RunnerHttpRoute RunnerHttp.RouteFailure(EHTTPMethod method, const char[] url,
    RunnerHttpFailure failure, int delayFrames = 0)
```

Requests with `method` and the URL pattern fail with `failure` after `delayFrames` frames.
`RunnerHttpFailure_None` is a native error.

```sourcepawn
RunnerHttp.RouteFailure(k_EHTTPMethodGET, "http://api.test/*", RunnerHttpFailure_Timeout);
```

### RunnerHttp.ClearRoutes

```sourcepawn
void RunnerHttp.ClearRoutes()
```

Removes every route of the test.

```sourcepawn
RunnerHttp.ClearRoutes();
```

### RunnerHttp.AllowPassthrough

```sourcepawn
void RunnerHttp.AllowPassthrough(const char[] host)
```

Lets requests to hosts matching `host` (`*` for any characters, case-insensitive, any port) that no
route and no mapping entry answers go over the network. The same as
`--steamworks-passthrough HOST` for the rest of the test. An empty host is a native error.

```sourcepawn
RunnerHttp.AllowPassthrough("127.0.0.1");
```

## RunnerHttpRoute methodmap

A route of the test, returned by the route natives.

### RunnerHttpRoute.SetHeader

```sourcepawn
void SetHeader(const char[] name, const char[] value)
```

Sets a header of the route's answer.

```sourcepawn
route.SetHeader("Retry-After", "30");
```

### RunnerHttpRoute.Remove

```sourcepawn
void Remove()
```

The route answers nothing from now on. Older routes and the mapping file answer again.

```sourcepawn
route.Remove();
```

### RunnerHttpRoute.Hits

```sourcepawn
property int Hits
```

How many requests the route answered.

```sourcepawn
Assert.Equal(route.Hits, 1);
```

## RunnerHttp: the request log

Every request the plugins send is logged, oldest first, until `ClearRequests`.

### RunnerHttp.RequestCount

```sourcepawn
int RunnerHttp.RequestCount()
```

The number of logged requests.

### RunnerHttp.GetRequest

```sourcepawn
RunnerHttpRequest RunnerHttp.GetRequest(int index)
```

The logged request number `index`; 0 is the oldest. An index outside the log is a native error.

```sourcepawn
RunnerHttpRequest first = RunnerHttp.GetRequest(0);
```

### RunnerHttp.CountRequests

```sourcepawn
int RunnerHttp.CountRequests(const char[] url = "*", EHTTPMethod method = k_EHTTPMethodInvalid)
```

How many logged requests match the URL pattern and method. `k_EHTTPMethodInvalid` matches any
method.

```sourcepawn
Assert.Equal(RunnerHttp.CountRequests("http://api.test/*", k_EHTTPMethodPOST), 1);
```

### RunnerHttp.FindRequest

```sourcepawn
int RunnerHttp.FindRequest(const char[] url, EHTTPMethod method = k_EHTTPMethodInvalid, int start = 0)
```

The index of the first matching request from `start` on, or -1.

```sourcepawn
int index = RunnerHttp.FindRequest("http://scores.test/upload", k_EHTTPMethodPOST);
```

### RunnerHttp.PendingRequests

```sourcepawn
int RunnerHttp.PendingRequests()
```

How many requests were sent and are not completed yet.

```sourcepawn
Assert.Equal(RunnerHttp.PendingRequests(), 1, "sent, not answered yet");
```

### RunnerHttp.ClearRequests

```sourcepawn
void RunnerHttp.ClearRequests()
```

Empties the request log.

```sourcepawn
RunnerHttp.ClearRequests();
Assert.Equal(RunnerHttp.RequestCount(), 0);
```

## RunnerHttpRequest

A logged request, by its index in the log.

### RunnerHttpRequest.Method

```sourcepawn
property EHTTPMethod Method
```

The request's method.

### RunnerHttpRequest.Status

```sourcepawn
property EHTTPStatusCode Status
```

The answer's status once the request completed. 0 before that, and when it failed or timed out.

### RunnerHttpRequest.Answered

```sourcepawn
property bool Answered
```

Whether the request is completed: its completed callback has run, or would have run had the
plugin set one.

### RunnerHttpRequest.Source

```sourcepawn
property RunnerHttpSource Source
```

Where the answer comes from. See [RunnerHttpSource](#runnerhttpsource).

### RunnerHttpRequest.BodyLength

```sourcepawn
property int BodyLength
```

The length of the body the plugin sent: a raw body, or the form of POST parameters.

### RunnerHttpRequest.GetUrl

```sourcepawn
void GetUrl(char[] buffer, int maxlength)
```

The URL, with the GET parameters of `SetGetOrPostParameter`.

### RunnerHttpRequest.GetHeader

```sourcepawn
bool GetHeader(const char[] name, char[] buffer, int maxlength)
```

A request header, by case-insensitive name, `User-Agent` and `Content-Type` included. Returns false
when the request has no such header.

```sourcepawn
char accept[32];
Assert.True(RunnerHttp.GetRequest(0).GetHeader("Accept", accept, sizeof(accept)));
```

### RunnerHttpRequest.GetParameter

```sourcepawn
bool GetParameter(const char[] name, char[] buffer, int maxlength)
```

The first GET or POST parameter of that name set with `SetGetOrPostParameter`. Returns false when
there is none.

```sourcepawn
char score[16];
Assert.True(sent.GetParameter("score", score, sizeof(score)));
```

### RunnerHttpRequest.GetBody

```sourcepawn
int GetBody(char[] buffer, int maxlength)
```

The body the plugin sent, NUL-terminated. Returns the bytes written, without the NUL.

```sourcepawn
char body[256];
sent.GetBody(body, sizeof(body));
Assert.StrContains(body, "score=42");
```

## RunnerSteam: the server

By default the server is connected to Steam and VAC secured, its public IP is 127.0.0.1 and it
has no fake IP.

### RunnerSteam.SetConnected

```sourcepawn
void RunnerSteam.SetConnected(bool connected)
```

Sets what `SteamWorks_IsConnected` answers, without firing a forward.

```sourcepawn
RunnerSteam.SetConnected(false);
```

### RunnerSteam.SetVACEnabled

```sourcepawn
void RunnerSteam.SetVACEnabled(bool enabled)
```

Sets what `SteamWorks_IsVACEnabled` answers.

### RunnerSteam.SetPublicIP

```sourcepawn
void RunnerSteam.SetPublicIP(const char[] address)
```

Sets the public IP, as `"a.b.c.d"`. `""` for none: then `SteamWorks_GetPublicIP` fails and
`SteamWorks_GetPublicIPCell` returns 0.

```sourcepawn
RunnerSteam.SetPublicIP("203.0.113.7");
```

### RunnerSteam.SetFakeIP

```sourcepawn
void RunnerSteam.SetFakeIP(const char[] address, int port = 0)
```

Sets what `SteamWorks_GetFakeIP` answers, as `"a.b.c.d"` and a port (0 to 65535). `""` for
none.

```sourcepawn
RunnerSteam.SetFakeIP("169.254.1.2", 27015);
```

## RunnerSteam: what plugins set

What plugins set with `SteamWorks_SetRule`, `SteamWorks_ClearRules`,
`SteamWorks_SetGameDescription`, `SteamWorks_SetMapName`, `SteamWorks_SetGameData` and
`SteamWorks_SetAdvertiseServerActive` is recorded for the test.

### RunnerSteam.GetRule

```sourcepawn
bool RunnerSteam.GetRule(const char[] key, char[] value, int maxlength)
```

The value of a server rule. Returns false when no plugin set it.

```sourcepawn
char value[64];
Assert.True(RunnerSteam.GetRule("league", value, sizeof(value)));
```

### RunnerSteam.RuleCount

```sourcepawn
int RunnerSteam.RuleCount()
```

The number of server rules.

### RunnerSteam.GetGameDescription

```sourcepawn
void RunnerSteam.GetGameDescription(char[] buffer, int maxlength)
```

The game description a plugin set.

### RunnerSteam.GetMapName

```sourcepawn
void RunnerSteam.GetMapName(char[] buffer, int maxlength)
```

The map name a plugin set.

### RunnerSteam.GetGameData

```sourcepawn
void RunnerSteam.GetGameData(char[] buffer, int maxlength)
```

The game data a plugin set.

### RunnerSteam.IsAdvertising

```sourcepawn
bool RunnerSteam.IsAdvertising()
```

Whether the server is advertised. True by default.

## RunnerSteam: forwards

These forwards fire when a test fires them. `SteamWorks_SteamServersConnected` also fires once on
its own, on the first frame of the first map.

### RunnerSteam.FireServersConnected

```sourcepawn
void RunnerSteam.FireServersConnected()
```

Fires `SteamWorks_SteamServersConnected`. The server is connected afterwards.

### RunnerSteam.FireServersConnectFailure

```sourcepawn
void RunnerSteam.FireServersConnectFailure(EResult result)
```

Fires `SteamWorks_SteamServersConnectFailure`. The server is not connected afterwards.

### RunnerSteam.FireServersDisconnected

```sourcepawn
void RunnerSteam.FireServersDisconnected(EResult result)
```

Fires `SteamWorks_SteamServersDisconnected`. The server is not connected afterwards.

### RunnerSteam.FireTokenRequested

```sourcepawn
void RunnerSteam.FireTokenRequested(char[] token, int maxlength)
```

Fires `SteamWorks_TokenRequested` and copies the token the plugins wrote.

```sourcepawn
char token[64];
RunnerSteam.FireTokenRequested(token, sizeof(token));
```

### RunnerSteam.FireRestartRequested

```sourcepawn
bool RunnerSteam.FireRestartRequested()
```

Fires `SteamWorks_RestartRequested`. Returns true when a plugin handled it.

```sourcepawn
Assert.True(RunnerSteam.FireRestartRequested(), "the plugin postponed the restart");
```

## RunnerSteam: clients

Clients are identified by their account ID, the number in `[U:1:<account>]`. By default the
connected clients own TF2 (app 440) and nothing else and are members of no group; bots and
accounts not on the server get `k_EUserHasLicenseResultNoAuth`.

### RunnerSteam.SetGroupStatus

```sourcepawn
void RunnerSteam.SetGroupStatus(int accountId, int groupId, bool member, bool officer)
```

Sets what `SteamWorks_GetUserGroupStatus` answers for the account and group from now on. The
answer arrives in `SteamWorks_OnClientGroupStatus` on the next frame, which fires on its own then.

```sourcepawn
RunnerSteam.SetGroupStatus(47, 1234, true, false);   // [U:1:47] is a member of group 1234
```

### RunnerSteam.SetLicense

```sourcepawn
void RunnerSteam.SetLicense(int accountId, int appId, EUserHasLicenseForAppResult result)
```

Sets what `SteamWorks_HasLicenseForApp` and `SteamWorks_HasLicenseForAppId` answer for the
account and app. A value that is not an `EUserHasLicenseForAppResult` is a native error.

```sourcepawn
RunnerSteam.SetLicense(47, 440, k_EUserHasLicenseResultDoesNotHaveLicense);
```

### RunnerSteam.FireValidateClient

```sourcepawn
void RunnerSteam.FireValidateClient(int ownerAccountId, int accountId)
```

Fires `SteamWorks_OnValidateClient` and `SW_OnValidateClient`.

```sourcepawn
RunnerSteam.FireValidateClient(100, 47);   // 47 plays on a family-shared account of 100
```

### RunnerSteam.FireClientGroupStatus

```sourcepawn
void RunnerSteam.FireClientGroupStatus(int accountId, int groupId, bool member, bool officer)
```

Fires `SteamWorks_OnClientGroupStatus` now.

```sourcepawn
RunnerSteam.FireClientGroupStatus(48, 1234, true, false);
```
