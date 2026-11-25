// Copyright 2026 davenonymous
#include "testing/test_runner.h"

#include <errno.h>
#include <stdlib.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fstream>
#include <memory>
#include <sstream>

#include "core/core.h"
#include "diagnostics.h"
#include "monotonic.h"
#include "smx/smx_file.h"
#include "server_spec.h"
#include "terminal.h"
#include "testing/test.h"
#include "testing/test_session.h"

using namespace SourceMod;
using namespace SourcePawn;

namespace smrunner {

namespace {

enum class Status { Passed, Failed, Skipped, Crashed, TimedOut };

struct Result {
	Status status = Status::Crashed;
	std::vector<std::string> failures;
	std::vector<std::string> notes;   // what the test printed for the report
	std::string console;
	std::string errorLog;
	double seconds = 0.0;
};

const char* StatusName(Status status) {
	switch (status) {
		case Status::Passed: return "PASS";
		case Status::Failed: return "FAIL";
		case Status::Skipped: return "SKIP";
		case Status::Crashed: return "CRASH";
		case Status::TimedOut: return "TIMEOUT";
	}
	return "?";
}

std::vector<Test> FindTests(const Options& options) {
	std::vector<Test> tests;
	for (const std::string& plugin : options.testPlugins) {
		std::string error;
		std::unique_ptr<SmxFile> smx = SmxFile::Load(plugin, &error);
		if (!smx) {
			Fatal("%s", error.c_str());
		}
		size_t before = tests.size();
		for (const std::string& name : smx->Publics()) {
			if (name.compare(0, 5, "Test_") != 0) {
				continue;
			}
			if (!options.filter.empty() && name.find(options.filter) == std::string::npos) {
				continue;
			}
			tests.push_back({plugin, name});
		}
		if (tests.size() == before && options.filter.empty()) {
			Warn("%s has no tests (public functions named Test_...)", plugin.c_str());
		}
	}
	return tests;
}

// The result travels from the test process to the runner as lines of
// "key<TAB>value", with \\, \n and \t escaped.
std::string Escape(const std::string& text) {
	std::string escaped;
	for (char c : text) {
		if (c == '\\') {
			escaped += "\\\\";
		} else if (c == '\n') {
			escaped += "\\n";
		} else if (c == '\t') {
			escaped += "\\t";
		} else {
			escaped += c;
		}
	}
	return escaped;
}

std::string Unescape(const std::string& text) {
	std::string plain;
	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] != '\\' || i + 1 == text.size()) {
			plain += text[i];
			continue;
		}
		char next = text[++i];
		plain += next == 'n' ? '\n' : next == 't' ? '\t' : next;
	}
	return plain;
}

void WriteAll(int fd, const std::string& text) {
	size_t written = 0;
	while (written < text.size()) {
		ssize_t count = write(fd, text.data() + written, text.size() - written);
		if (count < 0 && errno == EINTR) {
			continue;
		}
		if (count <= 0) {
			return;
		}
		written += static_cast<size_t>(count);
	}
}

std::string Serialize(const Result& result) {
	std::string text = std::string("status\t") + StatusName(result.status) + "\n";
	for (const std::string& failure : result.failures) {
		text += "failure\t" + Escape(failure) + "\n";
	}
	for (const std::string& note : result.notes) {
		text += "note\t" + Escape(note) + "\n";
	}
	text += "console\t" + Escape(result.console) + "\n";
	text += "errorlog\t" + Escape(result.errorLog) + "\n";
	return text;
}

Result Deserialize(const std::string& text) {
	Result result;
	std::istringstream lines(text);
	std::string line;
	bool sawStatus = false;
	while (std::getline(lines, line)) {
		size_t tab = line.find('\t');
		if (tab == std::string::npos) {
			continue;
		}
		std::string key = line.substr(0, tab);
		std::string value = Unescape(line.substr(tab + 1));
		if (key == "status") {
			sawStatus = true;
			result.status = value == "PASS" ? Status::Passed : value == "SKIP" ? Status::Skipped : Status::Failed;
		} else if (key == "failure") {
			result.failures.push_back(value);
		} else if (key == "note") {
			result.notes.push_back(value);
		} else if (key == "console") {
			result.console = value;
		} else if (key == "errorlog") {
			result.errorLog = value;
		}
	}
	if (!sawStatus) {
		result.status = Status::Crashed;
	}
	return result;
}

IPlugin* FindPlugin(const std::string& file) {
	IPlugin* found = nullptr;
	IPluginIterator* iterator = scripts->GetPluginIterator();
	for (; iterator->MorePlugins(); iterator->NextPlugin()) {
		if (BaseName(iterator->GetPlugin()->GetFilename()) == BaseName(file)) {
			found = iterator->GetPlugin();
		}
	}
	iterator->Release();
	return found;
}

