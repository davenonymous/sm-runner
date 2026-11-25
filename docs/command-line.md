# Command line

Every option of `smrunner`, its exit codes, its reports and the sandbox it builds.

- [Synopsis](#synopsis)
- [Modes](#modes)
- [Server options](#server-options)
- [SteamWorks options](#steamworks-options)
- [Run options](#run-options)
- [Test options](#test-options)
- [Output options](#output-options)
- [Exit codes](#exit-codes)
- [The test report](#the-test-report)
- [JUnit XML](#junit-xml)
- [The sandbox](#the-sandbox)
- [Which runner: smrunner or smrunner64](#which-runner-smrunner-or-smrunner64)

## Synopsis

```
smrunner run  [options] PLUGIN.smx...
smrunner test [options] TEST.smx... [--plugin PLUGIN.smx]...
smrunner list TEST.smx...
smrunner --help
smrunner --version
```

Options can come before or after the plugin files. An option that takes a value accepts it as the
next argument (`--map koth_viaduct`), and the long form also as `--option=value`
(`--map=koth_viaduct`). `--version` is the exception: it only works as the first argument,
`smrunner --version`.

## Modes

| Mode | Does |
|---|---|
| `run` | Loads the plugins on one server, runs `--command`s and `--frames`, fails on any problem. See [Run mode](run-mode.md). |
| `test` | Runs every `Test_` function of the test plugins, each on its own server. See [Writing tests](writing-tests.md). |
| `list` | Prints the tests of the test plugins, one per line as `<plugin> <test>`, without running them. |

```
$ smrunner list test_readyup.smx
test_readyup.smx Test_DisconnectForgetsTheReady
test_readyup.smx Test_MatchCanStartWhenEveryoneIsReady
test_readyup.smx Test_MinimumPlayersComeFromTheCvar
...
```

`--version` prints the versions of smrunner, SourceMod and the SourcePawn API, for example
`smrunner 0.1.0 (SourceMod 1.12.0-manual, SourcePawn API 16)`.

## Server options

These apply to both `run` and `test`.

| Option | Effect |
|---|---|
| `-p`, `--plugin FILE` | Loads a plugin, in the order given. In `run`, plugins can also be plain arguments. |
| `-e`, `--extensions DIR` | Adds every extension in `DIR`. |
| `-x`, `--extension FILE` | Adds one extension (`.ext.so`). |
| `-f`, `--file SRC=DEST` | Copies a file or folder into the game folder. |
| `-c`, `--cvar NAME=VALUE` | Sets a cvar before SourceMod starts, or when it is created. |
| `--core KEY=VALUE` | Sets an option of SourceMod's `core.cfg`. |
| `--map NAME` | The map to start on. Default `cp_badlands`. |
| `--maps NAME,...` | Other maps the server has, comma-separated. |
| `--maxplayers N` | Client slots, 1 to 101. Default 24. |
| `--tickrate N` | Ticks per second, 1 to 1000. Default 66.67. |
| `--no-jit` | Runs plugins in SourcePawn's interpreter instead of the JIT. |
| `--sandbox DIR` | Builds the sandbox in `DIR` and keeps it. |
| `--keep-sandbox` | Keeps the temporary sandbox and prints where it is. |

Details:

- **`--plugin`** takes a path to an `.smx` file. A bare name that is not a file is one of
  SourceMod 1.12's stock plugins, with or without `.smx`: `--plugin adminmenu`,
  `--plugin basevotes`, `--plugin admin-flatfile`. The stock plugins are `admin-flatfile`,
  `admin-sql-prefetch`, `admin-sql-threaded`, `adminhelp`, `adminmenu`, `antiflood`, `basebans`,
  `basechat`, `basecomm`, `basecommands`, `basetriggers`, `basevotes`, `clientprefs`,
  `funcommands`, `funvotes`, `mapchooser`, `nextmap`, `nominations`, `playercommands`,
  `randomcycle`, `reservedslots`, `rockthevote`, `sounds` and `sql-admin-manager`.
- **`--extensions DIR`** expects `DIR` laid out like `addons/sourcemod/extensions`: x86
  extensions at the top, x64 extensions in `x64/`. Point it at an extension's release folder.
  Extensions load when a plugin requires them, as on a server.
- **`--extension FILE`** adds a single `.ext.so`, for example one you just built:
  `--extension build/myext.ext.so`. It goes into the folder of the running binary's
  architecture (`extensions/` for `smrunner`, `extensions/x64/` for `smrunner64`).
- **`--file SRC=DEST`**: `DEST` is relative to the game folder `tf/`. Examples:
  `--file my.cfg=cfg/my.cfg`, `--file admins.ini=addons/sourcemod/configs/admins_simple.ini`,
  `--file data/=addons/sourcemod/data/myplugin`.
- **`--cvar NAME=VALUE`** sets the cvar's value. A cvar that does not exist yet gets the value
  when SourceMod or a plugin creates it; only its value comes from `--cvar`, while its default,
  bounds and flags stay the plugin's. Once the configs have run, a warning names each `--cvar`
  that did not match any cvar. Repeatable.
- **`--core KEY=VALUE`** sets an option of SourceMod's `core.cfg`, for example
  `--core ServerLang=de` (the server's language for translations) or
  `--core AllowClLanguageVar=Off`. Repeatable.
- **`--tickrate N`** changes the length of a frame. With `--tickrate 100`, a frame is 0.01
  seconds, so `Runner.Seconds(1.0)` runs 100 frames.
- **`--no-jit`** runs plugins in SourcePawn's interpreter instead of the JIT. Use it to rule out
  a JIT problem.
- **`--maxplayers N`**, for example `--maxplayers 2`, makes "server full" cases easy to test.
- **`--maps`** matters for a plugin's own `changelevel` or `ForceChangeLevel`, and for
  `IsMapValid`; see [Maps and map changes](server-and-time.md#maps-and-map-changes).
- **`--sandbox DIR`** keeps the sandbox for a look afterwards. In test mode it makes one folder
  per test, `DIR/<test plugin>/<test>/`, so
  `smrunner test test_greeter.smx --plugin greeter.smx --sandbox /tmp/sb` leaves
  `/tmp/sb/test_greeter.smx/Test_NewPlayersAreGreeted/tf/...`. A folder that already holds a
  sandbox is refused.

## SteamWorks options

| Option | Effect |
|---|---|
| `--steamworks [DIR]` | Loads the SteamWorks stand-in. With `DIR`, answers requests from `DIR/steamworks.json`. |
| `--steamworks-passthrough HOST` | Sends requests that nothing else answers to hosts matching `HOST` over the network. Repeatable. |
| `--steamworks-record DIR` | Writes the real answers of passthrough requests into `DIR/steamworks.json` and body files. |

`--steamworks` takes the next argument as `DIR` only if it is an existing folder. Write
`--steamworks=DIR` to be explicit. `--steamworks-passthrough` and `--steamworks-record` need
`--steamworks`. See [SteamWorks](steamworks.md).

## Run options

Only for `run`.

| Option | Effect |
|---|---|
| `--command CMD` | A server command, run on the first frame. Repeatable. |
| `--frames N` | Frames to run. Default 66. With `--frames 0` the commands never run. |

## Test options

Only for `test`.

| Option | Effect |
|---|---|
| `--filter TEXT` | Runs only the tests whose name contains `TEXT` (case-sensitive): `--filter ReadyList` runs the three `Test_ReadyList...` tests. |
| `--timeout SECONDS` | Real-time limit of each test, 1 to 86400. Default 30. |
| `--junit FILE` | Also writes a JUnit XML report to `FILE`. |
| `--echo` | Prints the server console of every test while it runs. |

## Output options

| Option | Effect |
|---|---|
| `-v`, `--verbose` | Prints smrunner's own notes, for example where the sandbox is and which requests the SteamWorks stand-in could not answer. |
| `-q`, `--quiet` | No warnings (in `run` and `test`, for example the `--cvar` warning), and in `run` no console echo. |
| `--color WHEN` | Colors of drawn menus: `auto` (default: on when standard output is a terminal and `NO_COLOR` is not set), `always` or `never`. |
| `--home DIR` | smrunner's install folder. Default: the folder above `bin/` of the running binary. Only needed when the binary is run from somewhere else, for example through a copy or a hard link. |
| `-h`, `--help` | Prints the usage. |
| `--version` | Prints the versions. Only as the first argument. |

## Exit codes

| Code | Meaning |
|---|---|
| 0 | No test failed (skipped tests do not count as failures), or the run was clean. |
| 1 | A test failed, timed out or crashed; or the run failed; or no test matched `--filter`. |
| 2 | The command line is wrong. The message names the problem, then the usage follows. |
| 70 | smrunner itself could not go on. The message starts with `[smrunner] fatal:` and says why. |

In `run`, every fatal problem exits with 70. In `test`, only problems found before the first test
starts do, for example an unreadable test plugin. Each test builds its own sandbox in its own
process, so a problem there, such as a `--plugin` name that is neither a file nor a stock plugin,
or a missing `--file` source, ends only that test's process. The test is reported as
`CRASH ... the test process ended without a result (exit code 70)`, and smrunner exits 1.

## The test report

`test` prints one block per test plugin, one line per test, and a summary:

```
test_readyup.smx
  PASS    Test_MatchCanStartWhenEveryoneIsReady (73 ms)
  FAIL    Test_ReadyIsAnnouncedToEveryone (72 ms)
          Assert.ClientReceived failed: client 2 did not receive "[Ready] Alice is ready." (test_readyup.sp:13)
          --- console (last lines; --echo shows all) ---
          | ...
  SKIP    Test_NeedsSteamWorks (73 ms)
          needs --steamworks
8 tests: 6 passed, 1 failed, 1 skipped
```

| Status | Meaning |
|---|---|
| `PASS` | The test passed. |
| `FAIL` | An assertion failed, a plugin error was not expected, an expected error did not come, or a plugin under test did not load. |
| `SKIP` | The test called `Runner.Skip`. The reason follows. |
| `TIMEOUT` | The test did not end within `--timeout` seconds. Counted as failed. |
| `CRASH` | The test's process died. Counted as failed. |

Below a failed test come its failures, the last 40 lines of its server console, and the last 40
lines of SourceMod's error log if there is one. How to read them is explained in
[Troubleshooting](troubleshooting.md#reading-a-failure). Output of `Runner.PrintClientMenu` appears below
the test's line, also for passed tests.

## JUnit XML

`--junit FILE` writes the results in the JUnit format that CI systems read:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<testsuite name="smrunner" tests="1" failures="1" errors="0" skipped="0">
  <testcase classname="test_greeter.smx" name="Test_NewPlayersAreGreeted" time="0.0719789">
    <failure message="Assert.ClientReceived failed: client 1 did not receive ...">...</failure>
    <system-out>...the server console...</system-out>
  </testcase>
</testsuite>
```

- `classname` is the test plugin's file name, `name` the test's name, `time` its duration in
  seconds.
- A failed test has a `<failure>` element, a skipped one `<skipped>`. A timed-out or crashed test
  has an `<error>` element with the status as its message.
- `<system-out>` holds the printed menus and the whole server console of the test.
- The report never contains color codes.

## The sandbox

For each run, and for each test, smrunner builds a server folder, the sandbox:

```
<sandbox>/
  tf/
    addons/sourcemod/
      configs/  gamedata/  translations/    SourceMod 1.12's files
      extensions/  extensions/x64/          links to the given extensions
      plugins/                              links to the given plugins
      data/  logs/
    cfg/
      server.cfg
      sourcemod/                            SourceMod's cfg files, AutoExecConfig output
    maps/
    bin/                                    stand-in game binaries for gamedata lookups
```

The game folder is `tf/`, so relative paths in plugins (`OpenFile`, `BuildPath`, `exec`) work as
on a TF2 server.

By default the folder is made below `$TMPDIR` (or `/tmp`) and removed afterwards, also when a test
times out. `--keep-sandbox` keeps it and prints its path. `--sandbox DIR` builds it in `DIR` and
always keeps it. Looking at a kept sandbox helps when a plugin cannot find a file (see
[Troubleshooting](troubleshooting.md)).

## Which runner: smrunner or smrunner64

- `smrunner` is a 32-bit program, like a 32-bit server. It loads x86 extensions.
- `smrunner64` is a 64-bit program, like a 64-bit server. It loads x64 extensions, from the `x64/`
  folder of `--extensions`.

Plugins (`.smx`) run on both. Use the runner that matches the servers the plugin runs on. If it
targets both kinds of server, run the tests with both; smrunner's own CI does that.
