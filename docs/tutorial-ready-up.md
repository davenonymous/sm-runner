# Tutorial: testing a ready-up plugin

This tutorial tests a realistic plugin from start to finish. The plugin is a ready-up system for
scrims: before a match, every player marks themselves as ready, and the match can start once
everyone is. Testing it by hand needs several players on a server at once. With smrunner, each
case takes a few lines and runs in milliseconds.

You will learn how to:

- connect several clients and run their commands,
- check chat messages and the client's console,
- change a cvar for one test,
- read a panel, select its items and let it time out,
- read a failure.

It assumes smrunner is installed in `/opt/smrunner` (see the
[README](../README.md#installation)).

- [The plugin](#the-plugin)
- [Step 1: the first test](#step-1-the-first-test)
- [Step 2: run it](#step-2-run-it)
- [Step 3: see a test fail](#step-3-see-a-test-fail)
- [Step 4: test the rules](#step-4-test-the-rules)
- [Step 5: test the panel](#step-5-test-the-panel)
- [Step 6: test the panel's timeout](#step-6-test-the-panels-timeout)
- [The complete test plugin](#the-complete-test-plugin)
- [What to read next](#what-to-read-next)

## The plugin

Save this plugin as `readyup.sp`. It offers three commands:

| Command | Does |
|---|---|
| `sm_ready` | Marks you ready and tells everyone in chat. |
| `sm_unready` | Takes it back. |
| `sm_readylist` | Shows a panel of who is ready, with a "Ready up" item. |

When all players in the game are ready, and there are at least `sm_readyup_min` of them (default
2), it announces that the match can start. Bots don't count.

```sourcepawn
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>

public Plugin myinfo = {
	name = "ready-up",
	author = "example",
	description = "Players mark themselves ready for a scrim",
	version = "1.0.0",
	url = ""
};

#define TAG "\x04[Ready]\x01 "

ConVar g_minPlayers;
bool g_ready[MAXPLAYERS + 1];

public void OnPluginStart() {
	g_minPlayers = CreateConVar("sm_readyup_min", "2", "Players needed before the match can start", _, true, 1.0);
	RegConsoleCmd("sm_ready", Command_Ready, "Mark yourself as ready");
	RegConsoleCmd("sm_unready", Command_Unready, "Take back your ready");
	RegConsoleCmd("sm_readylist", Command_ReadyList, "Show who is ready");
}

public void OnClientDisconnect(int client) {
	g_ready[client] = false;
}

Action Command_Ready(int client, int args) {
	if (client == 0) {
		ReplyToCommand(client, "[Ready] Only players can be ready.");
		return Plugin_Handled;
	}
	if (g_ready[client]) {
		ReplyToCommand(client, "[Ready] You are already ready.");
		return Plugin_Handled;
	}
	g_ready[client] = true;
	PrintToChatAll(TAG ... "%N is ready.", client);
	CheckAllReady();
	return Plugin_Handled;
}

Action Command_Unready(int client, int args) {
	if (client == 0 || !g_ready[client]) {
		ReplyToCommand(client, "[Ready] You are not ready.");
		return Plugin_Handled;
	}
	g_ready[client] = false;
	PrintToChatAll(TAG ... "%N is no longer ready.", client);
	return Plugin_Handled;
}

void CheckAllReady() {
	int players = 0;
	for (int i = 1; i <= MaxClients; i++) {
		if (IsClientInGame(i) && !IsFakeClient(i)) {
			players++;
			if (!g_ready[i]) {
				return;
			}
		}
	}
	if (players >= g_minPlayers.IntValue) {
		PrintToChatAll(TAG ... "All %d players are ready. The match can start!", players);
	}
}

Action Command_ReadyList(int client, int args) {
	if (client == 0) {
		return Plugin_Handled;
	}
	Panel panel = new Panel();
	panel.SetTitle("Ready players");
	char line[64];
	for (int i = 1; i <= MaxClients; i++) {
		if (IsClientInGame(i) && !IsFakeClient(i)) {
			Format(line, sizeof(line), "%N: %s", i, g_ready[i] ? "ready" : "not ready");
			panel.DrawText(line);
		}
	}
	panel.DrawItem("Ready up");
	panel.DrawItem("Close");
	panel.Send(client, PanelHandler_ReadyList, 30);
	delete panel;
	return Plugin_Handled;
}

int PanelHandler_ReadyList(Menu menu, MenuAction action, int client, int key) {
	if (action == MenuAction_Select && key == 1) {
		FakeClientCommand(client, "sm_ready");
	}
	return 0;
}
```

> **Why `sm_ready` and not `!ready`?** On a real server, players would type `!ready` in chat,
> and SourceMod turns that into `sm_ready`. smrunner does not simulate chat or chat triggers
> (see [Limits](limits.md#chat-and-chat-triggers)), so the tests run `sm_ready` directly. That
> tests the same code.

## Step 1: the first test

Create `test_readyup.sp`. A test plugin includes `smrunner.inc`, and every public function whose
name starts with `Test_` is a test:

```sourcepawn
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <smrunner>

public void Test_ReadyIsAnnouncedToEveryone() {
	int alice = Runner.ConnectClient("alice");
	int bob = Runner.ConnectClient("bob");

	Runner.ClientCommand(alice, "sm_ready");

	Assert.ClientReceived(bob, RunnerMessage_Chat, "[Ready] alice is ready.");
	Assert.ClientReceived(alice, RunnerMessage_Chat, "[Ready] alice is ready.");
}
```

What happens here:

- `Runner.ConnectClient("alice")` connects a player named alice and returns her client index.
  It returns once she is fully in the game, after `OnClientPostAdminCheck`.
- `Runner.ClientCommand(alice, "sm_ready")` runs `sm_ready` as if alice typed it in her
  console.
- `Assert.ClientReceived(bob, RunnerMessage_Chat, ...)` checks that bob got a chat line
  containing the text. The plugin's green `[Ready]` tag has color codes (`\x04`, `\x01`); the
  assertion also matches the text without them, so the test does not need to spell them out.

## Step 2: run it

Compile both plugins with spcomp; the test plugin needs `/opt/smrunner/share/include` on its
include path. Then run the test. The test plugin is the first argument, the plugin under test
comes with `--plugin`:

```sh
/opt/smrunner/bin/smrunner test test_readyup.smx --plugin readyup.smx
```

```
test_readyup.smx
  PASS    Test_ReadyIsAnnouncedToEveryone (78 ms)
1 tests: 1 passed, 0 failed, 0 skipped
```

The test ran on a server of its own, which smrunner started, ran and shut down in 78 ms.

## Step 3: see a test fail

A test is only useful if it fails when the plugin is wrong. Change the expected text to
`"[Ready] Alice is ready."` (capital A) and run it again:

```
test_readyup.smx
  FAIL    Test_ReadyIsAnnouncedToEveryone (72 ms)
          Assert.ClientReceived failed: client 2 did not receive "[Ready] Alice is ready." (test_readyup.sp:13)
          --- console (last lines; --echo shows all) ---
          | Unknown command "sm_flood_time"
          | ...
1 tests: 0 passed, 1 failed, 0 skipped
```

The failure names the assertion, the client (bob is client 2), the text and the line of the test.
The exit code is 1, so a CI job fails. The `Unknown command` lines in the console are normal (see
[Troubleshooting](troubleshooting.md#noise-in-the-console)). Change the text back.

## Step 4: test the rules

Now the rules of the plugin, one test each. Every test starts on a fresh server, so none of them
needs to clean up after another.

**Ready twice.** The second `sm_ready` is refused. `ReplyToCommand` answers in the client's
console, so the test checks `RunnerMessage_Console`. `Runner.ClientMessageCount` makes sure there
was only one chat announcement:

```sourcepawn
public void Test_ReadyTwiceIsRefused() {
	int alice = Runner.ConnectClient("alice");

	Runner.ClientCommand(alice, "sm_ready");
	Runner.ClientCommand(alice, "sm_ready");

	Assert.ClientReceived(alice, RunnerMessage_Console, "[Ready] You are already ready.");
	Assert.Equal(Runner.ClientMessageCount(alice, RunnerMessage_Chat), 1, "only one chat announcement");
}
```

**Everyone ready.** The match can start once both players are ready. A bot is connected too, to
check that bots don't count. `Runner.ClientReceived` returns a bool, for checks that something
did *not* happen yet:

```sourcepawn
public void Test_MatchCanStartWhenEveryoneIsReady() {
	int alice = Runner.ConnectClient("alice");
	int bob = Runner.ConnectClient("bob");
	Runner.ConnectBot("bot");

	Runner.ClientCommand(alice, "sm_ready");
	Assert.False(Runner.ClientReceived(alice, RunnerMessage_Chat, "The match can start"), "bob is not ready yet");

	Runner.ClientCommand(bob, "sm_ready");
	Assert.ClientReceived(alice, RunnerMessage_Chat, "[Ready] All 2 players are ready. The match can start!");
}
```

**The minimum from the cvar.** A test changes a cvar with SourceMod's own natives. The change
lasts only for this test:

```sourcepawn
public void Test_MinimumPlayersComeFromTheCvar() {
	FindConVar("sm_readyup_min").SetInt(3);
	int alice = Runner.ConnectClient("alice");
	int bob = Runner.ConnectClient("bob");

	Runner.ClientCommand(alice, "sm_ready");
	Runner.ClientCommand(bob, "sm_ready");

	Assert.False(Runner.ClientReceived(alice, RunnerMessage_Chat, "The match can start"), "3 players are needed");
}
```

**Unready.** Taking it back is announced, and the match cannot start while someone is not ready:

```sourcepawn
public void Test_UnreadyTakesItBack() {
	int alice = Runner.ConnectClient("alice");
	int bob = Runner.ConnectClient("bob");

	Runner.ClientCommand(alice, "sm_ready");
	Runner.ClientCommand(alice, "sm_unready");
	Runner.ClientCommand(bob, "sm_ready");

	Assert.ClientReceived(bob, RunnerMessage_Chat, "[Ready] alice is no longer ready.");
	Assert.False(Runner.ClientReceived(bob, RunnerMessage_Chat, "The match can start"));
}
```

**Leaving resets the ready state.** The plugin forgets a player's ready in `OnClientDisconnect`.
`Runner.Disconnect` disconnects alice. The next client takes her slot, and must not inherit her
ready state:

```sourcepawn
public void Test_DisconnectForgetsTheReady() {
	int alice = Runner.ConnectClient("alice");
	Runner.ConnectClient("bob");
	Runner.ClientCommand(alice, "sm_ready");

	Runner.Disconnect(alice);
	int carol = Runner.ConnectClient("carol");

	Assert.Equal(carol, alice, "carol gets alice's slot");
	Runner.ClientCommand(carol, "sm_unready");
	Assert.ClientReceived(carol, RunnerMessage_Console, "[Ready] You are not ready.");
}
```

## Step 5: test the panel

`sm_readylist` shows a panel. `Assert.ClientMenu` checks that the menu a client sees contains a
text. `Runner.PrintClientMenu` draws the menu in the report, which helps while writing the test:

```sourcepawn
public void Test_ReadyListShowsEveryPlayer() {
	int alice = Runner.ConnectClient("alice");
	Runner.ConnectClient("bob");
	Runner.ClientCommand(alice, "sm_ready");

	Runner.ClientCommand(alice, "sm_readylist");

	Runner.PrintClientMenu(alice);
	Assert.ClientMenu(alice, "Ready players");
	Assert.ClientMenu(alice, "alice: ready");
	Assert.ClientMenu(alice, "bob: not ready");
}
```

The report then shows alice's panel:

```
  PASS    Test_ReadyListShowsEveryPlayer (71 ms)
          menu of client 1 (alice)
          ╭─ Ready players ─╮
          │ alice: ready    │
          │ bob: not ready  │
          │ 1. Ready up     │
          │ 2. Close        │
          ╰────────── 30 s ─╯
```

`Runner.SelectMenuItem` presses the key of the item whose label contains a text, so the test
does not depend on the item's number. Selecting "Ready up" runs `sm_ready` for bob through the
panel's handler, and the panel closes:

```sourcepawn
public void Test_ReadyListCanReadyUp() {
	int alice = Runner.ConnectClient("alice");
	int bob = Runner.ConnectClient("bob");

	Runner.ClientCommand(bob, "sm_readylist");
	Runner.SelectMenuItem(bob, "Ready up");

	Assert.ClientReceived(alice, RunnerMessage_Chat, "[Ready] bob is ready.");
	char menu[256];
	Assert.False(Runner.GetClientMenu(bob, menu, sizeof(menu)), "the panel closed");
}
```

## Step 6: test the panel's timeout

The panel stays open for 30 seconds. smrunner's clock is simulated: it only moves when the test
runs frames, and `Runner.Seconds` runs as many frames as that much game time needs. So the test
does not wait 30 real seconds:

```sourcepawn
public void Test_ReadyListClosesAfter30Seconds() {
	int alice = Runner.ConnectClient("alice");
	Runner.ClientCommand(alice, "sm_readylist");

	Runner.Seconds(29.0);
	Assert.ClientMenu(alice, "Ready players");
	Runner.Seconds(2.0);
	char menu[256];
	Assert.False(Runner.GetClientMenu(alice, menu, sizeof(menu)));
}
```

## The complete test plugin

All tests together, as `test_readyup.sp`:

```sourcepawn
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <smrunner>

public void Test_ReadyIsAnnouncedToEveryone() {
	int alice = Runner.ConnectClient("alice");
	int bob = Runner.ConnectClient("bob");

	Runner.ClientCommand(alice, "sm_ready");

	Assert.ClientReceived(bob, RunnerMessage_Chat, "[Ready] alice is ready.");
	Assert.ClientReceived(alice, RunnerMessage_Chat, "[Ready] alice is ready.");
}

public void Test_ReadyTwiceIsRefused() {
	int alice = Runner.ConnectClient("alice");

	Runner.ClientCommand(alice, "sm_ready");
	Runner.ClientCommand(alice, "sm_ready");

	Assert.ClientReceived(alice, RunnerMessage_Console, "[Ready] You are already ready.");
	Assert.Equal(Runner.ClientMessageCount(alice, RunnerMessage_Chat), 1, "only one chat announcement");
}

public void Test_MatchCanStartWhenEveryoneIsReady() {
	int alice = Runner.ConnectClient("alice");
	int bob = Runner.ConnectClient("bob");
	Runner.ConnectBot("bot");

	Runner.ClientCommand(alice, "sm_ready");
	Assert.False(Runner.ClientReceived(alice, RunnerMessage_Chat, "The match can start"), "bob is not ready yet");

	Runner.ClientCommand(bob, "sm_ready");
	Assert.ClientReceived(alice, RunnerMessage_Chat, "[Ready] All 2 players are ready. The match can start!");
}

public void Test_MinimumPlayersComeFromTheCvar() {
	FindConVar("sm_readyup_min").SetInt(3);
	int alice = Runner.ConnectClient("alice");
	int bob = Runner.ConnectClient("bob");

	Runner.ClientCommand(alice, "sm_ready");
	Runner.ClientCommand(bob, "sm_ready");

	Assert.False(Runner.ClientReceived(alice, RunnerMessage_Chat, "The match can start"), "3 players are needed");
}

public void Test_UnreadyTakesItBack() {
	int alice = Runner.ConnectClient("alice");
	int bob = Runner.ConnectClient("bob");

	Runner.ClientCommand(alice, "sm_ready");
	Runner.ClientCommand(alice, "sm_unready");
	Runner.ClientCommand(bob, "sm_ready");

	Assert.ClientReceived(bob, RunnerMessage_Chat, "[Ready] alice is no longer ready.");
	Assert.False(Runner.ClientReceived(bob, RunnerMessage_Chat, "The match can start"));
}

public void Test_ReadyListShowsEveryPlayer() {
	int alice = Runner.ConnectClient("alice");
	Runner.ConnectClient("bob");
	Runner.ClientCommand(alice, "sm_ready");

	Runner.ClientCommand(alice, "sm_readylist");

	Runner.PrintClientMenu(alice);
	Assert.ClientMenu(alice, "Ready players");
	Assert.ClientMenu(alice, "alice: ready");
	Assert.ClientMenu(alice, "bob: not ready");
}

public void Test_ReadyListCanReadyUp() {
	int alice = Runner.ConnectClient("alice");
	int bob = Runner.ConnectClient("bob");

	Runner.ClientCommand(bob, "sm_readylist");
	Runner.SelectMenuItem(bob, "Ready up");

	Assert.ClientReceived(alice, RunnerMessage_Chat, "[Ready] bob is ready.");
	char menu[256];
	Assert.False(Runner.GetClientMenu(bob, menu, sizeof(menu)), "the panel closed");
}

public void Test_ReadyListClosesAfter30Seconds() {
	int alice = Runner.ConnectClient("alice");
	Runner.ClientCommand(alice, "sm_readylist");

	Runner.Seconds(29.0);
	Assert.ClientMenu(alice, "Ready players");
	Runner.Seconds(2.0);
	char menu[256];
	Assert.False(Runner.GetClientMenu(alice, menu, sizeof(menu)));
}

public void Test_DisconnectForgetsTheReady() {
	int alice = Runner.ConnectClient("alice");
	Runner.ConnectClient("bob");
	Runner.ClientCommand(alice, "sm_ready");

	Runner.Disconnect(alice);
	int carol = Runner.ConnectClient("carol");

	Assert.Equal(carol, alice, "carol gets alice's slot");
	Runner.ClientCommand(carol, "sm_unready");
	Assert.ClientReceived(carol, RunnerMessage_Console, "[Ready] You are not ready.");
}
```

Run them:

```
$ /opt/smrunner/bin/smrunner test test_readyup.smx --plugin readyup.smx
test_readyup.smx
  PASS    Test_DisconnectForgetsTheReady (119 ms)
  PASS    Test_MatchCanStartWhenEveryoneIsReady (105 ms)
  PASS    Test_MinimumPlayersComeFromTheCvar (127 ms)
  PASS    Test_ReadyIsAnnouncedToEveryone (112 ms)
  PASS    Test_ReadyListCanReadyUp (111 ms)
  PASS    Test_ReadyListClosesAfter30Seconds (115 ms)
  PASS    Test_ReadyListShowsEveryPlayer (116 ms)
          menu of client 1 (alice)
          ╭─ Ready players ─╮
          │ alice: ready    │
          │ bob: not ready  │
          │ 1. Ready up     │
          │ 2. Close        │
          ╰────────── 30 s ─╯
  PASS    Test_ReadyTwiceIsRefused (144 ms)
  PASS    Test_UnreadyTakesItBack (105 ms)
9 tests: 9 passed, 0 failed, 0 skipped
```

Tests run in alphabetical order. Each one runs in its own process, so their order never matters.

For CI, add `--junit results.xml` to get a report your CI system can show. Run the tests with
`smrunner64` too if the plugin also runs on 64-bit servers (see
[Which runner](command-line.md#which-runner-smrunner-or-smrunner64)). On GitHub, the
[test action](github-actions.md) does all of this: it compiles the plugins, runs both runners and
shows the results in the job summary.

## What to read next

- [Writing tests](writing-tests.md): the test lifecycle, `OnTestBoot`, expected errors.
- [Clients](clients.md): Steam IDs, admins, kicks, every kind of message.
- [Menus](menus.md): menus, pages, votes and the admin menu.
- [Hooks and mocks](hooks-and-mocks.md): what to do when the plugin calls something smrunner
  cannot provide.
