// Copyright 2026 davenonymous
//
// The game server's Steam state and its clients' side, faked: connected to
// Steam, VAC secured, public IP 127.0.0.1 and no fake IP by default, with
// SteamWorks_SteamServersConnected on the first frame of the first map;
// group status, licenses and Steam IDs of the simulated clients. What
// plugins set (rules, description, map name, game data, advertising) is
// kept for tests. GC messages and user stats are not faked: their natives
// fail with a native error naming them.
#ifndef SMRUNNER_STEAMWORKS_STEAM_H
#define SMRUNNER_STEAMWORKS_STEAM_H

#include <map>
#include <string>

namespace steamworks {

// SteamWorks' EUserHasLicenseForAppResult.
enum class License { Has = 0, DoesNotHave = 1, NoAuth = 2 };

struct ServerState {
	bool connected = true;
	bool vacSecured = true;
	unsigned int publicIp = 0x7F000001;   // host order; 0: none
	unsigned int fakeIp = 0;              // 0: none
	int fakePort = 0;
	std::map<std::string, std::string> rules;
	std::string gameDescription;
	std::string mapName;
	std::string gameData;
	bool advertising = true;
};

struct GroupStatus {
	bool member = false;
	bool officer = false;
};

void SteamStart();
void SteamStop();
// Once per game frame.
void SteamFrame();

// The test API's view.
ServerState& Server();
void SetGroupStatus(unsigned int account, unsigned int group, GroupStatus status);
void SetLicense(unsigned int account, int app, License license);
void FireServersConnected();
void FireServersConnectFailure(int result);
void FireServersDisconnected(int result);
void FireValidateClient(unsigned int owner, unsigned int account);
void FireClientGroupStatus(unsigned int account, unsigned int group, GroupStatus status);
// The token plugins wrote into the forward's buffer.
std::string FireTokenRequested();
// Whether a plugin handled the restart request (Plugin_Handled or more).
bool FireRestartRequested();

}  // namespace steamworks

#endif
