# Example: steamworks-motd

A message of the day fetched over HTTP with SteamWorks, tested with smrunner's SteamWorks
stand-in instead of the network.

`steamworks-motd.sp` fetches the message of the day from `sm_motd_url` once the configs have run, and
on `sm_motd_refresh`. It keeps the last message when a request fails, and shows it to every
player who joins. `sm_serverinfo` says how to connect, from Steam's view of the server.

## What the tests show

| Feature | Test |
|---|---|
| An answer from the mapping file (`steamworks/steamworks.json`) | `Test_MotdFromTheMappingFile` |
| Checking a request's header in the request log | `Test_MotdFromTheMappingFile` |
| A route that changes the answer | `Test_RouteChangesTheMotd` |
| A route that answers with an error status | `Test_FailedRequestKeepsTheLastMotd` |
| Steam state: public IP, VAC, a disconnect | `Test_ServerInfoFromSteam` |
| Server command output with `Runner.CommandOutput` | `Test_ServerInfoFromSteam` |

## Running it

Compile both `.sp` files against SourceMod's and smrunner's includes and SteamWorks 1.2.167's
`SteamWorks.inc`. Then pass the folder with the mapping file to `--steamworks`:

```sh
smrunner test test_steamworks_motd.smx --plugin steamworks-motd.smx --steamworks steamworks
```

See [SteamWorks](../../docs/steamworks.md) for everything the stand-in does.
