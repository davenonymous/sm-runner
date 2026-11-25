// Copyright 2026 davenonymous
//
// The runner's own messages on standard error, apart from the server console:
// notes (only with --verbose), warnings and fatal errors, which end the
// process with exit code 70 (EX_SOFTWARE) because the runner cannot go on.
#ifndef SMRUNNER_DIAGNOSTICS_H
#define SMRUNNER_DIAGNOSTICS_H

namespace smrunner {

enum class Verbosity { Quiet, Normal, Verbose };

void SetVerbosity(Verbosity verbosity);
bool IsVerbose();

void Note(const char* format, ...) __attribute__((format(printf, 1, 2)));
void Warn(const char* format, ...) __attribute__((format(printf, 1, 2)));
[[noreturn]] void Fatal(const char* format, ...) __attribute__((format(printf, 1, 2)));

// Runs before a fatal error ends the process (the temporary sandbox goes).
void SetFatalCleanup(void (*cleanup)());

constexpr int kExitFatal = 70;

}  // namespace smrunner

#endif
