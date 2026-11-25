// Copyright 2026 davenonymous
#include "testing/test_session.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "core/globals.h"
#include "diagnostics.h"
#include "sandbox.h"
#include "terminal.h"

using namespace SourcePawn;
using namespace SourceMod;

namespace smrunner {

static TestSession* sAttached = nullptr;

TestPlugins::TestPlugins(const std::vector<std::string>& files) {
	for (const std::string& file : files) {
		names_.push_back(BaseName(file));
	}
}

bool TestPlugins::Contains(IPlugin* plugin) const {
	return plugin && std::find(names_.begin(), names_.end(), BaseName(plugin->GetFilename())) != names_.end();
}

bool TestPlugins::Contains(IPluginContext* context) const {
	return context && scripts && Contains(scripts->FindPluginByContext(context->GetContext()));
}

TestSession::TestSession(bool hasTest, std::string testName, const std::vector<std::string>& testPluginFiles)
	: hasTest_(hasTest),
	  testName_(std::move(testName)),
	  testPlugins_(testPluginFiles),
	  hooks_(testPlugins_),
	  debugger_(testPlugins_) {}

TestSession TestSession::ForTest(const Test& test) {
	return TestSession(true, test.name, {test.plugin});
}

TestSession TestSession::ForRun() {
	return TestSession(false, std::string(), {});
}

TestSession::~TestSession() {
	Detach();
}

void TestSession::Attach() {
	if (sAttached) {
		Fatal("a test session is attached already");
	}
	sAttached = this;
	attached_ = true;
	sourcemod_ = logicore.debugger;
	g_pSourcePawn2->SetDebugListener(this);
	hooks_.Start();
	debugger_.Start();
	scripts->AddPluginsListener(this);
}

void TestSession::Detach() {
	if (!attached_) {
		return;
	}
	attached_ = false;
	sAttached = nullptr;
	scripts->RemovePluginsListener(this);
	debugger_.Stop();
	hooks_.Stop();
	g_pSourcePawn2->SetDebugListener(sourcemod_);
}

TestSession& TestSession::Current() {
	if (!sAttached) {
		Fatal("a test native was called without a test session");
	}
	return *sAttached;
}

bool TestSession::RequireTest(IPluginContext* context, const char* native) const {
	if (hasTest_) {
		return true;
	}
	context->ThrowNativeError("%s needs a test: it works in smrunner test, not in smrunner run", native);
	return false;
}

void TestSession::AddNote(const std::string& note) {
	if (hasTest_) {
		notes_.push_back(note);
		return;
	}
	fputs(ForTerminal(note).c_str(), stdout);
	fflush(stdout);
}

void TestSession::OnPluginLoaded(IPlugin* plugin) {
	if (!testPlugins_.Contains(plugin)) {
		return;
	}
	IPluginFunction* boot = plugin->GetBaseContext()->GetFunctionByName("OnTestBoot");
	if (!boot) {
		return;
	}
	boot->PushString(testName_.c_str());
	boot->Execute(nullptr);
}

std::string TestSession::FailureLocation(IFrameIterator& frames) const {
	for (frames.Reset(); !frames.Done(); frames.Next()) {
		if (!frames.IsScriptedFrame() || (hasTest_ && !testPlugins_.Contains(frames.Context()))) {
			continue;
		}
		const char* path = frames.FilePath();
		std::string location = path ? BaseName(path) : std::string("?");
		return location + ":" + std::to_string(frames.LineNumber());
	}
	return std::string();
}

static std::string DescribeStack(IFrameIterator& frames) {
	std::string stack;
	int index = 0;
	for (frames.Reset(); !frames.Done(); frames.Next()) {
		if (frames.IsInternalFrame()) {
			continue;
		}
		const char* name = frames.FunctionName();
		stack += "  [" + std::to_string(index++) + "] ";
		if (frames.IsScriptedFrame()) {
			const char* path = frames.FilePath();
			stack += std::string(path ? BaseName(path) : "?") + "::" + (name ? name : "?") + ", line " +
			         std::to_string(frames.LineNumber()) + "\n";
		} else {
			stack += std::string(name ? name : "?") + "()\n";
		}
	}
	return stack;
}

void TestSession::ReportError(const IErrorReport& report, IFrameIterator& iter) {
	// The VM keeps the first 1023 bytes of an error message.
	if (raising_ && raised_.substr(0, 1023) == report.Message()) {
		// An assertion recorded its failure before raising this error.
		raising_ = false;
		return;
	}
	std::string message = report.Message();
	IPlugin* plugin = report.Context() ? scripts->FindPluginByContext(report.Context()->GetContext()) : nullptr;
	std::string text = std::string(plugin ? plugin->GetFilename() : "?") + ": " + message;
	bool expected = false;
	for (Expectation& expectation : expected_) {
		if (!expectation.met && text.find(expectation.text) != std::string::npos) {
			expectation.met = true;
			expected = true;
			break;
		}
	}
	if (!expected) {
		unexpected_.push_back(text + "\n" + DescribeStack(iter));
	}
	iter.Reset();
	sourcemod_->ReportError(report, iter);
}

void TestSession::OnDebugSpew(const char* message, ...) {
	char buffer[2048];
	va_list ap;
	va_start(ap, message);
	vsnprintf(buffer, sizeof(buffer), message, ap);
	va_end(ap);
	sourcemod_->OnDebugSpew("%s", buffer);
}

void TestSession::Fail(IPluginContext* context, const std::string& message) {
	IFrameIterator* frames = context->CreateFrameIterator();
	std::string location = FailureLocation(*frames);
	context->DestroyFrameIterator(frames);
	failures_.push_back(location.empty() ? message : message + " (" + location + ")");
	raising_ = true;
	raised_ = message;
	context->ReportError("%s", message.c_str());
}

void TestSession::Skip(IPluginContext* context, const std::string& reason) {
	skipped_ = true;
	skipReason_ = reason;
	raising_ = true;
	raised_ = "test skipped: " + reason;
	context->ReportError("%s", raised_.c_str());
}

std::vector<std::string> TestSession::UnmetExpectations() const {
	std::vector<std::string> unmet;
	for (const Expectation& expectation : expected_) {
		if (!expectation.met) {
			unmet.push_back(expectation.text);
		}
	}
	return unmet;
}

}  // namespace smrunner
