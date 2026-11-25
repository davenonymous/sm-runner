// Copyright 2026 davenonymous
#include "engine/menu_display.h"

namespace smrunner {

static std::vector<std::string> SplitLines(const std::string& text) {
	std::vector<std::string> lines;
	size_t start = 0;
	while (start < text.size()) {
		size_t end = text.find('\n', start);
		if (end == std::string::npos) {
			lines.push_back(text.substr(start));
			break;
		}
		lines.push_back(text.substr(start, end - start));
		start = end + 1;
	}
	return lines;
}

static bool IsDigit(char c) {
	return c >= '0' && c <= '9';
}

// A line of the body: an item when it is the radio style's "N. " (g_RadioNumTable)
// after an optional "->".
static MenuLine BodyLine(const std::string& line, const MenuDisplay& display) {
	MenuLine parsed;
	parsed.text = line;
	parsed.label = line;
	if (line == " ") {
		parsed.kind = MenuLine::Kind::Spacer;
		parsed.label.clear();
		return parsed;
	}
	bool arrow = line.compare(0, 2, "->") == 0;
	size_t number = arrow ? 2 : 0;
	bool numbered = line.size() >= number + 3 && IsDigit(line[number]) && line[number + 1] == '.' &&
	                line[number + 2] == ' ';
	if (!numbered) {
		return parsed;
	}
	int digit = line[number] - '0';
	parsed.kind = MenuLine::Kind::Item;
	parsed.key = digit == 0 ? 10 : digit;
	parsed.label = line.substr(number + 3);
	parsed.selectable = arrow && (display.keys & MenuKeyBit(parsed.key)) != 0;
	parsed.navigation = parsed.selectable && (display.navigationKeys & MenuKeyBit(parsed.key)) != 0;
	return parsed;
}

std::vector<MenuLine> MenuLines(const MenuDisplay& display) {
	std::vector<MenuLine> lines;
	std::string body = display.text;
	bool hasTitle = !display.title.empty() && body.compare(0, display.title.size() + 1, display.title + "\n") == 0;
	if (hasTitle) {
		for (const std::string& line : SplitLines(display.title)) {
			MenuLine title;
			title.kind = MenuLine::Kind::Title;
			title.text = line;
			title.label = line;
			lines.push_back(title);
		}
		body = body.substr(display.title.size() + 1);
	}
	for (const std::string& line : SplitLines(body)) {
		lines.push_back(BodyLine(line, display));
	}
	return lines;
}

}  // namespace smrunner
