# Example: json-status

A plugin that uses a real extension, tested with that extension loaded.

`json-status.sp` prints the server's status as JSON with `sm_status_json`: the map, the host
name and the human players with their Steam IDs. It also writes the status to
`addons/sourcemod/data/status.json` at every map start. It needs the
[SMJansson](https://github.com/davenonymous/SMJansson) extension.

## What the tests show

`test_json_status.sp` parses the plugin's output with SMJansson too, and shows:

| Feature | Test |
|---|---|
| Loading a real extension with `--extensions` | every test |
| The status of an empty server | `Test_StatusOfAnEmptyServer` |
| Reading the console with `Runner.ClearConsole` and `Runner.GetConsole` | `StatusFromCommand` helper |
| Setting a cvar with SourceMod's natives | `Test_HostnameComesFromTheCvar` |
| Clients with Steam IDs, and bots | `Test_PlayersAreListedButNotBots` |
| Replies to the client that ran the command | `Test_RepliesGoToTheClientThatAsked` |
| A file the plugin writes at map start | `Test_StatusFileIsWrittenAtMapStart` |
| Mocking `GetClientAuthId` to make a Steam ID pending | `Test_PendingSteamIdsAreNull` |
| Breakpoints at a function and a line, reading locals | `Test_BreakpointsLookIntoThePlugin` |

## Running it

Compile both `.sp` files against SourceMod's, smrunner's and SMJansson's includes. Then:

```sh
smrunner test test_json_status.smx --plugin json-status.smx \
    --extensions <SMJansson's extensions folder>
```

The plugin also runs in [run mode](../../docs/run-mode.md):

```sh
smrunner run json-status.smx --extensions <SMJansson's extensions folder> \
    --command "sm_status_json" --frames 10
```
