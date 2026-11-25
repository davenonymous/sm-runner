/**
 * Copyright 2026 davenonymous
 *
 * Self tests of native hooks and mocks, against target.smx, and against
 * hook_dependent.smx with the provider of its native, hook_provider.smx.
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <smrunner>

int g_preCalls;
int g_postCalls;
char g_seenCvar[64];

Action Pre_GetConVarString(RunnerCall call) {
	g_preCalls++;
	return Plugin_Continue;
}

void Post_GetConVarString(RunnerCall call) {
	g_postCalls++;
	// The plugin's buffer is argument 2 of ConVar.GetString(this, buffer, maxlength).
	call.GetArgString(2, g_seenCvar, sizeof(g_seenCvar));
}

public void Test_HooksSeeCallsAndResults() {
	Runner.HookNative("ConVar.GetString", Pre_GetConVarString, Post_GetConVarString);
	Runner.Command("sm_target_greet");
	Assert.Equal(g_preCalls, 1);
	Assert.Equal(g_postCalls, 1);
	Assert.StrEqual(g_seenCvar, "hello", "the post hook sees what the native wrote");
	Assert.Equal(Runner.NativeCalls("ConVar.GetString"), 1);
}

Action Pre_ReplaceGreeting(RunnerCall call) {
	call.SetArgString(2, "howdy", call.Arg(3));
	call.Result = 1;
	return Plugin_Handled;
}

public void Test_PreHooksCanHandleTheCall() {
	Runner.HookNative("ConVar.GetString", Pre_ReplaceGreeting);
	Runner.Command("sm_target_greet");
	Assert.ConsoleContains("howdy, console");
}

public void Test_TestPluginsAreNotHooked() {
	Runner.HookNative("ConVar.GetString", Pre_ReplaceGreeting);
	char value[32];
	FindConVar("sm_target_greeting").GetString(value, sizeof(value));
	Assert.StrEqual(value, "hello", "the test plugin still calls the real native");
	Assert.Equal(Runner.NativeCalls("ConVar.GetString"), 0);
}

any Mock_GetClientName(RunnerCall call) {
	char real[64];
	// The real native, then a changed answer.
	if (!call.CallOriginal()) {
		return false;
	}
	call.GetArgString(2, real, sizeof(real));
	char changed[64];
	Format(changed, sizeof(changed), "[mocked] %s", real);
	call.SetArgString(2, changed, call.Arg(3));
	return true;
}

public void Test_MocksReplaceAndCanCallTheOriginal() {
	int client = Runner.ConnectClient("Dave");
	Runner.MockNative("GetClientName", Mock_GetClientName);
	Runner.ClientCommand(client, "sm_target_greet");
	Assert.ClientReceived(client, RunnerMessage_Console, "hello, [mocked] Dave");
	Runner.UnhookNative("GetClientName");
	Runner.ClientCommand(client, "sm_target_greet");
	Assert.ClientReceived(client, RunnerMessage_Console, "hello, Dave");
}

// GetCmdArg(argnum, buffer, maxlength); GetCmdArgInt is a stock on it.
any Mock_GetCmdArg(RunnerCall call) {
	call.SetArgString(2, "1000", call.Arg(3));
	return 4;
}

public void Test_MocksChangeWhatThePluginComputes() {
	Runner.MockNative("GetCmdArg", Mock_GetCmdArg);
	Runner.Command("sm_target_count 1");
	Assert.ConsoleContains("counter is 1000");
}

int g_bootMockCalls;

any Mock_CreateTimer(RunnerCall call) {
	g_bootMockCalls++;
	return call.CallOriginal();
}

public void OnTestBoot(const char[] test) {
	if (StrEqual(test, "Test_MocksFromOnTestBootSeeOnPluginStart")) {
		Runner.MockNative("CreateTimer", Mock_CreateTimer);
	}
}

public void Test_MocksFromOnTestBootSeeOnPluginStart() {
	Assert.Equal(g_bootMockCalls, 1, "target's OnPluginStart created its timer through the mock");
}

int g_checked;

public void Test_HookArgumentChecks() {
	Runner.HookNative("ConVar.GetString", Pre_CheckArguments);
	Runner.Command("sm_target_greet");
	Assert.Equal(g_checked, 1);
}

Action Pre_CheckArguments(RunnerCall call) {
	Assert.Equal(call.ArgCount, 3);
	char name[64];
	call.GetName(name, sizeof(name));
	Assert.StrEqual(name, "ConVar.GetString");
	char file[PLATFORM_MAX_PATH];
	GetPluginFilename(call.Caller, file, sizeof(file));
	Assert.StrEqual(file, "target.smx");
	g_checked++;
	return Plugin_Continue;
}

public void Test_ArgumentsOutsideTheCallersMemoryAreRefused() {
	Runner.ExpectError("argument 2 of ConVar.GetString is not 100000 cells long");
	Runner.HookNative("ConVar.GetString", Pre_ReadTooMuch);
	Runner.Command("sm_target_greet");
}

int g_tooMuch[100000];

Action Pre_ReadTooMuch(RunnerCall call) {
	call.GetArgArray(2, g_tooMuch, sizeof(g_tooMuch));
	return Plugin_Continue;
}

int g_onceCalls;

any Mock_GetGameTimeOnce(RunnerCall call) {
	g_onceCalls++;
	Runner.UnhookNative("GetGameTime");
	return call.CallOriginal();
}

public void Test_MocksCanUnhookThemselves() {
	Runner.MockNative("GetGameTime", Mock_GetGameTimeOnce);
	Runner.Seconds(2.1);
	Assert.Equal(g_onceCalls, 1, "the mock ran once and removed itself");
}

public void Test_HooksSurviveAPluginReload() {
	Runner.HookNative("ConVar.GetString", Pre_GetConVarString);
	Runner.Command("sm plugins reload target");
	Runner.Command("sm_target_greet");
	Assert.Equal(g_preCalls, 1, "the reloaded plugin is hooked");
	Runner.UnhookNative("ConVar.GetString");
	Runner.Command("sm_target_greet");
	Assert.Equal(g_preCalls, 1);
}

any Mock_ProviderValue(RunnerCall call) {
	return 99;
}

public void Test_HooksFollowAReloadedProvider() {
	Runner.MockNative("HookProvider_Value", Mock_ProviderValue);
	Runner.Command("hook_dependent_value");
	Assert.ConsoleContains("hook_dependent: value 99");
	Assert.Equal(Runner.NativeCalls("HookProvider_Value"), 1);
	// SourceMod reloads it on the next frame: the test plugin is running.
	// Unloading it unbinds the dependent's native, loading it binds the new
	// one, and the dependent calls it when the library is added.
	Runner.Command("sm plugins reload hook_provider");
	Runner.Frames(2);
	Assert.ConsoleContains("hook_dependent: provider added, value 99", "the new binding is mocked at once");
	Runner.ClearConsole();
	Runner.Command("hook_dependent_value");
	Assert.ConsoleContains("hook_dependent: value 99");
	Assert.Equal(Runner.NativeCalls("HookProvider_Value"), 3);
	Runner.UnhookNative("HookProvider_Value");
	Runner.Command("hook_dependent_value");
	Assert.ConsoleContains("hook_dependent: value 7", "unhooked, the dependent calls the new provider");
	Assert.Equal(Runner.NativeCalls("HookProvider_Value"), 3);
}

public void Test_HooksWrapANativeBoundAfterThem() {
	Runner.Command("sm plugins unload hook_provider");
	Runner.Frames(2);
	Runner.MockNative("HookProvider_Value", Mock_ProviderValue);
	Runner.Command("sm plugins load hook_provider");
	Runner.Frames(2);
	Runner.ClearConsole();
	Runner.Command("hook_dependent_value");
	Assert.ConsoleContains("hook_dependent: value 99", "the native bound once its provider loaded is mocked");
}
