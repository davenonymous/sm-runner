// Copyright 2026 davenonymous
//
// The natives follow SteamWorks 1.2.167's swhttprequest.cpp: the same
// handle type name, owner and security, the same callback parameters and
// return values, with ISteamHTTP's part done by the Answerer and the
// PendingRequest. Every span of plugin memory a native reads or writes is
// checked first; one that is not the plugin's is a native error.
#include "http.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <stdexcept>

#include "extension.h"
#include "passthrough.h"
#include "pending_request.h"
#include "testing/plugin_memory.h"

namespace steamworks {

using smrunner::PluginBytesArg;
using smrunner::PluginCellsArg;
using smrunner::PluginStringArg;

// ISteamHTTP's default network activity timeout.
static constexpr unsigned int kDefaultActivityTimeoutSeconds = 60;
static const char kUserAgent[] = "Valve/Steam HTTP Client 1.0 (440)";

struct HttpRequest {
	Handle_t handle = BAD_HANDLE;
	bool released = false;
	Method method = Method::Get;
	std::string url;
	NameValues headers;
	NameValues parameters;
	std::string userAgentInfo;
	bool hasRawBody = false;
	std::string rawBodyType;
	std::string rawBody;
	bool verifyCertificate = true;
	unsigned int absoluteTimeoutMs = 0;
	unsigned int activityTimeoutSeconds = kDefaultActivityTimeoutSeconds;
	uint64_t context = 0;
	IChangeableForward* completed = nullptr;
	IChangeableForward* headersReceived = nullptr;
	IChangeableForward* dataReceived = nullptr;

	std::unique_ptr<PendingRequest> pending;   // null until it was sent
	std::shared_ptr<LoggedRequest> log;

	~HttpRequest() {
		for (IChangeableForward* forward : {completed, headersReceived, dataReceived}) {
			if (forward) {
				forwards->ReleaseForward(forward);
			}
		}
	}

