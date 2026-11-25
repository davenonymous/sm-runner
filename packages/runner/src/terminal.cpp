// Copyright 2026 davenonymous
#include "terminal.h"

#include <stdlib.h>
#include <unistd.h>

namespace smrunner {

static bool sColor = false;

void SetColorMode(ColorMode mode) {
	if (mode == ColorMode::Auto) {
		const char* noColor = getenv("NO_COLOR");
		sColor = isatty(STDOUT_FILENO) && !(noColor && *noColor);
		return;
	}
	sColor = mode == ColorMode::Always;
}

bool ColorEnabled() {
	return sColor;
}

std::string StripAnsi(const std::string& text) {
	std::string stripped;
	stripped.reserve(text.size());
	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] != '\x1b' || i + 1 >= text.size() || text[i + 1] != '[') {
			stripped += text[i];
			continue;
		}
		// Parameters and intermediates up to the final byte (@ to ~).
		i += 2;
		while (i < text.size() && (text[i] < '@' || text[i] > '~')) {
			i++;
		}
	}
	return stripped;
}

std::string ForTerminal(const std::string& text) {
	return sColor ? text : StripAnsi(text);
}

}  // namespace smrunner
