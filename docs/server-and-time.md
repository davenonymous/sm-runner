# Server, time and commands

The simulated server does nothing on its own. Time moves only when a test runs frames, and
commands run when a test sends them. This makes every test repeat exactly. This page explains
the clock, server commands, the server console, cvars and map changes.

- [The simulated clock](#the-simulated-clock)
- [Runner.Frames and Runner.Seconds](#runnerframes-and-runnerseconds)
- [Testing timers](#testing-timers)
- [Runner.WaitUntil: waiting for threads](#runnerwaituntil-waiting-for-threads)
- [Where frames cannot run](#where-frames-cannot-run)
- [Server commands](#server-commands)
- [The command buffer](#the-command-buffer)
- [The server console](#the-server-console)
- [Cvars](#cvars)
- [Maps and map changes](#maps-and-map-changes)

## The simulated clock

A frame is one server tick. At the default tick rate of 66.67 ticks per second, one frame is
0.015 seconds of game time. In each frame the server runs, in order:

- the command buffer,
- the userinfo changes of clients (see [Clients](clients.md)),
- SourceMod's game frame: timers, `RequestFrame` callbacks, `OnGameFrame` and the frame hooks of
  extensions.

Nothing depends on real time. `GetGameTime()`, `GetGameTickCount()`, `GetEngineTime()` and
timers all follow the frames the test runs. The tick rate can be changed with `--tickrate`.

Frames run only when the test asks for them: with `Runner.Frames`, `Runner.Seconds` or
`Runner.WaitUntil`, and inside three natives that need them. `Runner.ConnectClient` (with its
default `authorize = true`) runs frames until SourceMod's next authorization check, about 0.7
seconds of game time. `Runner.ChangeMap` runs frames until the new map's configs have run.
Timers and buffered commands run during those frames too.

## Runner.Frames and Runner.Seconds

`Runner.Frames(count)` runs exactly `count` frames. `Runner.Seconds(seconds)` runs as many frames
as that much game time needs, rounded up:

```sourcepawn
public void Test_FramesAdvanceTheClock() {
	float before = GetGameTime();
	int ticks = GetGameTickCount();

	Runner.Frames(66);

	Assert.Equal(GetGameTickCount() - ticks, 66);
	Assert.FloatEqual(GetGameTime() - before, 0.99, 0.001);
}
```

Use `Runner.Frames(1)` when a plugin does something "on the next frame", for example
`RequestFrame`, `KickClient` or `ServerCommand`. Use `Runner.Seconds` for timers.

```sourcepawn
void Frame_Print() {
	PrintToServer("next frame");
}

public void Test_RequestFrameRunsOnTheNextFrame() {
	RequestFrame(Frame_Print);
	Assert.False(Runner.ConsoleContains("next frame"));

	Runner.Frames(1);
	Assert.True(Runner.ConsoleContains("next frame"));
}
```

## Testing timers

Because the clock is simulated, a 60-second timer takes a few milliseconds to test. Run the time
up to just before the timer and check that nothing happened yet, then run past it:

```sourcepawn
// adverts.sp: OnMapStart creates a repeating 60-second timer that
// prints "Visit example.com" to everyone in chat.
public void Test_AdvertEveryMinute() {
	int alice = Runner.ConnectClient("alice");

	Runner.Seconds(59.0);
	Assert.False(Runner.ClientReceived(alice, RunnerMessage_Chat, "Visit example.com"));

	Runner.Seconds(1.0);
	Assert.ClientReceived(alice, RunnerMessage_Chat, "Visit example.com");

	Runner.Seconds(60.0);
	Assert.Equal(Runner.ClientMessageCount(alice, RunnerMessage_Chat), 2, "two adverts");
}
```

Menu timeouts, vote durations and SteamWorks request timeouts follow the same clock.

## Runner.WaitUntil: waiting for threads

Some extensions work on threads of their own, for example an HTTP or database extension. Their
answers arrive in real time, not game time. `Runner.WaitUntil(condition, timeout = 5.0, data = 0)`
runs frames, with one millisecond of real time between them, until `condition` returns true.

The condition is a function of the test plugin. It may take one `any` parameter, which gets
`data`. Here it reads a global of the plugin under test (see
[Breakpoints and plugin variables](breakpoints.md)):

```sourcepawn
// stats.sp sends a threaded SQL query in OnConfigsExecuted and sets
// g_loaded to true in the query's callback.
bool StatsLoaded(Handle plugin) {
	return Runner.GetPluginValue(plugin, "g_loaded");
}

public void Test_StatsAreLoaded() {
	Handle stats = FindPluginByFile("stats.smx");
	Assert.True(Runner.WaitUntil(StatsLoaded, 2.0, stats), "no answer within 2 seconds");
}
```

It returns true as soon as the condition returns true. It returns false when `timeout` real
seconds passed first, or when the server quit.

## Where frames cannot run

A frame cannot run inside another frame, because that would run the outer frame's work again.
These natives throw a native error when called from inside a frame:

- `Runner.Frames`, `Runner.Seconds` and `Runner.WaitUntil`,
- `Runner.ChangeMap`,
- `Runner.ConnectClient` with `authorize = true` (authorization takes frames).

"Inside a frame" means in a timer callback, a `RequestFrame` callback, `OnGameFrame`, a frame
hook, a command run from the command buffer, or a breakpoint or hook reached from one of them.
Call these natives from the test function instead.

After a `quit` or `exit` command, the server runs no more frames.

## Server commands

`Runner.Command(format, ...)` runs server console commands right away, before it returns. Several
commands can be separated by `;`, as on a server:

```sourcepawn
Runner.Command("sm_greeter_message Hello; echo done");
```

`Runner.CommandOutput(output, maxlength, format, ...)` does the same and returns what the commands
printed. This is the most direct way to test a server command:

```sourcepawn
// counter.sp: the server command sm_counter prints "players seen: <n>",
// counting every client that connected.
public void Test_CounterCountsPlayers() {
	Runner.ConnectClient("alice");

	char output[128];
	Runner.CommandOutput(output, sizeof(output), "sm_counter");

	Assert.StrEqual(output, "players seen: 1\n");
}
```

Commands run with the server console as the caller (client 0). To run a command as a player, use
`Runner.ClientCommand` (see [Clients](clients.md)).

Commands follow the engine's rules. `//` outside quotes comments out the rest of the line. A
command of 511 characters or more is refused, as on a server, and the console prints
`WARNING: Command too long... ignoring!`.

## The command buffer

`ServerCommand` in a plugin does not run the command at once. It adds it to the command buffer,
which runs on the next frame:

```sourcepawn
public void Test_ServerCommandIsBuffered() {
	ServerCommand("echo buffered");
	Assert.ConsoleNotContains("buffered");

	Runner.Frames(1);
	Assert.ConsoleContains("buffered");
}
```

`ServerExecute()` runs the buffer at once, as on a server. Commands that a command adds while it
runs (an `exec`'d file, a `ServerCommand`) run right after it.

`Runner.Command` runs its own commands outside of the buffer. Commands buffered before it stay in
the buffer.

A `wait` in `Runner.Command`'s line delays the commands after it to the next run of the buffer.
`wait 3` delays them by three runs. This holds while `sv_allow_wait_command` is 1, the default.
With 0, `wait` does nothing and the commands after it run at once.

## The server console

Everything the server prints is recorded: `PrintToServer`, command replies to the console,
SourceMod's own messages, `echo`.

| Native | Does |
|---|---|
| `Runner.ConsoleContains(text)` | Returns whether the console printed `text`. |
| `Runner.GetConsole(buffer, maxlength)` | Copies the console text; the end of it if it does not fit. |
| `Runner.ClearConsole()` | Forgets everything printed so far. |
| `Assert.ConsoleContains(text)` | Fails unless the console printed `text`. |
| `Assert.ConsoleNotContains(text)` | Fails if the console printed `text`. |

All of them look at the text since the server started or since the last `Runner.ClearConsole()`.
Clear the console before an action to look only at what that action printed:

```sourcepawn
public void Test_ConsoleReading() {
	Runner.ClearConsole();
	Runner.Command("echo one; echo two");

	char console[64];
	Runner.GetConsole(console, sizeof(console));
	Assert.StrEqual(console, "one\ntwo\n");
}
```

## Cvars

There is no special API for cvars. A test reads and sets them with SourceMod's own natives, as
any plugin does:

```sourcepawn
public void Test_TheGreetingComesFromTheCvar() {
	FindConVar("sm_greeter_message").SetString("Hello");
	int alice = Runner.ConnectClient("alice");
	Assert.ClientReceived(alice, RunnerMessage_Chat, "Hello, alice!");
}
```

The same goes for admins (`SetUserFlagBits`, `CreateAdmin`), files and everything else SourceMod
offers.

A cvar must sometimes have its value before a plugin starts, for example when `OnPluginStart`
reads it. Set it on the command line with `--cvar NAME=VALUE`. If the cvar does not exist yet,
it gets the value when SourceMod or a plugin creates it:

```sh
smrunner test test_greeter.smx --plugin greeter.smx --cvar sm_greeter_message=Hi
```

After the configs have run, smrunner warns about every `--cvar` that did not match any cvar, so a
typo does not go unnoticed:

```
[smrunner] warning: --cvar sm_nope: no cvar of that name was made by the time the configs were executed
```

Inside one test, `OnTestBoot` can also set a cvar before the plugins start, if the cvar already
exists then (engine cvars and SourceMod's own do).

`AutoExecConfig` works as on a server: a missing config file is written from the plugin's cvars,
then executed.

## Maps and map changes

The server starts on `cp_badlands`, or on the map given with `--map NAME`.

`Runner.ChangeMap(map)` changes the map at once, with everything SourceMod does on a map change:
`OnMapEnd`, SourceMod's plugin refresh (plugins stay loaded, new ones load), `OnMapStart` and the
configs of the new map. It returns once `OnConfigsExecuted` has run on the new map:

```sourcepawn
public void Test_ChangeMap() {
	int alice = Runner.ConnectClient("alice");

	Runner.ChangeMap("koth_viaduct");

	char map[64];
	GetCurrentMap(map, sizeof(map));
	Assert.StrEqual(map, "koth_viaduct");
	Assert.False(IsClientConnected(alice), "a map change disconnects every client");
}
```

Things to know about map changes:

- **Clients disconnect.** On a real server they reconnect on their own. A test has to connect
  them again with `Runner.ConnectClient`.
- **The game clock starts at 0 again.** `GetGameTime()` and `GetGameTickCount()` restart on the
  new map.
- **Timers with `TIMER_FLAG_NO_MAPCHANGE` end**, as on a server.

`Runner.ChangeMap` accepts any map name. A plugin's own map change, through `changelevel`, `map`
or `ForceChangeLevel`, only works for maps the server has. These are the start map, the maps
listed with `--maps`, maps a test already changed to, and maps whose `.bsp` file is in
`tf/maps/`. Otherwise the command fails as on a server:

```sourcepawn
public void Test_ChangelevelNeedsAKnownMap() {
	Runner.Command("changelevel pl_upward");
	Assert.ConsoleContains("changelevel failed: \"pl_upward\" not found on server.");
}
```

```sh
# The vote plugin changes to one of these maps itself.
smrunner test test_menu_vote.smx --plugin menu-vote.smx --maps cp_process_final,koth_viaduct
```

A `changelevel` takes effect at the end of a frame: the frame it ran in, or the next frame when
it ran outside of one (from `Runner.Command`). Run `Runner.Frames(1)` before checking the new map.
`IsMapValid` answers for the same set of maps.
