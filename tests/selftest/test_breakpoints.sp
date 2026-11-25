/**
 * Copyright 2026 davenonymous
 *
 * Self tests of breakpoints: stopping at lines and functions of target.smx,
 * reading and changing its locals, arguments and globals while it waits.
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <smrunner>

int g_hits;
int g_seenValue;
int g_seenStep;
char g_seenFunction[64];
char g_seenGreeting[64];

void OnAddTo(Breakpoint frame, any data) {
	g_hits++;
	g_seenValue = frame.GetValue("value");
	g_seenStep = frame.GetValue("step");
	frame.GetFunction(g_seenFunction, sizeof(g_seenFunction));
	Assert.Equal(data, 77, "the breakpoint's data arrives");
}

public void Test_BreakAtFunctionReadsArguments() {
	int id = Runner.BreakAtFunction("AddTo", OnAddTo, 77);
	Runner.Command("sm_target_count 3");
	Assert.Equal(Runner.BreakpointHits(id), 1);
	Assert.Equal(g_hits, 1);
	Assert.Equal(g_seenValue, 0);
	Assert.Equal(g_seenStep, 3);
	Assert.StrEqual(g_seenFunction, "AddTo");
}

void OnAddToChangeStep(Breakpoint frame) {
	frame.SetValue("step", 40);
}

public void Test_BreakpointsChangeArguments() {
	Runner.BreakAtFunction("AddTo", OnAddToChangeStep);
	Runner.Command("sm_target_count 2");
	Assert.ConsoleContains("counter is 40", "AddTo ran with the changed step");
}

int g_expectedLine;

void OnSum(Breakpoint frame) {
	Assert.True(frame.HasVariable("sum"), "sum is visible on its line");
	Assert.Equal(frame.Line, g_expectedLine, "the plugin stopped on the line asked for");
	g_seenValue = frame.GetValue("sum");
	frame.SetValue("g_counter", 501);
}

public void Test_BreakAtLineReadsLocalsAndGlobals() {
	// target.sp: g_history[g_counter % sizeof(g_history)] = sum;
	g_expectedLine = FindLine("g_history[g_counter % sizeof(g_history)] = sum;");
	int id = Runner.BreakAtLine("target.sp", g_expectedLine, OnSum);
	Runner.Command("sm_target_count 5");
	Assert.Equal(Runner.BreakpointHits(id), 1);
	Assert.Equal(g_seenValue, 5, "sum was computed before the line");
	// The plugin wrote the history with the global the breakpoint changed.
	Handle target = FindPluginByFile("target.smx");
	Assert.Equal(Runner.GetPluginValue(target, "g_history", 501 % 4), 5);
	Assert.Equal(Runner.GetPluginValue(target, "g_history", 0), 0, "not where the old counter points");
}

void OnGreet(Breakpoint frame) {
	frame.GetString("greeting", g_seenGreeting, sizeof(g_seenGreeting));
	frame.SetString("greeting", "good evening");
}

public void Test_BreakpointsReadAndWriteStrings() {
	Runner.BreakAtLine("target.sp", FindLine("char name[64];"), OnGreet);
	Runner.Command("sm_target_greet");
	Assert.StrEqual(g_seenGreeting, "hello");
	Assert.ConsoleContains("good evening, console");
}

void OnTimer(Breakpoint frame) {
	g_hits++;
	Runner.RemoveBreakpoint(frame.Id);
}

public void Test_BreakpointsCanBeRemoved() {
	Runner.BreakAtFunction("Timer_Tick", OnTimer);
	Runner.Seconds(3.5);
	Assert.Equal(g_hits, 1, "the breakpoint removed itself on its first hit");
}

public void Test_BreakpointsInFramesRunWhileThePluginWaits() {
	Runner.BreakAtFunction("Command_Count", OnCountRunsFrames);
	int ticks = GetGameTickCount();
	Runner.Command("sm_target_count");
	Assert.Equal(GetGameTickCount(), ticks + 5, "the frames ran inside the breakpoint");
}

void OnCountRunsFrames(Breakpoint frame) {
	Runner.Frames(5);
}

// The line of target.sp that holds text, so the tests do not depend on
// line numbers.
int FindLine(const char[] text) {
	File file = OpenFile("addons/sourcemod/data/target.sp", "r");
	Assert.True(file != null, "the test was given target.sp (--file)");
	char line[256];
	int number = 0;
	while (file.ReadLine(line, sizeof(line))) {
		number++;
		if (StrContains(line, text) != -1) {
			delete file;
			return number;
		}
	}
	delete file;
	Assert.Fail("target.sp has no line with %s", text);
	return 0;
}
