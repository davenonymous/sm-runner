// Copyright 2026 davenonymous
//
// Color on the runner's standard output (--color): text the runner draws
// carries ANSI escape codes, which are written as they are when color is on
// and stripped when it is off, and never go into a JUnit report.
#ifndef SMRUNNER_TERMINAL_H
#define SMRUNNER_TERMINAL_H

#include <string>

namespace smrunner {

// auto: color when standard output is a terminal and NO_COLOR is not set.
enum class ColorMode { Auto, Always, Never };

void SetColorMode(ColorMode mode);
bool ColorEnabled();

// The text without its ANSI escape sequences (ESC [ ... final byte).
std::string StripAnsi(const std::string& text);
// The text as standard output shows it: with or without its colors.
std::string ForTerminal(const std::string& text);

// The ANSI codes the runner draws with.
namespace ansi {
constexpr const char* kReset = "\x1b[0m";
constexpr const char* kBold = "\x1b[1m";
constexpr const char* kDim = "\x1b[2m";
constexpr const char* kGreen = "\x1b[32m";
constexpr const char* kCyan = "\x1b[36m";
}  // namespace ansi

}  // namespace smrunner

#endif
