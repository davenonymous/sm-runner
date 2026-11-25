// Copyright 2026 davenonymous
//
// The test plugins of one server, by file name. A test plugin loads first
// and is never hooked nor stopped at a breakpoint; run mode has none.
#ifndef SMRUNNER_TESTING_TEST_PLUGINS_H
#define SMRUNNER_TESTING_TEST_PLUGINS_H

#include <IPluginSys.h>
#include <sp_vm_api.h>

#include <string>
#include <vector>

namespace smrunner {

class TestPlugins {
 public:
	// Paths or file names; only the file names count.
	explicit TestPlugins(const std::vector<std::string>& files);

	bool Contains(SourceMod::IPlugin* plugin) const;
	bool Contains(SourcePawn::IPluginContext* context) const;

 private:
	std::vector<std::string> names_;
};

}  // namespace smrunner

#endif
