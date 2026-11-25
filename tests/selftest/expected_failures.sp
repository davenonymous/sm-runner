/**
 * Copyright 2026 davenonymous
 *
 * Tests that must fail, each in its own way: tests/run.sh checks that
 * smrunner reports every one of them as failed with the right message.
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <smrunner>

public void Test_AssertEqualFails() {
	Assert.Equal(2 + 2, 5, "arithmetic %s", "still works");
}

public void Test_AssertStrEqualFails() {
	Assert.StrEqual("actual", "expected");
}

public void Test_AssertFloatEqualFails() {
	Assert.FloatEqual(1.0, 1.5, 0.1);
}

public void Test_ConsoleContainsFails() {
	Assert.ConsoleContains("this text was never printed");
}

public void Test_UnexpectedPluginErrorFails() {
	Handle target = FindPluginByFile("target.smx");
	Call_StartFunction(target, GetFunctionByName(target, "Target_Divide"));
	Call_PushCell(1);
	Call_PushCell(0);
	int result;
	Call_Finish(result);
}

public void Test_UnmetExpectedErrorFails() {
	Runner.ExpectError("an error that never happens");
}

public void Test_ErrorInTheTestPluginFails() {
	int values[2];
	int index = 5;
	values[index] = 1;
}

public void Test_AssertionInACallbackFails() {
	RequestFrame(OnFrame);
	Runner.Frames(2);
}

void OnFrame(any data) {
	Assert.Fail("failed in a frame callback");
}

public void Test_UnknownBreakpointLineFails() {
	Runner.BreakAtLine("nosuchfile.sp", 3, OnBreak);
}

public void Test_UnknownBreakpointFunctionFails() {
	Runner.BreakAtFunction("NoSuchFunction", OnBreak);
}

void OnBreak(Breakpoint frame) {
}

public void Test_TimeoutIsReported() {
	Runner.WaitUntil(Never, 100.0);
}

bool Never() {
	return false;
}

public void Test_LongAssertionFailsOnce() {
	char long1[1500];
	char long2[1500];
	for (int i = 0; i < sizeof(long1) - 1; i++) {
		long1[i] = 'a';
		long2[i] = 'b';
	}
	Assert.StrEqual(long1, long2);
}

public void Test_AssertClientMenuWithoutAMenuFails() {
	int client = Runner.ConnectClient("alice");
	Assert.ClientMenu(client, "anything");
}

int PanelHandler_Nothing(Menu menu, MenuAction action, int param1, int param2) {
	return 0;
}

void ShowApplePanel(int client) {
	Panel panel = new Panel();
	panel.DrawItem("Red apple");
	panel.DrawItem("Green apple");
	panel.Send(client, PanelHandler_Nothing, MENU_TIME_FOREVER);
	delete panel;
}

public void Test_SelectMenuItemWithoutAMatchFails() {
	int client = Runner.ConnectClient("alice");
	ShowApplePanel(client);
	Runner.SelectMenuItem(client, "pear");
}

public void Test_SelectMenuItemWithSeveralMatchesFails() {
	int client = Runner.ConnectClient("alice");
	ShowApplePanel(client);
	Runner.SelectMenuItem(client, "apple");
}

public void Test_AssertClientReceivedOnMissingChatFails() {
	int client = Runner.ConnectClient("alice");
	PrintToChat(client, "\x07FF0000hello");
	Assert.ClientReceived(client, RunnerMessage_Chat, "goodbye");
}
