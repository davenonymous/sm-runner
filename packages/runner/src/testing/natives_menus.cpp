// Copyright 2026 davenonymous
//
// The natives of smrunner.inc for the menu a simulated client shows: reading,
// drawing and selecting it.
#include <string>
#include <vector>

#include "core/core.h"
#include "core/players.h"
#include "core/sourcemod_api.h"
#include "engine/clients.h"
#include "testing/menu_box.h"
#include "testing/natives_common.h"
#include "testing/test_session.h"

using namespace SourceMod;
using namespace SourcePawn;

namespace smrunner {

static cell_t Runner_GetClientMenu(IPluginContext* context, const cell_t* params) {
	SimulatedClient* slot = ClientSlotOrError(context, params[1]);
	if (!slot) {
		return 0;
	}
	const MenuDisplay* menu = slot->CurrentMenu();
	WriteStringArg(context, params[2], params[3], menu ? menu->text : std::string());
	return menu != nullptr;
}

// The menu the client shows, drawn: under the test's line in the report, or
// on standard output in run mode.
static cell_t Runner_PrintClientMenu(IPluginContext* context, const cell_t* params) {
	SimulatedClient* slot = ClientSlotOrError(context, params[1]);
	if (!slot) {
		return 0;
	}
	std::string heading = "menu of client " + std::to_string(params[1]) + " (" + slot->Identity().name + ")";
	const MenuDisplay* menu = slot->CurrentMenu();
	std::string note = menu ? heading + "\n" + DrawMenuBox(*menu) : heading + ": none\n";
	TestSession::Current().AddNote(note);
	return 1;
}

static cell_t Runner_ClientMenuKeys(IPluginContext* context, const cell_t* params) {
	SimulatedClient* slot = ClientSlotOrError(context, params[1]);
	const MenuDisplay* menu = slot ? slot->CurrentMenu() : nullptr;
	return menu ? menu->keys : 0;
}

static void PressMenuKey(int client, int key) {
	Core::Get().sourcemod().SetGlobalTarget(client);
	Core::Get().players().ExecuteClientCommand(client, "menuselect " + std::to_string(key));
}

static cell_t Runner_SelectMenu(IPluginContext* context, const cell_t* params) {
	if (!ConnectedClientOrError(context, params[1])) {
		return 0;
	}
	PressMenuKey(params[1], params[2]);
	return 1;
}

static cell_t Runner_SelectMenuItem(IPluginContext* context, const cell_t* params) {
	SimulatedClient* slot = ConnectedClientOrError(context, params[1]);
	if (!slot) {
		return 0;
	}
	std::optional<std::string> wanted = StringArg(context, params[2]);
	if (!wanted) {
		return 0;
	}
	const std::string& text = *wanted;
	std::string what = "Runner.SelectMenuItem failed: ";
	std::string client = std::to_string(params[1]);
	const MenuDisplay* menu = slot->CurrentMenu();
	if (!menu) {
		return FailTest(context, what + "client " + client + " shows no menu", "");
	}
	std::vector<MenuLine> matches;
	for (const MenuLine& line : MenuLines(*menu)) {
		if (line.selectable && line.label.find(text) != std::string::npos) {
			matches.push_back(line);
		}
	}
	if (matches.empty()) {
		return FailTest(context,
		                what + "no line client " + client + " can select contains " + Quote(text) +
		                    "; the menu shows:\n" + DrawMenuBox(*menu),
		                "");
	}
	if (matches.size() > 1) {
		std::string lines;
		for (const MenuLine& line : matches) {
			lines += (lines.empty() ? "" : ", ") + Quote(line.text);
		}
		return FailTest(context, what + "more than one line contains " + Quote(text) + ": " + lines, "");
	}
	PressMenuKey(params[1], matches[0].key);
	return 1;
}

REGISTER_NATIVES(menuNatives) {
	{"Runner.GetClientMenu", Runner_GetClientMenu},
	{"Runner.ClientMenuKeys", Runner_ClientMenuKeys},
	{"Runner.PrintClientMenu", Runner_PrintClientMenu},
	{"Runner.SelectMenu", Runner_SelectMenu},
	{"Runner.SelectMenuItem", Runner_SelectMenuItem},
	{nullptr, nullptr},
};

}  // namespace smrunner
