/**
 * Copyright 2026 davenonymous
 *
 * The failing case of the test action's self test in CI: one test passes,
 * one fails and one is skipped, so the action's counts and result can be
 * checked. tests/run.sh does not run it.
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <smrunner>

public void Test_Passes() {
	Assert.Equal(2 + 2, 4);
}

public void Test_Fails() {
	Assert.Equal(2 + 2, 5, "fails on purpose");
}

public void Test_IsSkipped() {
	Runner.Skip("skipped on purpose");
}
