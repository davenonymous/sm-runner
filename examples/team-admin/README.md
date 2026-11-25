# Example: team-admin

An admin menu category, colored chat and a panel with a fixed key, tested through SourceMod's own
admin menu.

`team-admin.sp` adds a "Team tools" category to the admin menu (SourceMod's stock `adminmenu`
plugin). Its "Announce" item sends `sm_teamadmin_announcement` to everyone as a chat line with a
gold `[Team]` tag. `sm_teaminfo` shows a panel with a disabled line, an item that lists the
players in chat, and "Close" on key 0.

## What the tests show

| Feature | Test |
|---|---|
| Loading a stock plugin by name (`--plugin adminmenu`) | every test |
| Walking the admin menu by item text | `Test_AnnounceSendsColoredChatToEveryone` |
| Chat with and without its color codes | `Test_AnnounceSendsColoredChatToEveryone` |
| A disabled panel line, `Runner.ClientMenuKeys` | `Test_TeamInfoShowsDisabledLinesAndCloses` |
| Drawing a menu with `Runner.PrintClientMenu` | `Test_TeamInfoShowsDisabledLinesAndCloses` |
| Pressing key 0 with `Runner.SelectMenu(client, 10)` | `Test_TeamInfoShowsDisabledLinesAndCloses` |
| Several clients | `Test_PlayersOnlineAreListedInChat` |

## Running it

```sh
smrunner test test_team_admin.smx --plugin team-admin.smx --plugin adminmenu
```

See [Menus](../../docs/menus.md#the-admin-menu-top-menus) for the admin menu.
