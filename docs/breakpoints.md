# Breakpoints and plugin variables

Asserting on chat and console output only shows what a plugin chose to print. To check its inner
state, a test can read and write the plugin's global variables at any time. It can also stop the
plugin at a line or a function with a breakpoint, and look at its local variables there.

- [Requirements](#requirements)
- [The example plugin](#the-example-plugin)
- [Reading and writing globals](#reading-and-writing-globals)
- [Calling a plugin's public functions](#calling-a-plugins-public-functions)
- [Breaking at a function](#breaking-at-a-function)
- [Breaking at a line](#breaking-at-a-line)
- [Breakpoint methods: reading variables at a stop](#breakpoint-methods-reading-variables-at-a-stop)
- [Limiting a breakpoint to one plugin](#limiting-a-breakpoint-to-one-plugin)
- [Breaking in code that runs at load](#breaking-in-code-that-runs-at-load)
- [Removing breakpoints and counting hits](#removing-breakpoints-and-counting-hits)
- [Readable variable types](#readable-variable-types)

## Requirements

Variables are found by name in the plugin's debug information. That needs a plugin compiled by
spcomp 1.11 or later, which writes debug information by default. Do not strip it.

## The example plugin

The examples on this page test `greetlog.sp`. It greets players and keeps a log of the greetings
in globals. The line numbers matter for the line breakpoints below:

```sourcepawn
 1  #include <sourcemod>
 2
 3  ConVar g_message;
 4  int g_greetings;
 5  float g_lastGreetTime;
 6  char g_lastGreeted[MAX_NAME_LENGTH];
 7
 8  public void OnPluginStart() {
 9  	g_message = CreateConVar("sm_greetlog_message", "Welcome", "The greeting");
10  	RegServerCmd("sm_greetcount", Command_Count);
11  }
12
13  public void OnClientPostAdminCheck(int client) {
14  	if (!IsFakeClient(client)) {
15  		Greet(client);
16  	}
17  }
18
19  void Greet(int client) {
20  	char message[64];
21  	g_message.GetString(message, sizeof(message));
22  	g_greetings++;
23  	g_lastGreetTime = GetGameTime();
24  	GetClientName(client, g_lastGreeted, sizeof(g_lastGreeted));
25  	PrintToChat(client, "%s, %s!", message, g_lastGreeted);
26  }
27
28  Action Command_Count(int args) {
29  	PrintToServer("greetings: %d", g_greetings);
30  	return Plugin_Handled;
31  }
```

## Reading and writing globals

These natives work on any plugin, at any time, even in [run mode](run-mode.md):

| Native | Does |
|---|---|
| `Runner.GetPluginValue(plugin, name, index = 0)` | Reads a cell global (int, float, bool, handle), or one cell of a global array. |
| `Runner.SetPluginValue(plugin, name, value, index = 0)` | Writes it. |
| `Runner.GetPluginString(plugin, name, buffer, maxlength)` | Reads a global string (a char array). |
| `Runner.SetPluginString(plugin, name, value)` | Writes it. Text longer than the array is truncated. |

Get the plugin's handle with SourceMod's `FindPluginByFile`:

```sourcepawn
public void Test_GreetingsAreLogged() {
	Runner.ConnectClient("alice");
	Handle greetlog = FindPluginByFile("greetlog.smx");

	Assert.Equal(Runner.GetPluginValue(greetlog, "g_greetings"), 1);

	char name[64];
	Runner.GetPluginString(greetlog, "g_lastGreeted", name, sizeof(name));
	Assert.StrEqual(name, "alice");
}
```

Writing a global sets up a state that is hard to reach otherwise:

```sourcepawn
public void Test_CountCommandPrintsTheCount() {
	Handle greetlog = FindPluginByFile("greetlog.smx");
	Runner.SetPluginValue(greetlog, "g_greetings", 100);
	Runner.SetPluginString(greetlog, "g_lastGreeted", "nobody");

	Runner.Command("sm_greetcount");

	Assert.ConsoleContains("greetings: 100");
}
```

A float global is read as a cell. Use `view_as<float>` to compare it:

```sourcepawn
public void Test_GreetTimeIsTheGameTime() {
	Runner.Seconds(2.0);
	Runner.ConnectClient("alice");
	float greeted = view_as<float>(Runner.GetPluginValue(FindPluginByFile("greetlog.smx"), "g_lastGreetTime"));
	Assert.FloatEqual(greeted, GetGameTime(), 1.0);
}
```

A name the plugin does not have is a native error that names it.

## Calling a plugin's public functions

Calling a public function directly tests it without the event that normally triggers it, or runs
a forward a second time. There is no special API for this; use SourceMod's own function calls:

```sourcepawn
public void Test_CallingTheForwardAgainGreetsAgain() {
	int alice = Runner.ConnectClient("alice");
	Handle greetlog = FindPluginByFile("greetlog.smx");

	Function check = GetFunctionByName(greetlog, "OnClientPostAdminCheck");
	Call_StartFunction(greetlog, check);
	Call_PushCell(alice);
	Call_Finish();

	Assert.Equal(Runner.ClientMessageCount(alice, RunnerMessage_Chat), 2, "greeted twice");
}
```

## Breaking at a function

`Runner.BreakAtFunction(name, callback, data = 0, plugin = "")` stops plugins under test each time
they enter a function, before its first statement runs. `Breakpoint.Line` is then the line of the
function's header. The callback runs while the plugin waits; when it returns, the plugin goes on:

```sourcepawn
void Break_Greet(Breakpoint frame, any expectedClient) {
	Assert.Equal(frame.GetValue("client"), expectedClient);    // an argument
	Assert.Equal(frame.GetValue("g_greetings"), 0);            // a global
	frame.SetValue("g_greetings", 41);                         // changed in the plugin
}

public void Test_BreakAtFunction() {
	int id = Runner.BreakAtFunction("Greet", Break_Greet, 1);   // 1: the first client

	Runner.ConnectClient("alice");                             // calls Greet

	Assert.Equal(Runner.BreakpointHits(id), 1);
	Assert.Equal(Runner.GetPluginValue(FindPluginByFile("greetlog.smx"), "g_greetings"), 42);
}
```

`data` is passed to the callback. `Runner.BreakAtFunction` returns the breakpoint's id. A
function name that no loaded plugin under test has fails the test.

## Breaking at a line

`Runner.BreakAtLine(file, line, callback, data = 0, plugin = "")` stops at the first statement at
or after `line` of `file`. The file is matched against the source path the plugin was compiled
from: its name or the end of its path. For a plugin compiled from `scripting/greetlog.sp`, both
`"greetlog.sp"` and `"scripting/greetlog.sp"` match:

```sourcepawn
void Break_Message(Breakpoint frame) {
	char message[64];
	frame.GetString("message", message, sizeof(message));    // a local
	Assert.StrEqual(message, "Welcome");
	frame.SetString("message", "Howdy");
}

public void Test_BreakAtLine() {
	Runner.BreakAtLine("greetlog.sp", 25, Break_Message);     // the PrintToChat line

	int alice = Runner.ConnectClient("alice");

	Assert.ClientReceived(alice, RunnerMessage_Chat, "Howdy, alice!");
}
```

The plugin stops before the statement on that line runs. Changes the callback makes take effect
when the plugin goes on, which is why the greeting above says `Howdy`.

- A blank or comment line stops at the next statement after it.
- A function's header line (19) stops when the function is entered, like `BreakAtFunction`.
- A line with no statement at or after it in any loaded plugin under test fails the test.
- A line that holds the *first* statement of a function never fires, unless that statement
  declares a local variable. Line 14 (the first statement of `OnClientPostAdminCheck`) and line 29
  never fire; line 20 (`char message[64];`) does. This is a known issue; see
  [Limits](limits.md#known-issues). Break at the header line instead.

## Breakpoint methods: reading variables at a stop

The callback gets a `Breakpoint`, named `frame` in the examples:

| Member | Does |
|---|---|
| `Breakpoint.Id` | The breakpoint's id, as returned by `BreakAtLine` or `BreakAtFunction`. |
| `Breakpoint.Line` | The line the plugin stopped at. |
| `Breakpoint.Plugin` | The handle of the stopped plugin. |
| `Breakpoint.GetFile(buffer, maxlength)` | The source file, as compiled. |
| `Breakpoint.GetFunction(buffer, maxlength)` | The function the plugin stopped in. |
| `Breakpoint.HasVariable(name)` | Whether a variable of that name is visible here. |
| `Breakpoint.GetValue(name, index = 0)`, `Breakpoint.SetValue(name, value, index = 0)` | Read or write a cell variable, or one cell of an array. |
| `Breakpoint.GetString(name, buffer, maxlength)`, `Breakpoint.SetString(name, value)` | Read or write a string variable. |

Names are looked up the way the plugin sees them at that statement: the function's locals and
arguments first, then the plugin's globals. A `Breakpoint` is valid only inside its callback.

Where the plugin stopped, and what it can see there:

```sourcepawn
void Break_Where(Breakpoint frame) {
	char file[64], name[64];
	frame.GetFile(file, sizeof(file));
	frame.GetFunction(name, sizeof(name));

	Assert.StrContains(file, "greetlog.sp");
	Assert.StrEqual(name, "Greet");
	Assert.Equal(frame.Line, 25);
	Assert.True(frame.HasVariable("message"), "a local of Greet");
	Assert.True(frame.HasVariable("g_greetings"), "a global");
	Assert.False(frame.HasVariable("args"), "a local of another function");
}
```

The callback may do anything a test does: assert, run commands, connect clients. It can run
frames only when the plugin was stopped outside of a frame (see
[Where frames cannot run](server-and-time.md#where-frames-cannot-run)). Breakpoints do not fire
inside a breakpoint's callback.

## Limiting a breakpoint to one plugin

By default a breakpoint applies to every plugin under test, and never to the test plugin. Give a
plugin's file name as the last argument to limit it to that plugin:

```sourcepawn
Runner.BreakAtFunction("OnClientPostAdminCheck", Break_Check, 0, "greetlog.smx");
```

## Breaking in code that runs at load

Code in `OnPluginStart`, or in `OnMapStart` of the first map, has run before the test function
starts. Set the breakpoint in `OnTestBoot` instead, which runs before the plugins under test start
(see [Writing tests](writing-tests.md#ontestboot-setup-before-plugins-start)):

```sourcepawn
int g_startBreak;

void Break_Start(Breakpoint frame) {
	Assert.Equal(frame.GetValue("g_message"), 0, "the cvar is not created yet");
}

public void OnTestBoot(const char[] test) {
	if (StrEqual(test, "Test_StartCreatesTheCvar")) {
		g_startBreak = Runner.BreakAtFunction("OnPluginStart", Break_Start);
	}
}

public void Test_StartCreatesTheCvar() {
	Assert.True(Runner.BreakpointHits(g_startBreak) > 0);
	Assert.True(FindConVar("sm_greetlog_message") != null);
}
```

A breakpoint set in `OnTestBoot` currently fires twice for each time it is reached, which is why
the test checks `> 0`. See [Limits](limits.md#known-issues).

## Removing breakpoints and counting hits

`Runner.RemoveBreakpoint(id)` removes a breakpoint. `Runner.BreakpointHits(id)` returns how often
plugins reached it. Both throw a native error for an id that does not exist:

```sourcepawn
public void Test_RemovedBreakpointDoesNotFire() {
	int id = Runner.BreakAtFunction("Greet", Break_Greet, 1);
	Runner.RemoveBreakpoint(id);

	Runner.ConnectClient("alice");

	Assert.Equal(Runner.GetPluginValue(FindPluginByFile("greetlog.smx"), "g_greetings"), 1);
}
```

Breakpoints need a test; in [run mode](run-mode.md) they throw a native error.

## Readable variable types

| Variable | Readable |
|---|---|
| Single cells: `int`, `float`, `bool`, handles, enums | yes, with `GetValue` |
| Cells of one-dimensional arrays | yes, with an index; out of bounds is a native error |
| Strings (`char` arrays) | yes, with `GetString`; `SetString` truncates text that does not fit |
| By-reference arguments (`int &value`) | yes, the referenced value |
| Enum structs | no |
| Arrays of more than one dimension | no |

Every access is checked against the plugin's memory. A wrong access is a native error that says
what is wrong, never a crash.
