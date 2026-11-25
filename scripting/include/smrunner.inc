/**
 * Copyright 2026 davenonymous
 *
 * smrunner's test API. smrunner runs SourceMod plugins on a simulated
 * server: SourceMod's own logic library and VM on a mocked core, without a
 * game. Documentation: docs/api-reference.md and docs/writing-tests.md.
 *
 * A test plugin includes this file and declares tests: public functions
 * without parameters whose names start with "Test_". Each test runs on a
 * fresh server, in a process of its own:
 *
 *   1. the test plugin loads, then the plugins under test (--plugin);
 *   2. the map starts and its configs are executed (OnConfigsExecuted has
 *      fired);
 *   3. the test function runs: it drives the server (frames, commands,
 *      clients, menus) and checks what happened with the Assert methods.
 *
 * A test passes when it returns without a failed assertion, without an
 * unexpected plugin error, and with every expected error seen.
 *
 * A test plugin may define
 *
 *     public void OnTestBoot(const char[] test)
 *
 * smrunner calls it with the test's name once every plugin is loaded
 * (AskPluginLoad2 has run), before any OnPluginStart and OnMapStart: the place to install
 * mocks, set breakpoints, write files or set cvars the plugins under test
 * need when they start.
 *
 *     smrunner test my-tests.smx --plugin my-plugin.smx
 *
 * In run mode (smrunner run) there is no test. The natives that drive the
 * server, its clients and menus and the plugins' variables work as in a
 * test, and a failed assertion fails the run. The natives marked "Needs a
 * test" throw a native error there.
 *
 * Natives marked "Not inside a frame" throw a native error when called from
 * a timer, RequestFrame, OnGameFrame, a frame hook, a command run from the
 * command buffer, or a breakpoint or hook reached from one of these.
 */

#if defined _smrunner_included
	#endinput
#endif
#define _smrunner_included



/* The kind of a message a simulated client received. */
enum RunnerMessage {
	RunnerMessage_Console = 0,  // its console: PrintToConsole, ReplyToCommand, "Unknown command" answers
	RunnerMessage_Chat,         // chat: PrintToChat, SayText, SayText2
	RunnerMessage_Center,       // PrintCenterText
	RunnerMessage_Hint,         // PrintHintText
	RunnerMessage_Command,      // a command sent to it: ClientCommand, replicated cvars
	RunnerMessage_Kick,         // the reason it was kicked with
	RunnerMessage_Menu,         // a menu or panel display, its text as the client shows it
	RunnerMessage_UserMessage,  // the name of a user message without text
	RunnerMessage_Any = -1      // any kind, as a filter
};

/* One call of a hooked or mocked native. Valid only while the hook or mock
 * callback it was given to runs; using it later is a native error.
 * Arguments count from 1, as in the native's declaration. */
methodmap RunnerCall {
	// The number of arguments the plugin passed.
	property int ArgCount {
		public native get();
	}

	// The calling plugin.
	property Handle Caller {
		public native get();
	}

	// What the call returns. In a post hook: the native's result, which the
	// hook may change. In a pre hook that handles the call: what the call
	// returns (0 unless set).
	property any Result {
		public native get();
		public native set(any value);
	}

	// The native's name.
	public native void GetName(char[] buffer, int maxlength);

	// Argument n as passed: a cell, or for arrays and references their
	// address in the caller.
	public native any Arg(int n);

	// Changes argument n for the real native (in a pre hook, or before
	// CallOriginal in a mock).
	public native void SetArg(int n, any value);

	// The value a by-reference argument (int &value, float &value) refers
	// to, and writing it.
	public native any ArgRef(int n);
	public native void SetArgRef(int n, any value);

	// Copies string argument n; returns the bytes written.
	public native int GetArgString(int n, char[] buffer, int maxlength);

	// Writes value into the caller's string buffer that argument n names.
	// The buffer must hold maxlength bytes; usually the native's next
	// argument says how many (Arg(n + 1)).
	public native void SetArgString(int n, const char[] value, int maxlength);

	// Copies size cells of array argument n into values, or from values.
	public native void GetArgArray(int n, any[] values, int size);
	public native void SetArgArray(int n, const any[] values, int size);

	// From a mock: runs the real native with the arguments as they are now
	// and returns its result. When the real native throws, returns 0 and the
	// error reaches the calling plugin.
	public native any CallOriginal();
}

