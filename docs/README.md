# smrunner documentation

smrunner runs SourceMod plugins and their tests on a simulated server. New here? Read the
[README](../README.md) for installation and a quick start, then the
[tutorial](tutorial-ready-up.md).

## Guides

| Page | Covers |
|---|---|
| [Tutorial: testing a ready-up plugin](tutorial-ready-up.md) | A realistic plugin tested step by step: clients, commands, chat, cvars, panels, timeouts |
| [Writing tests](writing-tests.md) | Test plugins, the test lifecycle, `OnTestBoot`, when a test passes, assertions, expected errors, skipping |
| [Server, time and commands](server-and-time.md) | The simulated clock, frames, timers, `WaitUntil`, server commands, the command buffer, the console, cvars, maps |
| [Clients](clients.md) | Connecting players and bots, Steam IDs, authorization, kicks, admins, client commands, userinfo, received messages, chat colors |
| [Menus](menus.md) | Reading menus, selecting items, keys, pages, timeouts, panels, votes, the admin menu |
| [Hooks and mocks](hooks-and-mocks.md) | Replacing and intercepting natives, arguments and results, providing missing natives |
| [Breakpoints and plugin variables](breakpoints.md) | Reading and writing globals, breakpoints at lines and functions, locals |
| [Run mode](run-mode.md) | `smrunner run`: loading plugins and running commands without tests |
| [GitHub Actions](github-actions.md) | The setup and test actions: installing smrunner, compiling and testing plugins in CI, results |
| [SteamWorks](steamworks.md) | The SteamWorks stand-in: routes, the mapping file, request checks, passthrough, record and replay, Steam state |

## Reference

| Page | Covers |
|---|---|
| [Command line](command-line.md) | Every option, exit codes, the test report, JUnit XML, the sandbox |
| [API reference](api-reference.md) | Every native of `smrunner.inc` |
| [SteamWorks API reference](steamworks-api-reference.md) | Every native of `smrunner-steamworks.inc` |
| [What is simulated](simulated-server.md) | How close the simulated server is to a real TF2 server |
| [Limits](limits.md) | What does not work, workarounds, known issues |
| [Troubleshooting](troubleshooting.md) | Error messages and symptoms, and what to do about them |

## For contributors

| Page | Covers |
|---|---|
| [Architecture](architecture.md) | How smrunner works inside, the source layout, its own tests |

## Finding something

Search the `docs/` folder for the full name of a native, including its methodmap:
`Runner.SelectMenuItem`, `RunnerCall.SetArgString`, `RunnerHttp.Route`. Each has its own heading
in the API references, and the guides' tables use the same names. (Code examples use short
variable names such as `call.SetArgString`.) Messages smrunner prints are quoted verbatim in
[Troubleshooting](troubleshooting.md#messages).
