// Copyright 2026 davenonymous
//
// The test mode: finds the Test_ functions of the test plugins, runs each
// on a server of its own in a child process, and reports.
#ifndef SMRUNNER_TESTING_TEST_RUNNER_H
#define SMRUNNER_TESTING_TEST_RUNNER_H

#include "options.h"
#include "server.h"

namespace smrunner {

int ListTests(const Options& options);
int RunTests(const Options& options, const Install& install);

}  // namespace smrunner

#endif
