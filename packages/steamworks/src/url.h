// Copyright 2026 davenonymous
//
// URLs as the stand-in compares them: the parts it needs, the matching of
// mapping entries and routes against a request (exact, query parameters in
// any order, '*' for any characters) and of passthrough hosts.
#ifndef SMRUNNER_STEAMWORKS_URL_H
#define SMRUNNER_STEAMWORKS_URL_H

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace steamworks {

using NameValues = std::vector<std::pair<std::string, std::string>>;

struct Url {
	std::string scheme;   // "http" or "https", lower case
	std::string host;     // lower case, without the port
	int port = 0;         // the given one, or the scheme's
	std::string path;     // from the first '/' up to the query; "/" when empty
	std::string query;    // after '?', without the fragment; may be empty
};

// An absolute http:// or https:// URL with a host; nothing else.
std::optional<Url> ParseUrl(const std::string& text);

// '*' matches any run of characters, everything else itself.
bool WildcardMatch(std::string_view pattern, std::string_view text, bool ignoreCase = false);

// A pattern of a mapping entry or route against a request's URL: both with
// their query parameters sorted, so their order does not count.
bool UrlMatches(const std::string& pattern, const std::string& url);

// A passthrough host pattern against a host name, ignoring case.
bool HostMatches(const std::string& pattern, const std::string& host);

// name=value pairs, percent-encoded as a form or a query.
std::string FormEncode(const NameValues& pairs);

}  // namespace steamworks

#endif
