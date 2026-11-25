/**
 * Copyright 2026 davenonymous
 *
 * The test API of smrunner, which runs SourceMod plugins on a simulated
 * server: SourceMod's own logic library and VM with a mocked core, no game.
 *
 * A test plugin includes this file and declares tests: public functions
 * without parameters whose names start with "Test_". smrunner runs each test
 * on a server of its own, in a process of its own: the test plugin and the
 * plugins under test load (the test plugin first), the map starts and its
 * configs are executed (OnConfigsExecuted has fired), then the test function
 * is called. Inside it the test drives the server (frames, time, commands,
 * clients) and checks what happened with the Assert methods. A test passes
 * when it returns without a failed assertion, without an unexpected plugin
 * error and with every expected error seen.
 *
 * A test plugin may define
 *     public void OnTestBoot(const char[] test)
 * which smrunner calls with the test's name right after the test plugin was
 * loaded, before any plugin's OnPluginStart: the place to install mocks,
 * write files the plugins read or set engine cvars.
 *
 *     smrunner test my-tests.smx --plugin my-plugin.smx --extensions dist/extensions
 *
 * Everything here works on the simulated server only. In run mode
 * (smrunner run) there is no test: the natives that drive the server, its
 * clients and the plugins' variables work as in a test, a failed assertion
 * fails the run, and the natives that need a test (ExpectError, Skip,
 * GetTestName, the native hooks and mocks and the breakpoints) throw a
 * native error.
 */

#if defined _smrunner_included
	#endinput
#endif
#define _smrunner_included



/* What a simulated client received. */
enum RunnerMessage {
	RunnerMessage_Console = 0,  // its console (PrintToConsole, replies)
	RunnerMessage_Chat,         // chat (PrintToChat, replies to chat triggers)
	RunnerMessage_Center,       // PrintCenterText
	RunnerMessage_Hint,         // PrintHintText
	RunnerMessage_Command,      // a command sent to it (ClientCommand, replicated cvars)
	RunnerMessage_Kick,         // the reason it was kicked with
	RunnerMessage_Menu,         // a menu or panel display (its text as the client shows it)
	RunnerMessage_UserMessage,  // the name of a user message that carries no text
	RunnerMessage_Any = -1      // any of them
};

/* One call of a hooked or mocked native; valid only inside the hook or mock
 * callback it was given to. Arguments count from 1. */
methodmap RunnerCall {
	// The number of arguments the plugin passed.
	property int ArgCount {
		public native get();
	}

	// The calling plugin.
	property Handle Caller {
		public native get();
	}

	// What the call returns: in a post hook the native's result, which the
	// hook may change; in a pre hook that handles the call, what it returns.
	property any Result {
		public native get();
		public native set(any value);
	}

	public native void GetName(char[] buffer, int maxlength);

	// The argument as passed (a cell; for arrays and references their
	// address in the caller).
	public native any Arg(int n);

	// Changes an argument for the native (pre hooks and CallOriginal).
	public native void SetArg(int n, any value);

	// A by-reference argument (int &value, float &value).
	public native any ArgRef(int n);
	public native void SetArgRef(int n, any value);

	// A string argument.
	public native int GetArgString(int n, char[] buffer, int maxlength);

	// Writes a string into the caller's buffer that argument n names; it
	// must hold maxlength bytes, usually the next argument says how many.
	public native void SetArgString(int n, const char[] value, int maxlength);

	// An array argument.
	public native void GetArgArray(int n, any[] values, int size);
	public native void SetArgArray(int n, const any[] values, int size);

	// Runs the real native with the (possibly changed) arguments, from a mock.
	public native any CallOriginal();
}

/* A plugin stopped at a breakpoint; valid only inside the callback.
 * Variables are looked up by name: the function's locals and arguments
 * visible at the statement first, then the plugin's globals. */
methodmap Breakpoint {
	property int Id {
		public native get();
	}

	property int Line {
		public native get();
	}

	// The stopped plugin.
	property Handle Plugin {
		public native get();
	}

	public native void GetFile(char[] buffer, int maxlength);
	public native void GetFunction(char[] buffer, int maxlength);

	public native bool HasVariable(const char[] name);

	// A variable's cell, or one cell of an array variable.
	public native any GetValue(const char[] name, int index = 0);
	public native void SetValue(const char[] name, any value, int index = 0);

	// A string (char array) variable.
	public native int GetString(const char[] name, char[] buffer, int maxlength);
	public native void SetString(const char[] name, const char[] value);
}