	bool IsBuilding() const { return !pending; }
	bool HasWholeBody() const { return pending && pending->HasWholeBody(); }
	// ISteamHTTP's GetHTTPResponseBodyData, which the body callback and the
	// file writer go through too, refuses a streamed response.
	bool HasBufferedBody() const { return HasWholeBody() && !pending->IsStreaming(); }
	bool HasResponse() const { return pending && pending->HasResponse(); }
};

class HttpHandleDispatch : public IHandleTypeDispatch {
 public:
	void OnHandleDestroy(HandleType_t type, void* object) override;
	bool GetHandleApproxSize(HandleType_t type, void* object, unsigned int* size) override {
		*size = sizeof(HttpRequest);
		return true;
	}
};

static HttpHandleDispatch sDispatch;
static HandleType_t sHttpType = 0;
static std::vector<std::unique_ptr<HttpRequest>> sRequests;
static std::unique_ptr<Answerer> sAnswerer;
static std::unique_ptr<Passthrough> sPassthrough;

void HttpHandleDispatch::OnHandleDestroy(HandleType_t type, void* object) {
	HttpRequest* request = static_cast<HttpRequest*>(object);
	request->released = true;
	if (request->pending) {
		request->pending->Cancel();
	}
}

// ------------------------------------------------------------ sending

// The URL with the GET parameters of a method that sends them in the query.
static std::string EffectiveUrl(const HttpRequest& request) {
	if (request.parameters.empty() || SendsParametersInBody(request.method)) {
		return request.url;
	}
	std::string url = request.url.substr(0, request.url.find('#'));
	return url + (url.find('?') == std::string::npos ? "?" : "&") + FormEncode(request.parameters);
}

// The request as it goes out: its URL, its headers with User-Agent and
// Content-Type, its body.
static TransferRequest Outgoing(const HttpRequest& request) {
	TransferRequest transfer;
	transfer.method = request.method;
	transfer.url = EffectiveUrl(request);
	transfer.headers = request.headers;
	SetHeader(transfer.headers, "User-Agent",
	          request.userAgentInfo.empty() ? kUserAgent : std::string(kUserAgent) + " " + request.userAgentInfo);
	if (request.hasRawBody) {
		transfer.body = request.rawBody;
		SetHeader(transfer.headers, "Content-Type", request.rawBodyType);
	} else if (!request.parameters.empty() && SendsParametersInBody(request.method)) {
		transfer.body = FormEncode(request.parameters);
		SetHeader(transfer.headers, "Content-Type", "application/x-www-form-urlencoded");
	}
	transfer.verifyCertificate = request.verifyCertificate;
	transfer.absoluteTimeoutMs = request.absoluteTimeoutMs;
	transfer.activityTimeoutSeconds = request.activityTimeoutSeconds;
	return transfer;
}

static void Send(HttpRequest& request, bool streaming) {
	TransferRequest transfer = Outgoing(request);
	RequestTiming timing;
	timing.sentAt = timersys->GetTickedTime();
	timing.absoluteTimeoutMs = request.absoluteTimeoutMs;
	timing.activityTimeoutSeconds = request.activityTimeoutSeconds;
	timing.streaming = streaming;
	AnswerChoice choice = sAnswerer->Choose(transfer, request.parameters, &request.log);
	int nextFrame = CurrentFrame() + 1;
	if (auto* faked = std::get_if<FakedAnswer>(&choice)) {
		request.pending = std::make_unique<PendingRequest>(std::move(faked->answer), nextFrame + faked->delayFrames,
		                                                   timing);
	} else if (auto* network = std::get_if<NetworkAnswer>(&choice)) {
		request.pending =
			std::make_unique<PendingRequest>(*sPassthrough, sPassthrough->Start(std::move(network->transfer)), timing);
	} else {
		auto& missing = std::get<MissingAnswer>(choice);
		request.pending = std::make_unique<PendingRequest>(std::move(missing.answer), nextFrame, timing);
		std::string line =
			std::string("no answer for ") + MethodName(request.method) + " " + request.log->url + ": 404";
		ConsoleLine(line);
		Host()->Note(("SteamWorks: " + line).c_str());
	}
}

// ------------------------------------------------------------- callbacks

static void PushContext(IChangeableForward* forward, uint64_t context) {
	forward->PushCell(static_cast<cell_t>(context >> 32));
	forward->PushCell(static_cast<cell_t>(context & 0xFFFFFFFFu));
}

static void FireCompleted(HttpRequest& request, const PendingEvent& event) {
	if (!event.error.empty()) {
		ConsoleLine(std::string(MethodName(request.method)) + " " + request.log->url + " failed: " + event.error);
	}
	int status = event.successful ? request.pending->Status() : 0;
	request.log->answered = true;
	request.log->status = status;
	if (!request.completed || request.completed->GetFunctionCount() == 0) {
		return;
	}
	request.completed->PushCell(request.handle);
	request.completed->PushCell(event.failure);
	request.completed->PushCell(event.successful);
	request.completed->PushCell(status);
	PushContext(request.completed, request.context);
	request.completed->Execute(nullptr);
}

// The streaming notifications carry no failure flag (always false), as in
// SteamWorks. Steam posts HTTPRequestHeadersReceived_t only for a request
// sent with SendHTTPRequestAndStreamResponse; the answer of any request is
// recorded when its headers arrive.
static void FireHeadersReceived(HttpRequest& request, const PendingEvent& event) {
	if (event.fromNetwork) {
		sAnswerer->Record(*request.log, *request.pending->NetworkResult());
	}
	if (!request.pending->IsStreaming() || !request.headersReceived ||
	    request.headersReceived->GetFunctionCount() == 0) {
		return;
	}
	request.headersReceived->PushCell(request.handle);
	request.headersReceived->PushCell(false);
	PushContext(request.headersReceived, request.context);
	request.headersReceived->Execute(nullptr);
}

static void FireDataReceived(HttpRequest& request, const PendingEvent& event) {
	if (!request.dataReceived || request.dataReceived->GetFunctionCount() == 0) {
		return;
	}
	request.dataReceived->PushCell(request.handle);
	request.dataReceived->PushCell(false);
	request.dataReceived->PushCell(static_cast<cell_t>(event.offset));
	request.dataReceived->PushCell(static_cast<cell_t>(event.bytes));
	PushContext(request.dataReceived, request.context);
	request.dataReceived->Execute(nullptr);
}

// Fires the request's events of this frame; a callback that releases the
// request ends them.
static void Advance(HttpRequest& request) {
	while (!request.released) {
		std::optional<PendingEvent> event = request.pending->Advance(timersys->GetTickedTime(), CurrentFrame());
		if (!event) {
			return;
		}
		switch (event->kind) {
			case PendingEvent::Kind::Headers:
				FireHeadersReceived(request, *event);
				break;
			case PendingEvent::Kind::Data:
				FireDataReceived(request, *event);
				break;
			case PendingEvent::Kind::Completed:
				FireCompleted(request, *event);
				break;
		}
	}
}

void HttpFrame() {
	// Requests made by callbacks wait for the next frame.
	std::vector<HttpRequest*> current;
	for (auto& request : sRequests) {
		current.push_back(request.get());
	}
	for (HttpRequest* request : current) {
		if (!request->released && request->pending) {
			Advance(*request);
		}
	}
	sRequests.erase(std::remove_if(sRequests.begin(), sRequests.end(),
	                               [](const std::unique_ptr<HttpRequest>& request) { return request->released; }),
	                sRequests.end());
}

// ----------------------------------------------------------------- natives

static HttpRequest* ReadRequest(IPluginContext* context, cell_t handle) {
	HandleSecurity security(context->GetIdentity(), myself->GetIdentity());
	HttpRequest* request = nullptr;
	HandleError error = handlesys->ReadHandle(handle, sHttpType, &security, reinterpret_cast<void**>(&request));
	if (error != HandleError_None) {
		context->ThrowNativeError("Invalid Handle %x (error: %d)", handle, error);
		return nullptr;
	}
	return request;
}

// Copies bytes into a plugin buffer at address; nothing to copy is no
// buffer to check.
static bool CopyOut(IPluginContext* context, cell_t address, const char* bytes, size_t length) {
	if (length == 0) {
		return true;
	}
	char* buffer = PluginBytesArg(context, address, static_cast<cell_t>(length));
	if (buffer) {
		memcpy(buffer, bytes, length);
	}
	return buffer != nullptr;
}

static IPlugin* CallbackPlugin(IPluginContext* context, cell_t pluginHandle) {
	if (pluginHandle == BAD_HANDLE) {
		return plsys->FindPluginByContext(context->GetContext());
	}
	HandleError error;
	IPlugin* plugin = plsys->PluginFromHandle(pluginHandle, &error);
	if (!plugin) {
		context->ThrowNativeError("Plugin handle %x is invalid (error %d)", pluginHandle, error);
	}
	return plugin;
}

static cell_t CreateHTTPRequest(IPluginContext* context, const cell_t* params) {
	int method = params[1];
	std::optional<std::string> url = PluginStringArg(context, params[2]);
	if (!url) {
		return BAD_HANDLE;
	}
	if (method <= static_cast<int>(Method::Invalid) || method >= kMethodCount || !ParseUrl(*url)) {
		return BAD_HANDLE;
	}
	auto request = std::make_unique<HttpRequest>();
	request->method = static_cast<Method>(method);
	request->url = *url;
	Handle_t handle = handlesys->CreateHandle(sHttpType, request.get(), context->GetIdentity(), myself->GetIdentity(),
	                                          nullptr);
	if (handle == BAD_HANDLE) {
		return BAD_HANDLE;
	}
	request->handle = handle;
	sRequests.push_back(std::move(request));
	return handle;
}

// A setter's request: false (and nothing changed) once it was sent.
static HttpRequest* BuildingRequest(IPluginContext* context, cell_t handle, bool* valid) {
	HttpRequest* request = ReadRequest(context, handle);
	*valid = request && request->IsBuilding();
	return request;
}

static cell_t SetContextValue(IPluginContext* context, const cell_t* params) {
	HttpRequest* request = ReadRequest(context, params[1]);
	if (!request) {
		return 0;
	}
	request->context = static_cast<uint64_t>(static_cast<uint32_t>(params[2])) << 32 | static_cast<uint32_t>(params[3]);
	return 1;
}

static cell_t SetNetworkActivityTimeout(IPluginContext* context, const cell_t* params) {
	bool valid;
	HttpRequest* request = BuildingRequest(context, params[1], &valid);
	if (!valid) {
		return 0;
	}
	request->activityTimeoutSeconds = static_cast<unsigned int>(params[2]);
	return 1;
}

static cell_t SetHeaderValue(IPluginContext* context, const cell_t* params) {
	bool valid;
	HttpRequest* request = BuildingRequest(context, params[1], &valid);
	if (!valid) {
		return 0;
	}
	std::optional<std::string> name = PluginStringArg(context, params[2]);
	std::optional<std::string> value = name ? PluginStringArg(context, params[3]) : std::nullopt;
	if (!value) {
		return 0;
	}
	SetHeader(request->headers, *name, *value);
	return 1;
}

static cell_t SetGetOrPostParameter(IPluginContext* context, const cell_t* params) {
	bool valid;
	HttpRequest* request = BuildingRequest(context, params[1], &valid);
	if (!valid || (request->hasRawBody && SendsParametersInBody(request->method))) {
		return 0;
	}
	std::optional<std::string> name = PluginStringArg(context, params[2]);
	std::optional<std::string> value = name ? PluginStringArg(context, params[3]) : std::nullopt;
	if (!value) {
		return 0;
	}
	request->parameters.emplace_back(*name, *value);
	return 1;
}

static cell_t SetUserAgentInfo(IPluginContext* context, const cell_t* params) {
	bool valid;
	HttpRequest* request = BuildingRequest(context, params[1], &valid);
	if (!valid) {
		return 0;
	}
	std::optional<std::string> info = PluginStringArg(context, params[2]);
	if (!info) {
		return 0;
	}
	request->userAgentInfo = *info;
	return 1;
}

static cell_t SetRequiresVerifiedCertificate(IPluginContext* context, const cell_t* params) {
	bool valid;
	HttpRequest* request = BuildingRequest(context, params[1], &valid);
	if (!valid) {
		return 0;
	}
	request->verifyCertificate = params[2] != 0;
	return 1;
}

static cell_t SetAbsoluteTimeoutMS(IPluginContext* context, const cell_t* params) {
	bool valid;
	HttpRequest* request = BuildingRequest(context, params[1], &valid);
	if (!valid) {
		return 0;
	}
	request->absoluteTimeoutMs = static_cast<unsigned int>(params[2]);
	return 1;
}

static bool AddCallback(IPluginContext* context, IPlugin* plugin, cell_t function, IChangeableForward** forward,
                        int parameterCount) {
	if (function <= 0) {
		return true;
	}
	IPluginFunction* callback = plugin->GetBaseContext()->GetFunctionById(function);
	if (!callback) {
		context->ThrowNativeError("Invalid function id (%X)", function);
		return false;
	}
	if (!*forward) {
		static const ParamType kCells[6] = {Param_Cell, Param_Cell, Param_Cell, Param_Cell, Param_Cell, Param_Cell};
		*forward = forwards->CreateForwardEx(nullptr, ET_Ignore, parameterCount, kCells);
	}
	(*forward)->AddFunction(callback);
	return true;
}

static cell_t SetCallbacks(IPluginContext* context, const cell_t* params) {
	HttpRequest* request = ReadRequest(context, params[1]);
	if (!request) {
		return 0;
	}
	IPlugin* plugin = CallbackPlugin(context, params[5]);
	if (!plugin) {
		return 0;
	}
	if (!AddCallback(context, plugin, params[2], &request->completed, 6) ||
	    !AddCallback(context, plugin, params[3], &request->headersReceived, 4) ||
	    !AddCallback(context, plugin, params[4], &request->dataReceived, 6)) {
		return 0;
	}
	return 1;
}

static cell_t SendRequest(IPluginContext* context, const cell_t* params, bool streaming) {
	bool valid;
	HttpRequest* request = BuildingRequest(context, params[1], &valid);
	if (!valid) {
		return 0;
	}
	Send(*request, streaming);
	return 1;
}

static cell_t SendHTTPRequest(IPluginContext* context, const cell_t* params) {
	return SendRequest(context, params, false);
}

static cell_t SendHTTPRequestAndStreamResponse(IPluginContext* context, const cell_t* params) {
	return SendRequest(context, params, true);
}

// Defer and Prioritize order Steam's queue; the stand-in answers every
// request on its own schedule, so they only say whether it waits.
static cell_t ReorderRequest(IPluginContext* context, const cell_t* params) {
	HttpRequest* request = ReadRequest(context, params[1]);
	return request && request->pending && request->pending->IsWaiting() ? 1 : 0;
}

static cell_t GetResponseHeaderSize(IPluginContext* context, const cell_t* params) {
	HttpRequest* request = ReadRequest(context, params[1]);
	if (!request || !request->HasResponse()) {
		return 0;
	}
	std::optional<std::string> name = PluginStringArg(context, params[2]);
	if (!name) {
		return 0;
	}
	const std::string* value = FindHeader(request->pending->ResponseHeaders(), *name);
	if (!value) {
		return 0;
	}
	cell_t* size = PluginCellsArg(context, params[3], 1);
	if (!size) {
		return 0;
	}
	*size = static_cast<cell_t>(value->size() + 1);
	return 1;
}

static cell_t GetResponseHeaderValue(IPluginContext* context, const cell_t* params) {
	HttpRequest* request = ReadRequest(context, params[1]);
	if (!request || !request->HasResponse()) {
		return 0;
	}
	std::optional<std::string> name = PluginStringArg(context, params[2]);
	if (!name) {
		return 0;
	}
	const std::string* value = FindHeader(request->pending->ResponseHeaders(), *name);
	if (!value || params[4] < 0 || value->size() + 1 > static_cast<size_t>(params[4])) {
		return 0;
	}
	// The whole buffer the plugin gives must be its own.
	char* buffer = PluginBytesArg(context, params[3], params[4]);
	if (!buffer) {
		return 0;
	}
	memcpy(buffer, value->c_str(), value->size() + 1);
	return 1;
}

static cell_t GetResponseBodySize(IPluginContext* context, const cell_t* params) {
	HttpRequest* request = ReadRequest(context, params[1]);
	if (!request || !request->HasWholeBody()) {
		return 0;
	}
	cell_t* size = PluginCellsArg(context, params[2], 1);
	if (!size) {
		return 0;
	}
	*size = static_cast<cell_t>(request->pending->Body().size());
	return 1;
}

// ISteamHTTP wants the buffer size GetHTTPResponseBodySize gave, exactly:
// a larger buffer is refused as well as a smaller one.
static cell_t GetResponseBodyData(IPluginContext* context, const cell_t* params) {
	HttpRequest* request = ReadRequest(context, params[1]);
	if (!request || !request->HasBufferedBody() || params[3] < 0 ||
	    static_cast<size_t>(params[3]) != request->pending->Body().size()) {
		return 0;
	}
	const std::string& body = request->pending->Body();
	return CopyOut(context, params[2], body.data(), body.size()) ? 1 : 0;
}

// Only the chunk of the latest data callback, by its offset and length.
static cell_t GetStreamingResponseBodyData(IPluginContext* context, const cell_t* params) {
	HttpRequest* request = ReadRequest(context, params[1]);
	if (!request || !request->HasResponse() || !request->pending->IsStreaming() || params[2] < 0 || params[4] < 0) {
		return 0;
	}
	std::optional<StreamChunk> chunk = request->pending->LastChunk();
	size_t offset = static_cast<size_t>(params[2]), length = static_cast<size_t>(params[4]);
	if (!chunk || offset != chunk->offset || length != chunk->bytes) {
		return 0;
	}
	return CopyOut(context, params[3], request->pending->Body().data() + offset, length) ? 1 : 0;
}

static cell_t GetDownloadProgressPct(IPluginContext* context, const cell_t* params) {
	HttpRequest* request = ReadRequest(context, params[1]);
	if (!request || request->IsBuilding()) {
		return 0;
	}
	cell_t* percent = PluginCellsArg(context, params[2], 1);
	if (!percent) {
		return 0;
	}
	*percent = sp_ftoc(request->pending->ProgressPercent());
	return 1;
}

static cell_t GetHTTPRequestWasTimedOut(IPluginContext* context, const cell_t* params) {
	HttpRequest* request = ReadRequest(context, params[1]);
	if (!request) {
		return 0;
	}
	cell_t* timedOut = PluginCellsArg(context, params[2], 1);
	if (!timedOut) {
		return 0;
	}
	*timedOut = request->pending && request->pending->TimedOut() ? 1 : 0;
	return 1;
}

static bool SetRawBody(HttpRequest& request, const std::string& contentType, std::string body) {
	if (!request.IsBuilding() || request.method == Method::Get || request.method == Method::Head ||
	    (!request.parameters.empty() && SendsParametersInBody(request.method))) {
		return false;
	}
	request.hasRawBody = true;
	request.rawBodyType = contentType;
	request.rawBody = std::move(body);
	return true;
}

static cell_t SetRawPostBody(IPluginContext* context, const cell_t* params) {
	HttpRequest* request = ReadRequest(context, params[1]);
	if (!request || params[4] < 0) {
		return 0;
	}
	std::optional<std::string> contentType = PluginStringArg(context, params[2]);
	if (!contentType) {
		return 0;
	}
	cell_t length = params[4];
	const char* body = length == 0 ? "" : PluginBytesArg(context, params[3], length);
	if (!body) {
		return 0;
	}
	return SetRawBody(*request, *contentType, std::string(body, static_cast<size_t>(length))) ? 1 : 0;
}

static cell_t SetRawPostBodyFromFile(IPluginContext* context, const cell_t* params) {
	HttpRequest* request = ReadRequest(context, params[1]);
	if (!request) {
		return 0;
	}
	std::optional<std::string> contentType = PluginStringArg(context, params[2]);
	std::optional<std::string> file = contentType ? PluginStringArg(context, params[3]) : std::nullopt;
	if (!file) {
		return 0;
	}
	char path[PLATFORM_MAX_PATH];
	smutils->BuildPath(Path_Game, path, sizeof(path), "%s", file->c_str());
	FILE* input = fopen(path, "rb");
	if (!input) {
		return context->ThrowNativeError("Unable to open %s for reading. errno: %d", path, errno);
	}
	std::string body;
	char chunk[65536];
	size_t read;
	while ((read = fread(chunk, 1, sizeof(chunk), input)) > 0) {
		body.append(chunk, read);
	}
	bool failed = ferror(input) != 0;
	fclose(input);
	if (failed || body.empty()) {
		return 0;
	}
	return SetRawBody(*request, *contentType, std::move(body)) ? 1 : 0;
}

static cell_t GetResponseBodyCallback(IPluginContext* context, const cell_t* params) {
	HttpRequest* request = ReadRequest(context, params[1]);
	if (!request) {
		return 0;
	}
	IPlugin* plugin = CallbackPlugin(context, params[4]);
	if (!plugin) {
		return 0;
	}
	IPluginFunction* callback = plugin->GetBaseContext()->GetFunctionById(params[2]);
	if (!callback) {
		return context->ThrowNativeError("Invalid function id (%X)", params[2]);
	}
	if (!request->HasBufferedBody()) {
		return 0;
	}
	std::string body = request->pending->Body();   // the callback may delete the request
	callback->PushStringEx(&body[0], body.size() + 1, SM_PARAM_STRING_BINARY | SM_PARAM_STRING_COPY, 0);
	callback->PushCell(params[3]);
	callback->PushCell(static_cast<cell_t>(body.size()));
	callback->Execute(nullptr);
	return 1;
}

static cell_t WriteResponseBodyToFile(IPluginContext* context, const cell_t* params) {
	HttpRequest* request = ReadRequest(context, params[1]);
	if (!request || !request->HasBufferedBody()) {
		return 0;
	}
	std::optional<std::string> file = PluginStringArg(context, params[2]);
	if (!file) {
		return 0;
	}
	char path[PLATFORM_MAX_PATH];
	smutils->BuildPath(Path_Game, path, sizeof(path), "%s", file->c_str());
	FILE* output = fopen(path, "wb");
	if (!output) {
		return context->ThrowNativeError("Unable to open %s for writing. errno: %d", path, errno);
	}
	const std::string& body = request->pending->Body();
	fwrite(body.data(), 1, body.size(), output);
	fclose(output);
	return 1;
}

static const sp_nativeinfo_t kHttpNatives[] = {
	{"SteamWorks_CreateHTTPRequest", CreateHTTPRequest},
	{"SteamWorks_SetHTTPRequestContextValue", SetContextValue},
	{"SteamWorks_SetHTTPRequestNetworkActivityTimeout", SetNetworkActivityTimeout},
	{"SteamWorks_SetHTTPRequestHeaderValue", SetHeaderValue},
	{"SteamWorks_SetHTTPRequestGetOrPostParameter", SetGetOrPostParameter},
	{"SteamWorks_SetHTTPRequestUserAgentInfo", SetUserAgentInfo},
	{"SteamWorks_SetHTTPRequestRequiresVerifiedCertificate", SetRequiresVerifiedCertificate},
	{"SteamWorks_SetHTTPRequestAbsoluteTimeoutMS", SetAbsoluteTimeoutMS},
	{"SteamWorks_SetHTTPCallbacks", SetCallbacks},
	{"SteamWorks_SendHTTPRequest", SendHTTPRequest},
	{"SteamWorks_SendHTTPRequestAndStreamResponse", SendHTTPRequestAndStreamResponse},
	{"SteamWorks_DeferHTTPRequest", ReorderRequest},
	{"SteamWorks_PrioritizeHTTPRequest", ReorderRequest},
	{"SteamWorks_GetHTTPResponseHeaderSize", GetResponseHeaderSize},
	{"SteamWorks_GetHTTPResponseHeaderValue", GetResponseHeaderValue},
	{"SteamWorks_GetHTTPResponseBodySize", GetResponseBodySize},
	{"SteamWorks_GetHTTPResponseBodyData", GetResponseBodyData},
	{"SteamWorks_GetHTTPStreamingResponseBodyData", GetStreamingResponseBodyData},
	{"SteamWorks_GetHTTPDownloadProgressPct", GetDownloadProgressPct},
	{"SteamWorks_GetHTTPRequestWasTimedOut", GetHTTPRequestWasTimedOut},
	{"SteamWorks_SetHTTPRequestRawPostBody", SetRawPostBody},
	{"SteamWorks_SetHTTPRequestRawPostBodyFromFile", SetRawPostBodyFromFile},
	{"SteamWorks_GetHTTPResponseBodyCallback", GetResponseBodyCallback},
	{"SteamWorks_WriteHTTPResponseBodyToFile", WriteResponseBodyToFile},
	{"SteamWorksHTTPRequest.SteamWorksHTTPRequest", CreateHTTPRequest},
	{"SteamWorksHTTPRequest.SetContextValue", SetContextValue},
	{"SteamWorksHTTPRequest.SetNetworkActivityTimeout", SetNetworkActivityTimeout},
	{"SteamWorksHTTPRequest.SetHeaderValue", SetHeaderValue},
	{"SteamWorksHTTPRequest.SetGetOrPostParameter", SetGetOrPostParameter},
	{"SteamWorksHTTPRequest.SetUserAgentInfo", SetUserAgentInfo},
	{"SteamWorksHTTPRequest.SetRequiresVerifiedCertificate", SetRequiresVerifiedCertificate},
	{"SteamWorksHTTPRequest.SetAbsoluteTimeoutMS", SetAbsoluteTimeoutMS},
	{"SteamWorksHTTPRequest.SetCallbacks", SetCallbacks},
	{"SteamWorksHTTPRequest.Send", SendHTTPRequest},
	{"SteamWorksHTTPRequest.SendAndStreamResponse", SendHTTPRequestAndStreamResponse},
	{"SteamWorksHTTPRequest.Defer", ReorderRequest},
	{"SteamWorksHTTPRequest.Prioritize", ReorderRequest},
	{"SteamWorksHTTPRequest.GetResponseHeaderSize", GetResponseHeaderSize},
	{"SteamWorksHTTPRequest.GetResponseHeaderValue", GetResponseHeaderValue},
	{"SteamWorksHTTPRequest.GetResponseBodySize", GetResponseBodySize},
	{"SteamWorksHTTPRequest.GetResponseBodyData", GetResponseBodyData},
	{"SteamWorksHTTPRequest.GetStreamingResponseBodyData", GetStreamingResponseBodyData},
	{"SteamWorksHTTPRequest.GetDownloadProgressPct", GetDownloadProgressPct},
	{"SteamWorksHTTPRequest.GetWasTimedOut", GetHTTPRequestWasTimedOut},
	{"SteamWorksHTTPRequest.SetRawPostBody", SetRawPostBody},
	{"SteamWorksHTTPRequest.SetRawPostBodyFromFile", SetRawPostBodyFromFile},
	{"SteamWorksHTTPRequest.GetResponseBodyCallback", GetResponseBodyCallback},
	{"SteamWorksHTTPRequest.WriteResponseBodyToFile", WriteResponseBodyToFile},
	{nullptr, nullptr},
};

// -------------------------------------------------------------- lifecycle

void HttpStart() {
	smrunner::ISMRunnerHost* host = Host();
	std::string fixtureDir = host->SteamWorksFixtureDir();
	std::vector<AnswerRule> mappingRules;
	if (!fixtureDir.empty()) {
		mappingRules = ReadMappingFile(fixtureDir);
	}
	std::vector<std::string> passthroughHosts;
	for (size_t i = 0; i < host->SteamWorksPassthroughHostCount(); i++) {
		passthroughHosts.push_back(host->SteamWorksPassthroughHost(i));
	}
	sAnswerer = std::make_unique<Answerer>(std::move(mappingRules), std::move(passthroughHosts),
	                                       host->SteamWorksRecordDir());
	sPassthrough = std::make_unique<Passthrough>();
	sHttpType = handlesys->CreateType("HTTPHandle", &sDispatch, 0, nullptr, nullptr, myself->GetIdentity(), nullptr);
	sharesys->AddNatives(myself, kHttpNatives);
}

void HttpStop() {
	// Removing the type frees every handle (OnHandleDestroy marks them).
	handlesys->RemoveType(sHttpType, myself->GetIdentity());
	sPassthrough->Stop();
	sRequests.clear();
	sPassthrough.reset();
	sAnswerer.reset();
}

Answerer& HttpAnswerer() {
	return *sAnswerer;
}

int PendingRequestCount() {
	return static_cast<int>(
		std::count_if(sRequests.begin(), sRequests.end(), [](const std::unique_ptr<HttpRequest>& request) {
			return !request->released && request->pending && !request->pending->IsDone();
		}));
}

}  // namespace steamworks
