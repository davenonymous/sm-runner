# Troubleshooting

Solutions to common problems, grouped by the message or symptom you see. Search this page for the
text of your message; [Messages](#messages) quotes them verbatim, with placeholders written as
`<n>`, `<name>` and so on.

- [Reading a failure](#reading-a-failure)
- [Tools for looking closer](#tools-for-looking-closer)
- [Plugins that do not load](#plugins-that-do-not-load)
- [Messages](#messages)
- [A test fails although the plugin works](#a-test-fails-although-the-plugin-works)
- [Hooks, mocks and breakpoints](#hooks-mocks-and-breakpoints)
- [Timeouts and crashes](#timeouts-and-crashes)
- [Noise in the console](#noise-in-the-console)

## Reading a failure

A failed test prints, in this order:

1. The failures: each assertion that failed, with the test plugin's file and line; each
   unexpected plugin error with its call stack; each expected error that did not come.
2. `--- console (last lines; --echo shows all) ---`: the last 40 lines of the server console.
3. `--- SourceMod error log ---`: the last 40 lines of SourceMod's error log, if it has any.

```
  FAIL    Test_ShareSplitsThePoints (317 ms)
          Assert.ConsoleContains failed: the console did not print "each player gets" (test_points.sp:9)
          plugin error: points.smx: Divide by zero
            [0] points.sp::Command_Share, line 8
            [1] Runner.Command()
            [2] test_points.sp::Test_ShareSplitsThePoints, line 8
          --- console (last lines; --echo shows all) ---
          | ...
```

Here the test plugin's line 8 ran `sm_share 0`, which divided by zero in `points.sp` (see
[Run mode](run-mode.md#when-a-run-fails) for its code). The assertion on line 9 failed as a
consequence.

Read the call stack from the top: `[0]` is where the error happened, the lines below show how the
test got there.

## Tools for looking closer

| Tool | Shows |
|---|---|
| `--filter Test_Name` | Runs only the one test you are looking at. |
| `--echo` | The whole server console of each test, while it runs. |
| `-v` | smrunner's notes: the sandbox's path, the map start, unanswered SteamWorks requests. |
| `--keep-sandbox` | Keeps the sandbox, to look at its files, configs and logs. |
| `Runner.GetClientMenu` | The exact menu text that `Assert.ClientMenu` and `Runner.SelectMenuItem` match. |
| `Runner.PrintClientMenu` | The menu drawn for reading. Not the matched text: it drops `->` and adds `(disabled)`. |
| `PrintToServer` in the test | Your own trace lines; they appear in the console output. |

To list everything a client received:

```sourcepawn
void DumpMessages(int client) {
	char text[256];
	for (int i = 0; i < Runner.ClientMessageCount(client); i++) {
		RunnerMessage kind = Runner.GetClientMessage(client, i, text, sizeof(text));
		PrintToServer("message %d, kind %d: %s", i, kind, text);
	}
}
```

## Plugins that do not load

**`plugin <file> (evicted)`** or **`plugin <file> (failed to load: <reason>)`**

A plugin under test did not load or did not start. The reason is in the console and in
SourceMod's error log below the failure. Common reasons:

- **`Required extension "SDKTools" file("sdktools.ext") not running`**: the plugin requires an
  extension that does not load in smrunner. See [Limits](limits.md#extensions).
- **`Required extension "<name>" file("<file>") not running`** for an extension that should
  load: pass it with `--extensions DIR` or `--extension FILE`. With `smrunner64`, x64 extensions
  must be in `DIR/x64/`.
- **`Native "<name>" was not found`**: a native the plugin needs has no provider. Load the plugin
  that provides it with `--plugin`, or create it in the test plugin (see
  [Providing a native nobody provides](hooks-and-mocks.md#providing-a-native-nobody-provides)).
- **An error in `OnPluginStart`**, or `SetFailState`: the plugin's own error, with its stack.

**The test plugin itself is evicted**: a test plugin that includes `smrunner-steamworks.inc`
needs `--steamworks`.

## Messages

smrunner's own messages come first (`[smrunner] fatal:`, `smrunner:`, `[smrunner] warning:`,
`[SteamWorks]`). The messages of failed tests and native errors follow, alphabetically.

**`[smrunner] fatal: <message>`**

smrunner itself could not go on: a missing file, a wrong `--plugin` name, a sandbox folder that
is not empty. In `run`, and in `test` for problems found before the first test starts (for
example `[smrunner] fatal: nope.smx: cannot be read`), smrunner exits with code 70. A problem
found while a test builds its sandbox ends only that test, which reports
`CRASH ... the test process ended without a result (exit code 70)`. Common ones:

- `[smrunner] fatal: the plugin <name> is neither a file nor one of SourceMod's stock plugins`:
  check the path given to `--plugin`. A bare name is looked up among the
  [stock plugins](command-line.md#server-options).
- `[smrunner] fatal: the sandbox <dir> is not empty; give a new folder`: `--sandbox DIR` points
  at a folder that already holds a sandbox. Remove it or use another folder.
- `[smrunner] fatal: <file> is not a plugin (.smx)`: `--plugin` was given a file that does not
  end in `.smx`.
- `[smrunner] fatal: <name> is given twice (as <path>)`: two plugins or two extensions have the
  same file name. A sandbox folder can hold only one of them.
- `[smrunner] fatal: could not read the extension folder <dir>`: check the `--extensions` path.
- `[smrunner] fatal: --steamworks and a SteamWorks extension of --extensions or --extension: give one of them`:
  the stand-in and a real `SteamWorks.ext.so` cannot both be loaded. See
  [SteamWorks](steamworks.md#enabling-the-stand-in).
- `[smrunner] fatal: the runner's SourceMod files are missing from <dir>`: the install folder is
  incomplete, or `--home` points at the wrong folder. Unpack the release archive again.

**`smrunner: <problem>`, followed by the usage (exit code 2)**

The command line is wrong. The problems, verbatim:

| Message | Fix |
|---|---|
| `smrunner: unknown mode "<mode>" (run, test or list)` | The first argument must be `run`, `test` or `list`. |
| `smrunner: unknown option <option>` | Check the spelling. `--version` works only as the first argument. |
| `smrunner: <option> needs a value` | Give the option its value. |
| `smrunner: <option> needs NAME=VALUE` | `--cvar`, `--core` and `--file` take `NAME=VALUE` (`SRC=DEST`). |
| `smrunner: --tickrate needs a number between 1 and 1000` | The whole argument must be a number in that range. |
| `smrunner: <option> needs a whole number in range` | `--maxplayers` 1 to 101, `--frames` 0 or more, `--timeout` 1 to 86400. |
| `smrunner: --color takes auto, always or never, not "<value>"` | Use one of the three. |
| `smrunner: --steamworks: <dir> is no folder` | `--steamworks=DIR` must name an existing folder. |
| `smrunner: --steamworks-passthrough needs a host name` | Give a host pattern. |
| `smrunner: --steamworks-passthrough and --steamworks-record need --steamworks` | Add `--steamworks`. |
| `smrunner: run needs at least one plugin` | Name a plugin to run. |
| `smrunner: give at least one test plugin` | Name a test plugin to `test` or `list`. |

**`smrunner: no tests` and `smrunner: no tests match the filter`**

The test plugins have no `Test_` functions, or `--filter` matched none of them. smrunner exits 1.
Without a filter, a warning also names each test plugin without tests:
`[smrunner] warning: <file> has no tests (public functions named Test_...)`.

**`[smrunner] warning: --cvar <name>: no cvar of that name was made by the time the configs were executed`**

No cvar of that name exists. Check its spelling, and that the plugin that creates it is loaded.

**`[smrunner] warning: SteamWorks stand-in: <path>/steamworks.json: entry <n> ...`**

The mapping file has a wrong entry, so the SteamWorks stand-in did not load. The message names the
entry and the problem. See [The mapping file](steamworks.md#the-mapping-file-answers-from-a-file).

**`[SteamWorks] no answer for <METHOD> <url>: 404`**

No route, no mapping entry and no passthrough host matched the request. Add a route or an entry
for the URL. See [How a request is answered](steamworks.md#how-a-request-is-answered).

**`[SteamWorks] <METHOD> <url> failed: redirected to <url>, whose host <host> is no allowed passthrough host`**

A passthrough request was redirected to a host that is not allowed. Allow that host too.

**`"<line>" holds a wait: a client would run the commands after it in later frames of its own, which simulated clients do not have; send them with another Runner.ClientCommand after Runner.Frames`**

`Runner.ClientCommand` got a line with a `wait`. Do what the message says. See
[Client commands](clients.md#client-commands).

**`a client cannot send a command of <n> characters; its command buffer takes at most <n>`**

`Runner.ClientCommand` got a command of 511 characters or more, which a real client refuses to
send.

**`a hook needs a pre or a post function`** and **`a mock needs a function`**

`Runner.HookNative` got `INVALID_FUNCTION` for both callbacks, or `Runner.MockNative` for its
mock.

**`Assert.<name> failed: ...`**

An assertion failed. The message shows the values:

| Assertion | Message |
|---|---|
| `Assert.True` | `Assert.True failed` |
| `Assert.False` | `Assert.False failed` |
| `Assert.Equal` | `Assert.Equal failed: expected <expected>, got <actual>` |
| `Assert.NotEqual` | `Assert.NotEqual failed: both are <value>` |
| `Assert.FloatEqual` | `Assert.FloatEqual failed: expected <expected>, got <actual> (tolerance <tolerance>)` |
| `Assert.StrEqual` | `Assert.StrEqual failed: expected "<expected>", got "<actual>"` |
| `Assert.StrContains` | `Assert.StrContains failed: "<text>" does not contain "<part>"` |
| `Assert.ConsoleContains` | `Assert.ConsoleContains failed: the console did not print "<text>"` |
| `Assert.ConsoleNotContains` | `Assert.ConsoleNotContains failed: the console printed "<text>"` |
| `Assert.ClientReceived` | `Assert.ClientReceived failed: client <n> did not receive "<text>"` |
| `Assert.ClientMenu` | `Assert.ClientMenu failed: client <n> shows no menu`, or `Assert.ClientMenu failed: the menu of client <n> does not contain "<text>"; it shows:` and the drawn menu |
| `Assert.Fail` | `Assert.Fail: <message>` |

Your own message follows after a colon. The test plugin's file and line follow in parentheses.

**`breakpoint: no plugin has a statement at or after <file>:<line>`** and
**`breakpoint: no plugin has a function <name>`**

- The file name must match the source file the plugin was compiled from (`greetlog.sp`), not the
  `.smx`.
- Breakpoints only apply to plugins under test, not to the test plugin.
- The plugin must have debug information (spcomp 1.11 or later, not stripped).

**`cannot run frames inside a frame (in a timer, RequestFrame, OnGameFrame, a frame hook, a command from the buffer, or a breakpoint or hook reached from one)`**

`Runner.Frames`, `Runner.Seconds`, `Runner.WaitUntil`, `Runner.ChangeMap` or
`Runner.ConnectClient` with authorization was called inside a frame. Move the call into the test
function. See [Where frames cannot run](server-and-time.md#where-frames-cannot-run).

**`changelevel failed: "<map>" not found on server.`**

The map is unknown to the server. Add it with `--maps`. See
[Maps and map changes](server-and-time.md#maps-and-map-changes).

**`client <n> is not connected`** and **`client index <n> is invalid`**

A client native got a client that disconnected (also through a map change, which disconnects
everyone) or an index outside 1 to `--maxplayers`.

**`expected an error containing "<text>", none occurred`**

`Runner.ExpectError` was called, but no plugin error with that text came. Check the exact error
text in the console output, and that the error happens before the test ends.

**`<plugin> has no variable <name>`**

`Runner.GetPluginValue` and its relatives found no global of that name. When the message adds
`(it has no debug variables: compile it with spcomp 1.11 or later)`, the plugin has no debug
information.

**`Native "<name>" is not supported by smrunner (the simulated server has no engine for it)`**

The plugin called a native that needs the game. Mock it (see [Hooks and mocks](hooks-and-mocks.md)).

**`plugin error: <plugin>: <message>`**

A plugin threw an error that the test did not expect. The call stack follows. If the error is
the behavior under test, announce it with `Runner.ExpectError`.

**`Runner.<name> needs a test: it works in smrunner test, not in smrunner run`**

A native that needs a test was called in run mode. See [Run mode](run-mode.md).

**`Runner.SelectMenuItem failed: ...`**

- `client <n> shows no menu`: the client has no menu open. It may have closed (a key, its time,
  a newer menu), or the plugin has not shown it yet.
- `no line client <n> can select contains "<text>"; the menu shows:` and the drawn menu: no
  selectable item matches. Disabled items never match. Check the text with
  `Runner.GetClientMenu`.
- `more than one line contains "<text>": "<line>", "<line>"`: make the text longer, so that it
  matches one item only.

**`Server is full`**

`Runner.GetRejectReason` after a `Runner.ConnectClient` that returned 0: all `--maxplayers`
slots are taken.

**`the test did not end within <n> seconds`**

See [Timeouts and crashes](#timeouts-and-crashes).

**`the test process died of signal <signal>`** and
**`the test process ended without a result (exit code <n>)`**

See [Timeouts and crashes](#timeouts-and-crashes).

**`there is no breakpoint <n>`**

`Runner.RemoveBreakpoint` or `Runner.BreakpointHits` got an id that does not exist, or one that
was already removed.

**`this RunnerCall is not valid here (only inside its hook or mock)`** and
**`this Breakpoint is not valid here (only inside its callback)`**

A `RunnerCall` or `Breakpoint` was kept and used after its callback returned. Copy the values you
need into variables inside the callback.

**`WARNING: Command too long... ignoring!`**

A server command of 511 characters or more was refused, as on a server.

## A test fails although the plugin works

**The message arrives one frame later.** Several things happen on the next frame on a real
server too: `KickClient`, `ServerCommand`, `RequestFrame`, a `changelevel`, userinfo changes and
HTTP answers. Add `Runner.Frames(1)` before the assertion.

**The message has a different kind.** `ReplyToCommand` to a client command arrives in the
client's console (`RunnerMessage_Console`), not in chat. Use `RunnerMessage_Any` while
investigating, or the message dump above.

**The text does not match exactly.** `Assert.ClientReceived` matches a part of the text, with or
without color codes, but is case-sensitive. `%N` and other formats are already filled in.

**The menu text does not match.** Selectable items read `->1. Item`, disabled ones `1. Item`. Use
`Runner.GetClientMenu` to see the exact text; the box of `Runner.PrintClientMenu` is drawn for
reading and differs. See [Menus](menus.md#how-a-client-sees-a-menu).

**A timer has not fired yet.** Nothing happens between two statements unless the test runs
frames. Run `Runner.Seconds` past the timer's interval.

**The client is not connected any more.** A map change disconnects every client.

## Hooks, mocks and breakpoints

**A hook or mock has no effect.**

- Check the native's exact name. A hook on a name no plugin uses does nothing, without an error.
  `Runner.NativeCalls(name)` returns 0 then.
- The call might come from a stock, which is compiled into the plugin. Hook the native the stock
  calls.
- The call might come from the test plugin, which is never hooked.
- The call might happen before the hook was installed, for example in `OnPluginStart`. Install
  the hook in `OnTestBoot`.

**A breakpoint never fires.** Check `Runner.BreakpointHits(id)`. The line might not be reached in
this test, or the code ran before the breakpoint was set; for code in `OnPluginStart`, set the
breakpoint in `OnTestBoot`. Also see the breakpoint issues in
[Known issues](limits.md#known-issues).

## Timeouts and crashes

**`TIMEOUT` with `the test did not end within 30 seconds`**

The test ran longer than `--timeout` in real time. Usual causes: an endless loop in a plugin, a
very large `Runner.Frames` or `Runner.Seconds` count, or `Runner.WaitUntil` waiting for an
extension that never answers. Raise the limit with `--timeout` if the test is just slow.

**`CRASH` with `the test process died of signal Segmentation fault`**

The test's process crashed. This can be an extension crashing, or a known issue: hooks, mocks
and breakpoints crash when a plugin under test failed to load (see
[Known issues](limits.md#known-issues)). Run the test without the hook to see the load error.

**`CRASH` with `the test process ended without a result (exit code 70)`**

smrunner hit a fatal problem while building the test's server. The `[smrunner] fatal:` line above
the report says what; see [Messages](#messages).

## Noise in the console

**`Unknown command "sm_flood_time"`, `Unknown command "sm_reserve_type"` and similar lines** at
the start of every console are normal. SourceMod's `sourcemod.cfg` sets cvars of stock plugins
that are not loaded. A real server prints the same lines without those plugins. These are the
server console's messages; a client's unknown command gets TF2's `Unknown command: <name>`
instead.
