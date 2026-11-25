# Writing tests

A test checks one behavior of a plugin on a fresh simulated server. This page covers how a test
plugin is structured, what happens before and after each test, when a test passes, and every
assertion.

- [Terms](#terms)
- [The test plugin](#the-test-plugin)
- [What happens in a test](#what-happens-in-a-test)
- [OnTestBoot: setup before plugins start](#ontestboot-setup-before-plugins-start)
- [When a test passes](#when-a-test-passes)
- [Assertions](#assertions)
- [Expected errors](#expected-errors)
- [Skipping a test](#skipping-a-test)
- [The test's name](#the-tests-name)
- [Running tests](#running-tests)
- [Reading a failure](#reading-a-failure)
- [Tips for good tests](#tips-for-good-tests)

## Terms

These words mean the same thing on every page of this documentation:

- **Test plugin**: a plugin that includes `smrunner.inc` and holds tests. It is the plugin file
  given to `smrunner test` as a plain argument.
- **Plugin under test**: every other plugin of the server, that is, every plugin given with
  `--plugin`, including SourceMod stock plugins loaded by name.
- **Test**: a public function of a test plugin named `Test_...`.
- **Native error**: an error thrown by a native, for example `ThrowNativeError` or a native that
  smrunner refuses. It becomes a **plugin error** of the plugin that called the native, like any
  other runtime error (array index out of bounds, divide by zero).
- **Sandbox**: the server folder smrunner builds for each test. See
  [The sandbox](command-line.md#the-sandbox).
- **Frame**: one server tick. See [The simulated clock](server-and-time.md#the-simulated-clock).
- **Game folder**: the `tf/` folder inside the sandbox. Relative paths in plugins (`OpenFile`,
  `exec`) and in `--file DEST` start there, as on a TF2 server.

## The test plugin

A test plugin is an ordinary SourceMod plugin that includes `smrunner.inc`. Every public function
whose name starts with `Test_` and that takes no parameters is a test:

```sourcepawn
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <smrunner>

public void Test_NewPlayersAreGreeted() {
	int alice = Runner.ConnectClient("alice");
	Assert.ClientReceived(alice, RunnerMessage_Chat, "Welcome, alice!");
}
```

The test plugin is never treated as a plugin under test. Native hooks, mocks and breakpoints skip
it, so its own calls always reach the real natives. Functions that are not public, or whose names
do not start with `Test_`, are not tests. Use them as helpers.

A project can have several test plugins, for example one per feature. Pass all of them to one
`smrunner test` call. Each test loads only its own test plugin, never the others.

## What happens in a test

Every test runs on a new server, in a process of its own. Nothing carries over from one test to
the next: not cvars, not clients, not files, not plugin state.

For each test, smrunner:

1. Builds the sandbox with SourceMod's configs and the given plugins and extensions.
2. Loads the test plugin, then the plugins under test in the order of `--plugin`. Each plugin's
   `AskPluginLoad2` runs here. Natives that one plugin creates are available to all of them, as
   on a server.
3. Calls the test plugin's `OnTestBoot`, if it has one (see below).
4. Starts every plugin (`OnPluginStart`), then starts the map: `cp_badlands`, unless `--map`
   says otherwise. `OnMapStart` runs and the configs are executed.
5. Calls the test function once `OnConfigsExecuted` has fired. The test drives the server and
   asserts.
6. Shuts the server down: `OnMapEnd`, `OnPluginEnd`, extensions unload.
7. Removes the sandbox, unless `--keep-sandbox` or `--sandbox` is given.

A plugin under test that fails to load fails the test. Because each test has its own process, its
`AskPluginLoad2` runs once per test, and its failure is logged once per test.

## OnTestBoot: setup before plugins start

Some setup has to happen before the plugins under test start: a mock of a native they call in
`OnPluginStart` or `OnMapStart`, a file they read on start, or a breakpoint in their start code.
By the time the test function runs, that code has already run. Put such setup in `OnTestBoot`:

```sourcepawn
// rules.sp reads the "motd" key of addons/sourcemod/configs/rules.cfg in
// OnPluginStart. In OnMapStart it prints "[Rules] Now playing <map>. <motd>",
// with the map from GetCurrentMap.

any Mock_GetCurrentMap(RunnerCall call) {
	call.SetArgString(1, "pl_upward", call.Arg(2));
	return 0;
}

public void OnTestBoot(const char[] test) {
	// Every test: the config file the plugin reads on start.
	File file = OpenFile("addons/sourcemod/configs/rules.cfg", "w");
	file.WriteLine("\"Rules\" { \"motd\" \"Be nice\" }");
	delete file;

	// One test only: a mock that must be in place before OnMapStart runs.
	if (StrEqual(test, "Test_MapStartAnnouncesTheMap")) {
		Runner.MockNative("GetCurrentMap", Mock_GetCurrentMap);
	}
}

public void Test_MapStartAnnouncesTheMap() {
	Assert.ConsoleContains("[Rules] Now playing pl_upward. Be nice");
}
```

smrunner calls `OnTestBoot` with the name of the test that is about to run. At that point every
plugin is loaded (step 2 above), but none has started: no `OnPluginStart` has run, not even the
test plugin's, and no `OnMapStart`. Relative paths are relative to the game folder
(`tf/`), as for any plugin. `OnTestBoot` is not called in [run mode](run-mode.md).

## When a test passes

A test passes when the test function returns and all of the following hold:

- No assertion failed, neither in the test function nor in a callback.
- No plugin error occurred that was not expected. This covers errors in the plugins under test
  and in the test plugin itself, for example an array index out of bounds or a native error.
- Every error announced with `Runner.ExpectError` occurred.
- Every plugin under test loaded and is running.

A test that does not finish within `--timeout` seconds (default 30) fails as `TIMEOUT`. A test
whose process dies fails as `CRASH`.

`LogError` and other entries of SourceMod's error log do not fail a test on their own. In
[run mode](run-mode.md) they do.

## Assertions

Assertions are static methods of `Assert`. Each one takes an optional message at the end, with
format arguments like `Format`:

```sourcepawn
Assert.Equal(count, 3, "expected three players after %s joined", name);
```

| Assertion | Fails unless |
|---|---|
| `Assert.True(condition)` | `condition` is true |
| `Assert.False(condition)` | `condition` is false |
| `Assert.Equal(actual, expected)` | the two cells are equal (ints, bools, handles, enums) |
| `Assert.NotEqual(actual, unexpected)` | the two cells differ |
| `Assert.FloatEqual(actual, expected, tolerance = 0.0001)` | `abs(actual - expected) <= tolerance` |
| `Assert.StrEqual(actual, expected)` | the strings are equal, case-sensitively |
| `Assert.StrContains(text, part)` | `text` contains `part`, case-sensitively |
| `Assert.ConsoleContains(text)` | the server console printed `text` |
| `Assert.ConsoleNotContains(text)` | the server console did not print `text` |
| `Assert.ClientReceived(client, kind, text)` | the client received a message of `kind` containing `text` |
| `Assert.ClientMenu(client, text)` | the client has a menu open that contains `text` |
| `Assert.Fail(message)` | never: it always fails |

The console assertions look at everything printed since the server started or since the last
`Runner.ClearConsole()`. `Assert.ClientReceived` is explained in [Clients](clients.md),
`Assert.ClientMenu` in [Menus](menus.md). The exact failure messages are listed in the
[API reference](api-reference.md#assert).

An example of each:

```sourcepawn
public void Test_EveryAssertion() {
	Assert.True(IsServerProcessing());
	Assert.False(IsVoteInProgress(), "no vote at the start");
	Assert.Equal(GetMaxHumanPlayers(), 24);
	Assert.NotEqual(FindPluginByFile("greeter.smx"), INVALID_HANDLE);
	Assert.FloatEqual(GetTickInterval(), 0.015);

	char map[64];
	GetCurrentMap(map, sizeof(map));
	Assert.StrEqual(map, "cp_badlands");
	Assert.StrContains(map, "badlands");

	Runner.Command("echo hello");
	Assert.ConsoleContains("hello");
	Assert.ConsoleNotContains("goodbye");
}
```

`Assert.Fail` is for paths a test should never reach, for example a callback that must not run:

```sourcepawn
Action Timer_MustNotFire(Handle timer) {
	Assert.Fail("the timer fired although the round was cancelled");
	return Plugin_Stop;
}
```

### What a failed assertion stops

A failed assertion records the failure with the test plugin's file and line. Then it stops the
function of the test plugin that is running:

- **In the test function**, the test ends there. Later assertions do not run.
- **In a callback** (a timer, a hook, a mock, a breakpoint callback, a `RequestFrame` callback),
  only that callback stops. The test is failed, but the test function goes on until it returns.

## Expected errors

Sometimes the behavior under test is an error, for example a native error on bad input.
`Runner.ExpectError(text)` announces one plugin error whose message contains `text`:

```sourcepawn
// points.sp: the server command sm_share <players> prints 100 / players.
public void Test_ZeroPlayersIsAnError() {
	Runner.ExpectError("Divide by zero");
	Runner.Command("sm_share 0");
}
```

The matching error does not fail the test. If no such error occurs, the test fails with
`expected an error containing "Divide by zero", none occurred`.

Each call expects exactly one error. Call it twice to expect two errors. The text is matched
against `<plugin file>: <message>`, so `Runner.ExpectError("points.smx")` expects any one error of
`points.smx`.

## Skipping a test

`Runner.Skip(reason, ...)` marks the test as skipped and stops the test function. Use it when a
test cannot run in this setup:

```sourcepawn
public void Test_MotdIsFetched() {
	if (GetExtensionFileStatus("SteamWorks.ext") != 1) {    // 1: the extension is running
		Runner.Skip("needs --steamworks");
	}
	// ...
}
```

This works only in a test plugin that does not include `smrunner-steamworks.inc`: that include
requires SteamWorks, so without `--steamworks` the test plugin would not load at all.

The report shows `SKIP` and the reason. A skipped test does not make the exit code non-zero.

- A test with any failure counts as `FAIL`, even if it called `Runner.Skip`. The skip reason is
  then not shown.
- Called from a callback (a timer, a hook), `Runner.Skip` stops only that callback. The test
  function goes on, and the test is still reported as skipped if nothing fails.

## The test's name

`Runner.GetTestName(buffer, maxlength)` writes the name of the running test. It lets one helper
behave differently per test:

```sourcepawn
void ConnectPlayers() {
	char test[64];
	Runner.GetTestName(test, sizeof(test));
	int count = StrContains(test, "FullServer") != -1 ? 24 : 2;
	for (int i = 0; i < count; i++) {
		Runner.ConnectClient("player");
	}
}
```

## Running tests

```sh
smrunner test test_greeter.smx --plugin greeter.smx
```

Tests run one after the other, in alphabetical order of their names within each test plugin.
Useful options:

| Option | Effect |
|---|---|
| `--filter TEXT` | Run only tests whose name contains `TEXT`. |
| `--timeout SECONDS` | Time limit of each test, in real seconds (default 30). |
| `--echo` | Print the server console of every test while it runs. |
| `--junit FILE` | Also write a JUnit XML report. |

`smrunner list test_greeter.smx` prints the tests without running them. All options are listed in
[Command line](command-line.md).

## Reading a failure

The report of a failed test names the assertion, the values and the line of the test plugin. Then
come the last lines of the server console and of SourceMod's error log. Here, the README's
quick-start test expected `Welcome, Alice!` with a capital A:

```
test_greeter.smx
  FAIL    Test_NewPlayersAreGreeted (72 ms)
          Assert.ClientReceived failed: client 1 did not receive "Welcome, Alice!" (test_greeter.sp:6)
          --- console (last lines; --echo shows all) ---
          | ...
1 tests: 0 passed, 1 failed, 0 skipped
```

The full layout of the report, and how to read call stacks, is in
[Troubleshooting](troubleshooting.md#reading-a-failure).

## Tips for good tests

- **One behavior per test.** Every test starts from a fresh server anyway, and a short test names
  exactly what broke.
- **Name tests as sentences.** `Test_BotsAreNotGreeted` reads well in the report.
- **Match on stable text.** Assert on the part of a message that matters, not on the whole line.
- **Advance time explicitly.** Nothing happens between two statements of a test unless the test
  runs frames: directly, or through `Runner.ConnectClient`, `Runner.ChangeMap` or
  `Runner.WaitUntil`, which run frames themselves. See
  [Server, time and commands](server-and-time.md).
- **Prefer state over output.** When a plugin keeps state in a global variable, reading it with
  `Runner.GetPluginValue` is more precise than parsing chat. See
  [Breakpoints and plugin variables](breakpoints.md).
