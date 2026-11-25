// Copyright 2026 davenonymous
//
// The natives of smrunner.inc for native hooks and mocks (Runner) and the
// call a hook or mock callback gets (RunnerCall). Hooks and mocks need a
// test; the rules of a call's arguments are NativeCall's.
#include <string>
#include <vector>

#include "core/globals.h"
#include "testing/native_hooks.h"
#include "testing/natives_common.h"
#include "testing/plugin_memory.h"
#include "testing/test_session.h"

using namespace SourceMod;
using namespace SourcePawn;

namespace smrunner {

cell_t Runner_MockNative(IPluginContext* context, const cell_t* params) {
	TestSession& session = TestSession::Current();
	if (!session.RequireTest(context, "Runner.MockNative")) {
		return 0;
	}
	IPluginFunction* mock = OptionalFunction(context, params[2]);
	if (!mock) {
		return context->ThrowNativeError("a mock needs a function");
	}
	std::optional<std::string> native = StringArg(context, params[1]);
	if (!native) {
		return 0;
	}
	session.hooks().Mock(context, *native, mock);
	return 1;
}

cell_t Runner_HookNative(IPluginContext* context, const cell_t* params) {
	TestSession& session = TestSession::Current();
	if (!session.RequireTest(context, "Runner.HookNative")) {
		return 0;
	}
	IPluginFunction* pre = OptionalFunction(context, params[2]);
	IPluginFunction* post = OptionalFunction(context, params[3]);
	if (!pre && !post) {
		return context->ThrowNativeError("a hook needs a pre or a post function");
	}
	std::optional<std::string> native = StringArg(context, params[1]);
	if (!native) {
		return 0;
	}
	session.hooks().Hook(context, *native, pre, post);
	return 1;
}

static cell_t Runner_UnhookNative(IPluginContext* context, const cell_t* params) {
	TestSession& session = TestSession::Current();
	if (!session.RequireTest(context, "Runner.UnhookNative")) {
		return 0;
	}
	std::optional<std::string> native = StringArg(context, params[1]);
	if (!native) {
		return 0;
	}
	session.hooks().Unhook(*native);
	return 1;
}

static cell_t Runner_NativeCalls(IPluginContext* context, const cell_t* params) {
	TestSession& session = TestSession::Current();
	if (!session.RequireTest(context, "Runner.NativeCalls")) {
		return 0;
	}
	std::optional<std::string> native = StringArg(context, params[1]);
	return native ? session.hooks().Calls(*native) : 0;
}

static NativeCall* CallOrError(IPluginContext* context, cell_t id) {
	NativeCall* call = TestSession::Current().hooks().CallById(id);
	if (!call) {
		context->ThrowNativeError("this RunnerCall is not valid here (only inside its hook or mock)");
	}
	return call;
}

static cell_t NativeCall_ArgCount(IPluginContext* context, const cell_t* params) {
	NativeCall* call = CallOrError(context, params[1]);
	return call ? call->ArgCount() : 0;
}

static cell_t NativeCall_Caller(IPluginContext* context, const cell_t* params) {
	NativeCall* call = CallOrError(context, params[1]);
	if (!call) {
		return 0;
	}
	IPlugin* plugin = scripts->FindPluginByContext(call->Caller()->GetContext());
	return plugin ? plugin->GetMyHandle() : 0;
}

static cell_t NativeCall_GetResult(IPluginContext* context, const cell_t* params) {
	NativeCall* call = CallOrError(context, params[1]);
	return call ? call->Result() : 0;
}

static cell_t NativeCall_SetResult(IPluginContext* context, const cell_t* params) {
	NativeCall* call = CallOrError(context, params[1]);
	if (call) {
		call->SetResult(params[2]);
	}
	return 1;
}

static cell_t NativeCall_GetName(IPluginContext* context, const cell_t* params) {
	NativeCall* call = CallOrError(context, params[1]);
	if (call) {
		WriteStringArg(context, params[2], params[3], call->Name());
	}
	return 1;
}

static cell_t NativeCall_Arg(IPluginContext* context, const cell_t* params) {
	NativeCall* call = CallOrError(context, params[1]);
	cell_t value = 0;
	std::string error;
	return call && OkOrThrow(context, call->Arg(params[2], &value, &error), error) ? value : 0;
}

static cell_t NativeCall_SetArg(IPluginContext* context, const cell_t* params) {
	NativeCall* call = CallOrError(context, params[1]);
	std::string error;
	return call && OkOrThrow(context, call->SetArg(params[2], params[3], &error), error);
}

static cell_t NativeCall_ArgRef(IPluginContext* context, const cell_t* params) {
	NativeCall* call = CallOrError(context, params[1]);
	cell_t value = 0;
	std::string error;
	return call && OkOrThrow(context, call->ArgRef(params[2], &value, &error), error) ? value : 0;
}

static cell_t NativeCall_SetArgRef(IPluginContext* context, const cell_t* params) {
	NativeCall* call = CallOrError(context, params[1]);
	std::string error;
	return call && OkOrThrow(context, call->SetArgRef(params[2], params[3], &error), error);
}

static cell_t NativeCall_GetArgString(IPluginContext* context, const cell_t* params) {
	NativeCall* call = CallOrError(context, params[1]);
	std::string text;
	std::string error;
	if (!call || !OkOrThrow(context, call->ArgString(params[2], &text, &error), error)) {
		return 0;
	}
	return WriteStringArg(context, params[3], params[4], text);
}

static cell_t NativeCall_SetArgString(IPluginContext* context, const cell_t* params) {
	NativeCall* call = CallOrError(context, params[1]);
	std::optional<std::string> text = call ? StringArg(context, params[3]) : std::nullopt;
	std::string error;
	return text && OkOrThrow(context, call->SetArgString(params[2], *text, params[4], &error), error);
}

// The test plugin's own array of a GetArgArray or SetArgArray: count cells
// at address (none for 0 cells).
static bool OwnArrayOrError(IPluginContext* context, cell_t address, cell_t count, cell_t** cells) {
	if (count < 0) {
		context->ThrowNativeError("cannot copy %d cells", count);
		return false;
	}
	*cells = count == 0 ? nullptr : PluginCells(context, address, count);
	if (count > 0 && !*cells) {
		context->ThrowNativeError("%d cells do not fit the given array", count);
		return false;
	}
	return true;
}

static cell_t NativeCall_GetArgArray(IPluginContext* context, const cell_t* params) {
	NativeCall* call = CallOrError(context, params[1]);
	cell_t* ours;
	std::string error;
	return call && OwnArrayOrError(context, params[3], params[4], &ours) &&
	       OkOrThrow(context, call->GetArgArray(params[2], ours, params[4], &error), error);
}

static cell_t NativeCall_SetArgArray(IPluginContext* context, const cell_t* params) {
	NativeCall* call = CallOrError(context, params[1]);
	cell_t* ours;
	std::string error;
	return call && OwnArrayOrError(context, params[3], params[4], &ours) &&
	       OkOrThrow(context, call->SetArgArray(params[2], ours, params[4], &error), error);
}

static cell_t NativeCall_CallOriginal(IPluginContext* context, const cell_t* params) {
	NativeCall* call = CallOrError(context, params[1]);
	return call ? call->CallOriginal() : 0;
}

REGISTER_NATIVES(callNatives) {
	{"Runner.MockNative", Runner_MockNative},
	{"Runner.HookNative", Runner_HookNative},
	{"Runner.UnhookNative", Runner_UnhookNative},
	{"Runner.NativeCalls", Runner_NativeCalls},
	{"RunnerCall.ArgCount.get", NativeCall_ArgCount},
	{"RunnerCall.Caller.get", NativeCall_Caller},
	{"RunnerCall.Result.get", NativeCall_GetResult},
	{"RunnerCall.Result.set", NativeCall_SetResult},
	{"RunnerCall.GetName", NativeCall_GetName},
	{"RunnerCall.Arg", NativeCall_Arg},
	{"RunnerCall.SetArg", NativeCall_SetArg},
	{"RunnerCall.ArgRef", NativeCall_ArgRef},
	{"RunnerCall.SetArgRef", NativeCall_SetArgRef},
	{"RunnerCall.GetArgString", NativeCall_GetArgString},
	{"RunnerCall.SetArgString", NativeCall_SetArgString},
	{"RunnerCall.GetArgArray", NativeCall_GetArgArray},
	{"RunnerCall.SetArgArray", NativeCall_SetArgArray},
	{"RunnerCall.CallOriginal", NativeCall_CallOriginal},
	{nullptr, nullptr},
};

}  // namespace smrunner
