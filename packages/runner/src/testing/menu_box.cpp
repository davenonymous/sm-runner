// Copyright 2026 davenonymous
#include "testing/menu_box.h"

#include <algorithm>
#include <vector>

#include "terminal.h"

namespace smrunner {

namespace {

// One row of the box: its text, an optional part aligned to the right edge,
// and the color of both.
struct Row {
	std::string left;
	std::string right;
	const char* color;
};

// The columns a text takes: one per UTF-8 character.
size_t Width(const std::string& text) {
	size_t width = 0;
	for (unsigned char c : text) {
		if ((c & 0xC0) != 0x80) {
			width++;
		}
	}
	return width;
}

std::string Repeat(const char* piece, size_t count) {
	std::string text;
	for (size_t i = 0; i < count; i++) {
		text += piece;
	}
	return text;
}

// A line as a row: title lines bold, items without the "->" of a selectable
// one, keys the menu takes green (navigation cyan), the others dim and
// disabled.
Row RowOf(const MenuLine& line) {
	switch (line.kind) {
		case MenuLine::Kind::Spacer:
			return {"", "", nullptr};
		case MenuLine::Kind::Title:
			return {line.text, "", ansi::kBold};
		case MenuLine::Kind::Text:
			return {line.text, "", nullptr};
		case MenuLine::Kind::Item:
			break;
	}
	std::string item = std::to_string(line.key % 10) + ". " + line.label;
	if (!line.selectable) {
		return {item, "(disabled)", ansi::kDim};
	}
	return {item, "", line.navigation ? ansi::kCyan : ansi::kGreen};
}

size_t RowWidth(const Row& row) {
	return Width(row.left) + (row.right.empty() ? 0 : 1 + Width(row.right));
}

std::string Colored(const char* color, const std::string& text) {
	return color ? std::string(color) + text + ansi::kReset : text;
}

}  // namespace

std::string DrawMenuBox(const MenuDisplay& menu) {
	std::string title;
	std::vector<Row> rows;
	for (const MenuLine& line : MenuLines(menu)) {
		if (line.kind == MenuLine::Kind::Title && title.empty() && rows.empty()) {
			title = line.text;
			continue;
		}
		rows.push_back(RowOf(line));
	}
	std::string footer = menu.holdTime == 0 ? "forever" : std::to_string(menu.holdTime) + " s";

	// The columns between the padding spaces; the borders need room for the
	// title and the footer with a dash on each side.
	size_t inner = 10;
	for (const Row& row : rows) {
		inner = std::max(inner, RowWidth(row));
	}
	inner = std::max(inner, Width(title) + 2);
	inner = std::max(inner, Width(footer) + 2);

	std::string box = "╭─";
	if (title.empty()) {
		box += Repeat("─", inner + 1);
	} else {
		box += " " + Colored(ansi::kBold, title) + " " + Repeat("─", inner - Width(title) - 1);
	}
	box += "╮\n";
	for (const Row& row : rows) {
		std::string content = row.left;
		if (!row.right.empty()) {
			content += Repeat(" ", inner - RowWidth(row) + 1) + row.right;
		}
		box += "│ " + Colored(row.color, content) + Repeat(" ", inner - Width(content)) + " │\n";
	}
	box += "╰" + Repeat("─", inner - Width(footer) - 1) + " " + footer + " ─╯\n";
	return box;
}

}  // namespace smrunner
