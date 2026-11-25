# Run mode

Not every check needs a test plugin. Sometimes you only want to know whether a plugin loads,
starts cleanly and handles a few commands without errors. `smrunner run` does that: it loads the
plugins on one simulated server, runs the given commands and frames, prints the server console,
and fails if anything went wrong.

- [Usage](#usage)
- [When a run fails](#when-a-run-fails)
- [Commands and frames](#commands-and-frames)
- [Using smrunner.inc in run mode](#using-smrunnerinc-in-run-mode)
- [Examples](#examples)

## Usage

```sh
smrunner run [options] PLUGIN.smx...
```

Plugins are given as arguments, or with `--plugin`, and load in the given order. Every
[server option](command-line.md#server-options) works: `--extensions`, `--cvar`, `--file`,
`--map` and so on.

```
$ smrunner run counter.smx --command "sm_counter" --frames 10
...
players seen: 0
```

`counter.sp` here registers the server command `sm_counter`, which prints how many clients
connected.

The server console is printed while the server runs. `-q` (`--quiet`) hides it.

## When a run fails

The exit code is 0 when the run was clean. It is 1, with the reason on standard error, when:

- a plugin did not load or start, for example because `OnPluginStart` threw an error or called
  `SetFailState`;
- an assertion of `smrunner.inc` failed;
- SourceMod's error log has an entry: an error thrown in any plugin, or a `LogError`.

Take this plugin, which divides 100 points between the players:

```sourcepawn
#include <sourcemod>

public void OnPluginStart() {
	RegServerCmd("sm_share", Command_Share);
}

Action Command_Share(int args) {
	PrintToServer("each player gets %d points", 100 / GetCmdArgInt(1));
	return Plugin_Handled;
}
```

With 0 players it divides by zero, and the run fails:

```
$ smrunner run points.smx --command "sm_share 0" --frames 1 -q
smrunner: SourceMod reported errors:
L 11/25/2026 - 13:43:13: SourceMod error session started
L 11/25/2026 - 13:43:13: Info (map "cp_badlands") (file "/tmp/smrunner.SL2NOW/tf/addons/sourcemod/logs/errors_20261125.log")
L 11/25/2026 - 13:43:13: [SM] Exception reported: Divide by zero
L 11/25/2026 - 13:43:13: [SM] Blaming: points.smx
L 11/25/2026 - 13:43:13: [SM] Call stack trace:
L 11/25/2026 - 13:43:13: [SM]   [1] Line 8, points.sp::Command_Share
$ echo $?
1
```

This differs from test mode, where a `LogError` alone does not fail a test.

## Commands and frames

| Option | Effect |
|---|---|
| `--command CMD` | A server command. Repeat it for several commands. |
| `--frames N` | The number of frames to run (default 66, one second at the default tick rate). |

The map starts first and its configs are executed. Then the commands go into the command buffer,
in the given order, and the frames run. The buffer runs on the first frame, so the commands run
then.

`--frames 0` runs no frame at all, so the commands never run. This is a known issue (see
[Limits](limits.md#known-issues)); use at least `--frames 1` with `--command`.

To see timers fire, run enough frames: `--frames 4000` is about one minute of game time.

## Using smrunner.inc in run mode

A plugin run with `smrunner run` may include `smrunner.inc`. There is no test, but most of the
API works:

- **Works:** connecting clients, client commands, menus (`Runner.PrintClientMenu` prints to
  standard output), the console natives, plugin variables, and every assertion. A failed
  assertion fails the run with its message and line.
- **Throws a native error** (and so fails the run): everything that needs a test.
  `Runner.ExpectError`, `Runner.Skip`, `Runner.GetTestName`, the hooks and mocks
  (`HookNative`, `MockNative`, `UnhookNative`, `NativeCalls`) and the breakpoints
  (`BreakAtLine`, `BreakAtFunction`, `RemoveBreakpoint`, `BreakpointHits`). The error says so:
  `Runner.HookNative needs a test: it works in smrunner test, not in smrunner run`.

`--command` commands run from the command buffer, inside a frame. Natives that run frames
cannot be called from a command handler there (see
[Where frames cannot run](server-and-time.md#where-frames-cannot-run)). That includes
`Runner.ConnectClient` with `authorize = true`, so connect clients with `authorize = false`, and
let `--frames` run the time.

## Examples

**Does the plugin load and start?** A useful first CI step for any plugin:

```sh
smrunner run my-plugin.smx --frames 1 -q
```

**Does it load with its extension and handle its command?**

```sh
smrunner run json-status.smx --extensions path/to/smjansson/extensions \
    --command "sm_status_json" --frames 10 --quiet
```

**Does it survive an hour of timers?**

```sh
smrunner run my-plugin.smx --frames 240000 -q
```

**A small run-mode script.** A helper plugin can use `smrunner.inc` to set up a scene, and a
second command checks it after some frames have run. This one checks that `greeter.sp` from the
[README](../README.md#quick-start) greets a player once the player is authorized:

```sourcepawn
// run_check.sp. Run it with greeter.smx, two commands and 120 frames:
//   --command "check_join" --command "wait 100; check_greeted" --frames 120
#include <sourcemod>
#include <smrunner>

int g_alice;

public void OnPluginStart() {
	RegServerCmd("check_join", Command_Join);
	RegServerCmd("check_greeted", Command_Greeted);
}

// Runs inside a frame, so the client connects without waiting for
// authorization; SourceMod authorizes it during the next frames.
Action Command_Join(int args) {
	g_alice = Runner.ConnectClient("alice", "", false);
	return Plugin_Handled;
}

Action Command_Greeted(int args) {
	Assert.ClientReceived(g_alice, RunnerMessage_Chat, "Welcome, alice!");
	return Plugin_Handled;
}
```

The `wait 100` delays `check_greeted` by 100 frames, so the frames between the two commands give
SourceMod time to authorize the client. If the assertion fails, the run exits 1 with the message
and the line.

For anything more involved, write a test plugin instead (see [Writing tests](writing-tests.md)).
