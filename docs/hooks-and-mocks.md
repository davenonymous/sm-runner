# Hooks and mocks

A plugin under test often calls natives whose results a test wants to control: a native that the
simulated server cannot provide, one that depends on something outside the test, or one whose
failure path should be tested. smrunner can hook any native to watch or change its calls, or mock
it to replace it entirely.

- [Hook or mock?](#hook-or-mock)
- [Mocking a native](#mocking-a-native)
- [Calling the real native from a mock](#calling-the-real-native-from-a-mock)
- [Hooking a native](#hooking-a-native)
- [Reading and changing arguments](#reading-and-changing-arguments)
- [Counting calls](#counting-calls)
- [Removing a hook or mock](#removing-a-hook-or-mock)
- [Installing mocks before the plugin starts](#installing-mocks-before-the-plugin-starts)
- [Providing a native nobody provides](#providing-a-native-nobody-provides)
- [Rules and limits](#rules-and-limits)

## Hook or mock?

Hook a native to observe or adjust its calls. Mock it to replace what it does.

| | `Runner.HookNative` | `Runner.MockNative` |
|---|---|---|
| The real native runs | yes, unless the pre hook handles the call | no, unless the mock calls `CallOriginal()` |
| Callbacks | a pre hook before, a post hook after | one mock instead |
| Use it to | watch calls, change arguments or results, skip some calls | replace the native's behavior |

Both affect every plugin under test, including stock plugins loaded with `--plugin`. They also
affect plugins that load after the hook was installed, for example with `sm plugins load`. They never
affect the test plugin: its own calls always reach the real native. Both need a test; in
[run mode](run-mode.md) they throw a native error.

## Mocking a native

`Runner.MockNative(name, mock)` replaces a native. The mock gets the call as a `RunnerCall`, and
what it returns is what the native returns:

```sourcepawn
// nametag.sp greets each player in chat with "Hello, <name>!", reading the
// name with GetClientName(int client, char[] name, int maxlen).
any Mock_GetClientName(RunnerCall call) {
	call.SetArgString(2, "Mocked", call.Arg(3));
	return true;
}

public void Test_GreetingUsesTheName() {
	Runner.MockNative("GetClientName", Mock_GetClientName);

	int alice = Runner.ConnectClient("alice");

	Assert.ClientReceived(alice, RunnerMessage_Chat, "Hello, Mocked!");
}
```

The test plugin is not affected by the mock. Here, `GetClientName(alice, ...)` called in the test
still returns `alice`.

## Calling the real native from a mock

`RunnerCall.CallOriginal()` runs the real native with the call's arguments, as they are at that
moment, and returns its result. A mock can use it to change only part of the behavior:

```sourcepawn
any Mock_TagTheName(RunnerCall call) {
	any result = call.CallOriginal();          // fills the name buffer

	char name[64];
	call.GetArgString(2, name, sizeof(name));
	StrCat(name, sizeof(name), " (mocked)");
	call.SetArgString(2, name, call.Arg(3));
	return result;
}

public void Test_MockCallsTheOriginal() {
	Runner.MockNative("GetClientName", Mock_TagTheName);
	int alice = Runner.ConnectClient("alice");
	Assert.ClientReceived(alice, RunnerMessage_Chat, "Hello, alice (mocked)!");
}
```

When the real native throws an error, `CallOriginal` returns 0 and the error reaches the calling
plugin as usual.

## Hooking a native

`Runner.HookNative(name, pre, post = INVALID_FUNCTION)` hooks a native. Either callback may be
`INVALID_FUNCTION`, but not both.

- **The pre hook** runs before the native and returns an `Action`. `Plugin_Continue` lets the
  native run. `Plugin_Handled` or `Plugin_Stop` skips it; the call then returns
  `RunnerCall.Result`, which is 0 unless the hook set it.
- **The post hook** runs after the native, or after a pre hook that handled the call. It sees the
  result in `RunnerCall.Result` and may change it. Its return value is ignored. It does not run
  when the real native threw an error.

A pre hook that keeps players on the server:

```sourcepawn
// closeserver.sp: the admin command sm_kickall (kick flag) calls
// KickClient(i, "Server is closing") for everyone else.
Action Pre_KickClient(RunnerCall call) {
	char reason[64];
	call.GetArgString(2, reason, sizeof(reason));
	Assert.StrEqual(reason, "Server is closing");
	return Plugin_Handled;                     // nobody is kicked
}

public void Test_KickAllUsesTheRightReason() {
	Runner.HookNative("KickClient", Pre_KickClient);
	int alice = Runner.ConnectClient("alice");
	int admin = Runner.ConnectClient("admin");
	SetUserFlagBits(admin, ADMFLAG_KICK);

	Runner.ClientCommand(admin, "sm_kickall");
	Runner.Frames(1);                          // a kick would happen now

	Assert.True(IsClientConnected(alice));
	Assert.Equal(Runner.NativeCalls("KickClient"), 1);
}
```

A post hook that changes the result:

```sourcepawn
// greeter.sp (see the README) greets every player who is not a bot.
void Post_IsFakeClient(RunnerCall call) {
	call.Result = true;                        // everyone looks like a bot
}

public void Test_NobodyIsGreetedWhenEveryoneIsABot() {
	Runner.HookNative("IsFakeClient", INVALID_FUNCTION, Post_IsFakeClient);
	int alice = Runner.ConnectClient("alice");
	Assert.Equal(Runner.ClientMessageCount(alice, RunnerMessage_Chat), 0);
}
```

An assertion that fails in a callback fails the test and stops only that callback (see
[What a failed assertion stops](writing-tests.md#what-a-failed-assertion-stops)).

## Reading and changing arguments

Every callback gets a `RunnerCall`, named `call` in the examples. Arguments count from 1, as in
the native's declaration. A `RunnerCall` is valid only while its callback runs; using it later is
a native error.

| Member | Does |
|---|---|
| `RunnerCall.ArgCount` | The number of arguments the plugin passed. |
| `RunnerCall.Caller` | The handle of the calling plugin. |
| `RunnerCall.Result` | The call's result. Read it in a post hook, set it in a pre hook that handles the call. |
| `RunnerCall.GetName(buffer, maxlength)` | The native's name. |
| `RunnerCall.Arg(n)` | Argument `n` as a cell. For arrays and references, their address in the caller. |
| `RunnerCall.SetArg(n, value)` | Changes a cell argument for the real native (in a pre hook, or before `CallOriginal`). |
| `RunnerCall.ArgRef(n)`, `RunnerCall.SetArgRef(n, value)` | Read or write a by-reference argument (`int &value`). |
| `RunnerCall.GetArgString(n, buffer, maxlength)` | Reads a string argument. |
| `RunnerCall.SetArgString(n, value, maxlength)` | Writes into the caller's string buffer of `maxlength` bytes. |
| `RunnerCall.GetArgArray(n, values, size)`, `RunnerCall.SetArgArray(n, values, size)` | Read or write `size` cells of an array argument. |

`SetArgString` needs the size of the caller's buffer. Most natives receive it as the next
argument, so `call.Arg(n + 1)` is the usual value. Every access is checked against the caller's
memory; one outside of it is a native error instead of a crash.

**Changing a cell argument.** A pre hook can change an argument before the real native runs.
Here it redirects `PrintToChat` from one client to another:

```sourcepawn
// greeter.sp calls PrintToChat(client, ...).
int g_spectator;

Action Pre_PrintToChat(RunnerCall call) {
	call.SetArg(1, g_spectator);               // the spectator gets every chat line
	return Plugin_Continue;
}

public void Test_GreetingCanBeRedirected() {
	g_spectator = Runner.ConnectClient("spectator");
	Runner.HookNative("PrintToChat", Pre_PrintToChat);

	int alice = Runner.ConnectClient("alice");

	Assert.ClientReceived(g_spectator, RunnerMessage_Chat, "Welcome, alice!");
	Assert.Equal(Runner.ClientMessageCount(alice, RunnerMessage_Chat), 0);
}
```

**By-reference arguments.** `GetMapTimeLeft(int &timeleft)` writes through a reference:

```sourcepawn
// Five minutes are left on the map, whatever mp_timelimit says.
any Mock_GetMapTimeLeft(RunnerCall call) {
	call.SetArgRef(1, 300);
	return true;
}
```

**Arrays.** `GetClientEyeAngles(int client, float ang[3])` fills an array:

```sourcepawn
any Mock_GetClientEyeAngles(RunnerCall call) {
	float angles[3] = {45.0, 90.0, 0.0};
	call.SetArgArray(2, angles, 3);
	return 0;
}
```

**Who called, and how.** `ArgCount`, `Caller` and `GetName` help when one callback serves several
natives or plugins:

```sourcepawn
void Post_Log(RunnerCall call) {
	char name[64], plugin[64];
	call.GetName(name, sizeof(name));
	GetPluginFilename(call.Caller, plugin, sizeof(plugin));
	PrintToServer("%s called %s with %d arguments", plugin, name, call.ArgCount);
}
```

## Counting calls

`Runner.NativeCalls(name)` returns how often plugins under test called a hooked or mocked native
since the test hooked it. To count the calls of a native without changing anything, hook it with
a post hook that does nothing:

```sourcepawn
void Post_Nothing(RunnerCall call) {}

// greeter.sp greets each player once, with one PrintToChat.
public void Test_EachPlayerIsGreetedOnce() {
	Runner.HookNative("PrintToChat", INVALID_FUNCTION, Post_Nothing);
	Runner.ConnectClient("alice");
	Assert.Equal(Runner.NativeCalls("PrintToChat"), 1);
}
```

## Removing a hook or mock

`Runner.UnhookNative(name)` removes the hooks or the mock of a native. Plugins get the real native
back, and their calls are no longer counted. `Runner.NativeCalls` keeps the count it had.

```sourcepawn
Runner.HookNative("IsFakeClient", INVALID_FUNCTION, Post_IsFakeClient);
int alice = Runner.ConnectClient("alice");      // looks like a bot: not greeted
Runner.UnhookNative("IsFakeClient");
int bob = Runner.ConnectClient("bob");          // greeted again
```

A native has at most one set of callbacks at a time. `Runner.HookNative` replaces earlier hooks
and a mock of the same native, and `Runner.MockNative` replaces earlier hooks and mocks.

## Installing mocks before the plugin starts

A hook installed in the test function comes too late for calls in `OnPluginStart` or
`OnMapStart`: they have already happened when the test function starts. Install it in
`OnTestBoot` instead, which runs before the plugins under test start (see
[Writing tests](writing-tests.md#ontestboot-setup-before-plugins-start)):

```sourcepawn
// rules.sp prints "[Rules] Now playing <map>. <motd>" in OnMapStart, with the
// map from GetCurrentMap.
any Mock_GetCurrentMap(RunnerCall call) {
	call.SetArgString(1, "pl_upward", call.Arg(2));
	return 0;
}

public void OnTestBoot(const char[] test) {
	if (StrEqual(test, "Test_MapStartAnnouncesTheMap")) {
		Runner.MockNative("GetCurrentMap", Mock_GetCurrentMap);
	}
}

public void Test_MapStartAnnouncesTheMap() {
	Assert.ConsoleContains("[Rules] Now playing pl_upward.");
}
```

A mock of a native the plugin calls later, for example at connect like `Mock_GetClientName`
above, can be installed in the test function.

## Providing a native nobody provides

A plugin may need a native from an extension or plugin that smrunner cannot load, for example
SDKTools or another plugin of your project. The test plugin can create it in `AskPluginLoad2`, as
any plugin would. The test plugin loads first, so the native is there when the plugin under test
loads:

```sourcepawn
// kills.sp declares native int Stats_GetKills(int client), which another
// plugin of the project provides, and replies "You have <n> kills." to sm_kills.
public APLRes AskPluginLoad2(Handle myself, bool late, char[] error, int err_max) {
	CreateNative("Stats_GetKills", Native_GetKills);
	return APLRes_Success;
}

any Native_GetKills(Handle plugin, int params) {
	return 7;
}

public void Test_KillsCommand() {
	int alice = Runner.ConnectClient("alice");
	Runner.ClientCommand(alice, "sm_kills");
	Assert.ClientReceived(alice, RunnerMessage_Console, "You have 7 kills.");
}
```

Natives created this way can be hooked and mocked like any other, so one test can still change
what they return.

The same works for natives of an extension that does not load in smrunner, such as SDKTools,
with one condition: the plugin under test must not *require* the extension. A plugin that
includes `<sdktools>` normally requires it and then fails to load. Compile it with the extension
optional:

```sourcepawn
// eyes.sp
#undef REQUIRE_EXTENSIONS
#include <sdktools>
```

```sourcepawn
// The test plugin provides the one SDKTools native eyes.sp calls.
public APLRes AskPluginLoad2(Handle myself, bool late, char[] error, int err_max) {
	CreateNative("GetClientEyeAngles", Native_GetClientEyeAngles);
	return APLRes_Success;
}

any Native_GetClientEyeAngles(Handle plugin, int params) {
	float angles[3] = {12.0, 0.0, 0.0};
	SetNativeArray(2, angles, 3);
	return 0;
}
```

See [Limits](limits.md#extensions) for which extensions load.

## Rules and limits

- **Any native** can be hooked or mocked: SourceMod's core natives, natives of extensions, and
  natives that plugins create with `CreateNative`.
- **Stocks cannot.** A stock such as `GetCmdArgInt` is compiled into the plugin. Hook the native
  it calls instead (`GetCmdArg`).
- **The name must match exactly.** A hook on a name that no plugin under test uses does nothing,
  without an error. If a hook seems to have no effect, check the name and `Runner.NativeCalls`.
- **Hooks follow rebinding.** When the plugin providing a hooked native reloads, or an optional
  native gets bound because its provider loaded, the new binding is hooked at once.
- **Natives the simulated server does not have** (for example `GetEntProp`) throw
  `Native "GetEntProp" is not supported by smrunner (the simulated server has no engine for it)`.
  A mock replaces that error with whatever the test wants the native to return.
- **Known issues** with hooks are listed in [Limits](limits.md#known-issues).
