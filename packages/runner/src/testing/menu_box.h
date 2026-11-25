// Copyright 2026 davenonymous
//
// A menu display drawn for people: a box with rounded corners, the title in
// its top border, the hold time in its bottom border ("forever" or "20 s"),
// one row per line of the menu. Keys the menu takes are green (its
// navigation, Previous, Next, Back and Exit, cyan), disabled lines dim and
// marked "(disabled)", extra title lines bold. The box always carries the
// ANSI codes; ForTerminal and StripAnsi decide whether they are shown.
#ifndef SMRUNNER_TESTING_MENU_BOX_H
#define SMRUNNER_TESTING_MENU_BOX_H

#include <string>

#include "engine/menu_display.h"

namespace smrunner {

// The box, one line per row, each ending with a line break.
std::string DrawMenuBox(const MenuDisplay& menu);

}  // namespace smrunner

#endif
