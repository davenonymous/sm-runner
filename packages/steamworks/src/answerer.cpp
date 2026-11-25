// Copyright 2026 davenonymous
#include "answerer.h"

#include <algorithm>
#include <stdexcept>

#include "extension.h"
#include "recorder.h"

namespace steamworks {

Answerer::Answerer(std::vector<AnswerRule> mappingRules, std::vector<std::string> passthroughHosts,
                   std::string recordDir)
	: mappingRules_(std::move(mappingRules)),
	  passthroughHosts_(std::move(passthroughHosts)),
	  recordDir_(std::move(recordDir)) {}

bool Answerer::IsPassthroughHost(const std::string& host) const {
	return std::any_of(passthroughHosts_.begin(), passthroughHosts_.end(),
	                   [&host](const std::string& pattern) { return HostMatches(pattern, host); });
}

Answerer::Route* Answerer::MatchingRoute(Method method, const std::string& url) {
	for (auto route = routes_.rbegin(); route != routes_.rend(); ++route) {
		if (!route->removed && route->rule.method == method && UrlMatches(route->rule.url, url)) {
			return &*route;
		}
	}
	return nullptr;
}

// Where the request's answer comes from, and the rule of a faked one.
AnswerSource Answerer::ChooseSource(Method method, const std::string& url, const AnswerRule** rule) {
	*rule = nullptr;
	if (Route* route = MatchingRoute(method, url)) {
		route->hits++;
		*rule = &route->rule;
		return AnswerSource::Route;
	}
	if ((*rule = FindRule(mappingRules_, method, url))) {
		return AnswerSource::MappingFile;
	}
	std::optional<Url> parsed = ParseUrl(url);
	return parsed && IsPassthroughHost(parsed->host) ? AnswerSource::Passthrough : AnswerSource::Missing;
}

AnswerChoice Answerer::Choose(const TransferRequest& transfer, const NameValues& parameters,
                              std::shared_ptr<LoggedRequest>* logged) {
	const AnswerRule* rule = nullptr;
	AnswerSource source = ChooseSource(transfer.method, transfer.url, &rule);
	auto log = std::make_shared<LoggedRequest>(LoggedRequest{
		transfer.method, transfer.url, transfer.headers, parameters, transfer.body, source});
	log_.push_back(log);
	*logged = log;

	switch (source) {
		case AnswerSource::Route:
		case AnswerSource::MappingFile:
			return FakedAnswer{AnswerForRequest(rule->answer, transfer.method, FindHeader(transfer.headers, "Range")),
			                   rule->answer.delayFrames};
		case AnswerSource::Passthrough: {
			NetworkAnswer network{transfer};
			network.transfer.allowedHosts = passthroughHosts_;
			return network;
		}
		case AnswerSource::Missing:
			break;
	}
	Answer missing;
	missing.status = 404;
	return MissingAnswer{AnswerForRequest(missing, transfer.method, nullptr)};
}

Answerer::Route* Answerer::FindRoute(int id) {
	auto found = std::find_if(routes_.begin(), routes_.end(), [id](const Route& route) { return route.id == id; });
	return found == routes_.end() ? nullptr : &*found;
}

const Answerer::Route* Answerer::FindRoute(int id) const {
	auto found = std::find_if(routes_.begin(), routes_.end(), [id](const Route& route) { return route.id == id; });
	return found == routes_.end() ? nullptr : &*found;
}

int Answerer::AddRoute(AnswerRule rule) {
	routes_.push_back(Route{nextRouteId_++, std::move(rule)});
	return routes_.back().id;
}

bool Answerer::RemoveRoute(int id) {
	Route* route = FindRoute(id);
	if (route) {
		route->removed = true;
	}
	return route != nullptr;
}

bool Answerer::SetRouteHeader(int id, const std::string& name, const std::string& value) {
	Route* route = FindRoute(id);
	if (route) {
		SetHeader(route->rule.answer.headers, name, value);
	}
	return route != nullptr;
}

std::optional<int> Answerer::Hits(int id) const {
	const Route* route = FindRoute(id);
	return route ? std::optional<int>(route->hits) : std::nullopt;
}

void Answerer::ClearRoutes() {
	for (Route& route : routes_) {
		route.removed = true;
	}
}

void Answerer::AllowPassthroughHost(const std::string& host) {
	passthroughHosts_.push_back(host);
}

void Answerer::Record(const LoggedRequest& request, const TransferResult& result) const {
	if (recordDir_.empty()) {
		return;
	}
	RecordedAnswer recorded;
	recorded.method = request.method;
	recorded.url = request.url;
	recorded.status = result.status;
	recorded.headers = result.headers;
	recorded.body = result.body;
	try {
		RecordAnswer(recordDir_, recorded);
	} catch (const std::exception& error) {
		ConsoleLine(std::string("recording failed: ") + error.what());
		Host()->Warn((std::string("SteamWorks: recording failed: ") + error.what()).c_str());
	}
}

}  // namespace steamworks
