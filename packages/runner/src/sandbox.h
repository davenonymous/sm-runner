// Copyright 2026 davenonymous
//
// The sandbox: a server folder made for one run, laid out like a game
// server's, so that SourceMod finds everything where it looks for it:
//   <root>/tf/addons/sourcemod/{configs,gamedata,translations}  from the
//       SourceMod release the runner ships (share/sourcemod/), copied so
//       that plugins and tests may change them,
//   <root>/tf/addons/sourcemod/extensions/  links to the given extensions and
//       SourceMod's Top Menus extension (lib/<arch>/), unless one is given,
//       and with --steamworks the SteamWorks stand-in (lib/<arch>/),
//   <root>/tf/addons/sourcemod/plugins/     links to the given plugins, a bare
//       name being one of SourceMod's stock plugins (share/sourcemod/plugins/),
//   <root>/tf/addons/sourcemod/{data,logs},
//   <root>/tf/cfg/sourcemod/                 SourceMod's cfg files (share/sourcemod/cfg/),
//   <root>/tf/maps/                          empty,
//   <root>/tf/bin/                           links to the stand-in game binaries,
// plus the files the command line asks for. The sandbox never removes its
// folder: a TemporaryFolder does, its one owner.
#ifndef SMRUNNER_SANDBOX_H
#define SMRUNNER_SANDBOX_H

#include <sys/types.h>

#include <string>
#include <utility>
#include <vector>

namespace smrunner {

// Who decides what happens to a sandbox's folder after the server stopped.
enum class SandboxOwnership {
	Removed,   // a temporary folder the runner made; its TemporaryFolder removes it
	Kept,      // a temporary folder the runner made and keeps (--keep-sandbox)
	Given,     // the user's folder (--sandbox); never removed
};

struct SandboxRoot {
	std::string path;
	SandboxOwnership ownership = SandboxOwnership::Given;

	// Whether the folder outlives the run, which the user is told.
	bool IsKept() const { return ownership != SandboxOwnership::Removed; }
};

// A new folder below $TMPDIR (or /tmp), made by the constructor. Unless kept,
// the destructor removes it, and so does a fatal error of the process that
// made it; a forked child that shares the object never removes it.
class TemporaryFolder {
 public:
	explicit TemporaryFolder(bool keep);
	~TemporaryFolder();
	TemporaryFolder(const TemporaryFolder&) = delete;
	TemporaryFolder& operator=(const TemporaryFolder&) = delete;

	SandboxRoot Root() const;

 private:
	std::string path_;
	bool keep_;
	pid_t owner_;   // the process that made the folder and alone removes it
};

struct SandboxSpec {
	std::string shareDir;                       // the runner's share/ folder
	std::string libDir;                         // the runner's lib/<arch>/ folder
	SandboxRoot root;
	std::vector<std::string> extensionDirs;     // laid out like extensions/ (with x64/)
	std::vector<std::string> extensionFiles;    // single .ext.so files
	bool steamworks = false;                    // the SteamWorks stand-in (lib/<arch>/)
	std::vector<std::string> plugins;           // .smx files or stock plugin names, in load order
	std::vector<std::pair<std::string, std::string>> files;   // source, destination below tf/
};

class Sandbox {
 public:
	// Builds the folder; fails loud.
	explicit Sandbox(const SandboxSpec& spec);
	Sandbox(const Sandbox&) = delete;
	Sandbox& operator=(const Sandbox&) = delete;

	const std::string& Root() const { return root_; }
	std::string GameDir() const { return root_ + "/tf"; }
	std::string SourceModDir() const { return root_ + "/tf/addons/sourcemod"; }

	// The plugins' file names below plugins/, in load order.
	const std::vector<std::string>& PluginNames() const { return pluginNames_; }

 private:
	void AddPlugins(const std::vector<std::string>& plugins, const std::string& stockPlugins);
	void AddExtensions(const SandboxSpec& spec);

	std::string root_;
	std::vector<std::string> pluginNames_;
};

// File helpers, failing loud.
void MakeDirectories(const std::string& path);
void CopyTree(const std::string& from, const std::string& to);
void CopyFile(const std::string& from, const std::string& to);
void RemoveTree(const std::string& path);
bool PathExists(const std::string& path);
bool IsDirectory(const std::string& path);
std::string AbsolutePath(const std::string& path);
std::string BaseName(const std::string& path);

}  // namespace smrunner

#endif
