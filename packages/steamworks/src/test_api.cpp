// Copyright 2026 davenonymous
//
// The natives of smrunner-steamworks.inc: RunnerHttpRoute, RunnerHttpRequest,
// RunnerHttp and RunnerSteam.
#include "test_api.h"

#include <arpa/inet.h>
#include <string.h>

#include <algorithm>
#include <fstream>
#include <optional>
#include <sstream>

#include "extension.h"
#include "http.h"
#include "steam.h"
#include "testing/plugin_memory.h"

namespace steamworks {

using smrunner::PluginBytesArg;
using smrunner::PluginStringArg;

// Writes text into the test's buffer of maxlength bytes; false after a
// native error.
static bool WriteString(IPluginContext* context, cell_t buffer, cell_t maxlength, const std::string& text) {
	return smrunner::WritePluginString(context, buffer, maxlength, text).has_value();
}

// The values of RunnerHttpFailure, as the test passes them.
static std::optional<Failure> FailureParam(IPluginContext* context, cell_t value) {
	switch (static_cast<Failure>(value)) {
		case Failure::Io:
		case Failure::Timeout:
			return static_cast<Failure>(value);
		case Failure::None:
			break;
	}
	context->ThrowNativeError("%d is no RunnerHttpFailure of a failure (Io or Timeout)", value);
	return std::nullopt;
}

static std::optional<Method> MethodParam(IPluginContext* context, cell_t value) {
	if (value <= static_cast<cell_t>(Method::Invalid) || value >= kMethodCount) {
		context->ThrowNativeError("%d is no HTTP method", value);
		return std::nullopt;
	}
	return static_cast<Method>(value);
}

// --------------------------------------------------------------- routes

static cell_t NoRoute(IPluginContext* context, cell_t id) {
	return context->ThrowNativeError("%d is no route", id);
}

static cell_t AddChecked(IPluginContext* context, cell_t method, cell_t url, Answer answer) {
	std::optional<Method> parsed = MethodParam(context, method);
	if (!parsed) {
		return 0;
	}
	if (answer.delayFrames < 0) {
		return context->ThrowNativeError("delayFrames is %d, not 0 or more", answer.delayFrames);
	}
	std::optional<std::string> pattern = PluginStringArg(context, url);
	if (!pattern) {
		return 0;
	}
	AnswerRule rule;
	rule.method = *parsed;
	rule.url = *pattern;
	rule.answer = std::move(answer);
	return HttpAnswerer().AddRoute(std::move(rule));
}

static bool IsStatus(cell_t status) {
	return status >= 100 && status <= 599;
}

static cell_t RunnerHttp_Route(IPluginContext* context, const cell_t* params) {
	if (!IsStatus(params[3])) {
		return context->ThrowNativeError("%d is no HTTP status", params[3]);
	}
	std::optional<std::string> body = PluginStringArg(context, params[4]);
	if (!body) {
		return 0;
	}
	Answer answer;
	answer.status = params[3];
	answer.body = *body;
	answer.delayFrames = params[5];
	if (!answer.body.empty()) {
		answer.headers.emplace_back("Content-Type", "application/octet-stream");
	}
	return AddChecked(context, params[1], params[2], std::move(answer));
}

static cell_t RunnerHttp_RouteFile(IPluginContext* context, const cell_t* params) {
	if (!IsStatus(params[4])) {
		return context->ThrowNativeError("%d is no HTTP status", params[4]);
	}
	std::optional<std::string> given = PluginStringArg(context, params[3]);
	if (!given) {
		return 0;
	}
	const std::string& file = *given;
	char path[PLATFORM_MAX_PATH];
	smutils->BuildPath(Path_Game, path, sizeof(path), "%s", file.c_str());
	std::ifstream in(file[0] == '/' ? file : std::string(path), std::ios::binary);
	if (!in) {
		return context->ThrowNativeError("cannot read the body file %s", file.c_str());
	}
	std::ostringstream body;
	body << in.rdbuf();
	Answer answer;
	answer.status = params[4];
	answer.body = body.str();
	answer.delayFrames = params[5];
	answer.headers.emplace_back("Content-Type", ContentTypeFor(file));
	return AddChecked(context, params[1], params[2], std::move(answer));
}

static cell_t RunnerHttp_RouteFailure(IPluginContext* context, const cell_t* params) {
	std::optional<Failure> failure = FailureParam(context, params[3]);
	if (!failure) {
		return 0;
	}
	Answer answer;
	answer.failure = *failure;
	answer.delayFrames = params[4];
	return AddChecked(context, params[1], params[2], std::move(answer));
}

static cell_t RunnerHttp_ClearRoutes(IPluginContext*, const cell_t*) {
	HttpAnswerer().ClearRoutes();
	return 0;
}

static cell_t RunnerHttp_AllowPassthrough(IPluginContext* context, const cell_t* params) {
	std::optional<std::string> host = PluginStringArg(context, params[1]);
	if (!host) {
		return 0;
	}
	if (host->empty()) {
		return context->ThrowNativeError("AllowPassthrough needs a host name");
	}
	HttpAnswerer().AllowPassthroughHost(*host);
	return 0;
}

static cell_t RunnerHttpRoute_SetHeader(IPluginContext* context, const cell_t* params) {
	std::optional<std::string> name = PluginStringArg(context, params[2]);
	std::optional<std::string> value = name ? PluginStringArg(context, params[3]) : std::nullopt;
	if (!value) {
		return 0;
	}
	if (!HttpAnswerer().SetRouteHeader(params[1], *name, *value)) {
		return NoRoute(context, params[1]);
	}
	return 0;
}

static cell_t RunnerHttpRoute_Remove(IPluginContext* context, const cell_t* params) {
	if (!HttpAnswerer().RemoveRoute(params[1])) {
		return NoRoute(context, params[1]);
	}
	return 0;
}

static cell_t RunnerHttpRoute_Hits(IPluginContext* context, const cell_t* params) {
	std::optional<int> hits = HttpAnswerer().Hits(params[1]);
	return hits ? *hits : NoRoute(context, params[1]);
}

// ------------------------------------------------------------- requests

static const LoggedRequest* RequestParam(IPluginContext* context, cell_t index) {
	const auto& log = HttpAnswerer().RequestLog();
	if (index < 0 || static_cast<size_t>(index) >= log.size()) {
		context->ThrowNativeError("request %d does not exist (%zu sent)", index, log.size());
		return nullptr;
	}
	return log[static_cast<size_t>(index)].get();
}

static bool MatchesRequest(const LoggedRequest& request, const std::string& url, cell_t method) {
	return (method == 0 || static_cast<cell_t>(request.method) == method) && UrlMatches(url, request.url);
}

static cell_t RunnerHttp_RequestCount(IPluginContext*, const cell_t*) {
	return static_cast<cell_t>(HttpAnswerer().RequestLog().size());
}

static cell_t RunnerHttp_GetRequest(IPluginContext* context, const cell_t* params) {
	return RequestParam(context, params[1]) ? params[1] : -1;
}

static cell_t RunnerHttp_CountRequests(IPluginContext* context, const cell_t* params) {
	std::optional<std::string> url = PluginStringArg(context, params[1]);
	if (!url) {
		return 0;
	}
	cell_t count = 0;
	for (const auto& request : HttpAnswerer().RequestLog()) {
		count += MatchesRequest(*request, *url, params[2]) ? 1 : 0;
	}
	return count;
}

static cell_t RunnerHttp_FindRequest(IPluginContext* context, const cell_t* params) {
	std::optional<std::string> url = PluginStringArg(context, params[1]);
	if (!url) {
		return -1;
	}
	const auto& log = HttpAnswerer().RequestLog();
	for (size_t i = params[3] < 0 ? 0 : static_cast<size_t>(params[3]); i < log.size(); i++) {
		if (MatchesRequest(*log[i], *url, params[2])) {
			return static_cast<cell_t>(i);
		}
	}
	return -1;
}

static cell_t RunnerHttp_PendingRequests(IPluginContext*, const cell_t*) {
	return PendingRequestCount();
}

static cell_t RunnerHttp_ClearRequests(IPluginContext*, const cell_t*) {
	HttpAnswerer().ClearRequestLog();
	return 0;
}

static cell_t RunnerHttpRequest_Method(IPluginContext* context, const cell_t* params) {
	const LoggedRequest* request = RequestParam(context, params[1]);
	return request ? static_cast<cell_t>(request->method) : 0;
}

static cell_t RunnerHttpRequest_Status(IPluginContext* context, const cell_t* params) {
	const LoggedRequest* request = RequestParam(context, params[1]);
	return request && request->answered ? request->status : 0;
}

static cell_t RunnerHttpRequest_Answered(IPluginContext* context, const cell_t* params) {
	const LoggedRequest* request = RequestParam(context, params[1]);
	return request && request->answered ? 1 : 0;
}

static cell_t RunnerHttpRequest_Source(IPluginContext* context, const cell_t* params) {
	const LoggedRequest* request = RequestParam(context, params[1]);
	return request ? static_cast<cell_t>(request->source) : 0;
}

static cell_t RunnerHttpRequest_BodyLength(IPluginContext* context, const cell_t* params) {
	const LoggedRequest* request = RequestParam(context, params[1]);
	return request ? static_cast<cell_t>(request->body.size()) : 0;
}

static cell_t RunnerHttpRequest_GetUrl(IPluginContext* context, const cell_t* params) {
	const LoggedRequest* request = RequestParam(context, params[1]);
	if (request) {
		WriteString(context, params[2], params[3], request->url);
	}
	return 0;
}

static cell_t RunnerHttpRequest_GetHeader(IPluginContext* context, const cell_t* params) {
	const LoggedRequest* request = RequestParam(context, params[1]);
	std::optional<std::string> name = request ? PluginStringArg(context, params[2]) : std::nullopt;
	const std::string* value = name ? FindHeader(request->headers, *name) : nullptr;
	if (!value) {
		return 0;
	}
	return WriteString(context, params[3], params[4], *value) ? 1 : 0;
}

static cell_t RunnerHttpRequest_GetParameter(IPluginContext* context, const cell_t* params) {
	const LoggedRequest* request = RequestParam(context, params[1]);
	if (!request) {
		return 0;
	}
	std::optional<std::string> name = PluginStringArg(context, params[2]);
	if (!name) {
		return 0;
	}
	for (const auto& parameter : request->parameters) {
		if (parameter.first == *name) {
			return WriteString(context, params[3], params[4], parameter.second) ? 1 : 0;
		}
	}
	return 0;
}

static cell_t RunnerHttpRequest_GetBody(IPluginContext* context, const cell_t* params) {
	const LoggedRequest* request = RequestParam(context, params[1]);
	if (!request || params[3] < 1) {
		return 0;
	}
	char* buffer = PluginBytesArg(context, params[2], params[3]);
	if (!buffer) {
		return 0;
	}
	size_t length = std::min(request->body.size(), static_cast<size_t>(params[3]) - 1);
	memcpy(buffer, request->body.data(), length);
	buffer[length] = '\0';
	return static_cast<cell_t>(length);
}

// ---------------------------------------------------------------- steam

// "a.b.c.d" in host order; "" is 0. Invalid addresses are a native error.
static bool AddressParam(IPluginContext* context, cell_t param, unsigned int* address) {
	std::optional<std::string> text = PluginStringArg(context, param);
	if (!text) {
		return false;
	}
	if (text->empty()) {
		*address = 0;
		return true;
	}
	in_addr parsed;
	if (inet_pton(AF_INET, text->c_str(), &parsed) != 1) {
		context->ThrowNativeError("\"%s\" is no IPv4 address", text->c_str());
		return false;
	}
	*address = ntohl(parsed.s_addr);
	return true;
}

static cell_t RunnerSteam_SetConnected(IPluginContext*, const cell_t* params) {
	Server().connected = params[1] != 0;
	return 0;
}

static cell_t RunnerSteam_FireServersConnected(IPluginContext*, const cell_t*) {
	FireServersConnected();
	return 0;
}

static cell_t RunnerSteam_FireServersConnectFailure(IPluginContext*, const cell_t* params) {
	FireServersConnectFailure(params[1]);
	return 0;
}

static cell_t RunnerSteam_FireServersDisconnected(IPluginContext*, const cell_t* params) {
	FireServersDisconnected(params[1]);
	return 0;
}

static cell_t RunnerSteam_SetVACEnabled(IPluginContext*, const cell_t* params) {
	Server().vacSecured = params[1] != 0;
	return 0;
}

static cell_t RunnerSteam_SetPublicIP(IPluginContext* context, const cell_t* params) {
	unsigned int address;
	if (AddressParam(context, params[1], &address)) {
		Server().publicIp = address;
	}
	return 0;
}

static cell_t RunnerSteam_SetFakeIP(IPluginContext* context, const cell_t* params) {
	unsigned int address;
	if (!AddressParam(context, params[1], &address)) {
		return 0;
	}
	if (params[2] < 0 || params[2] > 65535) {
		return context->ThrowNativeError("%d is no port", params[2]);
	}
	Server().fakeIp = address;
	Server().fakePort = address == 0 ? 0 : params[2];
	return 0;
}

static cell_t RunnerSteam_GetRule(IPluginContext* context, const cell_t* params) {
	std::optional<std::string> key = PluginStringArg(context, params[1]);
	if (!key) {
		return 0;
	}
	auto rule = Server().rules.find(*key);
	if (rule == Server().rules.end()) {
		return 0;
	}
	return WriteString(context, params[2], params[3], rule->second) ? 1 : 0;
}

static cell_t RunnerSteam_RuleCount(IPluginContext*, const cell_t*) {
	return static_cast<cell_t>(Server().rules.size());
}

static cell_t RunnerSteam_GetGameDescription(IPluginContext* context, const cell_t* params) {
	WriteString(context, params[1], params[2], Server().gameDescription);
	return 0;
}

static cell_t RunnerSteam_GetMapName(IPluginContext* context, const cell_t* params) {
	WriteString(context, params[1], params[2], Server().mapName);
	return 0;
}

static cell_t RunnerSteam_GetGameData(IPluginContext* context, const cell_t* params) {
	WriteString(context, params[1], params[2], Server().gameData);
	return 0;
}

static cell_t RunnerSteam_IsAdvertising(IPluginContext*, const cell_t*) {
	return Server().advertising ? 1 : 0;
}

static cell_t RunnerSteam_FireTokenRequested(IPluginContext* context, const cell_t* params) {
	WriteString(context, params[1], params[2], FireTokenRequested());
	return 0;
}

static cell_t RunnerSteam_FireRestartRequested(IPluginContext*, const cell_t*) {
	return FireRestartRequested() ? 1 : 0;
}

static cell_t RunnerSteam_SetGroupStatus(IPluginContext*, const cell_t* params) {
	SetGroupStatus(static_cast<unsigned int>(params[1]), static_cast<unsigned int>(params[2]),
	               GroupStatus{params[3] != 0, params[4] != 0});
	return 0;
}

static cell_t RunnerSteam_SetLicense(IPluginContext* context, const cell_t* params) {
	if (params[3] < static_cast<cell_t>(License::Has) || params[3] > static_cast<cell_t>(License::NoAuth)) {
		return context->ThrowNativeError("%d is no EUserHasLicenseForAppResult", params[3]);
	}
	SetLicense(static_cast<unsigned int>(params[1]), params[2], static_cast<License>(params[3]));
	return 0;
}

static cell_t RunnerSteam_FireValidateClient(IPluginContext*, const cell_t* params) {
	FireValidateClient(static_cast<unsigned int>(params[1]), static_cast<unsigned int>(params[2]));
	return 0;
}

static cell_t RunnerSteam_FireClientGroupStatus(IPluginContext*, const cell_t* params) {
	FireClientGroupStatus(static_cast<unsigned int>(params[1]), static_cast<unsigned int>(params[2]),
	                      GroupStatus{params[3] != 0, params[4] != 0});
	return 0;
}

static const sp_nativeinfo_t kTestNatives[] = {
	{"RunnerHttpRoute.SetHeader", RunnerHttpRoute_SetHeader},
	{"RunnerHttpRoute.Remove", RunnerHttpRoute_Remove},
	{"RunnerHttpRoute.Hits.get", RunnerHttpRoute_Hits},
	{"RunnerHttpRequest.Method.get", RunnerHttpRequest_Method},
	{"RunnerHttpRequest.Status.get", RunnerHttpRequest_Status},
	{"RunnerHttpRequest.Answered.get", RunnerHttpRequest_Answered},
	{"RunnerHttpRequest.Source.get", RunnerHttpRequest_Source},
	{"RunnerHttpRequest.BodyLength.get", RunnerHttpRequest_BodyLength},
	{"RunnerHttpRequest.GetUrl", RunnerHttpRequest_GetUrl},
	{"RunnerHttpRequest.GetHeader", RunnerHttpRequest_GetHeader},
	{"RunnerHttpRequest.GetParameter", RunnerHttpRequest_GetParameter},
	{"RunnerHttpRequest.GetBody", RunnerHttpRequest_GetBody},
	{"RunnerHttp.Route", RunnerHttp_Route},
	{"RunnerHttp.RouteFile", RunnerHttp_RouteFile},
	{"RunnerHttp.RouteFailure", RunnerHttp_RouteFailure},
	{"RunnerHttp.ClearRoutes", RunnerHttp_ClearRoutes},
	{"RunnerHttp.AllowPassthrough", RunnerHttp_AllowPassthrough},
	{"RunnerHttp.RequestCount", RunnerHttp_RequestCount},
	{"RunnerHttp.GetRequest", RunnerHttp_GetRequest},
	{"RunnerHttp.CountRequests", RunnerHttp_CountRequests},
	{"RunnerHttp.FindRequest", RunnerHttp_FindRequest},
	{"RunnerHttp.PendingRequests", RunnerHttp_PendingRequests},
	{"RunnerHttp.ClearRequests", RunnerHttp_ClearRequests},
	{"RunnerSteam.SetConnected", RunnerSteam_SetConnected},
	{"RunnerSteam.FireServersConnected", RunnerSteam_FireServersConnected},
	{"RunnerSteam.FireServersConnectFailure", RunnerSteam_FireServersConnectFailure},
	{"RunnerSteam.FireServersDisconnected", RunnerSteam_FireServersDisconnected},
	{"RunnerSteam.SetVACEnabled", RunnerSteam_SetVACEnabled},
	{"RunnerSteam.SetPublicIP", RunnerSteam_SetPublicIP},
	{"RunnerSteam.SetFakeIP", RunnerSteam_SetFakeIP},
	{"RunnerSteam.GetRule", RunnerSteam_GetRule},
	{"RunnerSteam.RuleCount", RunnerSteam_RuleCount},
	{"RunnerSteam.GetGameDescription", RunnerSteam_GetGameDescription},
	{"RunnerSteam.GetMapName", RunnerSteam_GetMapName},
	{"RunnerSteam.GetGameData", RunnerSteam_GetGameData},
	{"RunnerSteam.IsAdvertising", RunnerSteam_IsAdvertising},
	{"RunnerSteam.FireTokenRequested", RunnerSteam_FireTokenRequested},
	{"RunnerSteam.FireRestartRequested", RunnerSteam_FireRestartRequested},
	{"RunnerSteam.SetGroupStatus", RunnerSteam_SetGroupStatus},
	{"RunnerSteam.SetLicense", RunnerSteam_SetLicense},
	{"RunnerSteam.FireValidateClient", RunnerSteam_FireValidateClient},
	{"RunnerSteam.FireClientGroupStatus", RunnerSteam_FireClientGroupStatus},
	{nullptr, nullptr},
};

void TestApiStart() {
	sharesys->AddNatives(myself, kTestNatives);
}

}  // namespace steamworks
