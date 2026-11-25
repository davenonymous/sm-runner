# Clients

Most plugins react to players: they greet them, check their admin rights, answer their commands
and send them messages. smrunner's simulated clients go through SourceMod's real connect sequence
and record every message the server sends them. This page covers connecting clients, acting as
them, and checking what they received.

- [Connecting a client](#connecting-a-client)
- [Steam IDs and IP addresses](#steam-ids-and-ip-addresses)
- [Rejected clients and a full server](#rejected-clients-and-a-full-server)
- [Authorization](#authorization)
- [Bots](#bots)
- [Disconnecting and kicking](#disconnecting-and-kicking)
- [Admins](#admins)
- [Client commands](#client-commands)
- [Client cvars and userinfo](#client-cvars-and-userinfo)
- [Team, alive state and health](#team-alive-state-and-health)
- [What a client received](#what-a-client-received)
- [Chat colors](#chat-colors)
- [Languages and translations](#languages-and-translations)

Examples that expect `Welcome, <name>!` test `greeter.sp` from the
[README's quick start](../README.md#quick-start). Other examples name their plugin in a comment.

## Connecting a client

`Runner.ConnectClient(name)` connects a player and returns its client index:

```sourcepawn
public void Test_NewPlayersAreGreeted() {
	int alice = Runner.ConnectClient("alice");
	Assert.ClientReceived(alice, RunnerMessage_Chat, "Welcome, alice!");
}
```

The client goes through SourceMod's whole sequence, as on a server: `OnClientConnect`,
`OnClientConnected`, `OnClientPutInServer`, `OnClientAuthorized` and `OnClientPostAdminCheck`.
By default the call returns after `OnClientPostAdminCheck`, so the test can assert right away. To
get there it runs frames until SourceMod's next authorization check, about 0.7 seconds of game
time; timers and buffered commands run during them (see
[The simulated clock](server-and-time.md#the-simulated-clock)).

The full signature is:

```sourcepawn
int Runner.ConnectClient(const char[] name, const char[] steamId = "", bool authorize = true,
                         const char[] ip = "127.0.0.1:27005");
```

Names longer than 31 bytes are truncated: the engine cuts them to 31 bytes, and SourceMod then to
whole UTF-8 characters. The client's language comes from its `cl_language` (`english` by
default); see [Languages and translations](#languages-and-translations).

## Steam IDs and IP addresses

Without a Steam ID, smrunner makes up a unique one for each client. Give one when the plugin cares
about identities, for example for admins or bans. All three common forms work. Any other text is
a native error:

```sourcepawn
int alice = Runner.ConnectClient("alice", "STEAM_0:1:23");        // Steam2
int bob   = Runner.ConnectClient("bob",   "[U:1:47]");            // Steam3
int carol = Runner.ConnectClient("carol", "76561197960265775");   // SteamID64
```

`GetClientAuthId` then returns the ID in whatever form the plugin asks for:

```sourcepawn
public void Test_ClientIdentity() {
	int alice = Runner.ConnectClient("alice", "STEAM_0:1:23", true, "10.0.0.5:27005");

	char auth[32];
	GetClientAuthId(alice, AuthId_Steam3, auth, sizeof(auth));
	Assert.StrEqual(auth, "[U:1:47]");

	char ip[32];
	GetClientIP(alice, ip, sizeof(ip));
	Assert.StrEqual(ip, "10.0.0.5");
}
```

The `ip` parameter includes the port. `GetClientIP` returns the address without the port, or with
it when its `remport` parameter is false.

## Rejected clients and a full server

`Runner.ConnectClient` returns 0 when the client did not get in: a plugin rejected it in
`OnClientConnect`, the server is full, or it was kicked during the connect.
`Runner.GetRejectReason(buffer, maxlength)` tells why:

```sourcepawn
// banlist.sp rejects "mallory" in OnClientConnect with "You are banned".
public void Test_BannedPlayersCannotJoin() {
	int mallory = Runner.ConnectClient("mallory");

	Assert.Equal(mallory, 0);
	char reason[64];
	Runner.GetRejectReason(reason, sizeof(reason));
	Assert.StrEqual(reason, "You are banned");
}
```

The server has 24 client slots by default. Change it with `--maxplayers N`. When all slots are
taken, the reason is `Server is full`.

## Authorization

On a real server, a player's Steam ID is validated a moment after the connect. With
`authorize = false`, `Runner.ConnectClient` returns before that happens, right after
`OnClientPutInServer`. This lets a test look at the time between connect and authorization:

```sourcepawn
public void Test_GreetedOnlyAfterTheAdminCheck() {
	int alice = Runner.ConnectClient("alice", "", false);
	Assert.False(IsClientAuthorized(alice));
	Assert.False(Runner.ClientReceived(alice, RunnerMessage_Chat, "Welcome"));

	Runner.Seconds(1.0);
	Assert.True(IsClientAuthorized(alice));
	Assert.ClientReceived(alice, RunnerMessage_Chat, "Welcome, alice!");
}
```

SourceMod checks for authorized clients every 0.7 seconds of game time, so one second is enough.
`authorize = true` runs frames, so it cannot be used inside a frame (see
[Server, time and commands](server-and-time.md#where-frames-cannot-run)).

## Bots

Plugins often treat bots differently from players: no greeting, no stats, no votes.
`Runner.ConnectBot(name)` connects a bot, as `CreateFakeClient` does. `IsFakeClient` is true for
it. `GetClientAuthId` returns `BOT` for `AuthId_Steam2`, `AuthId_Steam3` and `AuthId_Engine`, and
fails (returns false) for `AuthId_SteamID64`:

```sourcepawn
public void Test_BotsAreNotGreeted() {
	int bot = Runner.ConnectBot("Bot01");
	Assert.True(IsFakeClient(bot));
	Assert.Equal(Runner.ClientMessageCount(bot), 0);
}
```

## Disconnecting and kicking

`Runner.Disconnect(client, reason = "Disconnect by user.")` disconnects a client, with
`OnClientDisconnect` and `OnClientDisconnect_Post`.

A plugin's `KickClient` kicks on the next frame, as on a real server. Run a frame before
asserting the kick. The kick reason is recorded as a `RunnerMessage_Kick` message:

```sourcepawn
// closeserver.sp: the admin command sm_kickall (kick flag) kicks everyone
// else with "Server is closing".
public void Test_AdminCanKickEveryone() {
	int alice = Runner.ConnectClient("alice");
	int admin = Runner.ConnectClient("admin");
	SetUserFlagBits(admin, ADMFLAG_KICK);

	Runner.ClientCommand(admin, "sm_kickall");
	Runner.Frames(1);                       // KickClient kicks on the next frame

	Assert.False(IsClientConnected(alice));
	Assert.ClientReceived(alice, RunnerMessage_Kick, "Server is closing");
}
```

A client's received messages stay readable after it has disconnected, until another client takes
its slot. A map change disconnects every client (see
[Maps and map changes](server-and-time.md#maps-and-map-changes)).

## Admins

There is no special API for admins. Use SourceMod's own natives. The quickest way is to give a
connected client flags:

```sourcepawn
int admin = Runner.ConnectClient("admin");
SetUserFlagBits(admin, ADMFLAG_GENERIC | ADMFLAG_KICK);
```

To test a plugin's own admin checks at connect time, create the admin before the client connects.
SourceMod matches it by Steam ID, name or IP during the admin check:

```sourcepawn
// adminwelcome.sp: OnClientPostAdminCheck prints "[Admin] Welcome back" in
// chat to clients with the generic admin flag.
public void Test_AdminsAreWelcomedBack() {
	AdminId admin = CreateAdmin("alice");
	admin.BindIdentity(AUTHMETHOD_STEAM, "STEAM_0:1:23");
	admin.SetFlag(Admin_Generic, true);

	int alice = Runner.ConnectClient("alice", "[U:1:47]");   // the same account

	Assert.ClientReceived(alice, RunnerMessage_Chat, "[Admin] Welcome back");
}
```

Admin files work too. SourceMod reads them with its stock `admin-flatfile` plugin, which is not
loaded unless you ask for it. Copy the file into the sandbox with `--file` and load the plugin
by name:

```sh
smrunner test test_admins.smx --plugin my-plugin.smx --plugin admin-flatfile \
    --file admins_simple.ini=addons/sourcemod/configs/admins_simple.ini
```

## Client commands

`Runner.ClientCommand(client, format, ...)` runs a console command as if the client typed it:

```sourcepawn
// poke.sp: sm_poke <target> tells the target "<name> poked you." in chat;
// without an argument it replies "Usage: sm_poke <target>".
public void Test_PokeFindsTargets() {
	int alice = Runner.ConnectClient("alice");
	int bob = Runner.ConnectClient("bob");

	Runner.ClientCommand(alice, "sm_poke bo");             // a partial name, as typed
	Assert.ClientReceived(bob, RunnerMessage_Chat, "alice poked you.");

	Runner.ClientCommand(alice, "sm_poke");
	Assert.ClientReceived(alice, RunnerMessage_Console, "Usage: sm_poke <target>");
}
```

The command goes through everything SourceMod does with a client command: command listeners,
admin checks of `RegAdminCmd` commands, `OnClientCommand` and target patterns such as `#userid`,
`@all`, `@me` and partial names. A reply with `ReplyToCommand` arrives in the client's console.

Details:

- **Several commands.** The line is split at `;` and line breaks, outside quotes and `//`
  comments, as a client does.
- **Unknown commands** get TF2's answer `Unknown command: <name>` in the client's console. This
  includes TF2's own player commands such as `jointeam` and `joinclass`, which are not
  simulated, and `say`. (The server console's own `Unknown command "<name>"`, with quotes, is a
  different message.) Chat triggers (`!command`) are not simulated either; see
  [Limits](limits.md).
- **Engine commands** such as `exec` or `quit` are not run for a client. The client gets
  `Unknown command: <name>`, as for any command nothing takes.
- **Too long.** A command of 511 characters or more is a native error, because a real client
  refuses to send it.
- **`wait`.** A `wait` in the line is a native error while `sv_allow_wait_command` is 1. A real
  client runs the commands after a `wait` in later frames of its own, which simulated clients do
  not have. Send them with a second `Runner.ClientCommand` after `Runner.Frames`.

A plugin's `FakeClientCommand` works the same way. A plugin's `ClientCommand(client, ...)` sends
the command to the client, where it is recorded as a `RunnerMessage_Command` message.

## Client cvars and userinfo

`Runner.SetClientConVar(client, name, value)` changes one of the client's own cvars, such as
`name` or `cl_language`. `GetClientInfo` reads the new value at once. The server takes the change
on the next frame, as the engine does: `OnClientSettingsChanged` runs, a new `name` becomes the
client's name, and a new admin password is checked. A new `cl_language` does not change the
client's language yet (a [known issue](limits.md#known-issues)).

```sourcepawn
public void Test_NameChange() {
	int alice = Runner.ConnectClient("alice");
	Runner.SetClientConVar(alice, "name", "Alicia");

	Runner.Frames(1);

	char name[64];
	GetClientName(alice, name, sizeof(name));
	Assert.StrEqual(name, "Alicia");
}
```

`GetClientInfo` returns `""` for a cvar the client does not have. `QueryClientConVar` is not
simulated.

## Team, alive state and health

There is no game, so nothing sets a client's team, alive state or health. The test sets what
`GetClientTeam`, `IsPlayerAlive` and `GetClientHealth` return:

```sourcepawn
public void Test_TeamAliveHealth() {
	int alice = Runner.ConnectClient("alice");
	Runner.SetClientTeam(alice, 2);       // TF2: 2 is RED, 3 is BLU
	Runner.SetClientAlive(alice, true);
	Runner.SetClientHealth(alice, 125);

	Assert.Equal(GetClientTeam(alice), 2);
	Assert.True(IsPlayerAlive(alice));
	Assert.Equal(GetClientHealth(alice), 125);
}
```

Changing these does not fire game events, because there are none (see [Limits](limits.md)).

## What a client received

Every message the server sends to a client is recorded, in order, with its kind:

| Kind | Recorded for |
|---|---|
| `RunnerMessage_Chat` | `PrintToChat`, `PrintToChatAll`, the `SayText` and `SayText2` user messages |
| `RunnerMessage_Console` | `PrintToConsole`, `ReplyToCommand` to a client, `Unknown command: <name>` answers |
| `RunnerMessage_Center` | `PrintCenterText` |
| `RunnerMessage_Hint` | `PrintHintText` |
| `RunnerMessage_Command` | `ClientCommand` sent to the client, replicated cvars |
| `RunnerMessage_Kick` | the reason the client was kicked with |
| `RunnerMessage_Menu` | each menu or panel display (see [Menus](menus.md)) |
| `RunnerMessage_UserMessage` | any other user message, one without text, recorded by its name (for example `Shake`) |
| `RunnerMessage_Any` | matches every kind when passed to the natives below |

The natives to read them:

| Native | Does |
|---|---|
| `Runner.ClientReceived(client, kind, text)` | Returns whether a message of `kind` contained `text`. |
| `Assert.ClientReceived(client, kind, text)` | Fails unless a message of `kind` contained `text`. |
| `Runner.ClientMessageCount(client, kind = RunnerMessage_Any)` | Counts the messages of `kind`. |
| `Runner.GetClientMessage(client, index, buffer, maxlength)` | Copies message `index` (from 0) and returns its kind, or -1 when there is none. -1 is also the value of `RunnerMessage_Any`. |
| `Runner.ClearClientMessages(client)` | Forgets the client's messages so far. |

`Runner.GetClientMessage` walks the log in order, across all kinds:

```sourcepawn
// greeter.sp (see the README) greets every player in chat.
public void Test_MessageLog() {
	int alice = Runner.ConnectClient("alice");       // message 0: the welcome chat line
	PrintCenterText(alice, "center");
	PrintToConsole(alice, "console");

	char text[64];
	Assert.Equal(Runner.GetClientMessage(alice, 0, text, sizeof(text)), RunnerMessage_Chat);
	Assert.StrEqual(text, "Welcome, alice!");
	Assert.Equal(Runner.GetClientMessage(alice, 1, text, sizeof(text)), RunnerMessage_Center);
	Assert.Equal(Runner.GetClientMessage(alice, 2, text, sizeof(text)), RunnerMessage_Console);
	Assert.StrEqual(text, "console\n");
	Assert.Equal(Runner.GetClientMessage(alice, 3, text, sizeof(text)), -1, "no more messages");
}
```

Chat, center and hint text arrive as the user messages a TF2 client gets (`TextMsg`, `SayText`,
`SayText2`, `HintText`), also when a plugin sends those user messages itself. So a test plugin's
`HookUserMessage` hook sees them. Console text (`PrintToConsole`, `ReplyToCommand`) goes to the
client's console directly, as on a server, not as a user message.

Text is recorded as the client would show it: `%s1` to `%s4` are replaced by the parameters. A
localization token is kept with its parameters after it, for example `#TF_Chat_All (alice, hi)`.

To check the content of other user messages, hook them in the test plugin with
`HookUserMessage`.

The other kinds work the same way. A hint and a command sent to the client:

```sourcepawn
public void Test_HintAndCommand() {
	int alice = Runner.ConnectClient("alice");
	PrintHintText(alice, "Capture the point!");
	ClientCommand(alice, "playgamesound ui/duel_challenge.wav");

	Assert.ClientReceived(alice, RunnerMessage_Hint, "Capture the point!");
	Assert.ClientReceived(alice, RunnerMessage_Command, "playgamesound ui/duel_challenge.wav");
}
```

## Chat colors

`Runner.ClientReceived` and `Assert.ClientReceived` match the text as it was sent, and also with
its color codes removed. So a test does not need to know the colors to check the words:

```sourcepawn
// The plugin sends "\x07FFD700[Team]\x01 Scrim at eight" (a gold tag).
Assert.ClientReceived(player, RunnerMessage_Chat, "[Team] Scrim at eight");          // without colors
Assert.ClientReceived(player, RunnerMessage_Chat, "\x07FFD700[Team]\x01 Scrim at eight");  // exactly
```

The removed codes are `\x01` to `\x06`, `\x07RRGGBB` and `\x08RRGGBBAA`.
`Runner.GetClientMessage` returns the text as it was sent, with its colors.

## Languages and translations

SourceMod's translations work as on a server. A plugin's phrase file goes into the sandbox's
`translations/` folder with `--file`, and `%T` formats a phrase in a client's language.

A client's language comes from its `cl_language` when it connects: `english`, since simulated
clients connect with that. To test another language, set the server's language and make SourceMod
ignore `cl_language`, both with `--core`:

```sourcepawn
// polyglot.sp loads rules.phrases and greets each player with
// PrintToChat(client, "%T", "Welcome", client, name). Its phrase file has
// "en" "Welcome, {1}!" and "de" "Willkommen, {1}!".
public void Test_GermanGreeting() {
	int anna = Runner.ConnectClient("anna");
	Assert.ClientReceived(anna, RunnerMessage_Chat, "Willkommen, anna!");
}
```

```sh
smrunner test test_polyglot.smx --plugin polyglot.smx \
    --file rules.phrases.txt=addons/sourcemod/translations/rules.phrases.txt \
    --core ServerLang=de --core AllowClLanguageVar=Off
```

Changing `cl_language` with `Runner.SetClientConVar` after the connect does not change the
client's language. This is a known issue; see [Limits](limits.md#known-issues).
