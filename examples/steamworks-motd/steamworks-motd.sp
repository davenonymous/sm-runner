/**
 * Copyright 2026 davenonymous
 *
 * An example plugin for smrunner's SteamWorks stand-in: it fetches a
 * message of the day over HTTP with SteamWorks when the configs ran (and on
 * sm_motd_refresh), keeps the last one it got when a request fails, tells
 * it to every client that joins, and says how to connect with sm_serverinfo
 * from Steam's view of the server.
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <SteamWorks>

public Plugin myinfo = {
	name = "steamworks-motd",
	author = "davenonymous",
	description = "Message of the day over HTTP (smrunner example)",
	version = "1.0.0",
	url = ""
};

ConVar g_url;
char g_motd[256];

public void OnPluginStart() {
	g_url = CreateConVar("sm_motd_url", "http://motd.test/motd.txt", "Where the message of the day comes from");
	RegAdminCmd("sm_motd_refresh", Command_Refresh, ADMFLAG_CONFIG, "Fetches the message of the day again");
	RegConsoleCmd("sm_serverinfo", Command_ServerInfo, "How to connect to this server");
}

public void OnConfigsExecuted() {
	Fetch();
}

public Action Command_Refresh(int client, int args) {
	Fetch();
	ReplyToCommand(client, "[MOTD] Fetching the message of the day.");
	return Plugin_Handled;
}

void Fetch() {
	char url[256];
	g_url.GetString(url, sizeof(url));
	SteamWorksHTTPRequest request = new SteamWorksHTTPRequest(k_EHTTPMethodGET, url);
	request.SetHeaderValue("Accept", "text/plain");
	request.SetCallbacks(OnMotd);
	request.Send();
}

public void OnMotd(Handle hRequest, bool failure, bool successful, EHTTPStatusCode status) {
	SteamWorksHTTPRequest request = view_as<SteamWorksHTTPRequest>(hRequest);
	int size;
	if (failure || !successful || status != k_EHTTPStatusCode200OK || !request.GetResponseBodySize(size)
		|| size >= sizeof(g_motd)) {
		PrintToServer("[MOTD] The message of the day was not fetched (status %d); keeping the last one.", status);
		delete request;
		return;
	}
	request.GetResponseBodyData(g_motd, size);
	g_motd[size] = '\0';
	TrimString(g_motd);
	delete request;
}

public void OnClientPostAdminCheck(int client) {
	if (g_motd[0] != '\0' && !IsFakeClient(client)) {
		PrintToChat(client, "[MOTD] %s", g_motd);
	}
}

public Action Command_ServerInfo(int client, int args) {
	if (!SteamWorks_IsConnected()) {
		ReplyToCommand(client, "[MOTD] The server is not connected to Steam.");
		return Plugin_Handled;
	}
	int ip[4];
	if (!SteamWorks_GetPublicIP(ip)) {
		ReplyToCommand(client, "[MOTD] Steam does not know the server's address yet.");
		return Plugin_Handled;
	}
	ReplyToCommand(client, "[MOTD] connect %d.%d.%d.%d (%s)", ip[0], ip[1], ip[2], ip[3],
		SteamWorks_IsVACEnabled() ? "VAC secured" : "not VAC secured");
	return Plugin_Handled;
}
