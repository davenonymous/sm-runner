// Copyright 2026 davenonymous
//
// The tests' side of one simulated server: the test that runs (none in run
// mode), its test plugins, the failures the assertions recorded, the plugin
// errors that happened and the ones the test expects, the native hooks and
// the debugger. A session is attached once SourceMod booted, before any
// plugin loads, and detached once SourceMod shut down; Attach alone knows the
// order its parts start in. The natives of smrunner.inc reach it through
// Current().
//
// It sits in front of SourceMod's error reporter as the VM's debug listener:
// plugin errors are recorded and passed on to SourceMod, which logs them;
// the errors the assertions raise to stop a function are recorded as
// failures and not passed on.
//
// In run mode there is no test: assertions fail the run, notes go to
// standard output, and the natives that need a test (hooks, mocks,
// breakpoints, expected errors, skipping, the test's name) throw a native
// error that says so.
#ifndef SMRUNNER_TESTING_TEST_SESSION_H
#define SMRUNNER_TESTING_TEST_SESSION_H

#include <IPluginSys.h>
#include <sp_vm_api.h>

#include <string>
#include <vector>

#include "testing/debugger.h"
#include "testing/native_hooks.h"
#include "testing/test.h"
#include "testing/test_plugins.h"

namespace smrunner {

class TestSession : public SourcePawn::IDebugListener, public SourceMod::IPluginsListener {
 public:
	// One test: its test plugin is the session's one test plugin.
	static TestSession ForTest(const Test& test);
	// Run mode: no test and no test plugins.
	static TestSession ForRun();

	~TestSession();
	TestSession(const TestSession&) = delete;
	TestSession& operator=(const TestSession&) = delete;

	// After SourceMod booted, before plugins load: the debug listener, the
	// native hooks, the debugger and OnTestBoot, in this order.
	void Attach();
	// After SourceMod shut down.
	void Detach();

	// The attached session; the runner cannot go on without one, since the
	// natives that ask for it only exist while one is attached.
	static TestSession& Current();

	bool HasTest() const { return hasTest_; }
	// Whether a test runs; otherwise throws a native error naming native.
	bool RequireTest(SourcePawn::IPluginContext* context, const char* native) const;
	const std::string& TestName() const { return testName_; }
	const TestPlugins& testPlugins() const { return testPlugins_; }
	NativeHooks& hooks() { return hooks_; }
	Debugger& debugger() { return debugger_; }

	// Records a failure at the line that called the assertion (the test
	// plugin's, in run mode the calling plugin's) and stops the running
	// function with a VM error.
	void Fail(SourcePawn::IPluginContext* context, const std::string& message);
	void Skip(SourcePawn::IPluginContext* context, const std::string& reason);
	void ExpectError(const std::string& text) { expected_.push_back({text, false}); }
	// Records a failure that is not raised in a plugin (the runner's own).
	void AddFailure(const std::string& message) { failures_.push_back(message); }
	// Text for people to read (Runner.PrintClientMenu): under the test's line
	// in the report, in run mode on standard output.
	void AddNote(const std::string& note);

	const std::vector<std::string>& Notes() const { return notes_; }
	const std::vector<std::string>& Failures() const { return failures_; }
	const std::vector<std::string>& UnexpectedErrors() const { return unexpected_; }
	std::vector<std::string> UnmetExpectations() const;
	bool Skipped() const { return skipped_; }
	const std::string& SkipReason() const { return skipReason_; }

	// IDebugListener
	void ReportError(const SourcePawn::IErrorReport& report, SourcePawn::IFrameIterator& iter) override;
	void OnDebugSpew(const char* message, ...) override;

	// IPluginsListener: calls OnTestBoot of the test plugin right after it
	// was loaded.
	void OnPluginLoaded(SourceMod::IPlugin* plugin) override;

 private:
	struct Expectation {
		std::string text;
		bool met;
	};

	TestSession(bool hasTest, std::string testName, const std::vector<std::string>& testPluginFiles);
	// "file.sp:12" of the innermost frame that called an assertion, or "".
	std::string FailureLocation(SourcePawn::IFrameIterator& frames) const;

	bool hasTest_;
	std::string testName_;
	TestPlugins testPlugins_;
	NativeHooks hooks_;
	Debugger debugger_;
	bool attached_ = false;
	SourcePawn::IDebugListener* sourcemod_ = nullptr;
	std::vector<std::string> failures_;
	std::vector<std::string> notes_;
	std::vector<std::string> unexpected_;
	std::vector<Expectation> expected_;
	bool raising_ = false;   // an assertion raised the error in raised_
	std::string raised_;
	bool skipped_ = false;
	std::string skipReason_;
};

}  // namespace smrunner

#endif
