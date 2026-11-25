// Copyright 2026 davenonymous
//
// SourceMod's extension configuration of the SteamWorks stand-in: named and
// filed like SteamWorks, so plugins that require SteamWorks load it.
#ifndef _INCLUDE_SOURCEMOD_EXTENSION_CONFIG_H_
#define _INCLUDE_SOURCEMOD_EXTENSION_CONFIG_H_

#define SMEXT_CONF_NAME "SteamWorks"
#define SMEXT_CONF_DESCRIPTION "smrunner's stand-in for SteamWorks 1.2.167: faked HTTP, server and client state"
#define SMEXT_CONF_VERSION "1.2.167-smrunner"
#define SMEXT_CONF_AUTHOR "davenonymous"
#define SMEXT_CONF_URL ""
#define SMEXT_CONF_LOGTAG "SteamWorks"
#define SMEXT_CONF_LICENSE "GPL"
#define SMEXT_CONF_DATESTRING __DATE__

#define SMEXT_LINK(name) SDKExtension* g_pExtensionIface = name;

#define SMEXT_ENABLE_FORWARDSYS
#define SMEXT_ENABLE_HANDLESYS
#define SMEXT_ENABLE_PLAYERHELPERS
#define SMEXT_ENABLE_TIMERSYS
#define SMEXT_ENABLE_PLUGINSYS
#define SMEXT_ENABLE_ROOTCONSOLEMENU

#endif
