// Copyright 2026 davenonymous
//
// What a simulated client shows of a radio menu or panel, and its lines as
// SourceMod's radio style writes them (core/menu_radio.cpp, as upstream's
// MenuStyle_Radio.cpp): the title's lines first, then "->N. label" for an
// item whose key the menu takes, "N. label" for a disabled one, " " for a
// spacer and anything else as text. Everything that reads a display's text
// reads it through MenuLines.
#ifndef SMRUNNER_ENGINE_MENU_DISPLAY_H
#define SMRUNNER_ENGINE_MENU_DISPLAY_H

#include <string>
#include <vector>

namespace smrunner {

// The radio menu a client shows: the text (the title's lines first), the
// keys it takes (bit 0 is key 1, bit 9 key 0), which of them are a menu's
// navigation (Previous, Next, Back, Exit) and how long it stays (0: until
// replaced or a key).
struct MenuDisplay {
	std::string text;
	std::string title;
	int keys = 0;
	int navigationKeys = 0;
	unsigned int holdTime = 0;
	float shownAt = 0.0f;
};

struct MenuLine {
	enum class Kind { Title, Text, Spacer, Item };
	Kind kind = Kind::Text;
	std::string text;          // the line as the client shows it
	std::string label;         // an item's text after "N. ", else the text
	int key = 0;               // an item's key, 1 to 10 (10 is the key 0)
	bool selectable = false;   // an item drawn with "->" whose key the menu takes
	bool navigation = false;   // a selectable item that is Previous, Next, Back or Exit
};

// The bit of keys and navigationKeys for a key of 1 to 10.
constexpr int MenuKeyBit(int key) {
	return 1 << (key - 1);
}

std::vector<MenuLine> MenuLines(const MenuDisplay& display);

}  // namespace smrunner

#endif
