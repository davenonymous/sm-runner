// Copyright 2026 davenonymous
//
// Real time for what must not depend on the simulated clock: test time
// limits and waits for extensions that work on threads.
#ifndef SMRUNNER_MONOTONIC_H
#define SMRUNNER_MONOTONIC_H

#include <time.h>

namespace smrunner {

// Seconds since an arbitrary point, never going back.
inline double MonotonicSeconds() {
	timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec + now.tv_nsec / 1e9;
}

}  // namespace smrunner

#endif
