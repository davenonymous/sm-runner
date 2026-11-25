/**
 * Copyright 2026 davenonymous
 *
 * Self tests of smrunner's server: startup, frames and time, commands,
 * cvars, the console, clients and admin access, against target.smx.
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <smrunner>

Handle Target() {
	Handle plugin = FindPluginByFile("target.smx");
	Assert.True(plugin != null, "target.smx is loaded");
	return plugin;
}

public void Test_ServerIsUpWithConfigsExecuted() {
	Assert.ConsoleContains("target started");
	Assert.ConsoleContains("target configs executed");
	char map[64];
	GetCurrentMap(map, sizeof(map));
	Assert.StrEqual(map, "cp_badlands");
	Assert.Equal(MaxClients, 24);
	Assert.True(IsServerProcessing(), "the server is processing");
}

public void Test_AutoExecConfigWasWritten() {
	Assert.True(FileExists("cfg/sourcemod/plugin.target.cfg"), "plugin.target.cfg was written");
}

public void Test_FramesAdvanceTheClock() {
	int ticks = GetGameTickCount();
	float time = GetGameTime();
	Runner.Frames(10);
	Assert.Equal(GetGameTickCount(), ticks + 10);
	Assert.FloatEqual(GetGameTime(), time + 10 * GetTickInterval(), 0.0001);
}

public void Test_TimersFollowGameTime() {
	Handle target = Target();
	int ticks = Runner.GetPluginValue(target, "g_ticks");
	Runner.Seconds(3.05);
	Assert.Equal(Runner.GetPluginValue(target, "g_ticks"), ticks + 3, "a one second timer ran three times");
}

public void Test_ServerCommandRunsTheHook() {
	Runner.Command("sm_target_count 5");
	Runner.Command("sm_target_count");
	Assert.ConsoleContains("counter is 6");
	Assert.Equal(Runner.GetPluginValue(Target(), "g_counter"), 6);
}

public void Test_CommandOutputIsCaptured() {
	char output[256];
	Runner.CommandOutput(output, sizeof(output), "sm_target_count %d", 41);
	Assert.StrContains(output, "counter is 41");
}

public void Test_ServerCommandIsBufferedUntilTheNextFrame() {
	ServerCommand("sm_target_count 7");
	Assert.False(Runner.ConsoleContains("counter is 7"), "not before the next frame");
	Runner.Frames(1);
	Assert.ConsoleContains("counter is 7");
}

int g_hookCalls;
char g_hookOld[16];
char g_hookNew[16];

public void Test_ConVarsAreBoundedAndHooked() {
	ConVar limit = FindConVar("sm_target_limit");
	Assert.True(limit != null);
	limit.IntValue = 50;
	Assert.Equal(limit.IntValue, 10, "the upper bound holds");
	Runner.Command("sm_target_limit 3");
	Assert.Equal(limit.IntValue, 3, "the console sets cvars");
	limit.AddChangeHook(OnLimitChanged);
	limit.SetString("4");
	Assert.Equal(g_hookCalls, 1);
	Assert.StrEqual(g_hookOld, "3");
	Assert.StrEqual(g_hookNew, "4");
	limit.SetString("4");
	Assert.Equal(g_hookCalls, 1, "no hook without a change");
}

void OnLimitChanged(ConVar cvar, const char[] oldValue, const char[] newValue) {
	g_hookCalls++;
	strcopy(g_hookOld, sizeof(g_hookOld), oldValue);
	strcopy(g_hookNew, sizeof(g_hookNew), newValue);
}

public void Test_ClientsConnectAndAreAuthorized() {
	int client = Runner.ConnectClient("Alice", "STEAM_0:1:1234");
	Assert.True(client > 0, "Alice connected");
	Assert.True(IsClientInGame(client));
	Assert.True(IsClientAuthorized(client));
	char name[32];
	GetClientName(client, name, sizeof(name));
	Assert.StrEqual(name, "Alice");
	char auth[32];
	GetClientAuthId(client, AuthId_Steam3, auth, sizeof(auth));
	Assert.StrEqual(auth, "[U:1:2469]");
	Assert.ConsoleContains("post admin check Alice STEAM_0:1:1234");
	Assert.Equal(Runner.GetPluginValue(Target(), "g_connected"), 1);
	Runner.Disconnect(client);
	Assert.False(IsClientConnected(client));
	Assert.Equal(Runner.GetPluginValue(Target(), "g_connected"), 0);
}

public void Test_AReservedNameIsKickedAfterTheAdminChecks() {
	AdminId admin = CreateAdmin("Dana's admin");
	admin.BindIdentity(AUTHMETHOD_NAME, "Dana");
	admin.SetPassword("secret");
	int client = Runner.ConnectClient("Dana");
	Assert.True(client > 0, "Dana stays until the kick");
	Assert.ConsoleContains("post admin check Dana", "the admin checks ended for a connected client");
	Assert.Equal(GetUserAdmin(client), INVALID_ADMIN_ID, "no password, no admin");
	Assert.Equal(Runner.GetPluginValue(Target(), "g_connected"), 1);
	Runner.Seconds(0.2);
	Assert.False(IsClientConnected(client), "Dana was kicked");
	Assert.ClientReceived(client, RunnerMessage_Kick, "Your name is reserved by SourceMod");
	Assert.Equal(Runner.GetPluginValue(Target(), "g_connected"), 0);
}

public void Test_SteamIdsInEveryFormConnect() {
	char auth[32];
	int client = Runner.ConnectClient("Bob", "[U:1:47]");
	GetClientAuthId(client, AuthId_Steam2, auth, sizeof(auth));
	Assert.StrEqual(auth, "STEAM_0:1:23");
	client = Runner.ConnectClient("Carol", "76561197960265775");
	GetClientAuthId(client, AuthId_Steam3, auth, sizeof(auth));
	Assert.StrEqual(auth, "[U:1:47]");
}

public void Test_SteamIdWithoutItsClosingBracketIsRefused() {
	Runner.ExpectError("\"[U:1:47\" is not a Steam ID");
	Runner.ConnectClient("Erin", "[U:1:47");
}

public void Test_RepliesReachTheClient() {
	int client = Runner.ConnectClient("Bob");
	Runner.ClientCommand(client, "sm_target_greet");
	Assert.ClientReceived(client, RunnerMessage_Console, "hello, Bob");
	char last[64];
	Runner.GetPluginString(Target(), "g_lastName", last, sizeof(last));
	Assert.StrEqual(last, "Bob");
}

public void Test_AdminCommandsCheckAccess() {
	int client = Runner.ConnectClient("Carol");
	Runner.ClientCommand(client, "sm_target_admin");
	Assert.ClientReceived(client, RunnerMessage_Console, "You do not have access to this command");
	Assert.False(Runner.ClientReceived(client, RunnerMessage_Any, "admin command ran"));
	SetUserFlagBits(client, ADMFLAG_GENERIC);
	Runner.ClientCommand(client, "sm_target_admin");
	Assert.ClientReceived(client, RunnerMessage_Console, "admin command ran");
}

public void Test_BotsAreFakeClients() {
	int bot = Runner.ConnectBot("Robot");
	Assert.True(bot > 0);
	Assert.True(IsFakeClient(bot));
	char auth[32];
	GetClientAuthId(bot, AuthId_Engine, auth, sizeof(auth));
	Assert.StrEqual(auth, "BOT");
	Assert.True(GetClientAuthId(bot, AuthId_Steam2, auth, sizeof(auth)), "bots have a Steam2 ID");
	Assert.StrEqual(auth, "BOT");
	Assert.False(GetClientAuthId(bot, AuthId_SteamID64, auth, sizeof(auth)), "bots have no 64-bit Steam ID");
}

public void Test_PluginGlobalsCanBeSet() {
	Handle target = Target();
	Runner.SetPluginValue(target, "g_counter", 100);
	Runner.Command("sm_target_count");
	Assert.ConsoleContains("counter is 101");
	Runner.SetPluginString(target, "g_lastName", "set by the test");
	char last[64];
	Runner.GetPluginString(target, "g_lastName", last, sizeof(last));
	Assert.StrEqual(last, "set by the test");
}

public void Test_PluginFunctionsCanBeCalled() {
	Handle target = Target();
	Call_StartFunction(target, GetFunctionByName(target, "Target_Divide"));
	Call_PushCell(42);
	Call_PushCell(6);
	int result;
	Assert.Equal(Call_Finish(result), SP_ERROR_NONE);
	Assert.Equal(result, 7);
}

public void Test_ExpectedErrorsDoNotFail() {
	Runner.ExpectError("Divide by zero");
	Handle target = Target();
	Call_StartFunction(target, GetFunctionByName(target, "Target_Divide"));
	Call_PushCell(1);
	Call_PushCell(0);
	int result;
	Call_Finish(result);
}

public void Test_MapChange() {
	Runner.Frames(300);
	Runner.ClearConsole();
	Runner.ChangeMap("ctf_2fort");
	char map[64];
	GetCurrentMap(map, sizeof(map));
	Assert.StrEqual(map, "ctf_2fort");
	Assert.ConsoleContains("target configs executed");
	Assert.True(GetGameTickCount() < 300, "the clock started again on the new map (tick %d)", GetGameTickCount());
}

public void Test_ChangelevelRunsTheConfigsAndResetsTheClock() {
	Runner.Frames(300);
	Runner.ClearConsole();
	Runner.Command("changelevel cp_badlands");
	Assert.ConsoleNotContains("target configs executed", "the map changes at the end of the frame");
	Runner.Frames(1);
	Assert.ConsoleContains("target configs executed", "the frame of the changelevel ran the configs");
	Assert.True(GetGameTickCount() < 300, "the clock started again on the new map (tick %d)", GetGameTickCount());
	Assert.True(GetGameTime() < 300 * GetTickInterval(), "the game time started again");
}

void AssertNewestMapChange(const char[] map, const char[] reason) {
	char ended[64], why[128];
	int startTime;
	GetMapHistory(0, ended, sizeof(ended), why, sizeof(why), startTime);
	Assert.StrEqual(ended, map, "the map that ended");
	Assert.StrEqual(why, reason, "the reason of the change");
	Assert.True(startTime > 0 && startTime <= GetTime(), "the map's start time (%d)", startTime);
}

public void Test_MapHistoryRecordsEveryLevelChange() {
	Assert.Equal(GetMapHistorySize(), 0, "the first map is no change");
	Runner.Command("changelevel cp_badlands");
	Runner.Frames(1);
	AssertNewestMapChange("cp_badlands", "changelevel Command");
	// A change no command asked for, as SourceMod sees the map command.
	Runner.ChangeMap("ctf_2fort");
	AssertNewestMapChange("cp_badlands", " (Map overridden)");
	ForceChangeLevel("cp_badlands", "the test's reason");
	Runner.Frames(1);
	AssertNewestMapChange("ctf_2fort", "the test's reason");
	Assert.Equal(GetMapHistorySize(), 3);
}

public void Test_MapHistoryIsTrimmedToItsSize() {
	FindConVar("sm_maphistory_size").IntValue = 2;
	for (int change = 0; change < 3; change++) {
		Runner.Command("changelevel cp_badlands");
		Runner.Frames(1);
	}
	Assert.Equal(GetMapHistorySize(), 2, "the oldest change went");
}

public void Test_SkipEndsTheTest() {
	Runner.Skip("skipped on purpose (%d)", 42);
	Assert.Fail("not reached");
}

public void Test_RootConsoleCommandsOfTheCore() {
	char output[1024];
	Runner.CommandOutput(output, sizeof(output), "sm cvars target.smx");
	Assert.StrContains(output, "[SM] Listing 2 convars for: smrunner self test target");
	Assert.StrContains(output, "sm_target_limit");
	Runner.CommandOutput(output, sizeof(output), "sm cmds target.smx");
	Assert.StrContains(output, "sm_target_count");
	Assert.StrContains(output, "admin");
	Runner.CommandOutput(output, sizeof(output), "sm config ServerLang");
	Assert.StrContains(output, "[SM] Config option \"ServerLang\" is set to \"en\".");
}

public void Test_ClientsCannotRunServerCommands() {
	int client = Runner.ConnectClient("Mallory");
	Runner.ClientCommand(client, "changelevel ctf_2fort");
	Runner.Frames(2);
	char map[64];
	GetCurrentMap(map, sizeof(map));
	Assert.StrEqual(map, "cp_badlands", "a client's changelevel does nothing");
	Assert.ClientReceived(client, RunnerMessage_Console, "Unknown command: changelevel");
	Runner.ClientCommand(client, "sm plugins");
	Assert.ClientReceived(client, RunnerMessage_Console, "\"smrunner self test target\" (1.0.0) by davenonymous");
	Assert.True(FindPluginByFile("target.smx") != null, "the plugin is still there");
}

public void Test_AClientLineRunsOnlyWhatClientsMayRun() {
	int client = Runner.ConnectClient("Mallory");
	Runner.ClearConsole();
	Runner.ClientCommand(client, "sm_target_greet; echo leaked; changelevel cp_badlands");
	Assert.ClientReceived(client, RunnerMessage_Console, "hello, Mallory");
	Assert.ClientReceived(client, RunnerMessage_Console, "Unknown command: echo");
	Assert.ClientReceived(client, RunnerMessage_Console, "Unknown command: changelevel");
	Runner.ClearClientMessages(client);
	FakeClientCommand(client, "sm_target_greet; echo leaked too; changelevel cp_badlands");
	Assert.ClientReceived(client, RunnerMessage_Console, "hello, Mallory");
	Runner.Frames(2);
	Assert.ConsoleNotContains("leaked", "the engine's echo did not run for the client");
	Assert.Equal(GetMapHistorySize(), 0, "the engine's changelevel did not run for the client");
}

public void Test_TheSourceModVersionIsTheSameEverywhere() {
	char version[64], expected[128], line[256];
	FindConVar("sourcemod_version").GetString(version, sizeof(version));
	int client = Runner.ConnectClient("Vera");
	Runner.ClientCommand(client, "sm");
	FormatEx(expected, sizeof(expected), "SourceMod %s, by AlliedModders LLC", version);
	Assert.ClientReceived(client, RunnerMessage_Console, expected);
	File config = OpenFile("cfg/sourcemod/plugin.target.cfg", "r");
	Assert.True(config != null, "plugin.target.cfg can be read");
	config.ReadLine(line, sizeof(line));
	delete config;
	FormatEx(expected, sizeof(expected), "auto-generated by SourceMod (v%s)", version);
	Assert.StrContains(line, expected);
}

public void Test_BuildPathTurnsBackslashes() {
	char path[PLATFORM_MAX_PATH];
	BuildPath(Path_SM, path, sizeof(path), "configs\\x.cfg");
	Assert.StrContains(path, "addons/sourcemod/configs/x.cfg");
}

public void Test_ConsoleCommandOnACvarNameDoesNotThrow() {
	RegConsoleCmd("sm_target_limit", OnShadowed);
	Assert.Equal(FindConVar("sm_target_limit").IntValue, 5, "the cvar is still the cvar");
}

Action OnShadowed(int client, int args) {
	return Plugin_Handled;
}

public void Test_SendConVarValueRefusesBots() {
	Runner.ExpectError("is fake and cannot be targeted");
	int bot = Runner.ConnectBot("Robot");
	SendConVarValue(bot, FindConVar("sm_target_limit"), "3");
}

public void Test_ArraysOfMethodmapsAreReadable() {
	Handle target = Target();
	Assert.Equal(Runner.GetPluginValue(target, "g_cvars", 1), FindConVar("sm_target_limit"));
}

public void Test_FramesCannotRunInsideAFrame() {
	Runner.ExpectError("cannot run frames inside a frame");
	RequestFrame(OnFrameRunsFrames);
	Runner.Frames(2);
}

void OnFrameRunsFrames(any data) {
	Runner.Frames(1);
}

public void Test_ClientTimeCounts() {
	int client = Runner.ConnectClient("Grace");
	float connected = GetClientTime(client);
	Runner.Seconds(2.0);
	Assert.FloatEqual(GetClientTime(client), connected + 2.0, 0.02);
}

public void Test_CommandIteratorWalksSourceModsCommands() {
	bool found;
	char name[64];
	char description[64];
	CommandIterator commands = new CommandIterator();
	while (commands.Next()) {
		commands.GetName(name, sizeof(name));
		if (!StrEqual(name, "sm_target_admin")) {
			continue;
		}
		found = true;
		commands.GetDescription(description, sizeof(description));
		Assert.StrEqual(description, "Needs the generic flag");
		Assert.Equal(commands.AdminFlags, ADMFLAG_GENERIC);
		Assert.Equal(commands.Plugin, Target());
	}
	delete commands;
	Assert.True(found, "sm_target_admin is one of SourceMod's commands");
}

public void Test_FindConCommandWalksTheConsoleInTheOrderMade() {
	int greeting = -1;
	int count = -1;
	bool isCommand;
	bool greetingIsCommand = true;
	bool countIsCommand;
	char name[64];
	int flags;
	Handle search = FindFirstConCommand(name, sizeof(name), isCommand, flags);
	Assert.True(search != null);
	int index = 0;
	do {
		if (StrEqual(name, "sm_target_greeting")) {
			greeting = index;
			greetingIsCommand = isCommand;
		} else if (StrEqual(name, "sm_target_count")) {
			count = index;
			countIsCommand = isCommand;
		}
		index++;
	} while (FindNextConCommand(search, name, sizeof(name), isCommand, flags));
	delete search;
	Assert.True(greeting >= 0 && count >= 0, "both are found");
	Assert.True(greeting < count, "the cvar was made before the command");
	Assert.False(greetingIsCommand);
	Assert.True(countIsCommand);
}

int g_textMsgDest;
char g_textMsgText[256];

Action Hook_TextMsg(UserMsg id, BfRead message, const int[] players, int playersNum, bool reliable, bool init) {
	g_textMsgDest = message.ReadByte();
	message.ReadString(g_textMsgText, sizeof(g_textMsgText));
	return Plugin_Continue;
}

public void Test_AnUnknownClientCommandGetsTheGamesTextMsg() {
	int client = Runner.ConnectClient("Ivan");
	HookUserMessage(GetUserMessageId("TextMsg"), Hook_TextMsg);
	Runner.ClientCommand(client, "sm_no_such_command 1");
	Assert.Equal(g_textMsgDest, 2, "HUD_PRINTCONSOLE");
	Assert.StrEqual(g_textMsgText, "Unknown command: sm_no_such_command\n");
	Assert.ClientReceived(client, RunnerMessage_Console, "Unknown command: sm_no_such_command");
}

public void Test_GetClientIPHasThePortUnlessAskedNotTo() {
	int client = Runner.ConnectClient("Judy", "", true, "10.0.0.7:27006");
	char ip[64];
	GetClientIP(client, ip, sizeof(ip), false);
	Assert.StrEqual(ip, "10.0.0.7:27006");
	GetClientIP(client, ip, sizeof(ip));
	Assert.StrEqual(ip, "10.0.0.7");
}

public void Test_AnIPOf64CharactersIsRefused() {
	Runner.ExpectError("an address has fewer than 64");
	Runner.ConnectClient("Ken", "", true, "1234567890123456789012345678901234567890123456789012345678901234");
}

int g_languageChanges;
int g_changedLanguage = -1;

public void OnClientLanguageChanged(int client, int language) {
	g_languageChanges++;
	g_changedLanguage = language;
}

public void Test_AHumanGetsItsOwnLanguageAtTheConnect() {
	Runner.Command("sm config ServerLang de");
	int client = Runner.ConnectClient("Liam");
	Assert.Equal(g_languageChanges, 1, "OnClientLanguageChanged at the connect");
	Assert.Equal(g_changedLanguage, GetLanguageByCode("en"), "the client's cl_language");
	Assert.Equal(GetClientLanguage(client), GetLanguageByCode("en"));
	Runner.ConnectBot("Robot");
	Assert.Equal(g_languageChanges, 1, "bots keep the server's language without the forward");
}

public void Test_AllowClLanguageVarOffKeepsTheServerLanguage() {
	char output[256];
	Runner.CommandOutput(output, sizeof(output), "sm config AllowClLanguageVar maybe");
	Assert.StrContains(output, "Invalid value: must be \"on\" or \"off\"");
	Runner.Command("sm config ServerLang de");
	Runner.Command("sm config AllowClLanguageVar Off");
	int client = Runner.ConnectClient("Mia");
	Assert.Equal(g_languageChanges, 0);
	Assert.Equal(GetClientLanguage(client), GetLanguageByCode("de"));
}

int g_settingsChanges;
bool g_settingsChangeInGame[4];

public void OnClientSettingsChanged(int client) {
	if (g_settingsChanges < sizeof(g_settingsChangeInGame)) {
		g_settingsChangeInGame[g_settingsChanges] = IsClientInGame(client);
	}
	g_settingsChanges++;
}

public void Test_SettingsChangesComeInTheSignonAndAfterThePutInServer() {
	Runner.ConnectClient("Ruth", "", false);
	Assert.Equal(g_settingsChanges, 2, "the signon's userinfo and ActivatePlayer's call");
	Assert.False(g_settingsChangeInGame[0], "the signon's comes before OnClientPutInServer");
	Assert.True(g_settingsChangeInGame[1], "ActivatePlayer's comes after it");
	Runner.Frames(1);
	Assert.Equal(g_settingsChanges, 2, "the signon took the connect's change");
	Runner.ConnectBot("Robot");
	Assert.Equal(g_settingsChanges, 3, "a bot is put in at once");
	Runner.Frames(1);
	Assert.Equal(g_settingsChanges, 4, "the bot's userinfo change comes on the next frame");
}

public void Test_ALongNameIsCutWithoutHalfACharacter() {
	// 30 bytes, then a character of two: the engine keeps 31 bytes, which
	// cut the character; SourceMod drops what is left of it.
	char name[33];
	for (int i = 0; i < 30; i++) {
		name[i] = 'a';
	}
	name[30] = 0xC3;
	name[31] = 0xA4;
	int client = Runner.ConnectClient(name);
	char seen[64];
	GetClientName(client, seen, sizeof(seen));
	Assert.Equal(strlen(seen), 30);
	GetClientInfo(client, "name", seen, sizeof(seen));
	Assert.Equal(strlen(seen), 31, "the engine's name keeps the cut byte");
}

public void Test_ANameChangeToAReservedNameIsKickedInTheClientsLanguage() {
	AdminId admin = CreateAdmin("Rex's admin");
	admin.BindIdentity(AUTHMETHOD_NAME, "Rex");
	admin.SetPassword("secret");
	Runner.Command("sm config ServerLang de");
	Runner.Command("sm config AllowClLanguageVar Off");
	int client = Runner.ConnectClient("Sam");
	Runner.SetClientConVar(client, "name", "Rex");
	Runner.Frames(1);
	Assert.False(IsClientConnected(client), "kicked at once");
	Assert.ClientReceived(client, RunnerMessage_Kick, "Dein Name ist durch SourceMod reserviert");
}

public void Test_AUserinfoChangeReachesTheServerOnTheNextFrame() {
	int client = Runner.ConnectClient("Nina");
	Runner.Frames(1);
	int changes = g_settingsChanges;
	Runner.SetClientConVar(client, "name", "Nora");
	char name[32];
	GetClientInfo(client, "name", name, sizeof(name));
	Assert.StrEqual(name, "Nora", "the userinfo has the new name at once");
	GetClientName(client, name, sizeof(name));
	Assert.StrEqual(name, "Nina", "the server sees it on its next frame");
	Runner.Frames(1);
	Assert.Equal(g_settingsChanges, changes + 1, "OnClientSettingsChanged");
	GetClientName(client, name, sizeof(name));
	Assert.StrEqual(name, "Nora");
}

public void Test_APasswordSetLaterMakesTheClientAnAdmin() {
	AdminId admin = CreateAdmin("Olga's admin");
	admin.BindIdentity(AUTHMETHOD_STEAM, "STEAM_0:1:777");
	admin.SetPassword("secret");
	admin.SetFlag(Admin_Generic, true);
	int client = Runner.ConnectClient("Olga", "STEAM_0:1:777");
	Assert.Equal(GetUserAdmin(client), INVALID_ADMIN_ID, "no password, no admin");
	Runner.SetClientConVar(client, "_password", "secret");
	Runner.Frames(1);
	Assert.Equal(GetUserAdmin(client), admin, "the new password passed the admin checks");
}

public void Test_GetClientInfoAnswersForEveryName() {
	int client = Runner.ConnectClient("Pia");
	char value[32] = "untouched";
	Assert.True(GetClientInfo(client, "cl_no_such_cvar", value, sizeof(value)), "the engine answers \"\"");
	Assert.StrEqual(value, "");
	Assert.True(GetClientInfo(client, "NAME", value, sizeof(value)), "names are case-insensitive");
	Assert.StrEqual(value, "Pia");
}

public void Test_SmCreditsListsSourceModsCredits() {
	int client = Runner.ConnectClient("Quinn");
	Runner.ClientCommand(client, "sm credits");
	Assert.ClientReceived(client, RunnerMessage_Console, "SourceMod would not be possible without:");
	Assert.ClientReceived(client, RunnerMessage_Console, "SourceMod is open source under the GNU General Public License.");
}

public void Test_PrecachingLastsOneMap() {
	Assert.True(PrecacheModel("models/smrunner/test.mdl") > 0);
	Assert.True(IsModelPrecached("models/smrunner/test.mdl"));
	Runner.ChangeMap("ctf_2fort");
	Assert.False(IsModelPrecached("models/smrunner/test.mdl"), "the new map's tables are empty");
}

public void Test_APluginUnloadedOnAMapLoadsOnTheNextNotLate() {
	Assert.False(Runner.GetPluginValue(Target(), "g_lateLoad"), "the first map's load");
	// SourceMod unloads it on the next frame: the test plugin is running.
	Runner.Command("sm plugins unload target.smx");
	Runner.Frames(1);
	Assert.True(FindPluginByFile("target.smx") == null, "unloaded");
	Runner.ChangeMap("ctf_2fort");
	Assert.False(Runner.GetPluginValue(Target(), "g_lateLoad"), "LoadAll loads it in its normal pass");
}

public void Test_TheVoteProgressCvarsAreBounded() {
	ConVar chat = FindConVar("sm_vote_progress_chat");
	chat.IntValue = 2;
	Assert.Equal(chat.IntValue, 1);
}

public void Test_CommandsLeaveBufferedWaitsAlone() {
	ServerCommand("sm_target_count 1; wait; sm_target_count 2; wait; sm_target_count 3");
	Runner.Command("echo the test's own command");
	Assert.ConsoleContains("the test's own command", "Runner.Command runs at once");
	Assert.ConsoleNotContains("counter is", "the buffered commands wait for the next frame");
	char output[256];
	Runner.CommandOutput(output, sizeof(output), "echo captured");
	Assert.StrEqual(output, "captured\n", "only the command's own output is captured");
	// sm_target_count adds its argument: 1, 3, 6.
	Runner.Frames(1);
	Assert.ConsoleContains("counter is 1");
	Assert.ConsoleNotContains("counter is 3", "the first wait still holds");
	Runner.Frames(1);
	Assert.ConsoleContains("counter is 3");
	Assert.ConsoleNotContains("counter is 6", "the second wait still holds");
	Runner.Frames(1);
	Assert.ConsoleContains("counter is 6");
}

public void Test_AWaitOfACommandsOwnDefersItsRest() {
	Runner.Command("sm_target_count 10; wait; sm_target_count 20");
	Assert.ConsoleContains("counter is 10");
	Assert.ConsoleNotContains("counter is 30", "after its wait the rest runs in the next frame");
	Runner.Frames(1);
	Assert.ConsoleContains("counter is 30");
}

public void Test_CommentsInConfigsHideSemicolons() {
	File config = OpenFile("cfg/smrunner_comments.cfg", "w");
	config.WriteLine("// a comment; echo leaked from a comment line");
	config.WriteLine("echo before the comment // and; echo leaked after a command");
	config.WriteLine("echo \"quoted // is no comment; not split\"");
	config.WriteLine("echo after the comments");
	delete config;
	Runner.Command("exec smrunner_comments");
	Assert.ConsoleNotContains("leaked");
	Assert.ConsoleContains("before the comment \n", "the comment is not part of the command");
	Assert.ConsoleContains("\"quoted // is no comment; not split\"");
	Assert.ConsoleContains("after the comments");
}

// "echo " and count characters of fill: a command of that many plus 5.
void LongEcho(char[] command, int size, int count, char fill) {
	strcopy(command, size, "echo ");
	for (int i = 5; i < 5 + count && i < size - 1; i++) {
		command[i] = fill;
	}
	command[5 + count] = '\0';
}

public void Test_CommandsTooLongAreRefusedWithTheRestOfTheirText() {
	char longest[600];
	LongEcho(longest, sizeof(longest), 505, 'y');
	Runner.Command(longest);
	Assert.ConsoleContains("yyyyyyyyyy", "a command of 510 characters runs");
	char tooLong[600];
	LongEcho(tooLong, sizeof(tooLong), 506, 'z');
	Runner.Command("echo before it; %s; echo after it", tooLong);
	Assert.ConsoleContains("before it");
	Assert.ConsoleContains("WARNING: Command too long... ignoring!\n echo zzzz");
	Assert.ConsoleContains("Cbuf_AddText: buffer overflow");
	// The warning quotes the text from the refused command on.
	Assert.ConsoleContains("zzz; echo after it\n");
	Assert.ConsoleNotContains("\nafter it\n", "the rest of the text is dropped with it");
	Assert.ConsoleNotContains("\nzzzz", "the command did not run");
}

public void Test_AClientCannotSendACommandTooLong() {
	int client = Runner.ConnectClient("Rita");
	char tooLong[600];
	LongEcho(tooLong, sizeof(tooLong), 506, 'z');
	Runner.ExpectError("a client cannot send a command of 511 characters");
	Runner.ClientCommand(client, tooLong);
}

public void Test_CrlfLinesKeepTheirCarriageReturnInTheCommand() {
	File config = OpenFile("cfg/smrunner_crlf.cfg", "w");
	config.WriteString("echo one\rtwo\r\n", false);
	config.WriteString("sm_target_greeting \"howdy partner\"\r\n", false);
	config.WriteString("\r\n", false);
	delete config;
	Runner.Command("exec smrunner_crlf");
	Assert.ConsoleContains("one\rtwo\r\n", "a '\\r' splits nothing and stays in ArgS");
	char greeting[64];
	FindConVar("sm_target_greeting").GetString(greeting, sizeof(greeting));
	Assert.StrEqual(greeting, "howdy partner", "a cvar takes ArgS without its quotes and the '\\r'");
}

public void Test_EveryRunOfTheBufferIsATickForWaits() {
	ServerCommand("sm_target_count 1; wait; sm_target_count 2");
	ServerExecute();
	Assert.ConsoleContains("counter is 1");
	Assert.ConsoleNotContains("counter is 3", "the wait holds until the buffer runs again");
	ServerExecute();
	Assert.ConsoleContains("counter is 3", "the second ServerExecute of the frame is the wait's tick");
}

Action Command_Chain(int args) {
	ServerCommand("echo chained");
	return Plugin_Handled;
}

public void Test_TextAddedWhileTheBufferRunsRunsRightAfterTheCommand() {
	RegServerCmd("smrunner_chain", Command_Chain);
	ServerCommand("smrunner_chain; echo after the chain");
	Runner.Frames(1);
	Assert.ConsoleContains("chained\nafter the chain\n");
}

public void Test_WaitIsACommandWhenWaitsAreNotAllowed() {
	FindConVar("sv_allow_wait_command").BoolValue = false;
	// The setting is taken when the buffer runs; text is added after that.
	Runner.Frames(1);
	ServerCommand("sm_target_count 1; wait; sm_target_count 2");
	Runner.Frames(1);
	Assert.ConsoleContains("counter is 3", "both ran in one frame");
	Assert.ConsoleContains("Unknown command \"wait\"");
}

int g_argCount;
char g_args[8][32];

Action Command_Args(int args) {
	g_argCount = args;
	for (int i = 1; i <= args && i < sizeof(g_args); i++) {
		GetCmdArg(i, g_args[i], sizeof(g_args[]));
	}
	return Plugin_Handled;
}

public void Test_TheTokenizerSplitsAsTier1Does() {
	RegServerCmd("smrunner_args", Command_Args);
	// "Jose" with an o and an e with accents: J, C3 B6, s, C3 A9 in UTF-8.
	char name[8] = "J__s__";
	name[1] = 0xC3;
	name[2] = 0xB6;
	name[4] = 0xC3;
	name[5] = 0xA9;
	Runner.Command("smrunner_args %s", name);
	// A word ends at a signed char <= ' ': every byte of 0x80 or more ends
	// the word it is in.
	Assert.Equal(g_argCount, 5, "an unquoted UTF-8 name splits");
	Assert.StrEqual(g_args[1], "J");
	Assert.Equal(g_args[2][0] & 0xFF, 0xC3);
	Assert.Equal(g_args[3][0] & 0xFF, 0xB6);
	Assert.Equal(g_args[3][1], 's');
	Runner.Command("smrunner_args \"%s\"", name);
	Assert.Equal(g_argCount, 1, "a quoted one stays whole");
	Assert.StrEqual(g_args[1], name);
	// Control characters are no whitespace (isspace), but end a word.
	char control[8] = "a_b";
	control[1] = 0x01;
	Runner.Command("smrunner_args %s", control);
	Assert.Equal(g_argCount, 2);
	Assert.StrEqual(g_args[1], "a");
	Assert.Equal(g_args[2][0], 0x01);
	Assert.Equal(g_args[2][1], 'b');
}

public void Test_AWaitInAClientsCommandLineIsRefused() {
	int client = Runner.ConnectClient("Wanda");
	Runner.ExpectError("holds a wait: a client would run the commands after it in later frames of its own");
	Runner.ClientCommand(client, "sm_target_greet; wait; sm_target_greet");
}

// A buffer that a native is told is larger than the plugin's memory holds is
// a native error, before anything is written past it.
public void Test_ABufferBeyondThePluginsMemoryIsAnError() {
	char small[8];
	Runner.ExpectError("a buffer of 100000 bytes at");
	GetCurrentMap(small, 100000);
}
