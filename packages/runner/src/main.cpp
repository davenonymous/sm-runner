// Copyright 2026 davenonymous
//
// smrunner: runs SourceMod plugins and their tests on a simulated server.
#include <stdio.h>

#include <memory>

#include "options.h"
#include "server.h"
#include "terminal.h"
#include "testing/test_runner.h"
#include "testing/test_session.h"

using namespace smrunner;

// run: the plugins on one server, the commands, the frames; a plugin that
// is not running, a failed assertion or any error in SourceMod's error log
// fails the run.
static int RunMode(const Options& options, const Install& install) {
	std::unique_ptr<TemporaryFolder> temporary;
	SandboxRoot root{options.sandbox, SandboxOwnership::Given};
	if (options.sandbox.empty()) {
		temporary = std::make_unique<TemporaryFolder>(options.keepSandbox);
		root = temporary->Root();
	}
	TestSession session = TestSession::ForRun();
	Server server(ForRun(options, root), install);
	server.Start(session);
	for (const std::string& command : options.commands) {
		server.engine().console().AppendToBuffer(command + "\n");
	}
	server.RunFrames(options.frames);
	std::vector<std::string> problems = server.PluginProblems();
	std::string errors = server.ErrorLog();
	server.Stop();

	for (const std::string& problem : problems) {
		fprintf(stderr, "smrunner: plugin %s\n", problem.c_str());
	}
	for (const std::string& failure : session.Failures()) {
		fprintf(stderr, "smrunner: %s\n", ForTerminal(failure).c_str());
	}
	if (!errors.empty()) {
		fprintf(stderr, "smrunner: SourceMod reported errors:\n%s", errors.c_str());
	}
	return problems.empty() && session.Failures().empty() && errors.empty() ? 0 : 1;
}

int main(int argc, char** argv) {
	Options options = ParseOptions(argc, argv);
	SetVerbosity(options.verbosity);
	SetColorMode(options.color);
	Install install = Install::Locate(options.home);
	switch (options.mode) {
		case Mode::Run:
			return RunMode(options, install);
		case Mode::List:
			return ListTests(options);
		case Mode::Test:
			return RunTests(options, install);
	}
	return 2;
}
