# Architecture

How smrunner works inside, and where its code lives. This page is for people who want to read or
change smrunner. Using it is covered by the other pages.

- [The big picture](#the-big-picture)
- [Layers](#layers)
- [How a test runs](#how-a-test-runs)
- [The SourcePawn patches](#the-sourcepawn-patches)
- [The SteamWorks stand-in](#the-steamworks-stand-in)
- [Source layout](#source-layout)
- [smrunner's own tests](#smrunners-own-tests)
- [Pinned inputs](#pinned-inputs)

## The big picture

A SourceMod server is a stack: the Source engine at the bottom, SourceMod's core on top of it,
SourceMod's logic library on top of the core, and plugins in the SourcePawn VM. Only the core
talks to the engine. smrunner keeps the top of the stack and replaces the bottom:

```
   smrunner (one process per test, forked by the runner)
   +----------------------------------------------------------------------+
   |  test layer       Assert/Runner natives, hooks, breakpoints, results  |
   |  mocked core      CoreProvider bridge, ISourceMod, timers, players,   |
   |                   cvars, commands, configs, core natives              |
   |  simulated engine console, buffer, clock, map, client slots           |
   +-----------------------^--------------------------^--------------------+
          sourcemod.logic.so (SourceMod, unchanged)   libsourcepawn.so (patched)
                           ^                            ^
                     extensions (.ext.so)          plugins (.smx)
```

smrunner loads the VM (`libsourcepawn.so`) and the logic library (`sourcemod.logic.so`) the way
SourceMod's core does. It hands the logic library its bridge, and drives the same sequence the
core drives from the engine.

## Layers

**Simulated engine** (`packages/runner/src/engine/`). The console with cvars and commands, the
command buffer, the clock, the map and the client slots. It records what the engine delivered to
each client, decoded as TF2's client would show it.

**Mocked core** (`packages/runner/src/core/`). smrunner's stand-in for SourceMod's
engine-dependent core, behind the bridge the logic library expects. Most of it is ported from
SourceMod's core: timers, the player manager, cvars, console commands, `core.cfg`, the next map
manager, user messages, menus and the core natives. KeyValues are smrunner's own.

**Test layer** (`packages/runner/src/testing/`). The natives of `smrunner.inc`, the test session
(failures, expected errors, notes), native hooks, the debugger for breakpoints and variables, and
the test runner that forks a process per test.

## How a test runs

1. `smrunner test` reads each test plugin's publics (`packages/runner/src/smx/`) to find the
   `Test_` functions.
2. For each test, the runner makes a temporary sandbox folder and forks. The parent waits for the
   child's result on a pipe, with the `--timeout`, and removes the sandbox afterwards.
3. The child builds the sandbox (`sandbox.cpp`): SourceMod's configs, gamedata, translations and
   cfg files, links to the plugins and extensions, the stand-in game binaries.
4. It starts the core, as the engine would: startup and `core.cfg`, then the map. On
   `LevelInit` the plugins load, and `OnTestBoot` is called once all of them are loaded, before
   any of them starts.
   On `ServerActivate`, `OnMapStart` runs and the configs are buffered and executed.
5. It calls the test function. Frames the test runs go through the command buffer, the clients'
   userinfo, timers, frame hooks, `OnGameFrame` and think tasks.
6. It shuts the server down (map end, plugin unload, extension unload) and writes the result to
   the pipe.

Run mode does steps 3 to 6 in the runner's own process, with the commands and frames from the
command line instead of a test function.

## The SourcePawn patches

The VM is built from SourceMod's SourcePawn with two patches (`packages/patches/`):

- **`sourcepawn-debug-variables.patch`**: with debug breaks on, the VM validates a plugin's debug
  variable table and refuses entries that spcomp 1.12 writes (built-in constants as globals,
  types its RTTI reader does not know). That would refuse every plugin under a debugger. The
  patch skips the check; smrunner reads the tables itself (`packages/runner/src/smx/`) and skips
  such entries. Breakpoints use the VM's own debug break.
- **`sourcepawn-mutable-natives.patch`**: natives whose binding can be changed after a plugin
  loaded, for hooks and mocks, with every change of a binding reported to the runner. That is how
  a hook follows a provider plugin's reload.

The logic library itself is built unchanged from SourceMod's source list
(`packages/logic/AMBuilder`).

## The SteamWorks stand-in

`packages/steamworks/` is an extension with the file name and extension name of SteamWorks. It loads only inside
smrunner: it finds the runner through `packages/runner/public/smrunner_host.h`, the interface the
runner offers its own extensions (the stand-in's options, the frame number, notes).

Its parts:

- `http.cpp`: the request handles, their natives and callbacks;
- `answerer.cpp`: which answer a request gets, the routes and the request log;
- `pending_request.cpp`: a sent request on its way: headers, chunks, completion, timeouts;
- `answers.cpp`: the mapping file and ranges;
- `url.cpp`: URL pattern matching;
- `passthrough.cpp`: libcurl on a worker thread, redirects within the allowed hosts;
- `recorder.cpp`: record mode;
- `steam.cpp`: the natives and forwards of the server's and clients' Steam state;
- `test_api.cpp`: the natives of `smrunner-steamworks.inc`.

It compiles the runner's `plugin_memory.cpp` into itself, for its checks of plugin memory. Its libraries
(libcurl with Mbed TLS, zlib, nlohmann/json) come from vcpkg (`packages/vcpkg/`), linked
statically.

## Source layout

| Path | Contents |
|---|---|
| `packages/runner/src/main.cpp`, `options.cpp` | Entry point and command line. |
| `packages/runner/src/server.cpp`, `server_spec.cpp` | One server: built from the run's or one test's options. |
| `packages/runner/src/sandbox.cpp` | The server folder. |
| `packages/runner/src/diagnostics.cpp`, `terminal.cpp` | Notes, warnings, fatal errors, terminal colors. |
| `packages/runner/src/engine/` | The simulated engine: `console`, `engine`, `clients`, `bitbuf` (user message buffers), `menu_display` (reads the radio style's text). |
| `packages/runner/src/core/` | The mocked core: `core.cpp` (bridge, startup, map, frames), `timers`, `players`, `convars`, `concmds`, `coreconfig`, `nextmap`, `keyvalues.cpp`, `natives_engine.cpp`, user messages (`usermsgs`, `natives_usermsgs.cpp`), menus (`menus`, `menu_base`, `menu_radio`, `menu_voting`), `runner_host.cpp`. |
| `packages/runner/src/testing/` | The test API: `natives_*.cpp` (one file per part), `test_session`, `test_runner`, `native_hooks`, `debugger`, `plugin_memory`, `menu_box` (drawn menus). |
| `packages/runner/src/smx/` | The reader of a plugin's publics and debug tables. |
| `packages/runner/standin/` | The stand-in game binaries of the sandbox. |
| `packages/runner/public/smrunner_host.h` | The interface for smrunner's own extensions. |
| `packages/steamworks/` | The SteamWorks stand-in. |
| `packages/logic/AMBuilder`, `packages/topmenus/AMBuilder` | SourceMod's logic library and Top Menus extension, from SourceMod's sources. |
| `packages/patches/` | The SourcePawn patches. |
| `packages/vcpkg/` | The stand-in's libraries: manifest and triplets. |
| `scripting/include/` | `smrunner.inc` and `smrunner-steamworks.inc`. |
| `examples/` | Example plugins with their tests. |
| `tests/` | smrunner's own tests. |
| `setup/` | The setup action: `action.yml`, `install.sh` (shared with the test action) and `sourcemod.env`, the pinned SourceMod package. |
| `test/` | The test action: `action.yml` and `run.py`, which plans, compiles, runs and reports. |
| `.github/workflows/ci.yml` | The build, the tests, the actions' test and the release. |

## smrunner's own tests

`tests/run.sh RUNNER` runs every suite with one runner binary. CI runs it with `smrunner` and
`smrunner64`.

| Suite | Covers |
|---|---|
| `tests/selftest/test_basics.sp` (with `target.sp`) | the server, clients, commands, time |
| `tests/selftest/test_hooks.sp` | hooks and mocks, also of a native `hook_provider.sp` provides and `hook_dependent.sp` calls |
| `tests/selftest/test_breakpoints.sp` | breakpoints and variables |
| `tests/selftest/test_keyvalues.sp` | KeyValues |
| `tests/selftest/test_menus.sp` (with `menu_target.sp`) | menus, panels, votes, drawn menus |
| `tests/selftest/test_usermessages.sp` | user messages |
| `tests/selftest/test_topmenus.sp` | Top Menus with the stock adminmenu |
| `tests/selftest/test_cvars.sp` | cvars from the command line |
| `tests/selftest/test_steamworks*.sp` (with `steamworks_target.sp`) | the SteamWorks stand-in: with and without a mapping file, passthrough and record mode against `tests/http_server.py`, replay |
| `tests/selftest/expected_failures.sp` | tests that must each fail with their own message |
| `tests/selftest/run_mode.sp`, `broken_start.sp`, `failing_load.sp` | run mode: the test API, a plugin that fails to start, one that fails to load |
| `examples/*/test_*.sp` | the examples |

`run.sh` also checks the command line, sandboxes and drawn menus directly.

CI's `action` job tests the setup and test actions with the archive the build made: team-admin and
json-status compiled from their sources, and `tests/action/test_action_fails.sp`, whose passing,
failing and skipped test check the action's outputs. `run.sh` does not run that plugin.

A release publishes the archive with its `.sha256` file, which the setup action checks. A tag
`vX.Y.Z` also moves the major tag `vX` to it, for plugins that use the actions at `@vX`.

## Pinned inputs

CI pins every input of the build (SourceMod 1.12.0.7263 and its release package, Metamod:Source,
AMBuild, the vcpkg libraries, SMJansson and SteamWorks' include) by commit or SHA-256, so a
rebuild gets the same result. The pins live in `.github/workflows/ci.yml`, `setup/sourcemod.env`
(SourceMod's release package, which the setup action uses too) and `packages/vcpkg/vcpkg.json`.
