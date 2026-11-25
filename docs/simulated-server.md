# What is simulated

A test is only as good as the server it runs on. This page says what smrunner's simulated server
does, and how close it is to a real TF2 server running SourceMod 1.12. What it does not do is
listed in [Limits](limits.md).

- [The idea: real SourceMod, simulated engine](#the-idea-real-sourcemod-simulated-engine)
- [SourceMod](#sourcemod)
- [The engine](#the-engine)
- [Clients](#clients)
- [User messages](#user-messages)
- [KeyValues](#keyvalues)
- [Extensions and stock plugins](#extensions-and-stock-plugins)
- [Determinism](#determinism)

## The idea: real SourceMod, simulated engine

SourceMod consists of two parts. Its **logic library** (`sourcemod.logic.so`) holds most of what
plugins use and needs no game. Its **core** connects that library to the game engine.

smrunner loads the logic library unchanged, built from SourceMod 1.12's sources, together with
the SourcePawn VM. In place of the core and the engine, it provides a **mocked core** on a
**simulated engine**.

So most behavior is SourceMod's own code, not a copy: if a plugin works in smrunner, the same
SourceMod code runs it on a server. Where smrunner had to provide the core, it ports SourceMod's
own core code and follows the engine's behavior as closely as possible.

## SourceMod

Running unchanged, as SourceMod's own code:

- plugins, their load order, natives created by plugins, forwards;
- handles and their security, ADTs (`ArrayList`, `StringMap`, ...), data packs, files;
- translations and phrases, string formatting;
- admins, groups and overrides;
- the `sm` console commands (`sm plugins`, `sm exts`, ...), plus the core's `sm cvars`, `sm cmds`
  and `sm config`;
- menus, panels and votes in the radio style.

Ported from SourceMod's core:

- cvars with their hooks, console commands, admin commands and command listeners,
  `ServerCommandEx`;
- the timer system and `RequestFrame`;
- the player manager: the connect sequence, authorization, admin checks, target patterns;
- `core.cfg` and `AutoExecConfig`, which writes missing config files from the plugin's cvars;
- the map forwards: `OnMapInit`, `OnMapStart`, `OnAutoConfigsBuffered`, `OnServerCfg`,
  `OnConfigsExecuted`, `OnGameFrame`, `OnMapEnd`;
- the next map and map history (`sm_nextmap`, `GetMapHistory`), trimmed to `sm_maphistory_size`;
- command iterators (`CommandIterator`, `FindFirstConCommand`);
- the half-life natives: time, maps, random numbers, precaching (which lasts one map, as the
  engine's tables do), chat, center and hint text, the engine version;
- vector math, command line and KeyValues natives;
- user messages.

Natives that take a buffer with its size, such as `BfReadString` or `KeyValues.ExportToString`,
throw a native error for a size below 1 or a buffer outside the plugin's memory. On a real server
SourceMod would write past the buffer.

## The engine

- **The console**: cvars and commands, case-insensitive, with the engine's clamping and change
  rules. Commands are split into arguments exactly as the engine does. An unquoted UTF-8 name, for
  example, can split into several arguments, as on a server.
- **The command buffer** with `wait`, `exec`, `echo`, `changelevel`, `map`, `quit` and `exit`.
  See [Server, time and commands](server-and-time.md#the-command-buffer).
- **`server.cfg`**, executed on every map, and the common engine and TF2 cvars, such as
  `hostname`, `sv_cheats`, `mp_timelimit`, `mp_tournament` and `tf_bot_quota`.
- **The clock** and **the map**. See [The simulated clock](server-and-time.md#the-simulated-clock).

## Clients

- SourceMod's connect sequence, authorization (the Steam ID is valid at once and SourceMod checks
  for it every 0.7 seconds) and admin checks by name, IP and Steam ID.
- Reserved names with passwords: a client that uses a reserved name without its password is kicked
  0.1 seconds after the admin checks, as SourceMod does.
- The client's language from its `cl_language` (`english` by default), with
  `OnClientLanguageChanged`, unless `core.cfg`'s `AllowClLanguageVar` is `Off`.
- `OnClientSettingsChanged` when the engine runs it: once during the connect, once after
  `OnClientPutInServer`, and on the frame after each userinfo change.
- Names truncated to the engine's 31 bytes, and by SourceMod to whole UTF-8 characters.
- The client's IP address with its port, its cvars (`GetClientInfo`, case-insensitive), client commands,
  kicks, and every message the client received.

How to use all of this is described in [Clients](clients.md).

## User messages

User messages are TF2's: its message table and bit buffers (`BfWrite`, `BfRead`), not protobuf.

- Plugins send them with `StartMessage`, `StartMessageOne`, `StartMessageAll` and the bit buffer
  natives.
- `HookUserMessage` works with intercept hooks, blocking and post hooks.
- SourceMod's own chat, center and hint text goes through these messages too, as on TF2, so a
  hook on `SayText2` or `TextMsg` sees it.

A test plugin can hook a user message to read its content:

```sourcepawn
int g_shakes;

Action Hook_Shake(UserMsg id, BfRead message, const int[] players, int count, bool reliable, bool init) {
	g_shakes++;
	return Plugin_Continue;
}

public void Test_ShakeIsSent() {
	HookUserMessage(GetUserMessageId("Shake"), Hook_Shake);
	// ... make the plugin send a Shake ...
	Assert.Equal(g_shakes, 1);
}
```

Clients record a user message without text by its name, as a `RunnerMessage_UserMessage`
message (see [What a client received](clients.md#what-a-client-received)).

## KeyValues

KeyValues are smrunner's own implementation, because Valve's lives in the engine. It behaves like
Valve's and offers all of SourceMod's `KeyValues` methods and `Kv*` natives. It differs from a
server in a few points:

- `#include` and `#base` in a file are skipped, with a note on the console.
- Only the first top-level section of a text is read.
- Deleting a key that the traversal still uses (the current key, or one on the traversal stack)
  is a native error. On a server SourceMod would use freed memory.
- A key name keeps the spelling it was first created with, for every tree, as with Valve's
  shared symbol table.
- Pointer and wide-string values cannot be created, and UTF-16 files are not read.
- Extensions cannot read smrunner's KeyValues handles.

## Extensions and stock plugins

- **Extensions** that need neither the engine nor SourceMod's game-side interfaces load as they are,
  for example SMJansson. They load from the sandbox when a plugin requires them, as on a server.
  See [Limits](limits.md#extensions) for those that do not.
- **Top Menus**, built from the same SourceMod sources, is in every sandbox.
- **SteamWorks** has a stand-in. See [SteamWorks](steamworks.md).
- **Stock plugins** of SourceMod 1.12 load by name with `--plugin`, for example
  `--plugin adminmenu`. See [Command line](command-line.md#server-options).
- **Configs, gamedata and translations** of the SourceMod 1.12.0.7263 release are in every
  sandbox.

## Determinism

A test runs the same way every time:

- The clock moves only with the frames a test runs. Real time does not matter.
- The engine's random numbers (`GetRandomInt`, `GetRandomFloat`) start from the seed 0.
  `SetRandomSeed` works as on a server.
- Each test starts from a fresh server in a fresh process.

The exceptions are extensions that work on threads, and passthrough HTTP requests. They run in
real time; wait for them with `Runner.WaitUntil`.
