// Copyright 2026 davenonymous
#include "url.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>

namespace steamworks {

static std::string Lower(std::string text) {
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return tolower(c); });
	return text;
}

std::optional<Url> ParseUrl(const std::string& text) {
	size_t schemeEnd = text.find("://");
	if (schemeEnd == std::string::npos) {
		return std::nullopt;
	}
	Url url;
	url.scheme = Lower(text.substr(0, schemeEnd));
	if (url.scheme != "http" && url.scheme != "https") {
		return std::nullopt;
	}
	size_t authorityStart = schemeEnd + 3;
	size_t authorityEnd = text.find_first_of("/?#", authorityStart);
	std::string authority = text.substr(authorityStart, authorityEnd == std::string::npos ? std::string::npos
	                                                                                      : authorityEnd - authorityStart);
	size_t at = authority.rfind('@');
	if (at != std::string::npos) {
		authority = authority.substr(at + 1);
	}
	url.port = url.scheme == "https" ? 443 : 80;
	size_t colon = authority.rfind(':');
	if (colon != std::string::npos && authority.find(']', colon) == std::string::npos) {
		std::string port = authority.substr(colon + 1);
		char* end;
		long parsed = strtol(port.c_str(), &end, 10);
		if (port.empty() || *end != '\0' || parsed < 1 || parsed > 65535) {
			return std::nullopt;
		}
		url.port = static_cast<int>(parsed);
		authority = authority.substr(0, colon);
	}
	if (authority.empty()) {
		return std::nullopt;
	}
	url.host = Lower(authority);
	if (authorityEnd == std::string::npos) {
		url.path = "/";
		return url;
	}
	std::string rest = text.substr(authorityEnd);
	size_t fragment = rest.find('#');
	if (fragment != std::string::npos) {
		rest = rest.substr(0, fragment);
	}
	size_t question = rest.find('?');
	url.path = rest.substr(0, question);
	if (url.path.empty()) {
		url.path = "/";
	}
	if (question != std::string::npos) {
		url.query = rest.substr(question + 1);
	}
	return url;
}

bool WildcardMatch(std::string_view pattern, std::string_view text, bool ignoreCase) {
	auto same = [ignoreCase](char a, char b) {
		return ignoreCase ? tolower(static_cast<unsigned char>(a)) == tolower(static_cast<unsigned char>(b)) : a == b;
	};
	// Iterative with one backtrack point: the last '*' seen.
	size_t p = 0, t = 0, star = std::string_view::npos, resume = 0;
	while (t < text.size()) {
		if (p < pattern.size() && pattern[p] == '*') {
			star = p++;
			resume = t;
		} else if (p < pattern.size() && same(pattern[p], text[t])) {
			p++;
			t++;
		} else if (star != std::string_view::npos) {
			p = star + 1;
			t = ++resume;
		} else {
			return false;
		}
	}
	while (p < pattern.size() && pattern[p] == '*') {
		p++;
	}
	return p == pattern.size();
}

// The URL with its query parameters sorted; the fragment dropped.
static std::string Normalized(const std::string& url) {
	std::string withoutFragment = url.substr(0, url.find('#'));
	size_t question = withoutFragment.find('?');
	if (question == std::string::npos) {
		return withoutFragment;
	}
	std::vector<std::string> parameters;
	std::string query = withoutFragment.substr(question + 1);
	size_t start = 0;
	while (start <= query.size()) {
		size_t amp = query.find('&', start);
		std::string parameter = query.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
		if (!parameter.empty()) {
			parameters.push_back(parameter);
		}
		if (amp == std::string::npos) {
			break;
		}
		start = amp + 1;
	}
	std::sort(parameters.begin(), parameters.end());
	std::string normalized = withoutFragment.substr(0, question + 1);
	for (size_t i = 0; i < parameters.size(); i++) {
		normalized += (i ? "&" : "") + parameters[i];
	}
	return normalized;
}

bool UrlMatches(const std::string& pattern, const std::string& url) {
	return WildcardMatch(Normalized(pattern), Normalized(url));
}

bool HostMatches(const std::string& pattern, const std::string& host) {
	return WildcardMatch(pattern, host, true);
}

static std::string PercentEncode(const std::string& text) {
	std::string encoded;
	for (unsigned char c : text) {
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			encoded += static_cast<char>(c);
		} else {
			char escape[4];
			snprintf(escape, sizeof(escape), "%%%02X", c);
			encoded += escape;
		}
	}
	return encoded;
}

std::string FormEncode(const NameValues& pairs) {
	std::string encoded;
	for (const auto& pair : pairs) {
		if (!encoded.empty()) {
			encoded += '&';
		}
		encoded += PercentEncode(pair.first) + "=" + PercentEncode(pair.second);
	}
	return encoded;
}

}  // namespace steamworks
