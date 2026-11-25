# SteamWorks

Plugins use the [SteamWorks](https://github.com/alliedmodders/SM-SteamWorks) extension for HTTP
requests and for Steam's view of the server and its players. The real extension needs a running
Steam game server and a network, and a test should not depend on either. smrunner ships a
stand-in for SteamWorks: an extension with the same file name, natives and forwards that answers
HTTP requests from your test data and fakes the Steam state.

- [Enabling the stand-in](#enabling-the-stand-in)
- [How a request is answered](#how-a-request-is-answered)
- [Routes: answers from the test](#routes-answers-from-the-test)
- [The mapping file: answers from a file](#the-mapping-file-answers-from-a-file)
- [URL patterns](#url-patterns)
- [When answers arrive](#when-answers-arrive)
- [Checking the requests a plugin sent](#checking-the-requests-a-plugin-sent)
- [What answers look like](#what-answers-look-like)
- [Passthrough: real requests](#passthrough-real-requests)
- [Record and replay](#record-and-replay)
- [Steam state of the server](#steam-state-of-the-server)
- [Steam state of clients](#steam-state-of-clients)
- [Forwards](#forwards)
- [What is not faked](#what-is-not-faked)
- [A complete example](#a-complete-example)

The full API is listed in the [SteamWorks API reference](steamworks-api-reference.md). The
examples on this page test small plugins whose names are given in a comment; their code is shown
where it matters.

## Enabling the stand-in

Add `--steamworks` to the command line. Optionally give it a folder with a
[mapping file](#the-mapping-file-answers-from-a-file). The folder's name is up to you:

```sh
smrunner test test_motd.smx --plugin motd.smx --steamworks              # routes only
smrunner test test_motd.smx --plugin motd.smx --steamworks fixtures/    # and fixtures/steamworks.json
smrunner test test_motd.smx --plugin motd.smx --steamworks=fixtures/    # the same
```

The stand-in has the file name and extension name of SteamWorks (`SteamWorks.ext.so`) and the
natives, forwards and `SteamWorksHTTPRequest` methodmap of SteamWorks 1.2.167 for SourceMod 1.12.
A plugin that requires SteamWorks loads as on a server.

- **Without `--steamworks`** there is no SteamWorks, as on a server without it. A plugin that
  requires it fails to load; one that uses it optionally sees it missing.
- **Compile against the real include.** Plugins and tests compile against SteamWorks 1.2.167's
  `SteamWorks.inc`, which smrunner does not ship. Get it from the SteamWorks release.
- **Test plugins** include `smrunner-steamworks.inc` for the test API. It requires the stand-in,
  so without `--steamworks` the test plugin itself fails to load and every test fails with
  `plugin test_motd.smx (evicted)`.
- **Only one SteamWorks.** Do not also pass a real `SteamWorks.ext.so` with `--extensions` or
  `--extension`. smrunner refuses the combination with
  `[smrunner] fatal: --steamworks and a SteamWorks extension of --extensions or --extension: give one of them`.
  The stand-in only loads inside smrunner.

```sourcepawn
#include <sourcemod>
#include <SteamWorks>
#include <smrunner>
#include <smrunner-steamworks>
```

## How a request is answered

When a plugin calls `Send()` or `SendAndStreamResponse()` on a request, the stand-in picks the
answer from the first of these that matches the request's method and URL:

1. the newest matching **route** of the test (`RunnerHttp.Route`);
2. the first matching entry of the **mapping file**;
3. the **network**, if the request's host is allowed for [passthrough](#passthrough-real-requests);
4. otherwise **404** with an empty body. The console prints
   `[SteamWorks] no answer for <METHOD> <url>: 404`, for example
   `[SteamWorks] no answer for GET http://motd.test/motd.txt: 404`.

Case 4 usually means the test forgot a URL. To make sure no request went unanswered, assert
`Assert.ConsoleNotContains("[SteamWorks] no answer")`, or check that a request's `Source` is not
`RunnerHttpSource_Missing` (see [Checking the requests](#checking-the-requests-a-plugin-sent)).

## Routes: answers from the test

A route answers requests of one method and URL pattern from the moment it is added until it is
removed or the test ends. It wins over the mapping file and over older routes:

```sourcepawn
// steamworks-motd.sp fetches http://motd.test/motd.txt on sm_motd_refresh and shows the
// text to joining players as "[MOTD] <text>".
public void Test_RouteChangesTheMotd() {
	RunnerHttp.Route(k_EHTTPMethodGET, "http://motd.test/motd.txt", k_EHTTPStatusCode200OK,
	                 "Server restart at midnight.");

	Runner.Command("sm_motd_refresh");
	Runner.Frames(1);                              // the answer arrives on the next frame

	int alice = Runner.ConnectClient("alice");
	Assert.ClientReceived(alice, RunnerMessage_Chat, "[MOTD] Server restart at midnight.");
}
```

The route natives:

| Native | Answers with |
|---|---|
| `RunnerHttp.Route(method, url, status = 200, body = "", delayFrames = 0)` | a status and a body |
| `RunnerHttp.RouteFile(method, url, file, status = 200, delayFrames = 0)` | a status and the content of `file`, read now |
| `RunnerHttp.RouteFailure(method, url, failure, delayFrames = 0)` | no answer: `RunnerHttpFailure_Io` or `RunnerHttpFailure_Timeout` |
| `RunnerHttp.ClearRoutes()` | removes every route |

`RunnerHttp.RouteFile` takes a path relative to the game folder (`tf/`), or an absolute path.
The answer's Content-Type comes from the file's extension. A file copied into the sandbox with
`--file` works well:

```sourcepawn
// Run with --file fixtures/top.json=addons/sourcemod/data/top.json
RunnerHttp.RouteFile(k_EHTTPMethodGET, "http://scores.test/top", "addons/sourcemod/data/top.json");
```

Each route native returns a `RunnerHttpRoute`:

```sourcepawn
RunnerHttpRoute route = RunnerHttp.Route(k_EHTTPMethodGET, "http://api.test/limited",
                                         k_EHTTPStatusCode429TooManyRequests);
route.SetHeader("Retry-After", "30");     // a header of the answer
// ...
Assert.Equal(route.Hits, 1);              // how many requests it answered
route.Remove();                           // older routes and the mapping file answer again
```

A failure ends the request without an answer, as the real extension reports it:

| Failure | The completed callback gets |
|---|---|
| `RunnerHttpFailure_Io` | `bFailure` true: Steam's call failed |
| `RunnerHttpFailure_Timeout` | `bRequestSuccessful` false, `GetWasTimedOut` true |

```sourcepawn
// scores.sp: the server command sm_scores_upload <score> POSTs the score to
// http://scores.test/upload and prints "[Scores] upload failed" on failure.
public void Test_UploadThatTimesOut() {
	RunnerHttp.RouteFailure(k_EHTTPMethodPOST, "http://scores.test/*", RunnerHttpFailure_Timeout, 5);

	Runner.Command("sm_scores_upload 42");
	Runner.Frames(6);                              // the next frame, plus 5

	Assert.ConsoleContains("[Scores] upload failed");
}
```

## The mapping file: answers from a file

The mapping file holds answers that every test needs. It is `steamworks.json` in the folder given
to `--steamworks`: a JSON list of entries, checked in order:

```json
[
  { "url": "http://motd.test/motd.txt", "body_file": "motd.txt" },
  { "method": "POST", "url": "http://demos.test/upload", "status": 201,
    "headers": { "X-RateLimit-Remaining": "59", "Content-Type": "application/json" },
    "body": "{\"id\": 1}" },
  { "url": "http://api.test/slow", "body": "late", "delay_frames": 10 },
  { "url": "http://api.test/down", "fail": "io" }
]
```

| Key | Meaning | Default |
|---|---|---|
| `url` | The [URL pattern](#url-patterns). Required. | |
| `method` | `GET`, `HEAD`, `POST`, `PUT`, `DELETE`, `OPTIONS` or `PATCH`, case-insensitive. | `GET` |
| `status` | The HTTP status, 100 to 599. | `200` |
| `headers` | An object of headers of the answer, all values strings. | none |
| `body` | The body of the answer, as a string. | empty |
| `body_file` | A file with the body: relative to the folder, or absolute. Not together with `body`. | |
| `delay_frames` | Extra frames before the answer arrives. | `0` |
| `fail` | `"io"` or `"timeout"`: no answer, see the failures above. | |

The file is read once, when the stand-in loads. A folder without `steamworks.json` is fine; then
only routes answer.

A wrong entry, for example an unknown key, makes the stand-in fail to load. smrunner prints a
warning that names the file and the entry (`-q` hides it):

```
[smrunner] warning: SteamWorks stand-in: /path/to/fixtures/steamworks.json: entry 1 "fail" is neither "io" nor "timeout"
```

Plugins that require SteamWorks then fail to load too, so a test reports
`plugin <name> (evicted)`. The console in the report repeats the message after
`Unable to load extension "SteamWorks.ext":`.

## URL patterns

Routes, mapping entries and the request log natives all match URLs the same way:

- The pattern matches the whole URL, exactly.
- `*` matches any characters, including none: `http://api.test/team/*`.
- Query parameters match in any order: `http://api.test/search?a=1&b=2` matches
  `http://api.test/search?b=2&a=1`.
- GET parameters a plugin sets with `SetGetOrPostParameter` are part of the URL. For POST, PUT
  and PATCH they are sent as a form body instead.

## When answers arrive

A faked answer arrives on the frame after `Send()`, plus its delay frames. The plugin's callbacks
run on the game frame, as with the real extension. So a test sends, then runs a frame:

```sourcepawn
Runner.Command("sm_motd_refresh");     // the plugin sends its request
Runner.Frames(1);                      // the answer arrives, the callback runs
```

Request timeouts follow the simulated clock, so `Runner.Seconds` runs them out. The activity
timeout (`SetNetworkActivityTimeout`) is 60 seconds by default, as with Steam. The absolute
timeout (`SetAbsoluteTimeoutMS`) is off unless the plugin sets it. A request deleted before its
answer gets no callback.

## Checking the requests a plugin sent

Every request the plugins send is logged, oldest first:

| Native | Returns |
|---|---|
| `RunnerHttp.RequestCount()` | the number of logged requests |
| `RunnerHttp.GetRequest(index)` | request `index`, 0 being the oldest |
| `RunnerHttp.CountRequests(url = "*", method = k_EHTTPMethodInvalid)` | how many match; `k_EHTTPMethodInvalid` matches any method |
| `RunnerHttp.FindRequest(url, method = k_EHTTPMethodInvalid, start = 0)` | the index of the first match from `start` on, or -1 |
| `RunnerHttp.PendingRequests()` | how many were sent and are not completed yet |
| `RunnerHttp.ClearRequests()` | empties the log |

A `RunnerHttpRequest` has:

| Member | Is |
|---|---|
| `RunnerHttpRequest.Method` | the request's method |
| `RunnerHttpRequest.Status` | the answer's status once completed; 0 before, and for failures and timeouts |
| `RunnerHttpRequest.Answered` | whether the request is completed: its completed callback has run, or would have run had the plugin set one |
| `RunnerHttpRequest.Source` | where the answer came from: `RunnerHttpSource_Route`, `RunnerHttpSource_MappingFile`, `RunnerHttpSource_Passthrough` or `RunnerHttpSource_Missing` |
| `RunnerHttpRequest.BodyLength` | the length of the body the plugin sent |
| `RunnerHttpRequest.GetUrl(buffer, maxlength)` | the URL, with GET parameters |
| `RunnerHttpRequest.GetHeader(name, buffer, maxlength)` | a request header, by case-insensitive name; `User-Agent` and `Content-Type` included |
| `RunnerHttpRequest.GetParameter(name, buffer, maxlength)` | the first GET or POST parameter of that name |
| `RunnerHttpRequest.GetBody(buffer, maxlength)` | the body the plugin sent |

A header check:

```sourcepawn
// steamworks-motd.sp fetches the message of the day once when the configs
// have run, with the request header "Accept: text/plain".
public void Test_MotdFromTheMappingFile() {
	Assert.Equal(RunnerHttp.CountRequests("http://motd.test/motd.txt"), 1);

	char accept[32];
	Assert.True(RunnerHttp.GetRequest(0).GetHeader("Accept", accept, sizeof(accept)));
	Assert.StrEqual(accept, "text/plain");
}
```

A POST with form parameters, checked before and after its answer:

```sourcepawn
// scores.sp: sm_scores_upload <score> POSTs map=cp_badlands&score=<score> and
// prints "[Scores] uploaded (status <status>)".
public void Test_UploadSendsTheScoreAsAForm() {
	RunnerHttp.Route(k_EHTTPMethodPOST, "http://scores.test/upload", k_EHTTPStatusCode201Created);

	Runner.Command("sm_scores_upload 42");
	Assert.Equal(RunnerHttp.PendingRequests(), 1, "sent, not answered yet");
	Runner.Frames(1);
	Assert.Equal(RunnerHttp.PendingRequests(), 0);

	int index = RunnerHttp.FindRequest("http://scores.test/upload", k_EHTTPMethodPOST);
	Assert.NotEqual(index, -1);
	RunnerHttpRequest sent = RunnerHttp.GetRequest(index);
	char value[64];
	Assert.True(sent.GetParameter("score", value, sizeof(value)));
	Assert.StrEqual(value, "42");
	sent.GetBody(value, sizeof(value));
	Assert.StrContains(value, "score=42");
	Assert.ConsoleContains("[Scores] uploaded (status 201)");
}
```

Enum values compare directly with `Assert.Equal`, which takes any tag:

```sourcepawn
Assert.Equal(RunnerHttp.GetRequest(0).Source, RunnerHttpSource_Route);
```

`RunnerHttp.ClearRoutes()` and `RunnerHttp.ClearRequests()` reset a test halfway through:

```sourcepawn
RunnerHttp.ClearRoutes();               // from now on only the mapping file answers
RunnerHttp.ClearRequests();             // and the log starts empty
Assert.Equal(RunnerHttp.RequestCount(), 0);
```

## What answers look like

Faked answers behave like a simple file server:

- **Content-Length** is always set: the length of the body. For HEAD, the length a GET would get,
  or the entry's own `Content-Length` header.
- **Content-Type**, unless the answer sets one in its headers:
  - for a body file (`body_file`, `RouteFile`), from the file's extension: `.json`
    (application/json), `.txt`, `.cfg`, `.vdf` (text/plain), `.html`, `.htm` (text/html), `.xml`
    (application/xml), anything else application/octet-stream;
  - for a body given as text (`body`, `RunnerHttp.Route`): application/octet-stream;
  - for an answer without a body: none.
- **Ranges.** A `Range: bytes=...` request header on a 200 answer with a body gets 206 with that
  range and `Content-Range`, or 416 past the end.
- **HEAD** answers have no body, but the Content-Length of the body.
- **Streaming.** With `SendAndStreamResponse()`, the headers callback runs when the answer
  arrives, then one data callback per frame with 64 KiB chunks. A streamed body cannot be read
  whole: `GetResponseBodyData`, `GetResponseBodyCallback` and `WriteResponseBodyToFile` return
  false for it.
- **Buffer sizes.** `GetResponseBodyData` wants exactly the size `GetResponseBodySize` returned,
  as Steam does.

A streamed download, one 64 KiB chunk per frame:

```sourcepawn
// download.sp: sm_download fetches http://maps.test/big.bsp with
// SendAndStreamResponse, counts the data callbacks, and prints
// "[Download] done after <n> chunks" when the request completes.
public void Test_StreamedInChunks() {
	char body[200000];                             // about 195 KiB: four chunks
	for (int i = 0; i < sizeof(body) - 1; i++) {
		body[i] = 'x';
	}
	RunnerHttp.Route(k_EHTTPMethodGET, "http://maps.test/big.bsp", k_EHTTPStatusCode200OK, body);

	Runner.Command("sm_download");
	Runner.Frames(10);

	Assert.ConsoleContains("[Download] done after 4 chunks");
}
```

Every buffer and array a native of the stand-in reads or writes is checked to lie in the plugin's
memory. A wrong one is a native error, where the real extension would read or write past it.

## Passthrough: real requests

Sometimes a test should talk to a real server, for example to record answers or to run against a
local test server. A request that no route and no mapping entry answers goes over the network
when its host is allowed:

- on the command line: `--steamworks-passthrough HOST`, repeatable;
- in a test: `RunnerHttp.AllowPassthrough(host)`, for the rest of that test.

Host patterns are case-insensitive, match any port, and `*` matches any characters
(`*.example.com`).

```sh
smrunner test test_api.smx --plugin api.smx --steamworks --steamworks-passthrough 127.0.0.1
```

Passthrough requests run on a worker thread with libcurl, in real time. Their callbacks run on
the first frame after the answer has arrived, so a test waits for them with `Runner.WaitUntil`:

```sourcepawn
bool Answered(int index) {
	return RunnerHttp.GetRequest(index).Answered;
}

public void Test_RealAnswer() {
	RunnerHttp.AllowPassthrough("127.0.0.1");      // or --steamworks-passthrough 127.0.0.1
	Runner.Command("sm_motd_refresh");
	int index = RunnerHttp.RequestCount() - 1;     // the request just sent
	Assert.True(Runner.WaitUntil(Answered, 5.0, index), "no answer within 5 seconds");
	Assert.Equal(RunnerHttp.GetRequest(index).Source, RunnerHttpSource_Passthrough);
}
```

Details:

- `SetRequiresVerifiedCertificate` is honored against the system's CA bundle (install
  `ca-certificates`).
- The request's own timeouts apply, in real time.
- Redirects are followed, up to 10, but only to hosts that are allowed too. A redirect to another
  host fails the request (not successful, status 0), and the console names the host.
- Answer headers are passed on as the server sent them.

## Record and replay

Record mode saves real answers so that later runs do not need the network. Add
`--steamworks-record DIR` to a passthrough run:

```sh
# 1. Record: real requests, answers written to fixtures/
smrunner test test_api.smx --plugin api.smx \
    --steamworks --steamworks-passthrough api.example.com --steamworks-record fixtures

# 2. Replay: answers come from fixtures/steamworks.json, no network
smrunner test test_api.smx --plugin api.smx --steamworks fixtures
```

Every real answer becomes an entry of `DIR/steamworks.json`. An existing entry with the same
method and exactly the same URL is removed, and the new entry is added at the end of the list.
Since entries are checked in order, an earlier entry with a matching `*` pattern then wins over
the re-recorded one.

The body goes to a file below `DIR/bodies/<host>/<path>`:

- characters other than letters, digits, `.`, `-` and `_` become `_`;
- the query string becomes a hash in the name;
- methods other than GET add `@` and the method: `items.json` and `items@post.json`;
- an extension is added from the Content-Type when the path has none;
- a name already taken gets `~2`, `~3` and so on before its extension.

Headers are kept except `Date`, `Connection`, `Transfer-Encoding`, `Keep-Alive` and, except for
HEAD answers, `Content-Length`. A redirected request is recorded under the URL it was sent to,
with what its last hop answered. Without `--steamworks-record` nothing is written.

Recording also leaves `DIR/steamworks.json.lock`, which serializes the writes of the test
processes. It can be deleted, and does not belong in version control.

## Steam state of the server

By default the server is connected to Steam and VAC secured, its public IP is 127.0.0.1, and it
has no fake IP. A test changes this with `RunnerSteam`:

| Native | Changes |
|---|---|
| `RunnerSteam.SetConnected(connected)` | what `SteamWorks_IsConnected` answers, without a forward |
| `RunnerSteam.SetVACEnabled(enabled)` | what `SteamWorks_IsVACEnabled` answers |
| `RunnerSteam.SetPublicIP(address)` | the public IP; `""` for none |
| `RunnerSteam.SetFakeIP(address, port)` | what `SteamWorks_GetFakeIP` answers; `""` for none |

```sourcepawn
// steamworks-motd.sp: sm_serverinfo replies with the address and VAC state.
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
```

```sourcepawn
public void Test_ConnectionAndFakeIp() {
	RunnerSteam.SetConnected(false);
	Assert.False(SteamWorks_IsConnected());

	RunnerSteam.SetFakeIP("169.254.1.2", 27015);
	int ip, port;
	Assert.True(SteamWorks_GetFakeIP(ip, port));
	Assert.Equal(port, 27015);
}
```

What plugins set on the server (`SteamWorks_SetRule`, `SteamWorks_ClearRules`,
`SteamWorks_SetGameDescription`, `SteamWorks_SetMapName`, `SteamWorks_SetGameData`,
`SteamWorks_SetAdvertiseServerActive`) is recorded. Read it with `RunnerSteam.GetRule`,
`RunnerSteam.RuleCount`, `RunnerSteam.GetGameDescription`, `RunnerSteam.GetMapName`,
`RunnerSteam.GetGameData` and `RunnerSteam.IsAdvertising`:

```sourcepawn
// serverstate.sp sets the rule league=etf2l and the game description
// "TF2 scrims" in OnConfigsExecuted.
public void Test_ServerAdvertisesItsLeague() {
	char value[64];
	Assert.True(RunnerSteam.GetRule("league", value, sizeof(value)));
	Assert.StrEqual(value, "etf2l");
	Assert.Equal(RunnerSteam.RuleCount(), 1);

	RunnerSteam.GetGameDescription(value, sizeof(value));
	Assert.StrEqual(value, "TF2 scrims");
}

// In OnMapStart it also sets the map name "Badlands", the game data
// "scrim,etf2l" and stops advertising the server.
public void Test_WhatPluginsSet() {
	char value[64];
	RunnerSteam.GetMapName(value, sizeof(value));
	Assert.StrEqual(value, "Badlands");
	RunnerSteam.GetGameData(value, sizeof(value));
	Assert.StrEqual(value, "scrim,etf2l");
	Assert.False(RunnerSteam.IsAdvertising());
}
```

## Steam state of clients

Clients own TF2 (app 440) and nothing else, and are members of no Steam group.
`SteamWorks_GetClientSteamID` gives their 64-bit Steam ID. Bots and accounts that are not on the
server get `k_EUserHasLicenseResultNoAuth`. Change this per account ID:

| Native | Changes |
|---|---|
| `RunnerSteam.SetLicense(accountId, appId, result)` | what `SteamWorks_HasLicenseForApp` and `SteamWorks_HasLicenseForAppId` answer |
| `RunnerSteam.SetGroupStatus(accountId, groupId, member, officer)` | what `SteamWorks_GetUserGroupStatus` answers, in `SteamWorks_OnClientGroupStatus` on the next frame |

The account ID is the number in `[U:1:<account>]`. For `Runner.ConnectClient("alice", "[U:1:47]")`
it is 47.

```sourcepawn
public void Test_LicenseCanBeTakenAway() {
	RunnerSteam.SetLicense(47, 440, k_EUserHasLicenseResultDoesNotHaveLicense);
	int alice = Runner.ConnectClient("alice", "[U:1:47]");
	Assert.Equal(SteamWorks_HasLicenseForApp(alice, 440), k_EUserHasLicenseResultDoesNotHaveLicense);
}
```

```sourcepawn
// groups.sp asks for the group status of every joining player and greets
// members of group 1234 with "[Group] Welcome, member!".
public void Test_GroupMembersAreWelcomed() {
	RunnerSteam.SetGroupStatus(47, 1234, true, false);

	int alice = Runner.ConnectClient("alice", "[U:1:47]");
	Runner.Frames(1);                              // the group status arrives

	Assert.ClientReceived(alice, RunnerMessage_Chat, "[Group] Welcome, member!");
}
```

## Forwards

Two forwards fire on their own, as Steam would fire them:

- `SteamWorks_SteamServersConnected`, once, on the first frame of the first map;
- `SteamWorks_OnClientGroupStatus`, on the frame after a plugin calls
  `SteamWorks_GetUserGroupStatus` or `SteamWorks_GetUserGroupStatusAuthID`.

Every other forward, and these two at any other time, fires only when a test fires it:

| Native | Fires |
|---|---|
| `RunnerSteam.FireServersConnected()` | `SteamWorks_SteamServersConnected` |
| `RunnerSteam.FireServersConnectFailure(result)` | `SteamWorks_SteamServersConnectFailure` |
| `RunnerSteam.FireServersDisconnected(result)` | `SteamWorks_SteamServersDisconnected` |
| `RunnerSteam.FireTokenRequested(token, maxlength)` | `SteamWorks_TokenRequested`; copies the token plugins wrote |
| `RunnerSteam.FireRestartRequested()` | `SteamWorks_RestartRequested`; true when a plugin handled it |
| `RunnerSteam.FireValidateClient(ownerAccountId, accountId)` | `SteamWorks_OnValidateClient` and `SW_OnValidateClient` |
| `RunnerSteam.FireClientGroupStatus(accountId, groupId, member, officer)` | `SteamWorks_OnClientGroupStatus`, now |

```sourcepawn
// serverstate.sp writes the login token "LOGIN-TOKEN" when Steam asks for one,
// postpones restarts (returns Plugin_Handled), and logs family-shared accounts.
public void Test_SteamForwards() {
	char token[64];
	RunnerSteam.FireTokenRequested(token, sizeof(token));
	Assert.StrEqual(token, "LOGIN-TOKEN");

	Assert.True(RunnerSteam.FireRestartRequested(), "the plugin handled the restart");

	RunnerSteam.FireValidateClient(100, 47);
	Assert.ConsoleContains("[State] 47 plays on a family-shared account of 100");
}

// It also prints "[State] Steam connection failed (<result>)" in
// SteamWorks_SteamServersConnectFailure.
public void Test_ConnectFailure() {
	RunnerSteam.FireServersConnectFailure(k_EResultTimeout);
	Assert.ConsoleContains("[State] Steam connection failed");
	Assert.False(SteamWorks_IsConnected());
}
```

## What is not faked

- Game Coordinator messages and user stats. `SteamWorks_SendMessageToGC`,
  `SteamWorks_RequestStats`, `SteamWorks_RequestStatsAuthID` and the `SteamWorks_GetStat*`
  natives throw a native error that names them, and their forwards never fire. Mock them if a
  plugin needs them (see [Hooks and mocks](hooks-and-mocks.md)).
- `SteamWorks_DeferHTTPRequest` and `SteamWorks_PrioritizeHTTPRequest` reorder nothing. They
  return true while the request still waits for its answer.
- `SteamWorks_ForceHeartbeat` always returns false.

## A complete example

[`examples/steamworks-motd/`](../examples/steamworks-motd/) fetches a message of the day over
HTTP and shows it to every player who joins. Its tests use the mapping file for the normal answer
and routes for the special cases:

```sourcepawn
#define MOTD_URL "http://motd.test/motd.txt"

// The mapping file answers the request the plugin sends in OnConfigsExecuted.
public void Test_MotdFromTheMappingFile() {
	Assert.Equal(RunnerHttp.CountRequests(MOTD_URL), 1, "fetched once when the configs ran");
	char accept[32];
	Assert.True(RunnerHttp.GetRequest(0).GetHeader("Accept", accept, sizeof(accept)));
	Assert.StrEqual(accept, "text/plain");
	int alice = Runner.ConnectClient("alice");
	Assert.ClientReceived(alice, RunnerMessage_Chat, "[MOTD] Welcome! Matches start at 20:00.");
}

// A route makes the next fetch fail; the plugin keeps the message it had.
public void Test_FailedRequestKeepsTheLastMotd() {
	RunnerHttp.Route(k_EHTTPMethodGET, MOTD_URL, k_EHTTPStatusCode503ServiceUnavailable);
	Runner.Command("sm_motd_refresh");
	Runner.Frames(1);
	Assert.ConsoleContains("The message of the day was not fetched (status 503)");
	int alice = Runner.ConnectClient("alice");
	Assert.ClientReceived(alice, RunnerMessage_Chat, "[MOTD] Welcome!");
}
```

```sh
smrunner test test_steamworks_motd.smx --plugin steamworks-motd.smx --steamworks steamworks
```
