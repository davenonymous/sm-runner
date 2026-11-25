# Example: menu-vote

A map vote and an information panel, tested by voting through the vote to the map change.

`menu-vote.sp` has two commands:

- `sm_mapvote` (vote flag) starts a 20-second vote on the maps of `sm_mapvote_maps`. The winning
  map is announced in chat, and the server changes to it five seconds later.
- `sm_mapinfo` shows the current map and the number of players in a panel.

## What the tests show

| Feature | Test |
|---|---|
| Reading a vote menu with `Assert.ClientMenu` | `Test_EveryoneVotesAndTheWinnerIsNext` |
| Voting by item text with `Runner.SelectMenuItem` | `Test_EveryoneVotesAndTheWinnerIsNext` |
| Chat without its color codes | `Test_EveryoneVotesAndTheWinnerIsNext` |
| A plugin's own map change, after `Runner.Seconds` | `Test_EveryoneVotesAndTheWinnerIsNext` |
| A vote that runs out of time | `Test_AVoteWithoutVotesEndsAfterItsTime` |
| Replies in the client's console | `Test_OnlyOneVoteRunsAtATime` |
| Admin flags for commands | `Test_PlayersWithoutTheVoteFlagCannotStartOne` |
| A panel's text and closing it | `Test_MapInfoShowsTheMapAndCloses` |

## Running it

The plugin changes the map itself with `ForceChangeLevel`, so the maps it offers must exist on the
server. `--maps` declares them:

```sh
smrunner test test_menu_vote.smx --plugin menu-vote.smx --maps cp_process_final,koth_viaduct
```

See [Menus](../../docs/menus.md) for everything about menus and votes.
