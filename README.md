# smrunner

**Automated tests for SourceMod plugins, without a game server.**

Testing a SourceMod plugin usually means starting a game server, joining it, typing commands and
watching chat. That is slow, can't be automated, and never runs in CI. smrunner replaces the game
server with a simulated one. It loads your plugins into SourceMod's real logic library and
SourcePawn VM, starts a map, connects clients and runs frames, all inside one process in a few
milliseconds.

You write the tests in SourcePawn. A test connects clients, runs commands, selects menu items and
then asserts what happened:

```sourcepawn
#include <sourcemod>
#include <smrunner>

public void Test_NewPlayersAreGreeted() {
	int alice = Runner.ConnectClient("alice");
	Assert.ClientReceived(alice, RunnerMessage_Chat, "Welcome, alice!");
}
```

```
$ smrunner test test_greeter.smx --plugin greeter.smx
test_greeter.smx
  PASS    Test_NewPlayersAreGreeted (72 ms)
1 tests: 1 passed, 0 failed, 0 skipped
```

## Contents

- [Features](#features)
- [Requirements](#requirements)
- [Installation](#installation)
- [Quick start](#quick-start)
- [GitHub Actions](#github-actions)
- [Documentation](#documentation)
- [Examples](#examples)
- [AI disclosure](#ai-disclosure)
- [License](#license)

## Features

- **Real SourceMod, simulated engine.** SourceMod's own logic library and VM run unchanged, built
  from SourceMod 1.12. Admins, translations, timers, ADTs, KeyValues, menus and votes behave as on
  a server. See [What is simulated](docs/simulated-server.md).
- **Tests in SourcePawn.** A test plugin holds `Test_` functions. Each test runs on a fresh
  server in its own process. See [Writing tests](docs/writing-tests.md).
- **Deterministic time.** The clock advances only when the test runs frames. Timers fire at
  exact game times, so tests repeat exactly. See [Server, time and commands](docs/server-and-time.md).
- **Clients.** Connect players and bots with Steam IDs and IPs, run their commands, and check the
  chat, console, center, hint and kick messages they received. See [Clients](docs/clients.md).
- **Menus, panels and votes.** Read the menu a client sees, select items by their text, and run
  votes to the end. See [Menus](docs/menus.md).
- **Hooks and mocks.** Intercept or replace any native the plugin under test calls, including
  natives of extensions and other plugins. See [Hooks and mocks](docs/hooks-and-mocks.md).
- **Breakpoints.** Stop a plugin at a line or a function and read or change its local and global
  variables. See [Breakpoints and plugin variables](docs/breakpoints.md).
- **Extensions.** Extensions that need neither the engine nor SourceMod's game-side interfaces
  load as they are (for example SMJansson). SourceMod's Top Menus and stock plugins ship with
  smrunner. See [Limits](docs/limits.md#extensions).
- **SteamWorks stand-in.** A replacement for the SteamWorks extension fakes HTTP requests and Steam
  state, with optional passthrough to real servers, and record and replay. See
  [SteamWorks](docs/steamworks.md).
- **CI-friendly output.** Exit codes, a compact report and JUnit XML. See
  [Command line](docs/command-line.md).
- **GitHub Actions.** Two actions install smrunner, compile your plugins and run their tests in
  your repository's CI, with a job summary and annotations on failed assertions. See
  [GitHub Actions](docs/github-actions.md).
- **Run mode.** `smrunner run` loads plugins, runs commands and frames, and fails on any plugin
  error, without writing a test. See [Run mode](docs/run-mode.md).

## Requirements

- Linux on x86 or x86_64 with glibc 2.35 or later (for example Ubuntu 22.04).
- For `smrunner` (32-bit): the 32-bit C and C++ runtime, on Ubuntu
  `libc6-i386` and `lib32stdc++6`.
- For `smrunner64` (64-bit, for x64 extensions): nothing extra.
- Plugins compiled with spcomp 1.11 or later if you use breakpoints or read plugin variables.
- Optional: the system CA bundle (`ca-certificates`) if the SteamWorks stand-in should reach real
  HTTPS servers.

## Installation

smrunner ships as one archive with both runners, SourceMod's files and the include files. Download
it from the [releases page](https://github.com/davenonymous/sm-runner/releases), or take the
`smrunner-linux` artifact of a CI run. Each archive has its SHA-256 in a `.sha256` file beside it.
Check it, then unpack the archive anywhere:

```sh
sha256sum -c smrunner-v0.1.0-linux.tar.gz.sha256
tar -xzf smrunner-v0.1.0-linux.tar.gz -C /opt
/opt/smrunner/bin/smrunner --version
```

The archive unpacks into a single `smrunner/` folder:

| Path | Contents |
|---|---|
| `bin/smrunner` | The 32-bit runner, like a 32-bit server. It loads x86 extensions. |
| `bin/smrunner64` | The 64-bit runner, like a 64-bit server. It loads x64 extensions. |
| `share/include/smrunner.inc` | The test API. |
| `share/include/smrunner-steamworks.inc` | The test API of the SteamWorks stand-in. |
| `share/sourcemod/` | SourceMod 1.12's configs, gamedata, translations and stock plugins. |
| `lib/x86/`, `lib/x64/` | SourceMod's logic library, the VM and the bundled extensions. |
| `licenses/` | The licenses of smrunner and everything it ships. |

The runners find the other folders relative to `bin/`, so you can move the whole folder as one.
Use the runner that matches the servers your plugin runs on, or both (see
[Which runner](docs/command-line.md#which-runner-smrunner-or-smrunner64)).

## Quick start

This walks through testing a small plugin from scratch.

**1. The plugin under test.** `greeter.sp` greets every player who joins:

```sourcepawn
#include <sourcemod>

ConVar g_message;

public void OnPluginStart() {
	g_message = CreateConVar("sm_greeter_message", "Welcome", "The greeting");
}

public void OnClientPostAdminCheck(int client) {
	if (!IsFakeClient(client)) {
		char message[64];
		g_message.GetString(message, sizeof(message));
		PrintToChat(client, "%s, %N!", message, client);
	}
}
```

**2. The test plugin.** `test_greeter.sp` includes `smrunner.inc` and declares tests. A test is a
public function without parameters whose name starts with `Test_`:

```sourcepawn
#include <sourcemod>
#include <smrunner>

public void Test_NewPlayersAreGreeted() {
	int alice = Runner.ConnectClient("alice");
	Assert.ClientReceived(alice, RunnerMessage_Chat, "Welcome, alice!");
}

public void Test_BotsAreNotGreeted() {
	int bot = Runner.ConnectBot("Bot01");
	Assert.Equal(Runner.ClientMessageCount(bot), 0);
}

public void Test_TheGreetingComesFromTheCvar() {
	FindConVar("sm_greeter_message").SetString("Hello");
	int alice = Runner.ConnectClient("alice");
	Assert.ClientReceived(alice, RunnerMessage_Chat, "Hello, alice!");
}
```

**3. Compile both** with SourceMod's spcomp as usual. The test plugin needs smrunner's
`share/include` folder on its include path.

**4. Run the tests.** Name the test plugin, and each plugin under test with `--plugin`:

```
$ /opt/smrunner/bin/smrunner test test_greeter.smx --plugin greeter.smx
test_greeter.smx
  PASS    Test_BotsAreNotGreeted (71 ms)
  PASS    Test_NewPlayersAreGreeted (72 ms)
  PASS    Test_TheGreetingComesFromTheCvar (70 ms)
3 tests: 3 passed, 0 failed, 0 skipped
```

The exit code is 0 when no test failed and 1 when one did, so a CI job fails on its own (see
[Exit codes](docs/command-line.md#exit-codes)).

When a test fails, the report names the assertion, the values and the line. Had the test expected
`Welcome, Alice!` with a capital A, it would show:

```
  FAIL    Test_NewPlayersAreGreeted (72 ms)
          Assert.ClientReceived failed: client 1 did not receive "Welcome, Alice!" (test_greeter.sp:6)
          --- console (last lines; --echo shows all) ---
          | ...
```

## GitHub Actions

In a plugin's own repository, the `setup` and `test` actions run the tests on every push:

```yaml
jobs:
  test:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4
      - uses: davenonymous/sm-runner/setup@v0
        with:
          spcomp: true
      - uses: davenonymous/sm-runner/test@v0
        with:
          tests: tests/test_*.sp
          plugins: scripting/greeter.sp
```

They compile the `.sp` files with SourceMod 1.12's compiler, run the tests with both runners, and
report the results as a job summary and as annotations. Every input is in
[GitHub Actions](docs/github-actions.md).

## Documentation

Next, follow the [ready-up tutorial](docs/tutorial-ready-up.md). The
[documentation index](docs/README.md) lists every page. The ones you will need most:

- [Writing tests](docs/writing-tests.md): test structure, lifecycle, assertions.
- [API reference](docs/api-reference.md): every native of `smrunner.inc`.
- [Command line](docs/command-line.md): every option and exit code.
- [GitHub Actions](docs/github-actions.md): the setup and test actions for CI.
- [Limits](docs/limits.md): what does not work, and known issues.
- [Troubleshooting](docs/troubleshooting.md): every message, and what to do about it.

## Examples

The [`examples/`](examples/) folder holds four plugins with their tests. CI runs all of them on
both runners.

| Example | Shows |
|---|---|
| [`json-status`](examples/json-status/) | A real extension (SMJansson), clients, mocks, breakpoints |
| [`menu-vote`](examples/menu-vote/) | A map vote and a panel, voted through to the map change |
| [`team-admin`](examples/team-admin/) | An admin menu category, colored chat, panels with fixed keys |
| [`steamworks-motd`](examples/steamworks-motd/) | HTTP requests and Steam state with the SteamWorks stand-in |

## AI disclosure

AI helped build this project. If that bothers you, no hard feelings - this project is just not for
you.

## License

smrunner is licensed under the GNU General Public License version 3, with the same exceptions
SourceMod grants. See [LICENSE.txt](LICENSE.txt). The libraries and SourceMod files it ships keep
their own licenses, which the archive installs into `licenses/`.
