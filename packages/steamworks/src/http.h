// Copyright 2026 davenonymous
//
// SteamWorks' HTTP requests on the simulated server: the handles plugins
// get, the natives of the SteamWorksHTTPRequest methodmap and its
// SteamWorks_*HTTP* functions: the marshalling of plugin memory, the
// handles, and the callbacks of what the Answerer chose and the
// PendingRequest delivers. Faked answers arrive on the frame after Send()
// plus their delay, with timeouts on the simulated clock; real ones on the
// first frame after they came in. Callbacks run only on the game frame.
#ifndef SMRUNNER_STEAMWORKS_HTTP_H
#define SMRUNNER_STEAMWORKS_HTTP_H

#include "answerer.h"

namespace steamworks {

// Reads the mapping file and the passthrough hosts, makes the handle type
// and adds the natives. Throws std::runtime_error when the file is wrong.
void HttpStart();
void HttpStop();
// Once per game frame.
void HttpFrame();

// The test API's view: the routes, passthrough hosts and request log, and
// the requests sent and not completed yet.
Answerer& HttpAnswerer();
int PendingRequestCount();

}  // namespace steamworks

#endif