// In the test's own process: one server, one test.
Result RunInProcess(const Options& options, const Install& install, const Test& test, const SandboxRoot& root) {
	TestSession session = TestSession::ForTest(test);
	Result result;
	Server server(ForTest(options, test, root), install);
	server.Start(session);

	IPlugin* plugin = FindPlugin(test.plugin);
	std::vector<std::string> problems = server.PluginProblems();
	for (const std::string& problem : problems) {
		session.AddFailure("plugin " + problem);
	}
	if (plugin && plugin->GetStatus() == Plugin_Running) {
		IPluginFunction* function = plugin->GetBaseContext()->GetFunctionByName(test.name.c_str());
		if (!function) {
			session.AddFailure("the test plugin has no public " + test.name);
		} else {
			function->Execute(nullptr);
		}
	}
	server.Stop();

	result.failures = session.Failures();
	result.notes = session.Notes();
	for (const std::string& error : session.UnexpectedErrors()) {
		result.failures.push_back("plugin error: " + error);
	}
	for (const std::string& expected : session.UnmetExpectations()) {
		result.failures.push_back("expected an error containing \"" + expected + "\", none occurred");
	}
	if (session.Skipped() && result.failures.empty()) {
		result.status = Status::Skipped;
		result.failures.push_back(session.SkipReason());
	} else {
		result.status = result.failures.empty() ? Status::Passed : Status::Failed;
	}
	result.console = server.engine().output().Text();
	result.errorLog = server.ErrorLog();
	return result;
}

// Forks a process for the test and collects its result. The runner makes
// the test's temporary server folder and alone removes it, after the test
// process ended, so it goes also when that process was killed or died.
Result RunForked(const Options& options, const Install& install, const Test& test) {
	std::unique_ptr<TemporaryFolder> temporary;
	SandboxRoot root{options.sandbox + "/" + BaseName(test.plugin) + "/" + test.name, SandboxOwnership::Given};
	if (options.sandbox.empty()) {
		temporary = std::make_unique<TemporaryFolder>(options.keepSandbox);
		root = temporary->Root();
	}
	int channel[2];
	if (pipe(channel) != 0) {
		Fatal("could not create a pipe: %s", strerror(errno));
	}
	fflush(stdout);
	fflush(stderr);
	double start = MonotonicSeconds();
	pid_t child = fork();
	if (child < 0) {
		Fatal("could not fork: %s", strerror(errno));
	}
	if (child == 0) {
		close(channel[0]);
		Result result = RunInProcess(options, install, test, root);
		WriteAll(channel[1], Serialize(result));
		close(channel[1]);
		fflush(stdout);
		_exit(0);
	}
	close(channel[1]);

	std::string received;
	bool timedOut = false;
	double deadline = start + options.timeoutSeconds;
	while (true) {
		int wait = static_cast<int>((deadline - MonotonicSeconds()) * 1000);
		if (wait <= 0) {
			timedOut = true;
			break;
		}
		pollfd poller{channel[0], POLLIN, 0};
		int ready = poll(&poller, 1, wait);
		if (ready < 0 && errno == EINTR) {
			continue;
		}
		if (ready == 0) {
			timedOut = true;
			break;
		}
		char buffer[65536];
		ssize_t count = read(channel[0], buffer, sizeof(buffer));
		if (count < 0 && errno == EINTR) {
			continue;
		}
		if (count <= 0) {
			break;
		}
		received.append(buffer, static_cast<size_t>(count));
	}
	close(channel[0]);
	if (timedOut) {
		kill(child, SIGKILL);
	}
	int status = 0;
	waitpid(child, &status, 0);
	temporary.reset();

	Result result = Deserialize(received);
	result.seconds = MonotonicSeconds() - start;
	if (timedOut) {
		result.status = Status::TimedOut;
		result.failures.push_back("the test did not end within " + std::to_string(options.timeoutSeconds) +
		                          " seconds");
	} else if (WIFSIGNALED(status)) {
		result.status = Status::Crashed;
		result.failures.push_back(std::string("the test process died of signal ") + strsignal(WTERMSIG(status)));
	} else if (result.status == Status::Crashed) {
		result.failures.push_back("the test process ended without a result (exit code " +
		                          std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : -1) + ")");
	}
	return result;
}

std::string Tail(const std::string& text, size_t lines) {
	size_t position = text.size();
	for (size_t count = 0; position > 0 && count <= lines; count++) {
		position = text.rfind('\n', position - 1);
		if (position == std::string::npos) {
			return text;
		}
	}
	return text.substr(position + 1);
}

