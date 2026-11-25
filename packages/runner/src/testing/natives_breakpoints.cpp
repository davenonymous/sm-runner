// Copyright 2026 davenonymous
//
// The natives of smrunner.inc for breakpoints (Runner), the stopped plugin a
// breakpoint callback gets (Breakpoint), and plugins' variables by name at
// any time. Breakpoints need a test; the rules of reading and writing a
// variable are PluginVariable's.
#include <optional>
#include <string>

#include "core/globals.h"
#include "testing/natives_common.h"
#include "testing/test_session.h"

using namespace SourceMod;
using namespace SourcePawn;

namespace smrunner {

static cell_t BreakpointOrFail(IPluginContext* context, int id, const std::string& error) {
	if (id == 0) {
		TestSession::Current().Fail(context, "breakpoint: " + error);
	}
	return id;
}

static cell_t Runner_BreakAtLine(IPluginContext* context, const cell_t* params) {
	TestSession& session = TestSession::Current();
	if (!session.RequireTest(context, "Runner.BreakAtLine")) {
		return 0;
	}
	IPluginFunction* callback = OptionalFunction(context, params[3]);
	if (!callback) {
		return context->ThrowNativeError("a breakpoint needs a function");
	}
	std::optional<std::string> file = StringArg(context, params[1]);
	std::optional<std::string> pluginFile = file ? StringArg(context, params[5]) : std::nullopt;
	if (!pluginFile) {
		return 0;
	}
	std::string error;
	int id = session.debugger().AddLine(*file, params[2], *pluginFile, callback, params[4], &error);
	return BreakpointOrFail(context, id, error);
}

static cell_t Runner_BreakAtFunction(IPluginContext* context, const cell_t* params) {
	TestSession& session = TestSession::Current();
	if (!session.RequireTest(context, "Runner.BreakAtFunction")) {
		return 0;
	}
	IPluginFunction* callback = OptionalFunction(context, params[2]);
	if (!callback) {
		return context->ThrowNativeError("a breakpoint needs a function");
	}
	std::optional<std::string> function = StringArg(context, params[1]);
	std::optional<std::string> pluginFile = function ? StringArg(context, params[4]) : std::nullopt;
	if (!pluginFile) {
		return 0;
	}
	std::string error;
	int id = session.debugger().AddFunction(*function, *pluginFile, callback, params[3], &error);
	return BreakpointOrFail(context, id, error);
}

static cell_t Runner_RemoveBreakpoint(IPluginContext* context, const cell_t* params) {
	TestSession& session = TestSession::Current();
	if (!session.RequireTest(context, "Runner.RemoveBreakpoint")) {
		return 0;
	}
	if (!session.debugger().Remove(params[1])) {
		return context->ThrowNativeError("there is no breakpoint %d", params[1]);
	}
	return 1;
}

static cell_t Runner_BreakpointHits(IPluginContext* context, const cell_t* params) {
	TestSession& session = TestSession::Current();
	if (!session.RequireTest(context, "Runner.BreakpointHits")) {
		return 0;
	}
	int hits = session.debugger().Hits(params[1]);
	if (hits < 0) {
		return context->ThrowNativeError("there is no breakpoint %d", params[1]);
	}
	return hits;
}

static BreakFrame* FrameOrError(IPluginContext* context, cell_t id) {
	BreakFrame* frame = TestSession::Current().debugger().FrameById(id);
	if (!frame) {
		context->ThrowNativeError("this Breakpoint is not valid here (only inside its callback)");
	}
	return frame;
}

static cell_t Breakpoint_Id(IPluginContext* context, const cell_t* params) {
	BreakFrame* frame = FrameOrError(context, params[1]);
	return frame ? frame->breakpoint : 0;
}

static cell_t Breakpoint_Line(IPluginContext* context, const cell_t* params) {
	BreakFrame* frame = FrameOrError(context, params[1]);
	return frame ? static_cast<cell_t>(frame->Line()) : 0;
}

static cell_t Breakpoint_Plugin(IPluginContext* context, const cell_t* params) {
	BreakFrame* frame = FrameOrError(context, params[1]);
	return frame && frame->plugin ? frame->plugin->GetMyHandle() : 0;
}

static cell_t Breakpoint_GetFile(IPluginContext* context, const cell_t* params) {
	BreakFrame* frame = FrameOrError(context, params[1]);
	if (frame) {
		WriteStringArg(context, params[2], params[3], frame->File());
	}
	return 1;
}

static cell_t Breakpoint_GetFunction(IPluginContext* context, const cell_t* params) {
	BreakFrame* frame = FrameOrError(context, params[1]);
	if (frame) {
		WriteStringArg(context, params[2], params[3], frame->Function());
	}
	return 1;
}

// A variable of the stopped plugin (frame) or a global of plugin, by the
// name argument; a native error when there is none.
static std::optional<PluginVariable> VariableOrError(IPluginContext* context, BreakFrame* frame, IPlugin* plugin,
                                                     cell_t name) {
	std::optional<std::string> variableName = StringArg(context, name);
	if (!variableName) {
		return std::nullopt;
	}
	Debugger& debugger = TestSession::Current().debugger();
	std::string error;
	std::optional<PluginVariable> variable = frame ? debugger.Resolve(*frame, *variableName, &error)
	                                               : debugger.ResolveGlobal(plugin, *variableName, &error);
	OkOrThrow(context, variable.has_value(), error);
	return variable;
}

static cell_t ReadValue(IPluginContext* context, BreakFrame* frame, IPlugin* plugin, cell_t name, cell_t index) {
	std::optional<PluginVariable> variable = VariableOrError(context, frame, plugin, name);
	cell_t value = 0;
	std::string error;
	return variable && OkOrThrow(context, variable->ReadCell(index, &value, &error), error) ? value : 0;
}

static cell_t WriteValue(IPluginContext* context, BreakFrame* frame, IPlugin* plugin, cell_t name, cell_t value,
                         cell_t index) {
	std::optional<PluginVariable> variable = VariableOrError(context, frame, plugin, name);
	std::string error;
	return variable && OkOrThrow(context, variable->WriteCell(index, value, &error), error);
}

static cell_t ReadString(IPluginContext* context, BreakFrame* frame, IPlugin* plugin, cell_t name, cell_t buffer,
                         cell_t maxlength) {
	std::optional<PluginVariable> variable = VariableOrError(context, frame, plugin, name);
	std::string text;
	std::string error;
	if (!variable || !OkOrThrow(context, variable->ReadString(&text, &error), error)) {
		return 0;
	}
	return WriteStringArg(context, buffer, maxlength, text);
}

static cell_t WriteString(IPluginContext* context, BreakFrame* frame, IPlugin* plugin, cell_t name, cell_t value) {
	std::optional<PluginVariable> variable = VariableOrError(context, frame, plugin, name);
	std::optional<std::string> text = variable ? StringArg(context, value) : std::nullopt;
	std::string error;
	return text && OkOrThrow(context, variable->WriteString(*text, &error), error);
}

static cell_t Breakpoint_HasVariable(IPluginContext* context, const cell_t* params) {
	BreakFrame* frame = FrameOrError(context, params[1]);
	std::optional<std::string> name = frame ? StringArg(context, params[2]) : std::nullopt;
	std::string error;
	return name && TestSession::Current().debugger().Resolve(*frame, *name, &error);
}

static cell_t Breakpoint_GetValue(IPluginContext* context, const cell_t* params) {
	BreakFrame* frame = FrameOrError(context, params[1]);
	return frame ? ReadValue(context, frame, nullptr, params[2], params[3]) : 0;
}

static cell_t Breakpoint_SetValue(IPluginContext* context, const cell_t* params) {
	BreakFrame* frame = FrameOrError(context, params[1]);
	return frame ? WriteValue(context, frame, nullptr, params[2], params[3], params[4]) : 0;
}

static cell_t Breakpoint_GetString(IPluginContext* context, const cell_t* params) {
	BreakFrame* frame = FrameOrError(context, params[1]);
	return frame ? ReadString(context, frame, nullptr, params[2], params[3], params[4]) : 0;
}

static cell_t Breakpoint_SetString(IPluginContext* context, const cell_t* params) {
	BreakFrame* frame = FrameOrError(context, params[1]);
	return frame ? WriteString(context, frame, nullptr, params[2], params[3]) : 0;
}

static IPlugin* PluginOrError(IPluginContext* context, cell_t handle) {
	HandleError error;
	IPlugin* plugin = scripts->FindPluginByHandle(static_cast<Handle_t>(handle), &error);
	if (!plugin) {
		context->ThrowNativeError("invalid plugin handle %x (error %d)", handle, error);
	}
	return plugin;
}

static cell_t Runner_GetPluginValue(IPluginContext* context, const cell_t* params) {
	IPlugin* plugin = PluginOrError(context, params[1]);
	return plugin ? ReadValue(context, nullptr, plugin, params[2], params[3]) : 0;
}

static cell_t Runner_SetPluginValue(IPluginContext* context, const cell_t* params) {
	IPlugin* plugin = PluginOrError(context, params[1]);
	return plugin ? WriteValue(context, nullptr, plugin, params[2], params[3], params[4]) : 0;
}

static cell_t Runner_GetPluginString(IPluginContext* context, const cell_t* params) {
	IPlugin* plugin = PluginOrError(context, params[1]);
	return plugin ? ReadString(context, nullptr, plugin, params[2], params[3], params[4]) : 0;
}

static cell_t Runner_SetPluginString(IPluginContext* context, const cell_t* params) {
	IPlugin* plugin = PluginOrError(context, params[1]);
	return plugin ? WriteString(context, nullptr, plugin, params[2], params[3]) : 0;
}

REGISTER_NATIVES(breakpointNatives) {
	{"Runner.BreakAtLine", Runner_BreakAtLine},
	{"Runner.BreakAtFunction", Runner_BreakAtFunction},
	{"Runner.RemoveBreakpoint", Runner_RemoveBreakpoint},
	{"Runner.BreakpointHits", Runner_BreakpointHits},
	{"Runner.GetPluginValue", Runner_GetPluginValue},
	{"Runner.SetPluginValue", Runner_SetPluginValue},
	{"Runner.GetPluginString", Runner_GetPluginString},
	{"Runner.SetPluginString", Runner_SetPluginString},
	{"Breakpoint.Id.get", Breakpoint_Id},
	{"Breakpoint.Line.get", Breakpoint_Line},
	{"Breakpoint.Plugin.get", Breakpoint_Plugin},
	{"Breakpoint.GetFile", Breakpoint_GetFile},
	{"Breakpoint.GetFunction", Breakpoint_GetFunction},
	{"Breakpoint.HasVariable", Breakpoint_HasVariable},
	{"Breakpoint.GetValue", Breakpoint_GetValue},
	{"Breakpoint.SetValue", Breakpoint_SetValue},
	{"Breakpoint.GetString", Breakpoint_GetString},
	{"Breakpoint.SetString", Breakpoint_SetString},
	{nullptr, nullptr},
};

}  // namespace smrunner
