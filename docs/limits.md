# Limits

smrunner simulates a server without a game. Anything that needs the game itself does not exist.
This page lists what does not work, why, and the usual workaround, so you can tell early whether
a plugin can be tested with smrunner.

- [No game: entities, events, sounds](#no-game-entities-events-sounds)
- [Extensions](#extensions)
- [Chat and chat triggers](#chat-and-chat-triggers)
- [Clients](#clients)
- [Menus and user messages](#menus-and-user-messages)
- [Breakpoints and plugin variables](#breakpoints-and-plugin-variables)
- [SteamWorks](#steamworks)
- [Platform and game](#platform-and-game)
- [Known issues](#known-issues)

## No game: entities, events, sounds

There is no game DLL, so there are no entities, send props, data maps, game events, HUD text,
sounds or temp entities. The core natives for them throw a native error that names the native:

```
Native "GetEntProp" is not supported by smrunner (the simulated server has no engine for it)
```

**Workaround:** mock the natives the plugin under test calls, and return what the test needs (see
[Hooks and mocks](hooks-and-mocks.md)). Structure plugins so that game-dependent code sits in a
few functions and the logic can be tested apart from it.

Gamedata signatures and offsets cannot be looked up either: there is no game binary to search.
SourceMod's own game configs still load quietly, because the sandbox has stand-in binaries for
them.

## Extensions

Extensions that need neither the engine nor SourceMod's game-side interfaces load as they are,
for example SMJansson. These do **not** load: SDKTools, SDKHooks, TF2 Tools, DHooks, the real
SteamWorks, and every other extension that needs the engine. Metamod:Source plugins do not load
either.

A plugin that *requires* such an extension fails to load (`plugin my-plugin.smx (evicted)`).

**Workaround:** compile the plugin with the extension optional (`#undef REQUIRE_EXTENSIONS` before
the include), and let the test plugin provide the few natives the plugin calls with
`CreateNative` in its `AskPluginLoad2`. An example is in
[Hooks and mocks](hooks-and-mocks.md#providing-a-native-nobody-provides). For SteamWorks, use the
[stand-in](steamworks.md).

## Chat and chat triggers

Players cannot chat. `say` and `say_team` from a client get `Unknown command: say`, so chat
triggers (`!command`, `/command`), `OnClientSayCommand` and `OnClientSayCommand_Post` never run.

**Workaround:** test the command itself with `Runner.ClientCommand(client, "sm_command")`. A
`!command` trigger on a real server runs the same `sm_command`.

## Clients

- **No command buffer of their own.** A `wait` in `Runner.ClientCommand`'s line is a native
  error. Send the commands after it with another `Runner.ClientCommand` after `Runner.Frames`.
- **TF2's player commands** (`jointeam`, `joinclass`, ...) are not simulated. Set the state
  directly with `Runner.SetClientTeam`, `Runner.SetClientAlive` and `Runner.SetClientHealth`.
- **`QueryClientConVar`** is not simulated. Client cvars set with `Runner.SetClientConVar` are
  readable with `GetClientInfo`.
- **Network statistics** are fixed: latency, loss and choke are 0, the data rate is 80000.
- **The engine's name change rules** (its cooldown, and `(1)` for a taken name) are not
  simulated.
- **A map change disconnects every client.** On a real server they reconnect; in a test, connect
  them again.

## Menus and user messages

- **Only the radio style.** The valve style (ESC dialogs) does not exist:
  `GetMenuStyleHandle(MenuStyle_Valve)` throws a native error. Plugins that ask for the default
  style get the radio style.
- **No menu sounds.**
- **A menu displayed while a user message is being sent** (for example from a user message hook)
  is reported as a plugin error, which fails the test. On a real server, SourceMod's radio style
  crashes there.
- **A `ShowMenu` user message a plugin sends itself** is recorded by name, not as the client's
  menu, so `Runner.GetClientMenu` and `Runner.SelectMenuItem` do not see it.
- **User messages are TF2's bit buffers.** There are no protobuf natives, as on TF2. Extensions
  cannot reach user messages (`IUserMessages` is not offered).

## Breakpoints and plugin variables

- Plugins must be compiled by spcomp 1.11 or later, with debug information (the default).
- Variables of enum structs, and arrays of more than one dimension, cannot be read by name.
- Breakpoints do not fire inside a breakpoint's callback.

## SteamWorks

The stand-in fakes HTTP and the Steam state of the server and its clients, but not Game
Coordinator messages or user stats. The full list is in
[What is not faked](steamworks.md#what-is-not-faked).

## Platform and game

- **Linux only**, x86 and x86_64, glibc 2.35 or later.
- **TF2 only.** The simulated server is a TF2 server: its game folder is `tf/`, its user
  messages are TF2's, and unknown commands get TF2's answers.
- **SourceMod 1.12.** smrunner ships SourceMod 1.12.0.7263's logic library, configs, gamedata,
  translations and stock plugins.

## Known issues

Bugs that are known and not fixed yet. The other pages link here.

- **A crash with a failed plugin.** When a plugin under test failed to load, a test that hooks
  or mocks a native, or sets a breakpoint, crashes (`CRASH ... Segmentation fault`) instead of
  failing with the load error. Fix the load failure first, or run the test without the hook to
  see it.
- **A line breakpoint on a function's first statement never fires**, unless that statement
  declares a local variable (`char message[64];`). `Runner.BreakAtLine` accepts the line, but the
  plugin does not stop there. Break at the function's header line or use
  `Runner.BreakAtFunction` instead, which stops right before the first statement.
- **A breakpoint set in `OnTestBoot` fires twice** for each time the plugin reaches it, and
  `Runner.BreakpointHits` counts both. Set breakpoints in the test function where possible.
- **`--frames 0` in run mode** never runs the `--command`s. Use `--frames 1` or more.
- **A client's language does not follow `cl_language` changes.** smrunner reads `cl_language`
  only when the client connects. A later `Runner.SetClientConVar(client, "cl_language", ...)`
  neither changes `GetClientLanguage` nor fires `OnClientLanguageChanged`, while SourceMod on a
  server does both. Test other languages with the server's language instead (see
  [Languages and translations](clients.md#languages-and-translations)).
- **Counting after `Runner.UnhookNative`.** Plugins that load after the unhook, for example with
  `sm plugins load`, are wrapped again (without callbacks), and their calls are counted by
  `Runner.NativeCalls`.