/* A plugin stopped at a breakpoint. Valid only while the breakpoint's
 * callback runs. Variables are looked up by name as the plugin sees them
 * at the statement: the function's locals and arguments first, then the
 * plugin's globals. */
methodmap Breakpoint {
	// The breakpoint's id, as BreakAtLine or BreakAtFunction returned it.
	property int Id {
		public native get();
	}

	// The line the plugin stopped at.
	property int Line {
		public native get();
	}

	// The stopped plugin.
	property Handle Plugin {
		public native get();
	}

	// The source file (as compiled) and the function the plugin stopped in.
	public native void GetFile(char[] buffer, int maxlength);
	public native void GetFunction(char[] buffer, int maxlength);

	// Whether a variable of that name is visible at the statement.
	public native bool HasVariable(const char[] name);

	// A cell variable (int, float, bool, handle, enum), or cell index of a
	// one-dimensional array. An unknown name or an index out of bounds is a
	// native error. The plugin sees a change when it goes on.
	public native any GetValue(const char[] name, int index = 0);
	public native void SetValue(const char[] name, any value, int index = 0);

	// A string (char array) variable. SetString cuts text that does not fit.
	public native int GetString(const char[] name, char[] buffer, int maxlength);
	public native void SetString(const char[] name, const char[] value);
}

/* The condition of Runner.WaitUntil; data is WaitUntil's data. */
typeset RunnerCondition {
	function bool (any data);
	function bool ();
};

/* A pre or post hook of Runner.HookNative. A pre hook that returns
 * Plugin_Handled or more skips the native, and the call returns
 * call.Result. A post hook's return value is ignored. */
typeset RunnerNativeHook {
	function Action (RunnerCall call);
	function void (RunnerCall call);
};

/* A mock of Runner.MockNative: replaces the native; its return value is the
 * native's result. */
typeset RunnerNativeMock {
	function any (RunnerCall call);
};

/* Called when a plugin reaches a breakpoint; the plugin waits meanwhile. */
typeset RunnerBreakpoint {
	function void (Breakpoint frame, any data);
	function void (Breakpoint frame);
};

