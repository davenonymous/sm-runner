// Copyright 2026 davenonymous
//
// Record mode (--steamworks-record DIR): a real answer of a passthrough
// request becomes an entry of DIR/steamworks.json with its body file below
// DIR/bodies/<host>/, replacing an entry of the same method and URL. No two
// entries share a body file: a name another entry's body file has gets ~2,
// ~3, ... before its extension. The headers are kept but for the
// connection's, Date and (except for HEAD) Content-Length. Test processes
// record one after the other under a lock on the folder.
#ifndef SMRUNNER_STEAMWORKS_RECORDER_H
#define SMRUNNER_STEAMWORKS_RECORDER_H

#include <string>

#include "answers.h"

namespace steamworks {

struct RecordedAnswer {
	Method method = Method::Get;
	std::string url;
	int status = 0;
	NameValues headers;   // as the server sent them
	std::string body;
};

// The body file's name below the record folder: bodies/<host>/<path> with
// unsafe characters replaced, "index" for a path ending in '/', _<hash of
// the query> when there is one, @<method> in lower case for every method but
// GET (an '@' is never in a replaced path), and the path's own extension or,
// without one, an extension from the Content-Type (so a GET of team/4242, a
// GET of team/4242/matches and a POST to team/4242 become team/4242.json,
// team/4242/matches.json and team/4242@post.json).
std::string BodyFileName(const std::string& url, const std::string& contentType, Method method);

// Writes the answer; throws std::runtime_error naming what failed.
void RecordAnswer(const std::string& dir, const RecordedAnswer& answer);

}  // namespace steamworks

#endif
