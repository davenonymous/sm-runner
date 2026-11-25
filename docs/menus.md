# Menus

Menus are hard to test by hand: open the menu, read it, press keys, repeat for every case.
smrunner runs SourceMod's own menus, panels and votes in TF2's radio style and records what each
client sees. A test reads the menu as text, presses keys or picks items by their label, and runs
votes to the end.

- [How a client sees a menu](#how-a-client-sees-a-menu)
- [Checking a menu](#checking-a-menu)
- [Selecting items](#selecting-items)
- [Pressing keys](#pressing-keys)
- [Printing a menu](#printing-a-menu)
- [Pages](#pages)
- [Menu timeouts](#menu-timeouts)
- [Panels](#panels)
- [Votes](#votes)
- [The admin menu (Top Menus)](#the-admin-menu-top-menus)
- [What is not simulated](#what-is-not-simulated)

The examples on this page use this plugin:

```sourcepawn
// fruit.sp: sm_fruit shows a menu; the choice is announced in chat.
#include <sourcemod>

public void OnPluginStart() {
	RegConsoleCmd("sm_fruit", Command_Fruit);
}

Action Command_Fruit(int client, int args) {
	Menu menu = new Menu(Handler_Fruit);
	menu.SetTitle("Pick a fruit");
	menu.AddItem("apple", "Apple");
	menu.AddItem("banana", "Banana");
	menu.AddItem("cherry", "Cherry", ITEMDRAW_DISABLED);
	menu.Display(client, 20);
	return Plugin_Handled;
}

int Handler_Fruit(Menu menu, MenuAction action, int client, int item) {
	if (action == MenuAction_Select) {
		char info[32], name[32];
		menu.GetItem(item, info, sizeof(info), _, name, sizeof(name));
		PrintToChat(client, "You picked %s.", name);
	} else if (action == MenuAction_End) {
		delete menu;
	}
	return 0;
}
```

## How a client sees a menu

A radio menu reaches the client as text with numbered lines. smrunner keeps that text exactly as
the client would show it. For the fruit menu, `Runner.GetClientMenu` returns:

```
Pick a fruit
->1. Apple
->2. Banana
3. Cherry
 
->0. Exit
```

- `->N. label` is an item the client can select with key N.
- `N. label` without the arrow is a disabled item. Its key does nothing.
- Previous, Next, Back and Exit appear as SourceMod draws them, for example `->0. Exit`.
- Lines without a number are plain text, such as the title or a panel's text lines.

## Checking a menu

| Native | Does |
|---|---|
| `Assert.ClientMenu(client, text)` | Fails unless the client has a menu open that contains `text`. |
| `Runner.GetClientMenu(client, buffer, maxlength)` | Copies the menu text; returns false (and an empty buffer) when the client has no menu open. |
| `Runner.ClientMenuKeys(client)` | Returns the keys the menu takes: bit 0 for key 1 up to bit 8 for key 9, and bit 9 for key 0. 0 when there is no menu. |

`Assert.ClientMenu` matches any part of the text, including line breaks. This checks the title, an
item, or several lines in order:

```sourcepawn
public void Test_FruitMenu() {
	int alice = Runner.ConnectClient("alice");
	Runner.ClientCommand(alice, "sm_fruit");

	Assert.ClientMenu(alice, "Pick a fruit");
	Assert.ClientMenu(alice, "->1. Apple\n->2. Banana\n");
	Assert.ClientMenu(alice, "\n3. Cherry\n", "a disabled item has no arrow");
	Assert.Equal(Runner.ClientMenuKeys(alice), (1 << 0) | (1 << 1) | (1 << 9), "keys 1, 2 and 0");
}
```

A client has a menu open from the moment it arrives until the client presses a key the menu
takes, its time runs out, another menu replaces it, or the client disconnects.

Every display is also recorded once as a `RunnerMessage_Menu` message with its text (see
[What a client received](clients.md#what-a-client-received)). A new page is a new display.
SourceMod resends a menu every few seconds; these refreshes are not recorded again.

## Selecting items

`Runner.SelectMenuItem(client, text)` presses the key of the one selectable item whose label
contains `text`. The test does not need to know the item's number:

```sourcepawn
public void Test_PickingAFruitTellsIt() {
	int alice = Runner.ConnectClient("alice");
	Runner.ClientCommand(alice, "sm_fruit");

	Runner.SelectMenuItem(alice, "Banana");

	Assert.ClientReceived(alice, RunnerMessage_Chat, "You picked Banana.");
	char text[256];
	Assert.False(Runner.GetClientMenu(alice, text, sizeof(text)), "the menu closed");
}
```

It fails the test when the client has no menu open, when no selectable item matches, or when
more than one does. Disabled items never match. The failure shows the menu, so the reason is visible:

```
  FAIL    Test_CherryCannotBePicked (78 ms)
          Runner.SelectMenuItem failed: no line client 1 can select contains "Cherry"; the menu shows:
          ╭─ Pick a fruit ───────╮
          │ 1. Apple             │
          │ 2. Banana            │
          │ 3. Cherry (disabled) │
          │                      │
          │ 0. Exit              │
          ╰─────────────── 20 s ─╯
           (test_fruit.sp:24)
```

Navigation lines are items too, so `Runner.SelectMenuItem(client, "Next")` turns the page and
`Runner.SelectMenuItem(client, "Exit")` closes the menu.

## Pressing keys

`Runner.SelectMenu(client, key)` presses a key directly, as the client sends it
(`menuselect <key>`). Keys 1 to 9 are themselves, and 10 is key 0:

```sourcepawn
Runner.SelectMenu(alice, 10);    // key 0: Exit
```

Use it to test what happens on a key the menu does not take, or for a panel's fixed keys. When
the client has no menu open, `menuselect` is just another client command, which TF2 takes
without an answer.

## Printing a menu

`Runner.PrintClientMenu(client)` draws the client's open menu in a box, below the test's line in
the report. It is meant for people reading the output while writing a test:

```
  PASS    Test_FruitMenu (75 ms)
          menu of client 1 (alice)
          ╭─ Pick a fruit ───────╮
          │ 1. Apple             │
          │ 2. Banana            │
          │ 3. Cherry (disabled) │
          │                      │
          │ 0. Exit              │
          ╰─────────────── 20 s ─╯
```

The bottom line shows the menu's time. On a terminal, keys the menu takes are green, navigation
lines cyan, disabled lines dim and the title bold. `--color auto|always|never` controls the
colors. The JUnit report never has colors.

The box is for reading only. It drops the `->` markers and adds `(disabled)` to disabled items.
Assertions and `Runner.SelectMenuItem` match the text that `Runner.GetClientMenu` returns, not
the box.

Failures of `Assert.ClientMenu` and `Runner.SelectMenuItem` draw the menu the same way.

## Pages

A menu with more items than fit on one page gets Previous and Next lines. A radio menu page has
room for seven items, because keys 8, 9 and 0 are kept for Previous, Next and Exit. Select the
navigation lines by label:

```sourcepawn
// A menu of "Item 1" to "Item 12": seven items on the first page.
Assert.ClientMenu(alice, "->7. Item 7");
Runner.SelectMenuItem(alice, "Next");
Assert.ClientMenu(alice, "->1. Item 8");
Runner.SelectMenuItem(alice, "Previous");
Assert.ClientMenu(alice, "->1. Item 1");
```

## Menu timeouts

A menu's time follows the simulated clock, so `Runner.Seconds` runs it out:

```sourcepawn
public void Test_MenuClosesAfterItsTime() {
	int alice = Runner.ConnectClient("alice");
	Runner.ClientCommand(alice, "sm_fruit");       // displayed for 20 seconds

	Runner.Seconds(19.0);
	Assert.ClientMenu(alice, "Pick a fruit");

	Runner.Seconds(2.0);
	char text[256];
	Assert.False(Runner.GetClientMenu(alice, text, sizeof(text)));
}
```

A menu displayed with `MENU_TIME_FOREVER` stays open until a key closes it, another menu replaces
it, or the client disconnects:

```sourcepawn
// serverrules.sp: sm_rules shows a menu titled "Server rules" with
// menu.Display(client, MENU_TIME_FOREVER).
public void Test_RulesStayOpen() {
	int alice = Runner.ConnectClient("alice");
	Runner.ClientCommand(alice, "sm_rules");

	Runner.Seconds(600.0);
	Assert.ClientMenu(alice, "Server rules");
}
```

## Panels

Panels work like menus. Their text lines appear as plain lines, their items as numbered lines:

```sourcepawn
// team-admin.sp: sm_teaminfo sends a panel titled "Team information" with the
// text line "Practice every Tuesday", the item "Players online", the disabled
// item "Next scrim (none planned)" and "Close" on key 0.
public void Test_TeamInfoShowsDisabledLinesAndCloses() {
	int player = Runner.ConnectClient("player");
	Runner.ClientCommand(player, "sm_teaminfo");

	Assert.ClientMenu(player, "Team information\nPractice every Tuesday\n");
	Assert.ClientMenu(player, "\n2. Next scrim (none planned)\n", "a disabled line has no arrow");
	Assert.Equal(Runner.ClientMenuKeys(player), (1 << 0) | (1 << 9), "keys 1 and 0");

	Runner.SelectMenu(player, 10);                 // Close, on key 0
	char text[256];
	Assert.False(Runner.GetClientMenu(player, text, sizeof(text)));
}
```

## Votes

SourceMod's vote handler runs as on a server: `DisplayVoteToAll`, `IsVoteInProgress`, the vote
time, `MenuAction_VoteEnd` and `MenuAction_VoteCancel`. Each client votes by selecting an item:

```sourcepawn
// menu-vote.sp: sm_mapvote (vote flag) starts a 20-second vote titled
// "Choose the next map" on the maps of sm_mapvote_maps (by default
// cp_badlands, cp_process_final and koth_viaduct). The winner is announced in
// chat as "[Vote] Next map: <map>", and the map changes to it five seconds later.
public void Test_EveryoneVotesAndTheWinnerIsNext() {
	int alice = Runner.ConnectClient("alice");
	int bob = Runner.ConnectClient("bob");
	int carol = Runner.ConnectClient("carol");
	SetUserFlagBits(alice, ADMFLAG_VOTE);

	Runner.ClientCommand(alice, "sm_mapvote");
	Assert.ClientMenu(carol, "Choose the next map");

	Runner.SelectMenuItem(alice, "koth_viaduct");
	Runner.SelectMenuItem(bob, "koth_viaduct");
	Runner.SelectMenuItem(carol, "cp_badlands");
	Assert.ClientReceived(bob, RunnerMessage_Chat, "[Vote] Next map: koth_viaduct");

	Runner.Seconds(6.0);
	char map[64];
	GetCurrentMap(map, sizeof(map));
	Assert.StrEqual(map, "koth_viaduct");
}
```

The plugin changes the map itself, so the maps it offers must exist on the server
(see [Maps and map changes](server-and-time.md#maps-and-map-changes)):

```sh
smrunner test test_menu_vote.smx --plugin menu-vote.smx --maps cp_process_final,koth_viaduct
```

The vote ends early once everyone voted, as in SourceMod. A vote nobody answers ends after its
time:

```sourcepawn
public void Test_AVoteWithoutVotesEndsAfterItsTime() {
	// ... connect clients, start the 20-second vote ...
	Runner.Seconds(19.0);
	Assert.True(IsVoteInProgress());
	Runner.Seconds(2.0);
	Assert.False(IsVoteInProgress());
}
```

The full example is in [`examples/menu-vote/`](../examples/menu-vote/).

## The admin menu (Top Menus)

SourceMod's Top Menus extension is in every sandbox. Load SourceMod's stock admin menu plugin by
its name with `--plugin adminmenu`, and the test can go through `sm_admin` like an admin would:

```sh
smrunner test test_team_admin.smx --plugin team-admin.smx --plugin adminmenu
```

```sourcepawn
// team-admin.sp adds the category "Team tools" to the admin menu, with the
// item "Announce". It sends the text of sm_teamadmin_announcement (by default
// "Scrim starts in 5 minutes") to everyone as "[Team] <text>".
public void Test_AnnounceFromTheAdminMenu() {
	int admin = Runner.ConnectClient("admin");
	SetUserFlagBits(admin, ADMFLAG_GENERIC | ADMFLAG_CHAT);

	Runner.ClientCommand(admin, "sm_admin");
	Runner.SelectMenuItem(admin, "Team tools");    // the plugin's category
	Runner.SelectMenuItem(admin, "Announce");      // its item

	Assert.ClientReceived(admin, RunnerMessage_Chat, "[Team] Scrim starts in 5 minutes");
}
```

Every stock plugin of SourceMod 1.12 loads by its name the same way, for example `basevotes`,
`basecommands` or `funcommands`. The full example is in
[`examples/team-admin/`](../examples/team-admin/).

## What is not simulated

Only the radio style exists, without menu sounds. The details, such as what happens to the valve
style or to a `ShowMenu` message a plugin sends itself, are listed in
[Limits](limits.md#menus-and-user-messages).
