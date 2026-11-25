// Copyright 2026 davenonymous
#include "testing/plugin_memory.h"

#include <string.h>

#include <limits>

using namespace SourcePawn;

namespace smrunner {

char* PluginBytes(IPluginContext* context, cell_t address, cell_t bytes) {
	// The probes below go up to 3 bytes past the span's end.
	if (bytes <= 0 || address < 0 || address > std::numeric_limits<cell_t>::max() - 4 - bytes) {
		return nullptr;
	}
	cell_t* first;
	if (context->LocalToPhysAddr(address, &first) != SP_ERROR_NONE) {
		return nullptr;
	}
	cell_t last = address + bytes - 1;
	for (cell_t probe = address + 4; probe < last; probe += 4) {
		if (context->LocalToPhysAddr(probe, nullptr) != SP_ERROR_NONE) {
			return nullptr;
		}
	}
	if (context->LocalToPhysAddr(last, nullptr) != SP_ERROR_NONE) {
		return nullptr;
	}
	return reinterpret_cast<char*>(first);
}

cell_t* PluginCells(IPluginContext* context, cell_t address, cell_t count) {
	if (count <= 0 || count > std::numeric_limits<cell_t>::max() / 4) {
		return nullptr;
	}
	return reinterpret_cast<cell_t*>(PluginBytes(context, address, count * 4));
}

char* PluginChars(IPluginContext* context, cell_t address) {
	char* text = nullptr;
	if (context->LocalToString(address, &text) != SP_ERROR_NONE || !text) {
		return nullptr;
	}
	// Every cell the string reaches into must be the plugin's; the memory
	// ends at a cell boundary, so the probe stops at its end too.
	for (cell_t offset = 0;; offset++) {
		bool entersCell = offset > 0 && (address + offset) % 4 == 0;
		if (entersCell && context->LocalToPhysAddr(address + offset, nullptr) != SP_ERROR_NONE) {
			return nullptr;
		}
		if (text[offset] == '\0') {
			return text;
		}
	}
}

char* PluginCharsArg(IPluginContext* context, cell_t address) {
	char* text = PluginChars(context, address);
	if (!text) {
		context->ThrowNativeError("the string at %x is not in the plugin's memory", address);
	}
	return text;
}

bool PluginCharsOrNullArg(IPluginContext* context, cell_t address, char** text) {
	if (!PluginCharsArg(context, address)) {
		return false;
	}
	context->LocalToStringNULL(address, text);
	return true;
}

std::optional<std::string> PluginStringArg(IPluginContext* context, cell_t address) {
	char* text = PluginCharsArg(context, address);
	if (!text) {
		return std::nullopt;
	}
	return std::string(text);
}

cell_t* PluginCellsArg(IPluginContext* context, cell_t address, cell_t count) {
	cell_t* cells = PluginCells(context, address, count);
	if (cells) {
		return cells;
	}
	if (count == 1) {
		context->ThrowNativeError("the reference at %x is not in the plugin's memory", address);
	} else {
		context->ThrowNativeError("an array of %d cells at %x is not in the plugin's memory", count, address);
	}
	return nullptr;
}

char* PluginBytesArg(IPluginContext* context, cell_t address, cell_t bytes) {
	if (bytes < 1) {
		context->ThrowNativeError("a buffer needs at least 1 byte, not %d", bytes);
		return nullptr;
	}
	char* buffer = PluginBytes(context, address, bytes);
	if (!buffer) {
		context->ThrowNativeError("a buffer of %d bytes at %x is not in the plugin's memory", bytes, address);
	}
	return buffer;
}

std::optional<size_t> WritePluginString(IPluginContext* context, cell_t buffer, cell_t maxlength, const char* text,
                                        TextCut cut) {
	if (maxlength < 0) {
		context->ThrowNativeError("a buffer cannot have %d bytes", maxlength);
		return std::nullopt;
	}
	if (maxlength == 0) {
		return 0;
	}
	if (!PluginBytesArg(context, buffer, maxlength)) {
		return std::nullopt;
	}
	size_t written = 0;
	if (cut == TextCut::AtCharacter) {
		context->StringToLocalUTF8(buffer, static_cast<size_t>(maxlength), text, &written);
		return written;
	}
	context->StringToLocal(buffer, static_cast<size_t>(maxlength), text);
	return strnlen(text, static_cast<size_t>(maxlength) - 1);
}

}  // namespace smrunner
