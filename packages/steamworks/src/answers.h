// Copyright 2026 davenonymous
//
// Faked answers: what a mapping entry or a test's route answers, the mapping
// file steamworks.json that holds the entries, and what an answer becomes for
// one request (a range of its body, Content-Length).
#ifndef SMRUNNER_STEAMWORKS_ANSWERS_H
#define SMRUNNER_STEAMWORKS_ANSWERS_H

#include <optional>
#include <string>
#include <vector>

#include "url.h"

namespace steamworks {

// SteamWorks' EHTTPMethod.
enum class Method { Invalid = 0, Get, Head, Post, Put, Delete, Options, Patch };
constexpr int kMethodCount = 8;

std::optional<Method> MethodFromName(const std::string& name);   // "GET", any case
const char* MethodName(Method method);
// The methods whose GET-or-POST parameters go into the body.
bool SendsParametersInBody(Method method);

// How a faked request ends when it ends without an answer; the values of
// RunnerHttpFailure in smrunner-steamworks.inc.
enum class Failure { None = 0, Io = 1, Timeout = 2 };

struct Answer {
	int status = 200;
	NameValues headers;
	std::string body;
	int delayFrames = 0;
	Failure failure = Failure::None;
};

// A mapping entry or a route: a method and URL pattern and its answer.
struct AnswerRule {
	Method method = Method::Get;
	std::string url;
	Answer answer;
};

// The entries of DIR/steamworks.json, with their body files read; an
// error names the entry and what is wrong with it (std::runtime_error).
// A missing file is no entries.
std::vector<AnswerRule> ReadMappingFile(const std::string& dir);

// The first rule that answers the request, or null.
const AnswerRule* FindRule(const std::vector<AnswerRule>& rules, Method method, const std::string& url);

// The value of a header, any case, or null.
const std::string* FindHeader(const NameValues& headers, const std::string& name);
void SetHeader(NameValues& headers, const std::string& name, const std::string& value);

// The Content-Type of a body file by its extension.
std::string ContentTypeFor(const std::string& fileName);

// The answer for one request: a single "bytes=" range of a 200 answer with a
// body becomes 206 with Content-Range (416 past the end); a HEAD request
// gets no body; Content-Length is the length of the body sent (for HEAD:
// that a GET would get), except that a HEAD request keeps the Content-Length
// its rule gives.
Answer AnswerForRequest(const Answer& answer, Method method, const std::string* rangeHeader);

}  // namespace steamworks

#endif
