// Copyright 2026 davenonymous
#include "extension.h"

#include <stdexcept>

#include <curl/curl.h>

#include "http.h"
#include "steam.h"
#include "test_api.h"

namespace steamworks {

static StandIn sStandIn;
static smrunner::ISMRunnerHost* sHost = nullptr;

smrunner::ISMRunnerHost* Host() {
	return sHost;
}

void ConsoleLine(const std::string& text) {
	rootconsole->ConsolePrint("[SteamWorks] %s", text.c_str());
}

int CurrentFrame() {
	return sHost->FrameNumber();
}

static void OnGameFrame(bool simulating) {
	SteamFrame();
	HttpFrame();
}

bool StandIn::SDK_OnLoad(char* error, size_t maxlength, bool late) {
	if (!sharesys->RequestInterface(SMINTERFACE_SMRUNNERHOST_NAME, SMINTERFACE_SMRUNNERHOST_VERSION, myself,
	                                reinterpret_cast<SMInterface**>(&sHost))) {
		smutils->Format(error, maxlength, "smrunner's SteamWorks stand-in runs only inside smrunner");
		return false;
	}
	if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
		smutils->Format(error, maxlength, "libcurl did not start");
		return false;
	}
	try {
		HttpStart();
	} catch (const std::exception& problem) {
		smutils->Format(error, maxlength, "%s", problem.what());
		sHost->Warn((std::string("SteamWorks stand-in: ") + problem.what()).c_str());
		curl_global_cleanup();
		return false;
	}
	SteamStart();
	TestApiStart();
	sharesys->RegisterLibrary(myself, "SteamWorks");
	smutils->AddGameFrameHook(OnGameFrame);
	return true;
}

void StandIn::SDK_OnUnload() {
	smutils->RemoveGameFrameHook(OnGameFrame);
	HttpStop();
	SteamStop();
	curl_global_cleanup();
}

}  // namespace steamworks

SMEXT_LINK(&steamworks::sStandIn);
