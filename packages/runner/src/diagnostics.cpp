// Copyright 2026 davenonymous
#include "diagnostics.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

namespace smrunner {

static Verbosity sVerbosity = Verbosity::Normal;
static void (*sFatalCleanup)() = nullptr;

void SetFatalCleanup(void (*cleanup)()) {
	sFatalCleanup = cleanup;
}

void SetVerbosity(Verbosity verbosity) {
	sVerbosity = verbosity;
}

bool IsVerbose() {
	return sVerbosity == Verbosity::Verbose;
}

static void Emit(const char* prefix, const char* format, va_list ap) {
	fprintf(stderr, "[smrunner] %s", prefix);
	vfprintf(stderr, format, ap);
	fputc('\n', stderr);
	fflush(stderr);
}

void Note(const char* format, ...) {
	if (sVerbosity != Verbosity::Verbose) {
		return;
	}
	va_list ap;
	va_start(ap, format);
	Emit("", format, ap);
	va_end(ap);
}

void Warn(const char* format, ...) {
	if (sVerbosity == Verbosity::Quiet) {
		return;
	}
	va_list ap;
	va_start(ap, format);
	Emit("warning: ", format, ap);
	va_end(ap);
}

void Fatal(const char* format, ...) {
	va_list ap;
	va_start(ap, format);
	Emit("fatal: ", format, ap);
	va_end(ap);
	fflush(stdout);
	if (sFatalCleanup) {
		void (*cleanup)() = sFatalCleanup;
		sFatalCleanup = nullptr;
		cleanup();
	}
	_exit(kExitFatal);
}

}  // namespace smrunner
