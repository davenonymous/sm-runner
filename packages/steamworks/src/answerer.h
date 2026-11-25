// Copyright 2026 davenonymous
//
// Which answer a request gets, decided once when it is sent: the newest
// route of the test that matches it, else the first matching entry of the
// mapping file, else the network when its host is allowed, else 404. The
// answerer owns what decides it (routes, mapping entries, passthrough hosts),
// the record folder the network's answers go to, and the log of every
// request with where its answer came from.
#ifndef SMRUNNER_STEAMWORKS_ANSWERER_H
#define SMRUNNER_STEAMWORKS_ANSWERER_H

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "answers.h"
#include "passthrough.h"

namespace steamworks {

// Where a request's answer came from, chosen when it is sent; the values of
// RunnerHttpSource in smrunner-steamworks.inc.
enum class AnswerSource { Route = 1, MappingFile = 2, Passthrough = 3, Missing = 4 };

// A request as a test reads it: what was sent and how it was answered.
struct LoggedRequest {
	Method method = Method::Get;
	std::string url;              // with the GET parameters
	NameValues headers;           // with User-Agent and Content-Type
	NameValues parameters;        // GET or POST parameters, as set
	std::string body;             // raw or form body
	AnswerSource source;
	bool answered = false;        // the completed callback's time came
	int status = 0;               // 0 until then and on failures
};

// The answer of a route or the mapping file, cut to the request (a range,
// HEAD), due delayFrames after the frame after Send().
struct FakedAnswer {
	Answer answer;
	int delayFrames = 0;
};

// The request goes over the network.
struct NetworkAnswer {
	TransferRequest transfer;
};

// Nothing answers the request: 404 on the frame after Send().
struct MissingAnswer {
	Answer answer;
};

using AnswerChoice = std::variant<FakedAnswer, NetworkAnswer, MissingAnswer>;

class Answerer {
 public:
	Answerer(std::vector<AnswerRule> mappingRules, std::vector<std::string> passthroughHosts, std::string recordDir);

	// Logs the request (transfer as it goes out, parameters as set) and
	// chooses its answer; a route that answers it counts a hit.
	AnswerChoice Choose(const TransferRequest& transfer, const NameValues& parameters,
	                    std::shared_ptr<LoggedRequest>* logged);

	// A route answers requests before the mapping file and older routes;
	// its id stays valid after it was removed.
	int AddRoute(AnswerRule rule);
	// False for an id no route ever had.
	bool RemoveRoute(int id);
	bool SetRouteHeader(int id, const std::string& name, const std::string& value);
	std::optional<int> Hits(int id) const;
	void ClearRoutes();
	void AllowPassthroughHost(const std::string& host);

	// The network's answer of a logged request into the record folder, when
	// there is one; a failure is told on the console and as a warning.
	void Record(const LoggedRequest& request, const TransferResult& result) const;

	// Every request sent since the start or the last clear, oldest first; a
	// request still pending updates its entry after a clear too.
	const std::vector<std::shared_ptr<LoggedRequest>>& RequestLog() const { return log_; }
	void ClearRequestLog() { log_.clear(); }

 private:
	struct Route {
		int id;
		AnswerRule rule;
		int hits = 0;
		bool removed = false;
	};

	Route* FindRoute(int id);
	const Route* FindRoute(int id) const;
	Route* MatchingRoute(Method method, const std::string& url);
	// Counts the hit of a route that answers.
	AnswerSource ChooseSource(Method method, const std::string& url, const AnswerRule** rule);
	bool IsPassthroughHost(const std::string& host) const;

	std::vector<AnswerRule> mappingRules_;
	std::vector<std::string> passthroughHosts_;
	std::string recordDir_;
	std::vector<Route> routes_;
	int nextRouteId_ = 1;
	std::vector<std::shared_ptr<LoggedRequest>> log_;
};

}  // namespace steamworks

#endif
