// Copyright 2026 davenonymous
//
// The natives of smrunner.inc that drive the simulated server: frames, time,
// server commands, the map and the console.
#include <math.h>
#include <unistd.h>

#include <string>

#include "core/core.h"
#include "monotonic.h"
#include "testing/natives_common.h"
#include "testing/plugin_memory.h"

using namespace SourceMod;
using namespace SourcePawn;

namespace smrunner {

static cell_t Runner_Frames(IPluginContext* context, const cell_t* params) {
	if (!FramesAllowedOrError(context)) {
		return 0;
	}
	if (params[1] < 0) {
		return context->ThrowNativeError("cannot run %d frames", params[1]);
	}
	Core::Get().RunFrames(params[1]);
	return 1;
}

static cell_t Runner_Seconds(IPluginContext* context, const cell_t* params) {
	if (!FramesAllowedOrError(context)) {
		return 0;
	}
	float seconds = sp_ctof(params[1]);
	if (seconds < 0.0f) {
		return context->ThrowNativeError("cannot run %f seconds", seconds);
	}
	float interval = Core::Get().engine().globals().interval_per_tick;
	Core::Get().RunFrames(static_cast<int>(ceilf(seconds / interval - 0.0001f)));
	return 1;
}

static cell_t Runner_WaitUntil(IPluginContext* context, const cell_t* params) {
	if (!FramesAllowedOrError(context)) {
		return 0;
	}
	IPluginFunction* condition = context->GetFunctionById(params[1]);
	if (!condition) {
		return context->ThrowNativeError("invalid condition function %x", params[1]);
	}
	double deadline = MonotonicSeconds() + sp_ctof(params[2]);
	while (true) {
		cell_t met = 0;
		condition->PushCell(params[3]);
		if (condition->Execute(&met) != SP_ERROR_NONE) {
			return 0;
		}
		if (met) {
			return 1;
		}
		if (MonotonicSeconds() > deadline || !Core::Get().RunFrame()) {
			return 0;
		}
		usleep(1000);
	}
}

// ------------------------------------------------------------------- server

static cell_t Runner_Command(IPluginContext* context, const cell_t* params) {
	std::string command = FormattedArg(context, params, 1);
	Core::Get().engine().console().ExecuteNow(command);
	return 1;
}

static cell_t Runner_CommandOutput(IPluginContext* context, const cell_t* params) {
	// The buffer is checked before the command runs.
	if (params[2] < 0) {
		return context->ThrowNativeError("a buffer cannot have %d bytes", params[2]);
	}
	if (params[2] > 0 && !PluginBytesArg(context, params[1], params[2])) {
		return 0;
	}
	std::string command = FormattedArg(context, params, 3);
	Engine& engine = Core::Get().engine();
	std::string captured;
	engine.output().PushCapture(&captured);
	engine.console().ExecuteNow(command);
	engine.output().PopCapture();
	return WriteStringArg(context, params[1], params[2], captured);
}

static cell_t Runner_ChangeMap(IPluginContext* context, const cell_t* params) {
	if (!FramesAllowedOrError(context)) {
		return 0;
	}
	std::optional<std::string> given = StringArg(context, params[1]);
	if (!given) {
		return 0;
	}
	const std::string& map = *given;
	if (map.empty()) {
		return context->ThrowNativeError("a map name is needed");
	}
	Core& core = Core::Get();
	core.engine().DeclareMap(map);
	std::string error = core.ChangeMap(map);
	return OkOrThrow(context, error.empty(), error);
}

// ------------------------------------------------------------------ console

static cell_t Runner_ConsoleContains(IPluginContext* context, const cell_t* params) {
	std::optional<std::string> text = StringArg(context, params[1]);
	return text && Core::Get().engine().output().Text().find(*text) != std::string::npos;
}

static cell_t Runner_GetConsole(IPluginContext* context, const cell_t* params) {
	const std::string& text = Core::Get().engine().output().Text();
	size_t room = params[2] > 0 ? static_cast<size_t>(params[2] - 1) : 0;
	std::string tail = text.size() > room ? text.substr(text.size() - room) : text;
	return WriteStringArg(context, params[1], params[2], tail);
}

static cell_t Runner_ClearConsole(IPluginContext*, const cell_t*) {
	Core::Get().engine().output().Clear();
	return 1;
}

REGISTER_NATIVES(serverNatives) {
	{"Runner.Frames", Runner_Frames},
	{"Runner.Seconds", Runner_Seconds},
	{"Runner.WaitUntil", Runner_WaitUntil},
	{"Runner.Command", Runner_Command},
	{"Runner.CommandOutput", Runner_CommandOutput},
	{"Runner.ChangeMap", Runner_ChangeMap},
	{"Runner.ConsoleContains", Runner_ConsoleContains},
	{"Runner.GetConsole", Runner_GetConsole},
	{"Runner.ClearConsole", Runner_ClearConsole},
	{nullptr, nullptr},
};

}  // namespace smrunner
