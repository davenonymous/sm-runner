// Copyright 2026 davenonymous
//
// What the natives of smrunner.inc share: reading their own arguments,
// failing the test, finding clients and functions. The natives themselves
// are in natives_*.cpp, one file per part of the test API.
#ifndef SMRUNNER_TESTING_NATIVES_COMMON_H
#define SMRUNNER_TESTING_NATIVES_COMMON_H

#include <sp_vm_api.h>

#include <optional>
#include <string>

namespace smrunner {

class SimulatedClient;

// The text of the format argument at index and the arguments after it;
// empty when the native got no such argument.
std::string FormattedArg(SourcePawn::IPluginContext* context, const cell_t* params, unsigned int index);
// A string argument of the native; none after the native error when it is
// not in the plugin's memory (testing/plugin_memory.h).
std::optional<std::string> StringArg(SourcePawn::IPluginContext* context, cell_t address);
// Writes text into the plugin's buffer of maxlength bytes; returns the
// bytes written, 0 after the native error when the buffer is not in the
// plugin's memory.
cell_t WriteStringArg(SourcePawn::IPluginContext* context, cell_t buffer, cell_t maxlength, const std::string& text);

// Fails the test with "what: message" (or what alone) and stops the test
// plugin's function.
cell_t FailTest(SourcePawn::IPluginContext* context, const std::string& what, const std::string& message);
// Throws error as a native error unless ok.
bool OkOrThrow(SourcePawn::IPluginContext* context, bool ok, const std::string& error);

std::string Quote(const std::string& text);

// The engine's slot of a connected client, or a native error.
SimulatedClient* ConnectedClientOrError(SourcePawn::IPluginContext* context, cell_t client);
// The engine's slot of a client index, connected or not, or a native error.
SimulatedClient* ClientSlotOrError(SourcePawn::IPluginContext* context, cell_t client);
// Whether the client received a message of kind (RunnerMessage_Any: any)
// that contains text, as it was sent or without its chat color codes.
bool HasMessage(const SimulatedClient& client, cell_t kind, const std::string& text);

// A function argument; null for INVALID_FUNCTION, a native error when the
// id is no function.
SourcePawn::IPluginFunction* OptionalFunction(SourcePawn::IPluginContext* context, cell_t id);

// A frame cannot run inside a frame: its timers, frame actions and command
// buffer are being worked through. False with a native error inside one.
bool FramesAllowedOrError(SourcePawn::IPluginContext* context);

}  // namespace smrunner

#endif
