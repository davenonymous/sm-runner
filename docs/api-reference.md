# API reference: smrunner.inc

Every type and native of `smrunner.inc`, the test API. Each native has its own heading, so a
search for its name lands here. The guides explain how the natives work together:
[Writing tests](writing-tests.md), [Server, time and commands](server-and-time.md),
[Clients](clients.md), [Menus](menus.md), [Hooks and mocks](hooks-and-mocks.md) and
[Breakpoints and plugin variables](breakpoints.md).

The SteamWorks stand-in's API is in the [SteamWorks API reference](steamworks-api-reference.md).

Two markers appear in the entries:

- **Needs a test**: the native throws a native error in [run mode](run-mode.md).
- **Not inside a frame**: the native throws a native error when called inside a frame; see
  [Where frames cannot run](server-and-time.md#where-frames-cannot-run).

Contents:

- [Callbacks of the test plugin](#callbacks-of-the-test-plugin): `OnTestBoot`
- [Types](#types): `RunnerMessage`, `RunnerCondition`, `RunnerNativeHook`, `RunnerNativeMock`,
  `RunnerBreakpoint`
- [Runner: time](#runner-time): `Frames`, `Seconds`, `WaitUntil`
- [Runner: server](#runner-server): `Command`, `CommandOutput`, `ChangeMap`
- [Runner: console](#runner-console): `ConsoleContains`, `GetConsole`, `ClearConsole`
- [Runner: clients](#runner-clients): `ConnectClient`, `ConnectBot`, `GetRejectReason`,
  `Disconnect`, `ClientCommand`, `SetClientConVar`, `SetClientTeam`, `SetClientAlive`,
  `SetClientHealth`, `ClientReceived`, `ClientMessageCount`, `GetClientMessage`,
  `ClearClientMessages`
- [Runner: menus](#runner-menus): `GetClientMenu`, `ClientMenuKeys`, `PrintClientMenu`,
  `SelectMenu`, `SelectMenuItem`
- [Runner: the test](#runner-the-test): `ExpectError`, `Skip`, `GetTestName`
- [Runner: natives](#runner-natives): `MockNative`, `HookNative`, `UnhookNative`, `NativeCalls`
- [RunnerCall](#runnercall): `ArgCount`, `Caller`, `Result`, `GetName`, `Arg`, `SetArg`, `ArgRef`,
  `SetArgRef`, `GetArgString`, `SetArgString`, `GetArgArray`, `SetArgArray`, `CallOriginal`
- [Runner: breakpoints](#runner-breakpoints): `BreakAtLine`, `BreakAtFunction`,
  `RemoveBreakpoint`, `BreakpointHits`
- [Breakpoint](#breakpoint): `Id`, `Line`, `Plugin`, `GetFile`, `GetFunction`, `HasVariable`,
  `GetValue`, `SetValue`, `GetString`, `SetString`
- [Runner: plugin variables](#runner-plugin-variables): `GetPluginValue`, `SetPluginValue`,
  `GetPluginString`, `SetPluginString`
- [Assert](#assert): `True`, `False`, `Equal`, `NotEqual`, `FloatEqual`, `StrEqual`, `StrContains`,
  `ConsoleContains`, `ConsoleNotContains`, `ClientReceived`, `ClientMenu`, `Fail`

## Callbacks of the test plugin

### OnTestBoot

```sourcepawn
public void OnTestBoot(const char[] test)
```

Optional. Called with the name of the test about to run, once every plugin is loaded (their
`AskPluginLoad2` has run) and before any of them has started: before any `OnPluginStart`, not even
the test plugin's, and before `OnMapStart`. Not called in run mode.

The place to install mocks, set breakpoints, write files or set cvars that the plugins under test
need when they start. See [OnTestBoot](writing-tests.md#ontestboot-setup-before-plugins-start).

### Test functions

```sourcepawn
public void Test_Something()
```

Every public function without parameters whose name starts with `Test_` is a test. See
[Writing tests](writing-tests.md).

## Types

### RunnerMessage

The kind of a message a simulated client received: `RunnerMessage_Console` (0),
`RunnerMessage_Chat`, `RunnerMessage_Center`, `RunnerMessage_Hint`, `RunnerMessage_Command`,
`RunnerMessage_Kick`, `RunnerMessage_Menu`, `RunnerMessage_UserMessage`, and `RunnerMessage_Any`
(-1) as a filter for any kind. What each kind records is listed in
[What a client received](clients.md#what-a-client-received).

### RunnerCondition

```sourcepawn
typeset RunnerCondition {
	function bool (any data);
	function bool ();
};
```

The condition of [`Runner.WaitUntil`](#runnerwaituntil).

### RunnerNativeHook

```sourcepawn
typeset RunnerNativeHook {
	function Action (RunnerCall call);
	function void (RunnerCall call);
};
```

A pre or post hook of [`Runner.HookNative`](#runnerhooknative). A pre hook returning
`Plugin_Handled` or more skips the native; the call returns `call.Result`. A post hook's return
value is ignored.

### RunnerNativeMock

```sourcepawn
typeset RunnerNativeMock {
	function any (RunnerCall call);
};
```

A mock of [`Runner.MockNative`](#runnermocknative). Its return value is the native's.

### RunnerBreakpoint

```sourcepawn
typeset RunnerBreakpoint {
	function void (Breakpoint frame, any data);
	function void (Breakpoint frame);
};
```

The callback of [`Runner.BreakAtLine`](#runnerbreakatline) and
[`Runner.BreakAtFunction`](#runnerbreakatfunction).

## Runner: time

### Runner.Frames

```sourcepawn
void Runner.Frames(int count)
```

Runs `count` server frames, one tick each. Not inside a frame. A negative count is a native
error.

```sourcepawn
Runner.Frames(1);    // let "next frame" work happen: KickClient, ServerCommand, RequestFrame
```

### Runner.Seconds

```sourcepawn
void Runner.Seconds(float seconds)
```

Runs frames for `seconds` of game time, rounded up to whole frames. Not inside a frame. A
negative value is a native error.

```sourcepawn
Runner.Seconds(60.0);    // a 60-second timer fires
```

### Runner.WaitUntil

```sourcepawn
bool Runner.WaitUntil(RunnerCondition condition, float timeout = 5.0, any data = 0)
```

Runs frames, with one millisecond of real time between them, until `condition` returns true. For
extensions that work on threads. Not inside a frame.

- Returns true when the condition was met.
- Returns false when `timeout` real seconds passed first, or the server quit.
- `data` is passed to the condition.

```sourcepawn
Assert.True(Runner.WaitUntil(HasAnswered, 2.0), "no answer within 2 seconds");
```

## Runner: server

### Runner.Command

```sourcepawn
void Runner.Command(const char[] format, any ...)
```

Runs server console commands now, before it returns, outside of the command buffer. Commands the
line adds while it runs (an `exec`'d file, `ServerCommand`) run at once too, up to a `wait`. What
comes after a `wait` runs with the buffer, on the next frame or `ServerExecute`. Commands buffered
before are left as they are.

```sourcepawn
Runner.Command("sm_greeter_message Hello; echo done");
```

### Runner.CommandOutput

```sourcepawn
int Runner.CommandOutput(char[] output, int maxlength, const char[] format, any ...)
```

Runs server commands as [`Runner.Command`](#runnercommand) does and writes what they printed into
`output`. Returns the number of bytes written.

```sourcepawn
char output[128];
Runner.CommandOutput(output, sizeof(output), "sm_greetcount");   // greetlog.sp
Assert.StrEqual(output, "greetings: 1\n");
```

### Runner.ChangeMap

```sourcepawn
void Runner.ChangeMap(const char[] map)
```

Changes the map now, with `OnMapEnd`, SourceMod's plugin refresh, `OnMapStart` and the new map's
configs. Returns once `OnConfigsExecuted` has run. Not inside a frame.

- Every client disconnects, and the game clock starts again at 0.
- Any map name works; it then also exists for `changelevel` and `IsMapValid`.
- An empty name is a native error.

```sourcepawn
Runner.ChangeMap("koth_viaduct");
```

## Runner: console

### Runner.ConsoleContains

```sourcepawn
bool Runner.ConsoleContains(const char[] text)
```

Returns whether the server console printed `text` (case-sensitive) since the server started or
the last [`Runner.ClearConsole`](#runnerclearconsole).

### Runner.GetConsole

```sourcepawn
int Runner.GetConsole(char[] buffer, int maxlength)
```

Copies what the server console printed since the server started or the last
`Runner.ClearConsole`. When it does not fit, the end of it is copied. Returns the number of bytes
written.

### Runner.ClearConsole

```sourcepawn
void Runner.ClearConsole()
```

Forgets everything the console printed so far. Later console checks see only new output.

## Runner: clients

### Runner.ConnectClient

```sourcepawn
int Runner.ConnectClient(const char[] name, const char[] steamId = "", bool authorize = true,
                         const char[] ip = "127.0.0.1:27005")
```

Connects a client through SourceMod's whole connect sequence and returns its client index.

- `steamId`: `""` for a made-up unique ID, or a Steam2 ID (`"STEAM_0:1:23"`, any universe
  digit), a Steam3 ID (`"[U:1:47]"`) or a 64-bit ID. Anything else is a native error.
- `authorize = true` (the default): returns after `OnClientAuthorized` and
  `OnClientPostAdminCheck`. It runs frames until SourceMod's next authorization check, about 0.7
  seconds of game time. Not inside a frame.
- `authorize = false`: returns after `OnClientPutInServer`. SourceMod authorizes the client
  within a second of game time.
- `ip`: the address with its port, fewer than 64 characters.

Returns 0 when a plugin rejected the client, the server is full, or it was kicked while
connecting. [`Runner.GetRejectReason`](#runnergetrejectreason) tells why.

```sourcepawn
int alice = Runner.ConnectClient("alice", "[U:1:47]");
```

### Runner.ConnectBot

```sourcepawn
int Runner.ConnectBot(const char[] name)
```

Connects a bot, as `CreateFakeClient` does, and returns its client index, or 0.

### Runner.GetRejectReason

```sourcepawn
int Runner.GetRejectReason(char[] buffer, int maxlength)
```

Copies why the last `Runner.ConnectClient` or `Runner.ConnectBot` that returned 0 failed, for
example a plugin's reject message or `Server is full`. Returns the bytes written.

### Runner.Disconnect

```sourcepawn
void Runner.Disconnect(int client, const char[] reason = "Disconnect by user.")
```

Disconnects a client, with `OnClientDisconnect`. A client that is not connected is a native
error.

```sourcepawn
Runner.Disconnect(alice);
Assert.False(IsClientConnected(alice));
```

### Runner.ClientCommand

```sourcepawn
void Runner.ClientCommand(int client, const char[] format, any ...)
```

Runs a console command as typed by the client: split at `;` and line breaks outside quotes and
`//` comments, then each command through SourceMod's client command handling. Unknown commands get
TF2's `Unknown command: <name>` in the client's console. Native errors: a client that is not
connected, a command of 511 characters or more, and a `wait` while `sv_allow_wait_command` is 1.

```sourcepawn
Runner.ClientCommand(alice, "sm_ready");
```

### Runner.SetClientConVar

```sourcepawn
void Runner.SetClientConVar(int client, const char[] name, const char[] value)
```

Sets one of the client's own cvars (its userinfo), such as `name` or `cl_language`.
`GetClientInfo` returns the new value at once. The server takes the change on the next frame:
`OnClientSettingsChanged`, a new name, the admin checks for a new password.

### Runner.SetClientTeam

```sourcepawn
void Runner.SetClientTeam(int client, int team)
```

Sets what `GetClientTeam` returns. No game event fires.

```sourcepawn
Runner.SetClientTeam(alice, 2);    // TF2: 2 is RED, 3 is BLU
```

### Runner.SetClientAlive

```sourcepawn
void Runner.SetClientAlive(int client, bool alive)
```

Sets what `IsPlayerAlive` returns.

### Runner.SetClientHealth

```sourcepawn
void Runner.SetClientHealth(int client, int health)
```

Sets what `GetClientHealth` returns.

### Runner.ClientReceived

```sourcepawn
bool Runner.ClientReceived(int client, RunnerMessage kind, const char[] text)
```

Returns whether the client received a message of `kind` (or of any kind, with
`RunnerMessage_Any`) that contains `text`, either as sent or with its chat color codes removed.
Case-sensitive.

```sourcepawn
Assert.False(Runner.ClientReceived(alice, RunnerMessage_Chat, "The match can start"));
```

### Runner.ClientMessageCount

```sourcepawn
int Runner.ClientMessageCount(int client, RunnerMessage kind = RunnerMessage_Any)
```

Returns how many messages of `kind` the client received.

### Runner.GetClientMessage

```sourcepawn
RunnerMessage Runner.GetClientMessage(int client, int index, char[] buffer, int maxlength)
```

Copies the client's message number `index` (from 0, of any kind, in the order received) as it was
sent, with colors. Returns its kind, or -1 when there is no such message (-1 is also the value of
`RunnerMessage_Any`).

### Runner.ClearClientMessages

```sourcepawn
void Runner.ClearClientMessages(int client)
```

Forgets the client's messages so far.

```sourcepawn
Runner.ClearClientMessages(bob);   // ignore the welcome message
```

## Runner: menus

### Runner.GetClientMenu

```sourcepawn
bool Runner.GetClientMenu(int client, char[] buffer, int maxlength)
```

Copies the text of the radio menu the client has open, as the client shows it (`->1. Item` for a
key it takes, `1. Item` for a disabled item). Returns false, with an empty buffer, when the client
has no menu open.

```sourcepawn
char menu[256];
Assert.False(Runner.GetClientMenu(bob, menu, sizeof(menu)), "the panel closed");
```

### Runner.ClientMenuKeys

```sourcepawn
int Runner.ClientMenuKeys(int client)
```

Returns the keys the client's menu takes: bit 0 for key 1 up to bit 8 for key 9, and bit 9 for
key 0. Returns 0 when the client has no menu open.

```sourcepawn
Assert.Equal(Runner.ClientMenuKeys(alice), (1 << 0) | (1 << 9), "keys 1 and 0");
```

### Runner.PrintClientMenu

```sourcepawn
void Runner.PrintClientMenu(int client)
```

Draws the client's menu in a box for people to read: below the test's line in the report, or on
standard output in run mode. Colors follow `--color`. The box drops the `->` markers and adds
`(disabled)`; assertions match the text of `Runner.GetClientMenu` instead.

### Runner.SelectMenu

```sourcepawn
void Runner.SelectMenu(int client, int key)
```

The client presses a menu key, sending `menuselect <key>`: 1 to 9, and 10 for key 0. Without a
menu open, the command goes on as any client command. A client that is not connected is a native
error.

```sourcepawn
Runner.SelectMenu(player, 10);     // key 0
```

### Runner.SelectMenuItem

```sourcepawn
void Runner.SelectMenuItem(int client, const char[] text)
```

The client presses the key of the one item it can select whose label contains `text`. Fails the
test when the client has no menu open, when no selectable item matches, or when several do.

```sourcepawn
Runner.SelectMenuItem(alice, "koth_viaduct");
Runner.SelectMenuItem(alice, "Next");
```

## Runner: the test

### Runner.ExpectError

```sourcepawn
void Runner.ExpectError(const char[] text)
```

Needs a test. Expects one plugin error containing `text`, matched against
`<plugin file>: <message>`. That error does not fail the test; the test fails if it does not
occur. Call it once per expected error.

```sourcepawn
Runner.ExpectError("Divide by zero");
Runner.Command("sm_share 0");
```

### Runner.Skip

```sourcepawn
void Runner.Skip(const char[] reason, any ...)
```

Needs a test. Marks the test as skipped, with the formatted reason, and stops the running
function of the test plugin. Called from a callback, it stops only that callback. A test with a
failure is reported as failed, not skipped.

```sourcepawn
Runner.Skip("needs --steamworks");
```

### Runner.GetTestName

```sourcepawn
int Runner.GetTestName(char[] buffer, int maxlength)
```

Needs a test. Copies the running test's name. Returns the bytes written.

```sourcepawn
char test[64];
Runner.GetTestName(test, sizeof(test));
```

## Runner: natives

### Runner.MockNative

```sourcepawn
void Runner.MockNative(const char[] name, RunnerNativeMock mock)
```

Needs a test. Replaces the native `name` in every plugin under test. The mock's return value is the
native's result.

- Also applies to plugins loaded later.
- The test plugin keeps the real native.
- Replaces earlier hooks and mocks of the native.

```sourcepawn
Runner.MockNative("GetClientName", Mock_GetClientName);
```

### Runner.HookNative

```sourcepawn
void Runner.HookNative(const char[] name, RunnerNativeHook pre, RunnerNativeHook post = INVALID_FUNCTION)
```

Needs a test. Hooks the native `name` in every plugin under test.

- `pre` runs before the native; returning `Plugin_Handled` or more skips it.
- `post` runs after the native, also when `pre` handled the call, and may change
  `RunnerCall.Result`. It does not run when the real native threw an error.
- One of them may be `INVALID_FUNCTION`, not both.
- Also applies to plugins loaded later. The test plugin keeps the real native.
- Replaces earlier hooks and mocks of the native.

```sourcepawn
Runner.HookNative("KickClient", Pre_KickClient);
Runner.HookNative("IsFakeClient", INVALID_FUNCTION, Post_IsFakeClient);
```

### Runner.UnhookNative

```sourcepawn
void Runner.UnhookNative(const char[] name)
```

Needs a test. Removes the hooks or the mock of a native; plugins get the real one back. See the
[known issue](limits.md#known-issues) about plugins loaded after the unhook.

### Runner.NativeCalls

```sourcepawn
int Runner.NativeCalls(const char[] name)
```

Needs a test. Returns how often plugins under test called the hooked or mocked native since it
was first hooked. After `Runner.UnhookNative` the count stays, and calls of the plugins that were
loaded are no longer counted.

```sourcepawn
Assert.Equal(Runner.NativeCalls("KickClient"), 1);
```

## RunnerCall

One call of a hooked or mocked native, given to the hook or mock callback. Valid only while that
callback runs; using it later is a native error. Arguments count from 1.

### RunnerCall.ArgCount

```sourcepawn
property int ArgCount
```

The number of arguments the plugin passed.

```sourcepawn
PrintToServer("called with %d arguments", call.ArgCount);
```

### RunnerCall.Caller

```sourcepawn
property Handle Caller
```

The handle of the calling plugin.

```sourcepawn
char plugin[64];
GetPluginFilename(call.Caller, plugin, sizeof(plugin));
```

### RunnerCall.Result

```sourcepawn
property any Result
```

What the call returns. In a post hook, the native's result, which the hook may change. In a pre
hook that handles the call, what the call returns (0 unless set).

### RunnerCall.GetName

```sourcepawn
void GetName(char[] buffer, int maxlength)
```

Copies the native's name.

### RunnerCall.Arg

```sourcepawn
any Arg(int n)
```

Argument `n` as passed: a cell, or for arrays and references their address in the caller.

### RunnerCall.SetArg

```sourcepawn
void SetArg(int n, any value)
```

Changes argument `n` for the real native, in a pre hook or before `CallOriginal`.

```sourcepawn
call.SetArg(1, g_spectator);       // PrintToChat goes to another client
```

### RunnerCall.ArgRef

```sourcepawn
any ArgRef(int n)
```

The value a by-reference argument (`int &value`, `float &value`) refers to.

### RunnerCall.SetArgRef

```sourcepawn
void SetArgRef(int n, any value)
```

Writes the value a by-reference argument refers to.

```sourcepawn
call.SetArgRef(1, 300);            // GetMapTimeLeft(int &timeleft): 300 seconds
```

### RunnerCall.GetArgString

```sourcepawn
int GetArgString(int n, char[] buffer, int maxlength)
```

Copies string argument `n`. Returns the bytes written.

### RunnerCall.SetArgString

```sourcepawn
void SetArgString(int n, const char[] value, int maxlength)
```

Writes `value` into the caller's string buffer that argument `n` names. The buffer must hold
`maxlength` bytes; usually the native's next argument says how many (`call.Arg(n + 1)`).

### RunnerCall.GetArgArray

```sourcepawn
void GetArgArray(int n, any[] values, int size)
```

Copies `size` cells of array argument `n` into `values`.

```sourcepawn
float origin[3];
call.GetArgArray(2, origin, 3);
```

### RunnerCall.SetArgArray

```sourcepawn
void SetArgArray(int n, const any[] values, int size)
```

Writes `size` cells from `values` into array argument `n`.

```sourcepawn
float angles[3] = {45.0, 90.0, 0.0};
call.SetArgArray(2, angles, 3);    // GetClientEyeAngles(int client, float ang[3])
```

### RunnerCall.CallOriginal

```sourcepawn
any CallOriginal()
```

From a mock: runs the real native with the arguments as they are now and returns its result. When
the real native throws, returns 0 and the error reaches the calling plugin.

## Runner: breakpoints

### Runner.BreakAtLine

```sourcepawn
int Runner.BreakAtLine(const char[] file, int line, RunnerBreakpoint callback, any data = 0,
                       const char[] plugin = "")
```

Needs a test. Stops at the first statement at or after `line` of `file`. Returns the breakpoint's
id.

- `file` is the source file's name or the end of its path, as compiled.
- `plugin` limits the breakpoint to the plugin of that file name.
- Fails the test when no loaded plugin under test has a statement there.
- See the [known issues](limits.md#known-issues) about breakpoints.

```sourcepawn
Runner.BreakAtLine("greetlog.sp", 25, Break_Message);
```

### Runner.BreakAtFunction

```sourcepawn
int Runner.BreakAtFunction(const char[] functionName, RunnerBreakpoint callback, any data = 0,
                           const char[] plugin = "")
```

Needs a test. Stops each time a plugin under test (or only `plugin`) enters the function, before
its first statement. Returns the breakpoint's id. Fails the test when no loaded plugin under test
has a function of that name.

```sourcepawn
int id = Runner.BreakAtFunction("Greet", Break_Greet, 1);
```

### Runner.RemoveBreakpoint

```sourcepawn
void Runner.RemoveBreakpoint(int id)
```

Needs a test. Removes a breakpoint. An unknown id is a native error.

```sourcepawn
Runner.RemoveBreakpoint(id);
```

### Runner.BreakpointHits

```sourcepawn
int Runner.BreakpointHits(int id)
```

Needs a test. Returns how often plugins reached the breakpoint. An unknown id is a native error.

```sourcepawn
Assert.Equal(Runner.BreakpointHits(id), 1);
```

## Breakpoint

A plugin stopped at a breakpoint, given to the breakpoint's callback. Valid only while the
callback runs. Variables are looked up by name as the plugin sees them at that statement: the
function's locals and arguments first, then the plugin's globals.

### Breakpoint.Id

```sourcepawn
property int Id
```

The breakpoint's id.

### Breakpoint.Line

```sourcepawn
property int Line
```

The line the plugin stopped at.

### Breakpoint.Plugin

```sourcepawn
property Handle Plugin
```

The handle of the stopped plugin.

### Breakpoint.GetFile

```sourcepawn
void GetFile(char[] buffer, int maxlength)
```

Copies the source file the plugin stopped in, as compiled.

```sourcepawn
char file[64];
frame.GetFile(file, sizeof(file));
```

### Breakpoint.GetFunction

```sourcepawn
void GetFunction(char[] buffer, int maxlength)
```

Copies the name of the function the plugin stopped in.

```sourcepawn
char name[64];
frame.GetFunction(name, sizeof(name));
```

### Breakpoint.HasVariable

```sourcepawn
bool HasVariable(const char[] name)
```

Returns whether a variable of that name is visible at the statement.

```sourcepawn
Assert.True(frame.HasVariable("message"));
```

### Breakpoint.GetValue

```sourcepawn
any GetValue(const char[] name, int index = 0)
```

Reads a cell variable (int, float, bool, handle, enum), or cell `index` of a one-dimensional
array. An unknown name or an index out of bounds is a native error.

### Breakpoint.SetValue

```sourcepawn
void SetValue(const char[] name, any value, int index = 0)
```

Writes a cell variable, or cell `index` of an array. The plugin sees the change when it goes on.

```sourcepawn
frame.SetValue("g_greetings", 41);
```

### Breakpoint.GetString

```sourcepawn
int GetString(const char[] name, char[] buffer, int maxlength)
```

Copies a string variable. Returns the bytes written.

```sourcepawn
char message[64];
frame.GetString("message", message, sizeof(message));
```

### Breakpoint.SetString

```sourcepawn
void SetString(const char[] name, const char[] value)
```

Writes a string variable. Text longer than the variable's array is truncated.

```sourcepawn
frame.SetString("message", "Howdy");
```

## Runner: plugin variables

These work on any plugin, at any time, even in run mode. `plugin` is a plugin handle, for example
from `FindPluginByFile`. An invalid handle or unknown name is a native error.

### Runner.GetPluginValue

```sourcepawn
any Runner.GetPluginValue(Handle plugin, const char[] name, int index = 0)
```

Reads a global cell variable (int, float, bool, handle), or cell `index` of a global array.

```sourcepawn
Assert.Equal(Runner.GetPluginValue(FindPluginByFile("greetlog.smx"), "g_greetings"), 1);
```

### Runner.SetPluginValue

```sourcepawn
void Runner.SetPluginValue(Handle plugin, const char[] name, any value, int index = 0)
```

Writes a global cell variable, or cell `index` of a global array.

```sourcepawn
Runner.SetPluginValue(FindPluginByFile("greetlog.smx"), "g_greetings", 100);
```

### Runner.GetPluginString

```sourcepawn
int Runner.GetPluginString(Handle plugin, const char[] name, char[] buffer, int maxlength)
```

Copies a global string variable. Returns the bytes written.

```sourcepawn
char name[64];
Runner.GetPluginString(FindPluginByFile("greetlog.smx"), "g_lastGreeted", name, sizeof(name));
```

### Runner.SetPluginString

```sourcepawn
void Runner.SetPluginString(Handle plugin, const char[] name, const char[] value)
```

Writes a global string variable. Text longer than its array is truncated.

```sourcepawn
Runner.SetPluginString(FindPluginByFile("greetlog.smx"), "g_lastGreeted", "nobody");
```

## Assert

Every assertion takes an optional message at the end, with format arguments like `Format`. A
failed assertion records the failure with the test plugin's file and line, then stops the running
function of the test plugin: the test function, or the callback it failed in. See
[Assertions](writing-tests.md#assertions).

### Assert.True

```sourcepawn
void Assert.True(bool condition, const char[] message = "", any ...)
```

Fails unless `condition` is true.

### Assert.False

```sourcepawn
void Assert.False(bool condition, const char[] message = "", any ...)
```

Fails unless `condition` is false.

### Assert.Equal

```sourcepawn
void Assert.Equal(any actual, any expected, const char[] message = "", any ...)
```

Fails unless the two cells are equal: ints, bools, handles, enums. For floats use
[`Assert.FloatEqual`](#assertfloatequal). Failure: `Assert.Equal failed: expected 5, got 4`.

### Assert.NotEqual

```sourcepawn
void Assert.NotEqual(any actual, any unexpected, const char[] message = "", any ...)
```

Fails when the two cells are equal.

### Assert.FloatEqual

```sourcepawn
void Assert.FloatEqual(float actual, float expected, float tolerance = 0.0001, const char[] message = "", any ...)
```

Fails unless `abs(actual - expected) <= tolerance`.

### Assert.StrEqual

```sourcepawn
void Assert.StrEqual(const char[] actual, const char[] expected, const char[] message = "", any ...)
```

Fails unless the strings are equal, case-sensitively.

### Assert.StrContains

```sourcepawn
void Assert.StrContains(const char[] text, const char[] part, const char[] message = "", any ...)
```

Fails unless `text` contains `part`, case-sensitively.

### Assert.ConsoleContains

```sourcepawn
void Assert.ConsoleContains(const char[] text, const char[] message = "", any ...)
```

Fails unless the server console printed `text` since the server started or the last
`Runner.ClearConsole`.

### Assert.ConsoleNotContains

```sourcepawn
void Assert.ConsoleNotContains(const char[] text, const char[] message = "", any ...)
```

Fails when the server console printed `text` since the server started or the last
`Runner.ClearConsole`.

### Assert.ClientReceived

```sourcepawn
void Assert.ClientReceived(int client, RunnerMessage kind, const char[] text, const char[] message = "", any ...)
```

Fails unless the client received a message of `kind` that contains `text`, as sent or with its
chat color codes removed. See [Runner.ClientReceived](#runnerclientreceived).

### Assert.ClientMenu

```sourcepawn
void Assert.ClientMenu(int client, const char[] text, const char[] message = "", any ...)
```

Fails unless the client has a menu open whose text contains `text`. The failure draws the menu.

```sourcepawn
Assert.ClientMenu(alice, "->1. Apple");
```

### Assert.Fail

```sourcepawn
void Assert.Fail(const char[] message, any ...)
```

Always fails, with the message.

```sourcepawn
Assert.Fail("the timer fired although the round was cancelled");
```
