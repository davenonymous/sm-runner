// Copyright 2026 davenonymous
//
// Spans of a plugin's memory, checked before they are read or written: a
// plugin's addresses are only valid in its data and heap and in its stack,
// not in the free space between heap and stack, so a span is checked cell by
// cell, not by its ends alone, and a string up to its terminator. Every
// native of the runner and of the SteamWorks stand-in (an extension of its
// own, which compiles this file in as well) reads and writes plugin memory
// through these, so both check it with one set of rules.
#ifndef SMRUNNER_TESTING_PLUGIN_MEMORY_H
#define SMRUNNER_TESTING_PLUGIN_MEMORY_H

#include <sp_vm_api.h>

#include <optional>
#include <string>

namespace smrunner {

// The bytes [address, address + bytes) of the plugin's memory, or nullptr
// when bytes is not positive or any of them lies outside it.
char* PluginBytes(SourcePawn::IPluginContext* context, cell_t address, cell_t bytes);

// The cells [address, address + 4 * count), or nullptr as above.
cell_t* PluginCells(SourcePawn::IPluginContext* context, cell_t address, cell_t count);

// The string at address, or nullptr when it does not start in the plugin's
// memory or runs out of it before its terminator.
char* PluginChars(SourcePawn::IPluginContext* context, cell_t address);

// The arguments of a native, each checked as above; when the check fails
// the native error naming the argument is already thrown and the caller
// returns at once.

// A string argument, in the plugin's memory, or nullptr.
char* PluginCharsArg(SourcePawn::IPluginContext* context, cell_t address);
// A string argument that may be NULL_STRING (*text is then nullptr); false
// after the native error.
bool PluginCharsOrNullArg(SourcePawn::IPluginContext* context, cell_t address, char** text);
// A string argument, copied; none after the native error.
std::optional<std::string> PluginStringArg(SourcePawn::IPluginContext* context, cell_t address);
// A by-reference cell or an array of count cells.
cell_t* PluginCellsArg(SourcePawn::IPluginContext* context, cell_t address, cell_t count);
// A buffer of bytes bytes (at least 1).
char* PluginBytesArg(SourcePawn::IPluginContext* context, cell_t address, cell_t bytes);

// How a text too long for its buffer is cut: StringToLocalUTF8 at a
// character boundary, StringToLocal at the byte.
enum class TextCut { AtCharacter, AtByte };

// Writes text into the plugin's buffer of maxlength bytes, which must lie in
// its memory as a whole, as the VM's StringToLocalUTF8 or StringToLocal does:
// cut to fit, always terminated, nothing for a maxlength of 0. The bytes
// written without the terminator, or none after the native error (also for
// a negative maxlength).
std::optional<size_t> WritePluginString(SourcePawn::IPluginContext* context, cell_t buffer, cell_t maxlength,
                                        const char* text, TextCut cut = TextCut::AtCharacter);
inline std::optional<size_t> WritePluginString(SourcePawn::IPluginContext* context, cell_t buffer, cell_t maxlength,
                                               const std::string& text, TextCut cut = TextCut::AtCharacter) {
	return WritePluginString(context, buffer, maxlength, text.c_str(), cut);
}

}  // namespace smrunner

#endif