/* Runner.WaitUntil's condition. */
typeset RunnerCondition {
	function bool (any data);
	function bool ();
};

/* A native hook: Plugin_Handled or more skips the native and makes the call
 * return call.Result; a post hook's return value is ignored. */
typeset RunnerNativeHook {
	function Action (RunnerCall call);
	function void (RunnerCall call);
};

/* A mock: replaces the native; its return value is the native's. */
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

	// Runs that many server frames (one tick each).
	public static native void Frames(int count);

	// Runs frames for that much game time.
	public static native void Seconds(float seconds);

	// Runs frames until condition returns true, with a millisecond of real
	// time between frames, for extensions that work on threads. Returns false
	// when timeout (real seconds) passed first.
	public static native bool WaitUntil(RunnerCondition condition, float timeout = 5.0, any data = 0);

	/* --------------------------------------------------------- server */

	// Runs server commands now, before it returns, apart from the command
	// buffer: the commands of the line and what they add while they run
	// (exec'd files, ServerCommand) run at once, up to a "wait" of their
	// own, after which the rest runs when the buffer runs next (the next
	// frame or ServerExecute). Commands buffered before and their waits are
	// left as they are.
	public static native void Command(const char[] format, any ...);

	// Runs server commands now as Command does and returns what they
	// printed.
	public static native int CommandOutput(char[] output, int maxlength, const char[] format, any ...);

	// Changes the map now: OnMapEnd, plugins reload as on a real map change,
	// OnMapStart and the configs.
	public static native void ChangeMap(const char[] map);

	/* -------------------------------------------------------- console */

	// Whether the server console printed text since the server started or
	// the last ClearConsole().
	public static native bool ConsoleContains(const char[] text);

	// What the server console printed since the server started or the last
	// ClearConsole() (the end of it when it does not fit); returns the
	// number of bytes written.
	public static native int GetConsole(char[] buffer, int maxlength);

	public static native void ClearConsole();

	/* -------------------------------------------------------- clients */

	// Connects a client and puts it in the server. steamId is empty (one is
	// made up), "STEAM_0:1:23", "[U:1:47]" or a 64-bit id. With authorize the
	// call returns after OnClientAuthorized and OnClientPostAdminCheck.
	// Returns the client index, or 0 when a plugin rejected the client
	// (Runner.GetRejectReason tells why) or the server is full. ip is the
	// address with its port (GetClientIP), fewer than 64 characters.
	public static native int ConnectClient(const char[] name, const char[] steamId = "", bool authorize = true, const char[] ip = "127.0.0.1:27005");

	// Connects a bot (a fake client), like CreateFakeClient.
	public static native int ConnectBot(const char[] name);

	public static native int GetRejectReason(char[] buffer, int maxlength);

	public static native void Disconnect(int client, const char[] reason = "Disconnect by user.");

	// Runs a console command as typed by the client. A command of 511
	// characters or more is a native error: the client's own command buffer
	// refuses it. So is a "wait" while sv_allow_wait_command allows waits:
	// the client would send what follows in later frames of its own, which
	// simulated clients do not have.
	public static native void ClientCommand(int client, const char[] format, any ...);

	// Sets one of the client's own cvars, its userinfo: GetClientInfo reads
	// the new value at once; the server takes the change on the next frame,
	// as the engine does (OnClientSettingsChanged, a name change, the admin
	// checks for a new password). QueryClientConVar is not simulated.
	public static native void SetClientConVar(int client, const char[] name, const char[] value);

	public static native void SetClientTeam(int client, int team);
	public static native void SetClientAlive(int client, bool alive);
	public static native void SetClientHealth(int client, int health);

	// Whether the client received a message of the kind that contains text,
	// as it was sent or without its chat color codes.
	public static native bool ClientReceived(int client, RunnerMessage kind, const char[] text);

	// How many messages of the kind the client received.
	public static native int ClientMessageCount(int client, RunnerMessage kind = RunnerMessage_Any);

	// The index-th message (from 0) of any kind; returns its kind, or -1
	// when there is no such message.
	public static native RunnerMessage GetClientMessage(int client, int index, char[] buffer, int maxlength);

	public static native void ClearClientMessages(int client);

	/* ---------------------------------------------------------- menus */

	// The radio menu on the client's screen: its text as the client shows it
	// ("->1. Item" for a key it takes). False, with an empty buffer, when the
	// client shows none: never shown, closed by a key it takes, or its hold
	// time ran out.
	public static native bool GetClientMenu(int client, char[] buffer, int maxlength);

	// The keys the menu on the client's screen takes, bit 0 for key 1 to bit
	// 9 for key 0; 0 when it shows none.
	public static native int ClientMenuKeys(int client);

	// Draws the menu the client shows in a box (keys it takes green, its
	// navigation cyan, disabled lines dim) for people to read: printed under
	// the test's line in the report, or on standard output in run mode.
	// Colors follow smrunner's --color.
	public static native void PrintClientMenu(int client);

	// The client presses a menu key: "menuselect <key>" as the client sends
	// it (1 to 9, and 10 for the key 0). With no menu on the screen the
	// command goes on as any other client command.
	public static native void SelectMenu(int client, int key);

	// The client presses the key of the one item of its menu that it can
	// select ("->N. label") and whose label contains text. Fails the test
	// when the client shows no menu, no such item or more than one.
	public static native void SelectMenuItem(int client, const char[] text);

	/* --------------------------------------------------------- the test */

	// These need a test: in run mode they throw a native error.

	// The test fails unless a plugin error containing text occurs, and an
	// error that contains it does not fail the test. Each call expects one
	// error.
	public static native void ExpectError(const char[] text);

	// Ends the test as skipped.
	public static native void Skip(const char[] reason, any ...);

	public static native int GetTestName(char[] buffer, int maxlength);

	/* -------------------------------------------------------- natives */

	// Hooks and mocks need a test: in run mode they throw a native error.

	// Replaces a native in every plugin that is not a test plugin.
	public static native void MockNative(const char[] name, RunnerNativeMock mock);

	// Hooks a native in every plugin that is not a test plugin; pre runs
	// before the native, post after it (both may be INVALID_FUNCTION).
	public static native void HookNative(const char[] name, RunnerNativeHook pre, RunnerNativeHook post = INVALID_FUNCTION);

	// Removes a mock or the hooks of a native.
	public static native void UnhookNative(const char[] name);

	// How often plugins that are not test plugins called a hooked or mocked
	// native.
	public static native int NativeCalls(const char[] name);

	/* ---------------------------------------------------- breakpoints */

	// Breakpoints need a test: in run mode they throw a native error.

	// Stops at the first statement at or after line of file (a name or the
	// end of a path, as the plugin was compiled), in every plugin that is
	// not a test plugin, or in the named one. Returns the breakpoint's id;
	// fails the test when no loaded plugin has such a statement.
	public static native int BreakAtLine(const char[] file, int line, RunnerBreakpoint callback, any data = 0, const char[] plugin = "");

	// Stops at the first statement of a function, in every plugin that is
	// not a test plugin, or in the named one. Returns the breakpoint's id;
	// fails the test when no loaded plugin has a function of that name with
	// a statement.
	public static native int BreakAtFunction(const char[] functionName, RunnerBreakpoint callback, any data = 0, const char[] plugin = "");

	public static native void RemoveBreakpoint(int id);

	// How often the breakpoint was reached.
	public static native int BreakpointHits(int id);

	/* ------------------------------------------- variables of plugins */

	// A global variable of a plugin (a cell: int, float, bool, handle), or
	// one cell of a global array.
	public static native any GetPluginValue(Handle plugin, const char[] name, int index = 0);
	public static native void SetPluginValue(Handle plugin, const char[] name, any value, int index = 0);

	// A global string (char array) of a plugin.
	public static native int GetPluginString(Handle plugin, const char[] name, char[] buffer, int maxlength);
	public static native void SetPluginString(Handle plugin, const char[] name, const char[] value);
}

/* ---------------------------------------------------------------------
 * Assertions. A failed assertion records the failure with the test plugin's
 * file and line and stops the running function of the test plugin (with an
 * error that smrunner does not report as a plugin error). The message and
 * its arguments are formatted like Format().
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

	// Fails unless text contains part.
	public static native void StrContains(const char[] text, const char[] part, const char[] message = "", any ...);

	// Fails unless the server console printed text since the server started
	// or the last Runner.ClearConsole().
	public static native void ConsoleContains(const char[] text, const char[] message = "", any ...);

	// Fails when the server console printed text.
	public static native void ConsoleNotContains(const char[] text, const char[] message = "", any ...);

	// Fails unless the client received a message of the kind that contains
	// text, as it was sent or without its chat color codes.
	public static native void ClientReceived(int client, RunnerMessage kind, const char[] text, const char[] message = "", any ...);

	// Fails unless the client shows a menu that contains text.
	public static native void ClientMenu(int client, const char[] text, const char[] message = "", any ...);

	// Fails always.
	public static native void Fail(const char[] message, any ...);
}
