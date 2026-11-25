// Copyright 2026 davenonymous
//
// One test: a public Test_ function of a test plugin.
#ifndef SMRUNNER_TESTING_TEST_H
#define SMRUNNER_TESTING_TEST_H

#include <string>

namespace smrunner {

struct Test {
	std::string plugin;   // the test plugin's path, as given
	std::string name;
};

}  // namespace smrunner

#endif