methodmap Runner {
	/* ----------------------------------------------------------- time */

	// Runs count server frames, one tick each. Not inside a frame.
	public static native void Frames(int count);

	// Runs frames for that much game time, rounded up to whole frames. Not
	// inside a frame.
	public static native void Seconds(float seconds);

	// Runs frames, with a millisecond of real time between them, until
	// condition returns true; for extensions that work on threads. Returns
	// false when timeout real seconds passed first or the server quit. Not
	// inside a frame.
	public static native bool WaitUntil(RunnerCondition condition, float timeout = 5.0, any data = 0);

	/* --------------------------------------------------------- server */

	// Runs server commands now, before it returns, outside of the command
	// buffer. What the commands add while they run (an exec'd file,
	// ServerCommand) runs at once too, up to a "wait"; what comes after a
	// wait runs when the buffer runs next (the next frame or
	// ServerExecute). Commands buffered before are left as they are.
	public static native void Command(const char[] format, any ...);

	// Runs server commands as Command does and writes what they printed
	// into output. Returns the bytes written.
	public static native int CommandOutput(char[] output, int maxlength, const char[] format, any ...);

	// Changes the map now: OnMapEnd, SourceMod's plugin refresh, OnMapStart
	// and the new map's configs. Returns once OnConfigsExecuted ran. Every
	// client disconnects; the game clock starts again at 0. Not inside a
	// frame.
	public static native void ChangeMap(const char[] map);

	/* -------------------------------------------------------- console */

	// Whether the server console printed text since the server started or
	// the last ClearConsole().
	public static native bool ConsoleContains(const char[] text);

	// Copies what the server console printed since the server started or
	// the last ClearConsole(); the end of it when it does not fit. Returns
	// the bytes written.
	public static native int GetConsole(char[] buffer, int maxlength);

	// Forgets what the console printed so far.
	public static native void ClearConsole();

	/* -------------------------------------------------------- clients */

	// Connects a client through SourceMod's connect sequence and returns its
	// index. steamId is "" (a unique one is made up), a Steam2 id
	// ("STEAM_0:1:23"), a Steam3 id ("[U:1:47]") or a 64-bit id. With authorize, returns after
	// OnClientAuthorized and OnClientPostAdminCheck (then not inside a
	// frame); without, after OnClientPutInServer. ip is the address with its
	// port (GetClientIP), fewer than 64 characters. Returns 0 when a plugin
	// rejected the client, the server is full or the client was kicked
	// while connecting; GetRejectReason tells why.
	public static native int ConnectClient(const char[] name, const char[] steamId = "", bool authorize = true, const char[] ip = "127.0.0.1:27005");

	// Connects a bot, as CreateFakeClient does. Returns its index or 0.
	public static native int ConnectBot(const char[] name);

	// Why the last ConnectClient or ConnectBot that returned 0 failed.
	public static native int GetRejectReason(char[] buffer, int maxlength);

	// Disconnects a client.
	public static native void Disconnect(int client, const char[] reason = "Disconnect by user.");

	// Runs a console command as typed by the client: split at ';' and line
	// breaks outside quotes and // comments. Unknown commands get TF2's
	// "Unknown command: <name>" in the client's console. A command of 511
	// characters or more is a native error, as the client's own command
	// buffer refuses it. So is a "wait" while sv_allow_wait_command allows
	// waits: a client would send what follows in later frames of its own,
	// which simulated clients do not have.
	public static native void ClientCommand(int client, const char[] format, any ...);

	// Sets one of the client's own cvars, its userinfo. GetClientInfo reads
	// the new value at once; the server takes the change on the next frame,
	// as the engine does (OnClientSettingsChanged, a name change, the admin
	// checks for a new password). QueryClientConVar is not simulated.
	public static native void SetClientConVar(int client, const char[] name, const char[] value);

	// Set what GetClientTeam, IsPlayerAlive and GetClientHealth return.
	public static native void SetClientTeam(int client, int team);
	public static native void SetClientAlive(int client, bool alive);
	public static native void SetClientHealth(int client, int health);

	// Whether the client received a message of the kind that contains text
	// (case-sensitive), as it was sent or without its chat color codes.
	public static native bool ClientReceived(int client, RunnerMessage kind, const char[] text);

	// How many messages of the kind the client received.
	public static native int ClientMessageCount(int client, RunnerMessage kind = RunnerMessage_Any);

	// Copies the index-th message (from 0, of any kind) as it was sent;
	// returns its kind, or -1 when there is no such message.
	public static native RunnerMessage GetClientMessage(int client, int index, char[] buffer, int maxlength);

	// Forgets the client's messages so far.
	public static native void ClearClientMessages(int client);

	/* ---------------------------------------------------------- menus */

	// Copies the radio menu on the client's screen, its text as the client
	// shows it ("->1. Item" for a key it takes, "1. Item" for a disabled
	// item). False, with an empty buffer, when the client shows none: never
	// shown, closed by a key it takes, replaced, or its time ran out.
	public static native bool GetClientMenu(int client, char[] buffer, int maxlength);

	// The keys the menu on the client's screen takes: bit 0 for key 1 to
	// bit 8 for key 9, bit 9 for key 0. 0 when it shows none.
	public static native int ClientMenuKeys(int client);

	// Draws the menu the client shows in a box (keys it takes green, its
	// navigation cyan, disabled lines dim) for people to read: below the
	// test's line in the report, or on standard output in run mode. Colors
	// follow smrunner's --color.
	public static native void PrintClientMenu(int client);

	// The client presses a menu key: "menuselect <key>" as the client sends
	// it, 1 to 9, and 10 for the key 0. With no menu on the screen the
	// command goes on as any other client command.
	public static native void SelectMenu(int client, int key);

	// The client presses the key of the one item of its menu that it can
	// select ("->N. label") and whose label contains text. Fails the test
	// when the client shows no menu, no such item or more than one.
	public static native void SelectMenuItem(int client, const char[] text);

	/* --------------------------------------------------------- the test */

	// Needs a test. Expects one plugin error whose "<plugin file>: <message>"
	// contains text: that error does not fail the test, and the test fails
	// when no such error occurs. Call it once per expected error.
	public static native void ExpectError(const char[] text);

	// Needs a test. Marks the test as skipped and stops the running function
	// of the test plugin (from a callback: only that callback). A test with
	// a failure is reported as failed.
	public static native void Skip(const char[] reason, any ...);

	// Needs a test. Copies the running test's name.
	public static native int GetTestName(char[] buffer, int maxlength);

	/* -------------------------------------------------------- natives */

	// Needs a test. Replaces a native in every plugin under test, also in
	// plugins loaded later; test plugins keep the real one. Replaces earlier
	// hooks and mocks of the native.
	public static native void MockNative(const char[] name, RunnerNativeMock mock);

	// Needs a test. Hooks a native in every plugin under test, also in
	// plugins loaded later; test plugins keep the real one. pre runs before
	// the native, post after it (also when pre handled the call, but not
	// when the native threw); either may be INVALID_FUNCTION, not both. Replaces earlier hooks and mocks of
	// the native. A name no plugin under test uses does nothing.
	public static native void HookNative(const char[] name, RunnerNativeHook pre, RunnerNativeHook post = INVALID_FUNCTION);

	// Needs a test. Removes the mock or the hooks of a native.
	public static native void UnhookNative(const char[] name);

	// Needs a test. How often plugins under test called a hooked or mocked
	// native. The count stays after UnhookNative, and the calls of the
	// plugins loaded then are no longer counted.
	public static native int NativeCalls(const char[] name);

	/* ---------------------------------------------------- breakpoints */

	// Needs a test. Stops at the first statement at or after line of file
	// (the source file's name or the end of its path, as compiled), in every
	// plugin under test, or only in the plugin named by its file name.
	// Returns the breakpoint's id; fails the test when no loaded plugin under
	// test has such a statement.
	public static native int BreakAtLine(const char[] file, int line, RunnerBreakpoint callback, any data = 0, const char[] plugin = "");

	// Needs a test. Stops each time a plugin under test (or only the named
	// one) enters the function, before its first statement. Returns the
	// breakpoint's id; fails the test when no loaded plugin under test has a
	// function of that name.
	public static native int BreakAtFunction(const char[] functionName, RunnerBreakpoint callback, any data = 0, const char[] plugin = "");

	// Needs a test. Removes a breakpoint; an unknown id is a native error.
	public static native void RemoveBreakpoint(int id);

	// Needs a test. How often plugins reached the breakpoint.
	public static native int BreakpointHits(int id);

	/* ------------------------------------------- variables of plugins */

	// A global variable of any plugin, at any time: a cell (int, float,
	// bool, handle), or one cell of a global array.
	public static native any GetPluginValue(Handle plugin, const char[] name, int index = 0);
	public static native void SetPluginValue(Handle plugin, const char[] name, any value, int index = 0);

	// A global string (char array) of a plugin. SetPluginString cuts text
	// that does not fit.
	public static native int GetPluginString(Handle plugin, const char[] name, char[] buffer, int maxlength);
	public static native void SetPluginString(Handle plugin, const char[] name, const char[] value);
}

