// Copyright 2026 davenonymous
#include "testing/natives_common.h"

#include "core/core.h"
#include "core/sourcemod_api.h"
#include "engine/clients.h"
#include "testing/plugin_memory.h"
#include "testing/test_session.h"

using namespace SourcePawn;

namespace smrunner {

std::string FormattedArg(IPluginContext* context, const cell_t* params, unsigned int index) {
	if (static_cast<cell_t>(index) > params[0]) {
		return std::string();
	}
	char buffer[2048];
	DetectExceptions exceptions(context);
	Core::Get().sourcemod().FormatString(buffer, sizeof(buffer), context, params, index);
	return exceptions.HasException() ? std::string() : std::string(buffer);
}

std::optional<std::string> StringArg(IPluginContext* context, cell_t address) {
	return PluginStringArg(context, address);
}

cell_t WriteStringArg(IPluginContext* context, cell_t buffer, cell_t maxlength, const std::string& text) {
	std::optional<size_t> written = WritePluginString(context, buffer, maxlength, text);
	return written ? static_cast<cell_t>(*written) : 0;
}

cell_t FailTest(IPluginContext* context, const std::string& what, const std::string& message) {
	TestSession::Current().Fail(context, message.empty() ? what : what + ": " + message);
	return 0;
}

bool OkOrThrow(IPluginContext* context, bool ok, const std::string& error) {
	if (!ok) {
		context->ThrowNativeError("%s", error.c_str());
	}
	return ok;
}

std::string Quote(const std::string& text) {
	return "\"" + text + "\"";
}

SimulatedClient* ConnectedClientOrError(IPluginContext* context, cell_t client) {
	SimulatedClient* slot = Core::Get().engine().clients().Slot(client);
	if (!slot || !slot->IsConnected()) {
		context->ThrowNativeError("client %d is not connected", client);
		return nullptr;
	}
	return slot;
}

SimulatedClient* ClientSlotOrError(IPluginContext* context, cell_t client) {
	SimulatedClient* slot = Core::Get().engine().clients().Slot(client);
	if (!slot) {
		context->ThrowNativeError("client index %d is invalid", client);
	}
	return slot;
}

bool HasMessage(const SimulatedClient& client, cell_t kind, const std::string& text) {
	for (const ClientMessage& message : client.Received()) {
		if (kind >= 0 && message.kind != kind) {
			continue;
		}
		if (message.text.find(text) != std::string::npos ||
		    StripChatColors(message.text).find(text) != std::string::npos) {
			return true;
		}
	}
	return false;
}

IPluginFunction* OptionalFunction(IPluginContext* context, cell_t id) {
	if (id == 0 || id == -1) {   // INVALID_FUNCTION (0 since SourceMod 1.11, -1 before)
		return nullptr;
	}
	IPluginFunction* function = context->GetFunctionById(id);
	if (!function) {
		context->ThrowNativeError("invalid function %x", id);
	}
	return function;
}

bool FramesAllowedOrError(IPluginContext* context) {
	if (Core::Get().IsInFrame()) {
		context->ThrowNativeError(
			"cannot run frames inside a frame (in a timer, RequestFrame, OnGameFrame, a frame hook, a command from "
			"the buffer, or a breakpoint or hook reached from one)");
		return false;
	}
	return true;
}

}  // namespace smrunner