void PrintIndented(const std::string& text, const char* indent) {
	std::istringstream lines(text);
	std::string line;
	while (std::getline(lines, line)) {
		printf("%s%s\n", indent, line.c_str());
	}
}

std::string XmlEscape(const std::string& text) {
	std::string escaped;
	for (char c : text) {
		switch (c) {
			case '&': escaped += "&amp;"; break;
			case '<': escaped += "&lt;"; break;
			case '>': escaped += "&gt;"; break;
			case '"': escaped += "&quot;"; break;
			default:
				if (static_cast<unsigned char>(c) < 0x20 && c != '\n' && c != '\t') {
					escaped += ' ';
				} else {
					escaped += c;
				}
		}
	}
	return escaped;
}

void WriteJUnit(const std::string& path, const std::vector<Test>& tests, const std::vector<Result>& results) {
	std::ofstream out(path);
	if (!out) {
		Fatal("could not write %s", path.c_str());
	}
	int failures = 0, skipped = 0, errors = 0;
	for (const Result& result : results) {
		failures += result.status == Status::Failed;
		skipped += result.status == Status::Skipped;
		errors += result.status == Status::Crashed || result.status == Status::TimedOut;
	}
	out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
	out << "<testsuite name=\"smrunner\" tests=\"" << tests.size() << "\" failures=\"" << failures
	    << "\" errors=\"" << errors << "\" skipped=\"" << skipped << "\">\n";
	for (size_t i = 0; i < tests.size(); i++) {
		const Result& result = results[i];
		out << "  <testcase classname=\"" << XmlEscape(BaseName(tests[i].plugin)) << "\" name=\""
		    << XmlEscape(tests[i].name) << "\" time=\"" << result.seconds << "\">\n";
		std::string message;
		for (const std::string& failure : result.failures) {
			message += StripAnsi(failure) + "\n";
		}
		std::string notes;
		for (const std::string& note : result.notes) {
			notes += StripAnsi(note);
		}
		if (result.status == Status::Failed) {
			out << "    <failure message=\"" << XmlEscape(result.failures.empty() ? "" : StripAnsi(result.failures[0]))
			    << "\">" << XmlEscape(message) << "</failure>\n";
		} else if (result.status == Status::Skipped) {
			out << "    <skipped message=\"" << XmlEscape(message) << "\"/>\n";
		} else if (result.status != Status::Passed) {
			out << "    <error message=\"" << XmlEscape(StatusName(result.status)) << "\">" << XmlEscape(message)
			    << "</error>\n";
		}
		out << "    <system-out>" << XmlEscape(notes + result.console) << "</system-out>\n";
		out << "  </testcase>\n";
	}
	out << "</testsuite>\n";
}

}  // namespace

int ListTests(const Options& options) {
	for (const Test& test : FindTests(options)) {
		printf("%s %s\n", BaseName(test.plugin).c_str(), test.name.c_str());
	}
	return 0;
}

int RunTests(const Options& options, const Install& install) {
	std::vector<Test> tests = FindTests(options);
	if (tests.empty()) {
		fprintf(stderr, "smrunner: no tests%s\n", options.filter.empty() ? "" : " match the filter");
		return 1;
	}
	std::vector<Result> results;
	int passed = 0, failed = 0, skipped = 0;
	std::string current;
	for (const Test& test : tests) {
		if (test.plugin != current) {
			current = test.plugin;
			printf("%s\n", BaseName(current).c_str());
		}
		Result result = RunForked(options, install, test);
		printf("  %-7s %s (%.0f ms)\n", StatusName(result.status), test.name.c_str(), result.seconds * 1000);
		for (const std::string& note : result.notes) {
			PrintIndented(ForTerminal(note), "          ");
		}
		if (result.status == Status::Passed) {
			passed++;
		} else if (result.status == Status::Skipped) {
			skipped++;
			for (const std::string& reason : result.failures) {
				PrintIndented(ForTerminal(reason), "          ");
			}
		} else {
			failed++;
			for (const std::string& failure : result.failures) {
				PrintIndented(ForTerminal(failure), "          ");
			}
			if (!result.console.empty()) {
				printf("          --- console (last lines; --echo shows all) ---\n");
				PrintIndented(Tail(result.console, 40), "          | ");
			}
			if (!result.errorLog.empty()) {
				printf("          --- SourceMod error log ---\n");
				PrintIndented(Tail(result.errorLog, 40), "          | ");
			}
		}
		fflush(stdout);
		results.push_back(result);
	}
	printf("%zu tests: %d passed, %d failed, %d skipped\n", tests.size(), passed, failed, skipped);
	if (!options.junitFile.empty()) {
		WriteJUnit(options.junitFile, tests, results);
	}
	return failed == 0 ? 0 : 1;
}

}  // namespace smrunner
