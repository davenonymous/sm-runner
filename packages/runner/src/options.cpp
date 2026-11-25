// Copyright 2026 davenonymous
#include "options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <functional>
#include <map>

#include <sourcemod_version.h>
#include <sp_vm_api.h>

#include "sandbox.h"
#include "version.h"

namespace smrunner {

static const char kUsage[] =
	"Usage: smrunner run  [options] PLUGIN.smx...\n"
	"       smrunner test [options] TEST.smx... [--plugin PLUGIN.smx]...\n"
	"       smrunner list TEST.smx...\n"
	"\n"
	"Runs SourceMod plugins and their tests on a simulated server, without a\n"
	"game: SourceMod's own logic library and VM on a mocked core.\n"
	"\n"
	"  run   load the plugins, start a map, run the --command lines and\n"
	"        --frames; exit 1 if a plugin failed to load or start, an\n"
	"        assertion failed or SourceMod logged an error\n"
	"  test  run every Test_ function of the test plugins, each on a fresh\n"
	"        server in its own process; exit 1 if a test failed\n"
	"  list  print the tests of the test plugins without running them\n"
	"\n"
	"Server:\n"
	"  -p, --plugin FILE        load a plugin, in the order given (run: also as\n"
	"                           a plain argument); a bare name that is not a\n"
	"                           file loads that SourceMod stock plugin\n"
	"  -e, --extensions DIR     add every extension in DIR, laid out like\n"
	"                           addons/sourcemod/extensions (x64/ for 64-bit)\n"
	"  -x, --extension FILE     add one extension (.ext.so)\n"
	"  -f, --file SRC=DEST      copy a file or folder to DEST, relative to the\n"
	"                           game folder (tf/)\n"
	"  -c, --cvar NAME=VALUE    set a cvar before SourceMod starts, or as soon\n"
	"                           as a plugin creates it\n"
	"      --core KEY=VALUE     set a core.cfg option\n"
	"      --map NAME           the map to start on (default cp_badlands)\n"
	"      --maps NAME,...      more maps the server has, for changelevel\n"
	"      --maxplayers N       client slots, 1 to 101 (default 24)\n"
	"      --tickrate N         ticks per second, 1 to 1000 (default 66.67)\n"
	"      --no-jit             run plugins in the interpreter\n"
	"      --sandbox DIR        build the server folder in DIR and keep it\n"
	"                           (test: DIR/<test plugin>/<test>/)\n"
	"      --keep-sandbox       keep the temporary server folder\n"
	"SteamWorks:\n"
	"      --steamworks [DIR]   load the SteamWorks stand-in; answer HTTP\n"
	"                           requests from DIR/steamworks.json (DIR is the\n"
	"                           next argument if it is a folder, or =DIR)\n"
	"      --steamworks-passthrough HOST  send requests that nothing else\n"
	"                           answers over the network to hosts matching\n"
	"                           HOST (* matches any characters; repeatable)\n"
	"      --steamworks-record DIR  write the real answers of those requests\n"
	"                           into DIR/steamworks.json, with body files\n"
	"Run:\n"
	"      --command CMD        a server command, run on the first frame\n"
	"                           (repeatable)\n"
	"      --frames N           frames to run (default 66; with 0, no\n"
	"                           command runs)\n"
	"Test:\n"
	"      --filter TEXT        run only tests whose name contains TEXT\n"
	"      --timeout SECONDS    real-time limit of each test (default 30)\n"
	"      --junit FILE         also write a JUnit XML report\n"
	"      --echo               print the server console of every test\n"
	"Output:\n"
	"  -v, --verbose            print the runner's own notes\n"
	"  -q, --quiet              no console echo, no warnings\n"
	"      --color WHEN         colors of drawn menus: auto (default: on a\n"
	"                           terminal, unless NO_COLOR is set), always, never\n"
	"      --home DIR           the install folder (default: the folder above\n"
	"                           bin/)\n"
	"  -h, --help               print this text\n"
	"      --version            print the versions of smrunner and SourceMod\n"
	"                           (only as the first argument)\n"
	"\n"
	"Documentation: README.md and docs/ of the smrunner repository.\n";

[[noreturn]] static void UsageError(const char* format, const char* argument) {
	fprintf(stderr, "smrunner: ");
	fprintf(stderr, format, argument);
	fprintf(stderr, "\n\n%s", kUsage);
	exit(2);
}

static std::pair<std::string, std::string> SplitPair(const char* option, const std::string& value) {
	size_t equals = value.find('=');
	if (equals == std::string::npos || equals == 0) {
		UsageError("%s needs NAME=VALUE", option);
	}
	return {value.substr(0, equals), value.substr(equals + 1)};
}

static double ParseNumber(const char* option, const std::string& value, double min, double max) {
	char* end;
	double parsed = strtod(value.c_str(), &end);
	if (value.empty() || *end != '\0' || !(parsed >= min && parsed <= max)) {
		char range[64];
		snprintf(range, sizeof(range), " needs a number between %g and %g", min, max);
		UsageError("%s", (option + std::string(range)).c_str());
	}
	return parsed;
}

static int ParseInt(const char* option, const std::string& value, int min, int max) {
	char* end;
	long parsed = strtol(value.c_str(), &end, 10);
	if (value.empty() || *end != '\0' || parsed < min || parsed > max) {
		UsageError("%s needs a whole number in range", option);
	}
	return static_cast<int>(parsed);
}

Options ParseOptions(int argc, char** argv) {
	Options options;
	if (argc < 2) {
		fputs(kUsage, stderr);
		exit(2);
	}
	int first = 1;
	std::string mode = argv[1];
	if (mode == "-h" || mode == "--help") {
		fputs(kUsage, stdout);
		exit(0);
	}
	if (mode == "--version") {
		printf("smrunner %s (SourceMod %s, SourcePawn API %d)\n", SMRUNNER_VERSION, SM_VERSION_STRING,
		       SOURCEPAWN_ENGINE2_API_VERSION);
		exit(0);
	}
	if (mode == "run") {
		options.mode = Mode::Run;
	} else if (mode == "test") {
		options.mode = Mode::Test;
	} else if (mode == "list") {
		options.mode = Mode::List;
	} else {
		UsageError("unknown mode \"%s\" (run, test or list)", argv[1]);
	}
	first = 2;

	std::vector<std::string> positional;
	using Handler = std::function<void(const std::string&)>;
	std::map<std::string, std::pair<bool, Handler>> handlers = {
		{"--plugin", {true, [&](const std::string& v) { options.plugins.push_back(v); }}},
		{"--extensions", {true, [&](const std::string& v) { options.extensionDirs.push_back(v); }}},
		{"--extension", {true, [&](const std::string& v) { options.extensionFiles.push_back(v); }}},
		{"--file", {true, [&](const std::string& v) { options.files.push_back(SplitPair("--file", v)); }}},
		{"--cvar", {true, [&](const std::string& v) { options.cvars.push_back(SplitPair("--cvar", v)); }}},
		{"--core", {true, [&](const std::string& v) { options.coreConfig.push_back(SplitPair("--core", v)); }}},
		{"--map", {true, [&](const std::string& v) { options.map = v; }}},
		{"--maps", {true, [&](const std::string& v) {
			 size_t start = 0;
			 while (start <= v.size()) {
				 size_t comma = v.find(',', start);
				 std::string map = v.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
				 if (!map.empty()) {
					 options.maps.push_back(map);
				 }
				 if (comma == std::string::npos) {
					 break;
				 }
				 start = comma + 1;
			 }
		 }}},
		{"--maxplayers", {true, [&](const std::string& v) { options.maxPlayers = ParseInt("--maxplayers", v, 1, 101); }}},
		{"--tickrate", {true, [&](const std::string& v) {
			 options.tickInterval = static_cast<float>(1.0 / ParseNumber("--tickrate", v, 1.0, 1000.0));
		 }}},
		{"--no-jit", {false, [&](const std::string&) { options.jit = false; }}},
		{"--sandbox", {true, [&](const std::string& v) { options.sandbox = v; }}},
		{"--keep-sandbox", {false, [&](const std::string&) { options.keepSandbox = true; }}},
		{"--command", {true, [&](const std::string& v) { options.commands.push_back(v); }}},
		{"--frames", {true, [&](const std::string& v) { options.frames = ParseInt("--frames", v, 0, 100000000); }}},
		{"--filter", {true, [&](const std::string& v) { options.filter = v; }}},
		{"--timeout", {true, [&](const std::string& v) { options.timeoutSeconds = ParseInt("--timeout", v, 1, 86400); }}},
		{"--junit", {true, [&](const std::string& v) { options.junitFile = v; }}},
		{"--echo", {false, [&](const std::string&) { options.echo = true; }}},
		{"--verbose", {false, [&](const std::string&) { options.verbosity = Verbosity::Verbose; }}},
		{"--quiet", {false, [&](const std::string&) { options.verbosity = Verbosity::Quiet; }}},
		{"--home", {true, [&](const std::string& v) { options.home = v; }}},
		{"--steamworks-passthrough", {true, [&](const std::string& v) {
			 if (v.empty()) {
				 UsageError("%s needs a host name", "--steamworks-passthrough");
			 }
			 options.steamworks.passthroughHosts.push_back(v);
		 }}},
		{"--steamworks-record", {true, [&](const std::string& v) { options.steamworks.recordDir = v; }}},
		{"--color", {true, [&](const std::string& v) {
			 if (v == "auto") {
				 options.color = ColorMode::Auto;
			 } else if (v == "always") {
				 options.color = ColorMode::Always;
			 } else if (v == "never") {
				 options.color = ColorMode::Never;
			 } else {
				 UsageError("--color takes auto, always or never, not \"%s\"", v.c_str());
			 }
		 }}},
	};
	std::map<std::string, std::string> shortNames = {
		{"-p", "--plugin"}, {"-e", "--extensions"}, {"-x", "--extension"}, {"-f", "--file"},
		{"-c", "--cvar"}, {"-v", "--verbose"}, {"-q", "--quiet"},
	};

	for (int i = first; i < argc; i++) {
		std::string argument = argv[i];
		if (argument == "-h" || argument == "--help") {
			fputs(kUsage, stdout);
			exit(0);
		}
		// --steamworks takes a folder only when one is given: =DIR, or a next
		// argument that is a folder.
		if (argument == "--steamworks" || argument.compare(0, 13, "--steamworks=") == 0) {
			options.steamworks.enabled = true;
			if (argument.size() > 13) {
				options.steamworks.fixtureDir = argument.substr(13);
			} else if (i + 1 < argc && argv[i + 1][0] != '-' && IsDirectory(argv[i + 1])) {
				options.steamworks.fixtureDir = argv[++i];
			}
			if (!options.steamworks.fixtureDir.empty() && !IsDirectory(options.steamworks.fixtureDir)) {
				UsageError("--steamworks: %s is no folder", options.steamworks.fixtureDir.c_str());
			}
			continue;
		}
		if (argument.size() < 2 || argument[0] != '-') {
			positional.push_back(argument);
			continue;
		}
		std::string name = argument;
		std::string value;
		bool hasValue = false;
		size_t equals = argument.find('=');
		if (argument.compare(0, 2, "--") == 0 && equals != std::string::npos) {
			name = argument.substr(0, equals);
			value = argument.substr(equals + 1);
			hasValue = true;
		}
		auto shortName = shortNames.find(name);
		if (shortName != shortNames.end()) {
			name = shortName->second;
		}
		auto handler = handlers.find(name);
		if (handler == handlers.end()) {
			UsageError("unknown option %s", argument.c_str());
		}
		if (handler->second.first && !hasValue) {
			if (i + 1 >= argc) {
				UsageError("%s needs a value", name.c_str());
			}
			value = argv[++i];
		}
		handler->second.second(value);
	}

	if (!options.steamworks.enabled &&
	    (!options.steamworks.passthroughHosts.empty() || !options.steamworks.recordDir.empty())) {
		UsageError("%s", "--steamworks-passthrough and --steamworks-record need --steamworks");
	}

	if (options.mode == Mode::Run) {
		options.plugins.insert(options.plugins.end(), positional.begin(), positional.end());
		if (options.plugins.empty()) {
			UsageError("%s", "run needs at least one plugin");
		}
	} else {
		if (positional.empty()) {
			UsageError("%s", "give at least one test plugin");
		}
		options.testPlugins = positional;
	}
	return options;
}

}  // namespace smrunner
