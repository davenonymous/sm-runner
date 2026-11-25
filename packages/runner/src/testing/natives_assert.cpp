// Copyright 2026 davenonymous
//
// The assertions of smrunner.inc (Assert) and the natives about the test
// itself: expected errors, skipping, its name.
#include <math.h>
#include <stdio.h>

#include <string>

#include "core/core.h"
#include "engine/clients.h"
#include "testing/menu_box.h"
#include "testing/natives_common.h"
#include "testing/test_session.h"

using namespace SourceMod;
using namespace SourcePawn;

namespace smrunner {

static cell_t Assert_True(IPluginContext* context, const cell_t* params) {
	if (!params[1]) {
		return FailTest(context, "Assert.True failed", FormattedArg(context, params, 2));
	}
	return 1;
}

static cell_t Assert_False(IPluginContext* context, const cell_t* params) {
	if (params[1]) {
		return FailTest(context, "Assert.False failed", FormattedArg(context, params, 2));
	}
	return 1;
}

static cell_t Assert_Equal(IPluginContext* context, const cell_t* params) {
	if (params[1] != params[2]) {
		std::string what =
			"Assert.Equal failed: expected " + std::to_string(params[2]) + ", got " + std::to_string(params[1]);
		return FailTest(context, what, FormattedArg(context, params, 3));
	}
	return 1;
}

static cell_t Assert_NotEqual(IPluginContext* context, const cell_t* params) {
	if (params[1] == params[2]) {
		return FailTest(context, "Assert.NotEqual failed: both are " + std::to_string(params[1]),
		                FormattedArg(context, params, 3));
	}
	return 1;
}

static cell_t Assert_FloatEqual(IPluginContext* context, const cell_t* params) {
	float actual = sp_ctof(params[1]);
	float expected = sp_ctof(params[2]);
	float tolerance = sp_ctof(params[3]);
	if (!(fabsf(actual - expected) <= tolerance)) {
		char what[160];
		snprintf(what, sizeof(what), "Assert.FloatEqual failed: expected %g, got %g (tolerance %g)", expected, actual,
		         tolerance);
		return FailTest(context, what, FormattedArg(context, params, 4));
	}
	return 1;
}

static cell_t Assert_StrEqual(IPluginContext* context, const cell_t* params) {
	std::optional<std::string> actual = StringArg(context, params[1]);
	std::optional<std::string> expected = actual ? StringArg(context, params[2]) : std::nullopt;
	if (!expected) {
		return 0;
	}
	if (*actual != *expected) {
		return FailTest(context, "Assert.StrEqual failed: expected " + Quote(*expected) + ", got " + Quote(*actual),
		                FormattedArg(context, params, 3));
	}
	return 1;
}

static cell_t Assert_StrContains(IPluginContext* context, const cell_t* params) {
	std::optional<std::string> text = StringArg(context, params[1]);
	std::optional<std::string> part = text ? StringArg(context, params[2]) : std::nullopt;
	if (!part) {
		return 0;
	}
	if (text->find(*part) == std::string::npos) {
		return FailTest(context, "Assert.StrContains failed: " + Quote(*text) + " does not contain " + Quote(*part),
		                FormattedArg(context, params, 3));
	}
	return 1;
}

static cell_t Assert_ConsoleContains(IPluginContext* context, const cell_t* params) {
	std::optional<std::string> text = StringArg(context, params[1]);
	if (!text) {
		return 0;
	}
	if (Core::Get().engine().output().Text().find(*text) == std::string::npos) {
		return FailTest(context, "Assert.ConsoleContains failed: the console did not print " + Quote(*text),
		                FormattedArg(context, params, 2));
	}
	return 1;
}

static cell_t Assert_ConsoleNotContains(IPluginContext* context, const cell_t* params) {
	std::optional<std::string> text = StringArg(context, params[1]);
	if (!text) {
		return 0;
	}
	if (Core::Get().engine().output().Text().find(*text) != std::string::npos) {
		return FailTest(context, "Assert.ConsoleNotContains failed: the console printed " + Quote(*text),
		                FormattedArg(context, params, 2));
	}
	return 1;
}

static cell_t Assert_ClientReceived(IPluginContext* context, const cell_t* params) {
	SimulatedClient* slot = Core::Get().engine().clients().Slot(params[1]);
	std::optional<std::string> text = StringArg(context, params[3]);
	if (!text) {
		return 0;
	}
	if (!slot || !HasMessage(*slot, params[2], *text)) {
		return FailTest(context,
		                "Assert.ClientReceived failed: client " + std::to_string(params[1]) + " did not receive " +
		                    Quote(*text),
		                FormattedArg(context, params, 4));
	}
	return 1;
}

static cell_t Assert_ClientMenu(IPluginContext* context, const cell_t* params) {
	SimulatedClient* slot = Core::Get().engine().clients().Slot(params[1]);
	std::optional<std::string> text = StringArg(context, params[2]);
	if (!text) {
		return 0;
	}
	const MenuDisplay* menu = slot ? slot->CurrentMenu() : nullptr;
	std::string client = std::to_string(params[1]);
	if (!menu) {
		return FailTest(context, "Assert.ClientMenu failed: client " + client + " shows no menu",
		                FormattedArg(context, params, 3));
	}
	if (menu->text.find(*text) == std::string::npos) {
		return FailTest(context,
		                "Assert.ClientMenu failed: the menu of client " + client + " does not contain " + Quote(*text) +
		                    "; it shows:\n" + DrawMenuBox(*menu),
		                FormattedArg(context, params, 3));
	}
	return 1;
}

static cell_t Assert_Fail(IPluginContext* context, const cell_t* params) {
	return FailTest(context, "Assert.Fail", FormattedArg(context, params, 1));
}

static cell_t Runner_ExpectError(IPluginContext* context, const cell_t* params) {
	if (!TestSession::Current().RequireTest(context, "Runner.ExpectError")) {
		return 0;
	}
	std::optional<std::string> text = StringArg(context, params[1]);
	if (!text) {
		return 0;
	}
	TestSession::Current().ExpectError(*text);
	return 1;
}

static cell_t Runner_Skip(IPluginContext* context, const cell_t* params) {
	if (!TestSession::Current().RequireTest(context, "Runner.Skip")) {
		return 0;
	}
	TestSession::Current().Skip(context, FormattedArg(context, params, 1));
	return 0;
}

static cell_t Runner_GetTestName(IPluginContext* context, const cell_t* params) {
	if (!TestSession::Current().RequireTest(context, "Runner.GetTestName")) {
		return 0;
	}
	return WriteStringArg(context, params[1], params[2], TestSession::Current().TestName());
}

REGISTER_NATIVES(assertNatives) {
	{"Assert.True", Assert_True},
	{"Assert.False", Assert_False},
	{"Assert.Equal", Assert_Equal},
	{"Assert.NotEqual", Assert_NotEqual},
	{"Assert.FloatEqual", Assert_FloatEqual},
	{"Assert.StrEqual", Assert_StrEqual},
	{"Assert.StrContains", Assert_StrContains},
	{"Assert.ConsoleContains", Assert_ConsoleContains},
	{"Assert.ConsoleNotContains", Assert_ConsoleNotContains},
	{"Assert.ClientReceived", Assert_ClientReceived},
	{"Assert.ClientMenu", Assert_ClientMenu},
	{"Assert.Fail", Assert_Fail},
	{"Runner.ExpectError", Runner_ExpectError},
	{"Runner.Skip", Runner_Skip},
	{"Runner.GetTestName", Runner_GetTestName},
	{nullptr, nullptr},
};

}  // namespace smrunner