/* ---------------------------------------------------------------------
 * Assertions. A failed assertion records the failure with the test
 * plugin's file and line, then stops the running function of the test
 * plugin: the test function, or the callback (timer, hook, mock,
 * breakpoint) it failed in. The message and its arguments are formatted
 * like Format().
 */
methodmap Assert {
	// Fails unless condition is true.
	public static native void True(bool condition, const char[] message = "", any ...);

	// Fails unless condition is false.
	public static native void False(bool condition, const char[] message = "", any ...);

	// Fails unless the two cells are equal (ints, bools, handles, enums).
	public static native void Equal(any actual, any expected, const char[] message = "", any ...);

	// Fails when the two cells are equal.
	public static native void NotEqual(any actual, any unexpected, const char[] message = "", any ...);

	// Fails unless |actual - expected| <= tolerance.
	public static native void FloatEqual(float actual, float expected, float tolerance = 0.0001, const char[] message = "", any ...);

	// Fails unless the strings are equal, case-sensitively.
	public static native void StrEqual(const char[] actual, const char[] expected, const char[] message = "", any ...);

	// Fails unless text contains part, case-sensitively.
	public static native void StrContains(const char[] text, const char[] part, const char[] message = "", any ...);

	// Fails unless the server console printed text since the server started
	// or the last Runner.ClearConsole().
	public static native void ConsoleContains(const char[] text, const char[] message = "", any ...);

	// Fails when the server console printed text since the server started
	// or the last Runner.ClearConsole().
	public static native void ConsoleNotContains(const char[] text, const char[] message = "", any ...);

	// Fails unless the client received a message of the kind that contains
	// text, as it was sent or without its chat color codes.
	public static native void ClientReceived(int client, RunnerMessage kind, const char[] text, const char[] message = "", any ...);

	// Fails unless the client shows a menu that contains text.
	public static native void ClientMenu(int client, const char[] text, const char[] message = "", any ...);

	// Fails always.
	public static native void Fail(const char[] message, any ...);
}
