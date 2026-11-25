# sm-runner

`smrunner` runs SourceMod plugins and their tests on a simulated server, without a game, a game folder or a
network: SourceMod's own logic library and SourcePawn VM, built from SourceMod's sources, behind a mocked core on a
simulated engine. Plugins load, start a map, run frames, take commands and clients as on a server, show menus and
send user messages; extensions that need neither the engine nor SourceMod's core load as they are, SourceMod's Top
Menus and stock plugins come with it, and a stand-in for SteamWorks fakes its HTTP requests and Steam state.
Tests are SourcePawn: a test plugin drives the server, connects clients, reads and selects what they are shown,
hooks and mocks natives, stops plugins at breakpoints to read and change their variables, and asserts what happened.
It is meant for tests in a project's CI run.

- [Quick start](#quick-start)
- [Writing tests](#writing-tests)
- [SteamWorks](#steamworks)
- [Command line](#command-line)
- [What the simulated server does](#what-the-simulated-server-does)
- [Limits](#limits)
- [Using it from another project](#using-it-from-another-project)
- [How it works](#how-it-works)
- [Building and its tests](#building-and-its-tests)
- [Layout](#layout)

## Quick start

Unpack a release archive (or the `smrunner-linux` artifact of a CI run), which holds everything under `smrunner/`:

```
tar -xzf smrunner-v0.1.0-linux.tar.gz -C /opt   # /opt/smrunner
/opt/smrunner/bin/smrunner run my-plugin.smx --command "sm_mycommand 1" --frames 100
/opt/smrunner/bin/smrunner test my-tests.smx --plugin my-plugin.smx --extensions path/to/extensions
```

`smrunner` is the 32-bit runner (for x86 extensions), `smrunner64` the 64-bit one (for `extensions/x64/`). Both
need a Linux with 32-bit respectively 64-bit libc and glibc 2.35 or later (Ubuntu 22.04).

`run` starts a map, runs the given server commands and frames and prints the server console; it exits 1 when a
plugin did not load or start, an assertion failed, or SourceMod logged an error. `test` runs every test (below) and exits 1 when one did
not pass. `list` prints the tests of test plugins.

## Writing tests

A test plugin includes `smrunner.inc` (in `scripting/include/`, installed as `share/include/smrunner.inc`) and
declares tests: public functions without parameters whose names start with `Test_`. Each test runs on a server of
its own, in a process of its own:

1. the test plugin loads first, then the plugins under test (`--plugin`, in the given order), in SourceMod's two
   passes, so natives that one plugin creates are there for all; a plugin that fails to load is tried once (its
   `AskPluginLoad2` runs once, its failure is logged once and fails the test) and is not in SourceMod's plugin list
   on that first map; on later maps SourceMod's `LoadAll` alone loads what is not loaded, not late, as on a server;
2. right after the test plugin loaded, before any `OnPluginStart`, smrunner calls the test plugin's
   `public void OnTestBoot(const char[] test)` if it has one: the place to install mocks, write files or set
   engine cvars for one test;
3. the map starts (`cp_badlands` by default) and its configs are executed (`OnConfigsExecuted` has fired);
4. the test function runs; it drives the server and asserts;
5. the server shuts down (`OnPluginEnd`, extension unload).

A test passes when it returns without a failed assertion, without an unexpected plugin error (any plugin's, the
test plugin's too) and with every expected error seen.

```sourcepawn
#include <sourcemod>
#include <smrunner>

public void Test_CounterCountsUp() {
	Runner.Command("sm_count 5");               // a server command, now
	Assert.ConsoleContains("counter is 5");
	Assert.Equal(Runner.GetPluginValue(FindPluginByFile("counter.smx"), "g_counter"), 5);
}
```

Failures name the assertion, the values and the test plugin's file and line, and show the end of the server
console and SourceMod's error log. The examples (`examples/`) and the self tests (`tests/selftest/`) show every
feature at work.

### Assertions

`Assert.True`, `False`, `Equal` (cells), `NotEqual`, `FloatEqual` (with a tolerance), `StrEqual`, `StrContains`,
`ConsoleContains`, `ConsoleNotContains`, `ClientReceived`, `ClientMenu` and `Fail`. Each takes an optional message with
format arguments. A failed assertion stops the running function of the test plugin; an assertion in a callback (a
hook, a mock, a timer, a breakpoint) fails the test and stops that callback.

### Time and the server

The clock is simulated: a frame advances it by one tick (0.015 seconds at the default 66.67 ticks), timers follow
game time, and nothing depends on real time, so runs repeat exactly. `Runner.Frames(n)` and `Runner.Seconds(s)`
run frames; they, `WaitUntil`, `ChangeMap` and `ConnectClient` with authorization fail when called inside a frame
(from a timer, `RequestFrame`, `OnGameFrame` or a breakpoint or hook reached there), whose work they would run
again. Once the server quit (`quit`, `exit`) no frame runs any more. Extensions that work on threads answer in real
time: `Runner.WaitUntil(condition, timeout)` runs frames with a millisecond of real time between them until the
condition function returns true.

`Runner.Command(format, ...)` runs server commands now, before it returns, apart from the command buffer: the line's
commands and what they add while they run (an `exec`'d file, a `ServerCommand`) run at once, up to a `wait` of their
own (what waits runs when the buffer runs next); commands buffered before and their `wait`s are left as they are.
`Runner.CommandOutput` also returns what its commands printed. The command buffer is tier1's: `ServerCommand` outside
of a command buffers until the buffer runs, once a frame and at every `ServerExecute`, and text added while a
command runs (`ServerCommand`, `InsertServerCommand`, `exec`) runs right after that command; a `wait` delays the
commands after it by one run of the buffer (`wait 3`: three), while `sv_allow_wait_command` is 1. As on a server, a
`//` outside quotes comments out the rest of a line (a `;` in it splits nothing), lines split at `\n` only (the
`\r` of a CRLF line stays in the command as whitespace, a cvar is set without it), and a command of 511 characters
or more is refused: the command buffer drops it and the rest of its text with `WARNING: Command too long...
ignoring!`. `Runner.ChangeMap(map)`
changes the map with everything SourceMod does on a map change and returns once the new map's configs were executed
(`OnConfigsExecuted`); a `changelevel` (also from `ForceChangeLevel`) changes it the same way at the end of its
frame. Either way the game clock (`GetGameTime`, `GetGameTickCount`) starts again at 0 on the new map. `Runner.ConsoleContains`, `Runner.GetConsole` and
`Runner.ClearConsole` read the server console since the server started. Cvars, commands, files, admins and
everything else are the plugins' own natives: a test sets a cvar with `FindConVar(...).SetInt(...)`.

### Clients

`Runner.ConnectClient(name, steamId = "", authorize = true)` connects a client through SourceMod's whole sequence
(`OnClientConnect` may reject it, `OnClientConnected`, `OnClientPutInServer`, `OnClientAuthorized`,
`OnClientPostAdminCheck`) and returns its index; Steam IDs are `STEAM_0:1:23`, `[U:1:47]` or 64-bit, and admin
identities match them. `Runner.ConnectBot`, `Runner.Disconnect`, `Runner.ClientCommand` (as typed by the client:
the line split at `;` and line breaks outside quotes and `//` comments, then each command on its own; one of 511
characters or more is a native error, since the client's own command buffer refuses it, and so is a `wait` while
`sv_allow_wait_command` allows waits (see Limits): SourceMod's commands with
command listeners and admin checks, `OnClientCommand` for the others, SourceMod's answer to `sm`; the engine's own
commands are not the client's, also not after a `;` or through `FakeClientCommand`; a command nothing takes gets
TF2's answer `Unknown command: <name>`, a `TextMsg` to the client's console that user message hooks see, except the
commands TF2's game rules take without an answer: `menuselect`, `voicemenu`, `objcmd`, `mvm_allupgrades`, and `vban`
and `VModEnable` with an argument; the TF2 player's own commands, such as `jointeam` and `joinclass`, are not
simulated and get that answer too), `Runner.SetClientConVar` (the client's userinfo: `GetClientInfo` reads the new
value at once, the server takes the change on the next frame as the engine does, with `OnClientSettingsChanged`, a
new name and the admin checks for a new password), `SetClientTeam`, `SetClientAlive` and `SetClientHealth`. What a client was sent is
recorded by kind (`RunnerMessage_Console`, `Chat`, `Center`, `Hint`, `Command`, `Kick`, `Menu`, `UserMessage`):
`Runner.ClientReceived`, `ClientMessageCount`, `GetClientMessage`, `ClearClientMessages` and
`Assert.ClientReceived`. Chat, center, hint and console text arrive as the user messages a TF2 client gets
(`TextMsg`, `SayText`, `SayText2`, `HintText`, also when a plugin sends them itself) and are recorded as the client
shows them: `%s1` to `%s4` replaced by the parameters, a `#token` kept with its parameters after it
(`#TF_Chat_All (alice, hi)`). `ClientReceived` and `Assert.ClientReceived` match the text as it was sent or
without its color codes (`\x01` to `\x06`, `\x07RRGGBB`, `\x08RRGGBBAA`), so `"[Team] ready"` matches a gold
tag; `GetClientMessage` returns it as sent. Every other user message is recorded as its name
(`RunnerMessage_UserMessage`, `"Shake"`); a test that needs its content hooks it with `HookUserMessage`. Admin
rights come from SourceMod's own natives (`SetUserFlagBits`, `CreateAdmin`, ...).

### Menus

Menus, panels and votes are SourceMod's own, in TF2's radio style: the numbered lines a client shows, `->1. Item`
for a key it takes and `3. Item` for a disabled one, Previous, Next, Back and Exit as SourceMod draws them. Every
display is recorded once as a `RunnerMessage_Menu` with its text when its ShowMenu message reaches the client (a new
page is a new display; the refreshes SourceMod sends every 4 seconds are not recorded). A menu displayed where no user
message can be started, while another is being sent or in its hook, is a plugin error: SourceMod's radio style
crashes the server there. `Runner.GetClientMenu(client, buffer, maxlength)` reads
the menu the client shows now and `Runner.ClientMenuKeys(client)` the keys it takes (bit 0 for key 1 to bit 9 for
key 0); the client stops showing a menu when it presses a key the menu takes, when its time runs out or when it
disconnects. `Assert.ClientMenu(client, text)` fails unless the client shows a menu that contains text.

`Runner.PrintClientMenu(client)` draws the menu the client shows for people to read, under the test's line in the
report (in run mode on standard output); the failures of `Assert.ClientMenu` and `Runner.SelectMenuItem` show the
menu the same way. Keys the menu takes are green, its navigation (Previous, Next, Back, Exit) cyan, disabled lines
dim, the title bold; colors follow `--color` and never go into the JUnit report.

```
menu of client 1 (alice)
╭─ Pick a fruit ───────╮
│ 1. Apple             │
│ 2. Banana            │
│ 3. Cherry (disabled) │
│                      │
│ 0. Exit              │
╰─────────────── 20 s ─╯
```

`Runner.SelectMenuItem(client, text)` presses the key of the one item the client can select (`->N. label`) whose
label contains text, and fails the test when the client shows no menu, no such item or several. A line is an item
only as the radio style writes one, the key's digit, a dot and a space; anything else is text. `Runner.SelectMenu(client, key)` presses
a key as the client sends it (`menuselect <key>`, 1 to 9 and 10 for the key 0); with no menu on the screen the
command goes on as any other client command and TF2's game rules take it without an answer, as on a server. Timeouts and vote times follow the simulated clock, so
`Runner.Seconds` runs them out.

```sourcepawn
public void Test_VoteForAMap() {
	int alice = Runner.ConnectClient("alice");
	SetUserFlagBits(alice, ADMFLAG_VOTE);
	Runner.ClientCommand(alice, "sm_mapvote");
	Assert.ClientMenu(alice, "Choose the next map");
	Runner.SelectMenuItem(alice, "koth_viaduct");
	Assert.ClientReceived(alice, RunnerMessage_Chat, "Next map: koth_viaduct");
}
```

The Top Menus extension is in every sandbox, and `--plugin adminmenu` loads SourceMod's stock admin menu (any stock
plugin loads by its name), so a plugin's entries of the admin menu are tested through `sm_admin`
(`examples/team-admin/`). The valve style (ESC dialogs) is not simulated: `GetMenuStyleHandle(MenuStyle_Valve)` is
a plugin error.

### Hooks and mocks

`Runner.HookNative(name, pre, post)` hooks a native in every plugin under test (test plugins keep the real one):
`pre` runs before the native and handles the call with `Plugin_Handled` (the call then returns `call.Result`),
`post` runs after it and sees and may change `call.Result`. `Runner.MockNative(name, mock)` replaces the native;
`call.CallOriginal()` runs the real one from the mock. The callbacks get a `RunnerCall`: `Arg(n)`, `SetArg`,
`ArgRef`/`SetArgRef` for references, `GetArgString`/`SetArgString` and `GetArgArray`/`SetArgArray` for buffers,
`ArgCount`, `Caller` (the plugin) and `GetName`. `Runner.UnhookNative` restores the native and
`Runner.NativeCalls(name)` counts the calls. Every native can be hooked, also natives of extensions and fake natives
of other plugins; stocks (such as `GetCmdArgInt`) cannot, hook the natives they call. A hook follows SourceMod's own
rebinding: when the plugin providing a hooked native is reloaded, or an optional native is bound once its provider
loads, the new binding is hooked at once, and `Runner.UnhookNative` gives a plugin back the binding it has then. To
provide a native nobody provides, the test plugin creates it with `CreateNative` in `AskPluginLoad2`, as any plugin
would.

### Breakpoints

`Runner.BreakAtLine(file, line, callback)` stops every plugin under test (or one named plugin) at the first
statement at or after the line, `Runner.BreakAtFunction(name, callback)` at a function's first statement. The file
is a name or the end of a path as the plugin was compiled (`"my-plugin.sp"`). The callback runs while the plugin
waits, with a `Breakpoint`: `Line`, `Plugin`, `GetFile`, `GetFunction`, `HasVariable` and, by name, the plugin's
variables visible there (the function's locals and arguments, then its globals): `GetValue`/`SetValue` for cells
and cells of arrays (ints, floats, bools, handles), `GetString`/`SetString` for strings. A change takes effect in the
plugin when it goes on. The callback may also run frames, commands or anything else. `Runner.RemoveBreakpoint` and
`Runner.BreakpointHits`. A breakpoint on a line or a function no loaded plugin has fails the test.

### Plugin variables, functions and errors

`Runner.GetPluginValue(plugin, name, index)`, `SetPluginValue`, `GetPluginString` and `SetPluginString` read and
write a plugin's globals at any time; public functions of plugins are called with SourceMod's `Call_StartFunction`
and `GetFunctionByName`. `Runner.ExpectError(text)` expects one plugin error containing text: it does not fail the
test, and the test fails when it does not come. `Runner.Skip(reason)` ends the test as skipped,
`Runner.GetTestName` returns the test's name.

### In run mode

A plugin run with `smrunner run` may use `smrunner.inc` too, but there is no test: the natives that drive the server,
its clients and menus and the plugins' variables work as in a test (`Runner.PrintClientMenu` prints on standard
output), and a failed assertion fails the run with its message and line. The natives that need a test,
`Runner.ExpectError`, `Skip`, `GetTestName`, the hooks and mocks (`HookNative`, `MockNative`, `UnhookNative`,
`NativeCalls`) and the breakpoints (`BreakAtLine`, `BreakAtFunction`, `RemoveBreakpoint`, `BreakpointHits`), throw a
native error that says so, which fails the run.

## SteamWorks

`--steamworks [DIR]` puts smrunner's stand-in for SteamWorks into the sandbox: an extension named and filed like
SteamWorks (`SteamWorks.ext.so`, x86 and x86_64) with the natives, forwards and `SteamWorksHTTPRequest` methodmap of
SteamWorks 1.2.167 for SourceMod 1.12 (`SteamWorks-1.2.167-sm1.12-linux.tar.gz`), so a plugin that requires
SteamWorks loads as on a server. Without the option it is not there, and a plugin that uses SteamWorks optionally sees
it missing, as on a server without it. It loads only inside smrunner. Plugins compile against the release's
`SteamWorks.inc`, which smrunner does not ship.

**HTTP.** A request is answered when the plugin calls `Send()` or `SendAndStreamResponse()`, from the first of:

1. the newest route of the test that matches it (`RunnerHttp.Route`, below);
2. the first matching entry of the mapping file `DIR/steamworks.json`;
3. the network, when its host is allowed (`--steamworks-passthrough HOST` or `RunnerHttp.AllowPassthrough`);
4. else 404 with an empty body, and the console line `[SteamWorks] no answer for GET <url>: 404` (a runner note
   with `-v`).

A pattern matches a request's URL exactly, its query parameters in any order, `*` matching any characters
(`http://api.test/team/*`); the GET parameters of `SetGetOrPostParameter` are part of the URL, POST, PUT and PATCH
send them as a form body. Faked answers arrive on the frame after `Send()`, later by their delay frames; their
timeouts (`SetAbsoluteTimeoutMS`, `SetNetworkActivityTimeout`, 60 seconds by default) follow the simulated clock, so
`Runner.Seconds` runs them out. A `Range: bytes=...` request header on a faked 200 answer with a body gets 206 with
that range and `Content-Range` (416 past the end), as a file server answers it; faked answers always have
`Content-Length` (the length of the body sent; for HEAD the one a GET would get, or the entry's own), and
`Content-Type` from the body file's extension (`.json`, `.txt`, `.html`, `.xml`, else `application/octet-stream`)
unless they set one. As with Steam, the headers and data callbacks run only for a request sent with
`SendAndStreamResponse()`: its headers callback when the answer arrives, then data callbacks of 64 KiB, one per frame,
with `GetDownloadProgressPct`; `GetStreamingResponseBodyData` reads only the chunk of the latest data callback, by its
offset and length. A streamed body is not read whole: `GetResponseBodyData`, `GetResponseBodyCallback` and
`WriteResponseBodyToFile` return false for it. `GetResponseBodyData` wants the buffer size `GetResponseBodySize`
gave, exactly (ISteamHTTP refuses a larger one as well as a smaller one). Every buffer, array and reference a native
of the stand-in writes or reads is checked to lie in the plugin's memory; one that does not is a native error, where
SteamWorks would write or read past it. The completed callback gets what SteamWorks gives it: bFailure only for an IO failure,
bRequestSuccessful and the status for an answer, neither for a timeout (`GetWasTimedOut` true). Callbacks run on the
game frame only, and a request deleted before its answer gets no callback.

The mapping file is a JSON list of entries, read when the stand-in loads (a wrong one fails it, naming the entry):

```json
[
  { "url": "http://etf2l.test/team/4242", "body_file": "team/4242.json" },
  { "method": "POST", "url": "http://demos.test/upload", "status": 201,
    "headers": { "X-RateLimit-Remaining": "59" }, "body": "{\"id\": 1}" },
  { "url": "http://etf2l.test/slow", "body": "late", "delay_frames": 10 },
  { "url": "http://etf2l.test/down", "fail": "io" }
]
```

`method` defaults to `GET` and `status` to 200; `body_file` is a path relative to DIR, `body` the body itself (one of
the two, or neither for an empty body); `delay_frames` delays the answer; `fail` is `"io"` (bFailure) or `"timeout"`.
A `HEAD` entry may give the `Content-Length` of a body it has no file of in `headers`.

**Passthrough and record mode.** A request nothing else answers goes over the network when its host matches a
`--steamworks-passthrough HOST` (case-insensitive, any port, `*` for any characters; repeatable) or a host the test
allowed. libcurl with Mbed TLS sends it from a worker thread of the stand-in, honours `SetRequiresVerifiedCertificate`
against the system's CA bundle and the request's timeouts in real time, and its callbacks run on the first frame after
the answer came in; a test waits for it with `Runner.WaitUntil`. The answer's headers are the server's as they came
(a HEAD answer keeps its `Content-Length`); a DELETE or OPTIONS request without a body goes out without one, and no
request gets a `Content-Type` it did not set. Redirects are followed (up to 10) only to hosts that
are allowed too: one to another host fails the request (not successful, status 0) with a console line naming the host.
With `--steamworks-record DIR` every real answer is written into `DIR/steamworks.json` (an entry of the same method
and URL is replaced; a redirected request under the URL it was sent to, with what its last hop answered) with its body
file `DIR/bodies/<host>/<path>` (characters other than letters, digits, `.`, `-` and `_` replaced by `_`), its query
as a hash in the name, `@` and the method in lower case for every method but GET (`items.json` and `items@post.json`)
and an extension from its Content-Type when the path has none. No two entries share a body file: a name another
entry's body file already has gets `~2`, `~3` and so on before its extension (a GET of `data` answered as
`application/octet-stream` and a GET of `data.bin` both make `data.bin`). Neither `@` nor `~` is ever left in a path,
so these names cannot clash with another request's own name. Its headers are kept except `Date`, `Connection`, `Transfer-Encoding`, `Keep-Alive` and, but for a HEAD answer, `Content-Length`.
Run once with passthrough and record mode, then with `--steamworks DIR` alone, and the tests answer from what was
recorded. Without `--steamworks-record` nothing is written.

**The server and its clients.** The game server is connected to Steam and VAC secured, its public IP 127.0.0.1, without
a fake IP; `SteamWorks_SteamServersConnected` fires on the first frame of the first map. What plugins set (`SetRule`,
`ClearRules`, `SetGameDescription`, `SetMapName`, `SetGameData`, `SetAdvertiseServerActive`) is kept for the test.
Clients are not members of any group (`GetUserGroupStatus` answers in `SteamWorks_OnClientGroupStatus` on the next
frame), own TF2 (app 440) and nothing else, bots and accounts not on the server get NoAuth, and
`GetClientSteamID` gives their 64-bit Steam ID. `SteamWorks_OnValidateClient`, `SW_OnValidateClient`,
`SteamWorks_TokenRequested` and `SteamWorks_RestartRequested` fire only when a test fires them. GC messages and user
stats are not faked: `SteamWorks_SendMessageToGC`, `RequestStats`, `RequestStatsAuthID` and the `GetStat` natives fail
with a native error naming them, and their forwards never fire.

**The test API** is `smrunner-steamworks.inc` (installed next to `smrunner.inc`), whose natives come from the stand-in
and which requires it:

- `RunnerHttp.Route(method, url, status, body, delayFrames)`, `RouteFile(method, url, file, ...)` (a file below the
  game folder or an absolute one) and `RouteFailure(method, url, RunnerHttpFailure_Io|Timeout, delayFrames)` return a
  `RunnerHttpRoute` with `SetHeader`, `Remove` and `Hits`; `RunnerHttp.ClearRoutes`, `AllowPassthrough(host)`.
- The requests the plugins sent: `RunnerHttp.RequestCount`, `GetRequest(index)` (0 the oldest), `CountRequests(url,
  method)`, `FindRequest(url, method, start)`, `PendingRequests` and `ClearRequests`; a `RunnerHttpRequest` has
  `Method`, `Status`, `Answered`, `Source` (route, mapping file, passthrough or missing), `BodyLength`, `GetUrl`,
  `GetHeader` (`User-Agent` and `Content-Type` included), `GetParameter` and `GetBody`.
- `RunnerSteam`: `SetConnected`, `FireServersConnected`, `FireServersConnectFailure`, `FireServersDisconnected`,
  `SetVACEnabled`, `SetPublicIP`, `SetFakeIP`; `GetRule`, `RuleCount`, `GetGameDescription`, `GetMapName`,
  `GetGameData`, `IsAdvertising`; `FireTokenRequested`, `FireRestartRequested`; `SetGroupStatus`, `SetLicense`,
  `FireValidateClient` and `FireClientGroupStatus`, by account ID.

```sourcepawn
#include <SteamWorks>
#include <smrunner>
#include <smrunner-steamworks>

public void Test_FailedRequestKeepsTheLastMotd() {
	RunnerHttp.Route(k_EHTTPMethodGET, "http://motd.test/motd.txt", k_EHTTPStatusCode503ServiceUnavailable);
	Runner.Command("sm_motd_refresh");
	Runner.Frames(1);                           // the answer's frame
	Assert.Equal(RunnerHttp.CountRequests("http://motd.test/*"), 2);
	int alice = Runner.ConnectClient("alice");
	Assert.ClientReceived(alice, RunnerMessage_Chat, "[MOTD] Welcome!");
}
```

## Command line

```
smrunner run  [options] PLUGIN.smx...
smrunner test [options] TEST.smx... [--plugin PLUGIN.smx]...
smrunner list TEST.smx...
```

| Option | |
|---|---|
| `-p`, `--plugin FILE` | load a plugin, in the given order (run: also as an argument); a bare name that is no file loads that SourceMod stock plugin (`adminmenu`) |
| `-e`, `--extensions DIR` | every extension in DIR, laid out like `addons/sourcemod/extensions` (`x64/` for 64-bit) |
| `-x`, `--extension FILE` | one extension (`.ext.so`) |
| `-f`, `--file SRC=DEST` | copy a file or folder to DEST below the game folder (`tf/`) |
| `-c`, `--cvar NAME=VALUE` | set a cvar before SourceMod starts; one not made yet gets the value when SourceMod or a plugin makes it, with its own default, bounds and flags (a warning names those not made once the configs ran) |
| `--core KEY=VALUE` | a `core.cfg` option |
| `--map NAME`, `--maps NAME,...` | the map to start (default `cp_badlands`), other maps that exist |
| `--maxplayers N`, `--tickrate N` | client slots (default 24), ticks per second (default 66.67) |
| `--no-jit` | run plugins in the interpreter |
| `--sandbox DIR`, `--keep-sandbox` | build the server folder in DIR (test: `DIR/<test plugin>/<test>`), or keep the temporary one |
| `--command CMD`, `--frames N` | run: server commands after the map started, frames after them (default 66) |
| `--filter TEXT`, `--timeout SECONDS` | test: only tests whose name contains TEXT; time limit per test (default 30) |
| `--junit FILE`, `--echo` | test: a JUnit XML report; print the console of every test |
| `-v`, `-q`, `--home DIR` | runner notes; no console echo and no warnings; the install folder |
| `--steamworks [DIR]` | the SteamWorks stand-in, answering from `DIR/steamworks.json` (DIR: the next argument when it is a folder, or `--steamworks=DIR`) |
| `--steamworks-passthrough HOST` | requests nothing else answers go to hosts matching HOST over the network (repeatable) |
| `--steamworks-record DIR` | the real answers of those requests are written into `DIR/steamworks.json` with their body files |
| `--color auto\|always\|never` | colors of drawn menus: `auto` (default) when standard output is a terminal and `NO_COLOR` is not set |

Exit codes: 0 passed, 1 failed, 2 wrong command line, 70 the runner itself could not go on (its message says why).

## What the simulated server does

- **SourceMod**: all of SourceMod's logic library as it is: handles and their security, plugins and their load
  order, extensions (from the sandbox's `extensions/`, as the plugins require them), natives of plugins, forwards,
  translations and phrases, admins, groups and overrides, timers' natives, ADTs, data packs, files, string
  formatting, the `sm` console commands (with the core's `sm cvars`, `sm cmds` and `sm config`). Ported from SourceMod's core: cvars and their
  hooks, console commands (server, console and admin commands, command listeners, `ServerCommandEx`), the timer
  system, the player manager, core.cfg and `AutoExecConfig` (missing files written from the plugin's cvars),
  `OnMapInit`, `OnMapStart`, `OnAutoConfigsBuffered`, `OnServerCfg`, `OnConfigsExecuted`, `OnGameFrame`,
  `OnMapEnd`, `RequestFrame` and extensions' frame hooks and frame actions, half-life natives (time, maps, random
  numbers seeded with 0, precaching, which lasts one map as the engine's precache tables do, chat, center and hint
  text, engine version), vector math, command line and
  KeyValues natives, the next map manager (`sm_nextmap`, the map history of every level change with its reason,
  trimmed to `sm_maphistory_size`; a `Runner.ChangeMap` is recorded as SourceMod records a change no command asked
  for, `" (Map overridden)"`), command iterators (`CommandIterator`, and `FindFirstConCommand` over every command
  and cvar in the order they were made), user messages (TF2's table, `StartMessage`, the bit buffer natives,
  `HookUserMessage` with intercept, block and post hooks; the core's own text goes through them as on TF2), and
  menus, panels and votes in the radio style (menu manager, base style, radio style, vote handler; no menu sounds).
  SourceMod's Top Menus extension, built from the same checkout, is in every sandbox; SourceMod's stock plugins of
  the server image load by name. Where a core native takes a buffer or an array with its length (`BfReadString`,
  `KeyValues.ExportToString`, the clients of `StartMessage`), a length below 1 (below 0 for the clients) or a span
  outside the plugin's memory is a native error, where SourceMod would write or read past it.
- **Engine**: the console with cvars and commands (case-insensitive, the engine's clamping and change rules, the
  tokenizer exactly as tier1's: `isspace` whitespace, words ending at `{}()':`, a quote or a signed char `<= ' '`, so
  an unquoted UTF-8 name splits into several arguments as on a server), the command buffer, `exec`, `echo`, `changelevel`, `map`, `quit`,
  `server.cfg`, common engine and TF2 cvars, the clock and the map.
- **Clients**: SourceMod's connect, authorization (Steam ID validated at once, checked every 0.7 seconds as
  SourceMod does), admin checks by name, IP and Steam ID (a reserved name without its password is kicked 0.1 seconds
  after the checks, as SourceMod does), the client's language from its `cl_language` (`english`) at the connect with
  `OnClientLanguageChanged` (unless core.cfg's `AllowClLanguageVar` is `Off`), `OnClientSettingsChanged` as the
  engine runs it (for a human once in its signon, after `OnClientConnected`, and once after `OnClientPutInServer`;
  for a bot after `OnClientPutInServer` and on the next frame; for any later userinfo change on the next frame, with
  a name change, the reserved name check that kicks in the client's language, and a new admin password), names cut
  to the engine's 31 bytes and by SourceMod to whole UTF-8 characters, the client's cvars
  by name in any case (`""` for one it does not have, as the engine answers), the address with its port
  (`GetClientIP(..., false)`), target patterns (`#userid`, `#STEAM_...`, `@all`, `@me`, partial names), client
  commands, kicks, and the messages a client was sent.

## Limits

The simulated server has no game DLL, so there are no entities, send props, data maps, game events, HUD text,
sounds or temp entities; their core natives fail with a message naming the native (a test can mock them). Not
simulated yet: the valve menu style (`GetMenuStyleHandle(MenuStyle_Valve)` is a plugin error), chat and chat
triggers (`say`, `!command`, `OnClientSayCommand`), client cvar queries (`QueryClientConVar`), the client network
statistics (latency, loss and choke are zero, the data rate 80000), the engine's name change rules (its cooldown and
the `(1)` of a name taken), Metamod:Source plugins, and gamedata signatures and offsets (there is no game binary to find
them in; the sandbox's stand-in binaries make SourceMod's own game configs load quietly). User messages are TF2's
bit buffers only (no protobuf natives, as on TF2), and extensions cannot reach them (`IUserMessages` is not
offered); a ShowMenu a plugin sends itself is recorded by name, not as the client's menu. KeyValues: see the
note at the end of this section. Extensions that need the engine or SourceMod's game-side interfaces (SDKTools,
SDKHooks, TF2 Tools, DHooks, the real SteamWorks, ...) do not load; a test plugin can provide the natives a plugin
needs from them with `CreateNative` and SourceMod's `MarkNativeAsOptional` rules. SteamWorks has a stand-in
(`--steamworks`, above) that fakes neither GC messages nor user stats.

A map change disconnects every client, as SourceMod sees it on a server (there clients reconnect); a test connects
them again after `Runner.ChangeMap`. Simulated clients have no command buffer or frames of their own, so a `wait` in
`Runner.ClientCommand`'s line, which a real client honours by sending the commands after it in its later frames, is
a native error naming the line; a test sends those commands with another `Runner.ClientCommand` after
`Runner.Frames`. Breakpoints do not fire inside a breakpoint's callback.

Plugins must be built by spcomp 1.11 or later (debug tables of the new format) for breakpoints and plugin variables;
variables of enum structs and arrays of more than one dimension cannot be read by name.

KeyValues (`packages/runner/src/core/keyvalues.cpp`, a tree of the runner's own that behaves like Valve's tier1 KeyValues,
with all of SourceMod's `Kv*` and `KeyValues` natives) differs from a server in four ways: `#include` and `#base`
are skipped with a note on the console; only the first top-level section of a text is read; deleting a key the
traversal stack still holds, the current key or a key below a subkey (`KvDeleteKey` with an empty name or a path)
fails with a native error where SourceMod would use freed memory; the first spelling of a key name is kept for every
tree (one symbol table, as on Valve's). Ptr and wide string values cannot be made, and
UTF-16 files are not read. Extensions cannot read the runner's KeyValues handles (`ReadKeyValuesHandle`).

## Using it from another project

A project unpacks the release archive into a stage of its own and runs its tests there, after compiling them with
SourceMod's spcomp and `/opt/smrunner/share/include`:

```dockerfile
FROM ubuntu:22.04 AS tests
RUN dpkg --add-architecture i386 \
 && apt-get update \
 && apt-get install -y --no-install-recommends libc6-i386 lib32stdc++6 ca-certificates \
 && rm -rf /var/lib/apt/lists/*
ADD smrunner-v0.1.0-linux.tar.gz /opt/
COPY --from=plugin-build /out/ /work/
RUN cd /work \
 && /opt/smrunner/bin/smrunner test tests/my-tests.smx --plugin plugins/my-plugin.smx --extensions extensions \
 && /opt/smrunner/bin/smrunner64 test tests/my-tests.smx --plugin plugins/my-plugin.smx --extensions extensions
```

Any Ubuntu 22.04 image with the 32-bit libc (`libc6-i386`, `lib32stdc++6`) runs `smrunner`; the SteamWorks
stand-in's passthrough of HTTPS with verified certificates also needs the system's CA bundle (`ca-certificates`).
Each test makes and removes a temporary server folder below `$TMPDIR` (or `/tmp`).

## How it works

```
   smrunner (one process per test, forked by the runner)
   +----------------------------------------------------------------------+
   |  test layer       Assert/Runner natives, hooks, breakpoints, results  |
   |  mocked core      CoreProvider bridge, ISourceMod, timers, players,   |
   |                   cvars, commands, configs, core natives              |
   |  simulated engine console, buffer, clock, map, client slots           |
   +-----------------------^--------------------------^--------------------+
          sourcemod.logic.so (SourceMod, unchanged)   libsourcepawn.so (patched)
                           ^                            ^
                     extensions (.ext.so)          plugins (.smx)
```

smrunner loads the VM (`libsourcepawn.so`) and the logic library (`sourcemod.logic.so`) as SourceMod's core does,
hands the logic library its bridge and drives the same sequence the core drives from the engine: startup and
`core.cfg`, map start (`LevelInit`: plugins load; `ServerActivate`: `OnMapStart`, configs buffered), frames (command
buffer, timers, frame hooks, `OnGameFrame`, think tasks), map end and shutdown. The VM is built with two patches (`packages/patches/`): debug breaks for
breakpoints and natives that can be rebound for hooks, with every change of a binding reported to the runner. Breakpoints use the VM's debug break and the plugin's own
debug tables (`packages/runner/src/smx/`). Each test runs in a forked process. The sandbox holds SourceMod's
configs, gamedata, translations and cfg files of SourceMod's release package 1.12.0.7263, the stock plugins asked
for by name and the Top Menus extension. Menus and user messages are ported from SourceMod's core like the rest of
it.

## Building and its tests

The GitHub workflow (`.github/workflows/ci.yml`) builds on every push, pull request and manual run, natively on
Ubuntu 22.04 with clang, for x86 and x86_64. Every input is pinned: SourceMod at the commit of its release
1.12.0.7263, whose SourcePawn is built standalone with the patches of `packages/patches/`, Metamod:Source on `1.11-dev` for
SourceHook's headers, and AMBuild, each by commit; SourceMod's release package 1.12.0.7263 (spcomp and the configs,
gamedata, translations, cfg files and stock plugins of `share/sourcemod/`), SMJansson and SteamWorks 1.2.167's
include by SHA-256. The SteamWorks stand-in's libraries (libcurl with Mbed TLS, its zlib, nlohmann/json) are built by
vcpkg at the commit `packages/vcpkg/vcpkg.json` names, static and position-independent (`packages/vcpkg/triplets/`), and cached
between runs. The build checks that the stand-in exports only `GetSMExtAPI` and that no binary needs a glibc newer
than 2.35; the libraries' licenses go to `licenses/`. It compiles the self tests and the examples with the package's
spcomp, runs them with both runners (`tests/run.sh`) and uploads the install tree as `smrunner-<commit>-linux.tar.gz`
(everything under `smrunner/`). A pushed tag `v<version>` must match `SMRUNNER_VERSION` in `packages/runner/src/version.h`
and publishes the archive, then named `smrunner-<tag>-linux.tar.gz`, as a GitHub release. The apt packages of the
runner are not pinned to versions.

- `tests/selftest/`: the runner's own tests against `target.sp`: the server (`test_basics`), hooks and mocks
  (`test_hooks`, also on a native of `hook_provider.sp` that `hook_dependent.sp` calls), breakpoints (`test_breakpoints`), KeyValues (`test_keyvalues`), menus, panels and votes against
  `menu_target.sp` (`test_menus`), user messages (`test_usermessages`), Top Menus with the stock adminmenu
  (`test_topmenus`), cvars from the command line (`test_cvars`), the test API in run mode (`run_mode`, through `run.sh`), and `expected_failures`, whose tests
  must each fail with their own message, which `run.sh` checks. `broken_start.sp` must fail a run, and so must
  `failing_load.sp`, whose `AskPluginLoad2` must run once.
- `tests/selftest/test_steamworks*.sp`: the SteamWorks stand-in against `steamworks_target.sp`: every faked native
  with the mapping file in `steamworks/` (`test_steamworks`), without one (`test_steamworks_bare`), passthrough and
  record mode against a local HTTP server that `run.sh` starts on a free port (`tests/http_server.py`, with a redirect
  on its host and one to another, POST and DELETE answers; `test_steamworks_passthrough`) and the
  replay of what was recorded without it (`test_steamworks_replay`); a plugin requiring SteamWorks must fail a run
  without `--steamworks`, a wrong mapping file must fail it naming the entry.
- `examples/json-status/`: a plugin that only uses SMJansson and printing, and its tests.
- `examples/menu-vote/`: a map vote and a panel, voted through by the text of the items, up to the map change.
- `examples/team-admin/`: an admin menu category with colored chat for everyone, and a panel with a disabled line
  and a fixed key 0, with SourceMod's stock adminmenu.
- `examples/steamworks-motd/`: a message of the day over HTTP and Steam's view of the server, with the SteamWorks
  stand-in, its mapping file and routes.

## Layout

- `packages/runner/src/engine/`: the simulated engine (console, cvars, commands, clock, map), its client slots (`clients`:
  the simulated clients, what the engine delivered to them, decoded as TF2's client does), the bit buffers of user
  messages (`bitbuf`) and `menu_display`: what a client shows of a menu and its lines, the one reader of the radio
  style's text.
- `packages/runner/src/core/`: the mocked core: `core.cpp` (bridge, startup, map, frames), `sourcemod_api.h`, `timers`,
  `players` (SourceMod's view of the client slots), `convars`, `concmds`, `coreconfig`, `nextmap`, `keyvalues.cpp`,
  `natives_engine.cpp`, user messages (`usermsgs`, `natives_usermsgs.cpp`) and menus (`menus`, `menu_base`,
  `menu_radio`, `menu_voting`).
- `packages/runner/src/testing/`: the test API (`natives_*.cpp`, one file per part: assertions, server, clients, menus, calls,
  breakpoints), the test session (`test_session`: the test, its test plugins, failures, native hooks and the debugger
  of one server), native calls and plugin variables with the rules of their memory (`native_hooks`, `debugger`,
  `plugin_memory`), drawn menus (`menu_box`) and the test runner.
- `packages/runner/src/smx/`: the reader of a plugin's publics and debug tables.
- `packages/runner/src/` (top): command line (`options`), the server spec built from it (`server_spec`: the run's or one
  test's server), sandbox, server, diagnostics, terminal colors; `packages/runner/standin/`: the stand-in
  binaries; `packages/runner/public/smrunner_host.h`: the interface the runner offers its own extensions (the options of the
  SteamWorks stand-in, its frame number and notes), implemented in `packages/runner/src/core/runner_host.cpp`.
- `packages/steamworks/`: the SteamWorks stand-in: `src/http.cpp` (the request handles, their natives and callbacks),
  `answerer.cpp` (which answer a request gets, the routes and the request log), `pending_request.cpp` (a sent request
  on its way: headers, chunks, completion, timeouts), `answers.cpp` (the mapping file, ranges), `url.cpp` (matching),
  `passthrough.cpp` (libcurl on a worker thread, redirects within the allowed hosts), `recorder.cpp` (record mode),
  `steam.cpp` (the server's and clients' natives and forwards), `test_api.cpp` (the natives of
  `smrunner-steamworks.inc`); it compiles in the runner's `packages/runner/src/testing/plugin_memory.cpp` for its checks of
  plugin memory; `packages/vcpkg/`: its libraries.
- `packages/logic/AMBuilder`: SourceMod's logic library from SourceMod's own source list; `packages/topmenus/AMBuilder`: SourceMod's
  Top Menus extension.
- `packages/patches/`: the SourcePawn patches. `scripting/include/smrunner.inc`: the test API;
  `scripting/include/smrunner-steamworks.inc`: the SteamWorks stand-in's.
- `.github/workflows/ci.yml`: the build; `tests/run.sh`: runs every suite with one runner.
