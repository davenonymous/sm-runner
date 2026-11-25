// Copyright 2026 davenonymous
//
// One simulated server for one run or one test: the sandbox, the engine and
// SourceMod, started up to the point where a map runs and its configs were
// executed (OnConfigsExecuted has fired), and shut down again.
#ifndef SMRUNNER_SERVER_H
#define SMRUNNER_SERVER_H

#include <memory>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "sandbox.h"
#include "server_spec.h"
#include "testing/test_session.h"

namespace smrunner {

// The runner's install folder.
struct Install {
	std::string home;
	std::string libDir() const;
	std::string shareDir() const { return home + "/share"; }
	static Install Locate(const std::string& override);
};

class Server {
 public:
	Server(ServerSpec spec, const Install& install);
	~Server();

	// Starts SourceMod and the map and runs frames until the configs were
	// executed; fails loud when they never are, and warns of every --cvar
	// whose cvar was not made by then. The session is attached once SourceMod
	// started, before any plugin loads.
	void Start(TestSession& session);
	void RunFrames(int count);
	// Plugin unload, extension unload, SourceMod shutdown; then the session
	// is detached.
	void Stop();

	Engine& engine() { return *engine_; }
	const Sandbox& sandbox() const { return *sandbox_; }

	// Every plugin that is not running, with its status and error.
	std::vector<std::string> PluginProblems() const;
	// What SourceMod wrote to its error logs (logs/errors_*.log).
	std::string ErrorLog() const;

 private:
	ServerSpec spec_;
	Install install_;
	std::unique_ptr<Sandbox> sandbox_;
	std::unique_ptr<Engine> engine_;
	TestSession* session_ = nullptr;   // attached while the server runs
};

}  // namespace smrunner

#endif
