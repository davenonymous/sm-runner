// Copyright 2026 davenonymous
#include "sandbox.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fstream>
#include <vector>

#include "diagnostics.h"

namespace smrunner {

bool PathExists(const std::string& path) {
	struct stat info;
	return lstat(path.c_str(), &info) == 0;
}

bool IsDirectory(const std::string& path) {
	struct stat info;
	return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

std::string AbsolutePath(const std::string& path) {
	char resolved[PATH_MAX];
	if (!realpath(path.c_str(), resolved)) {
		Fatal("%s: %s", path.c_str(), strerror(errno));
	}
	return resolved;
}

std::string BaseName(const std::string& path) {
	size_t slash = path.rfind('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

void MakeDirectories(const std::string& path) {
	for (size_t slash = path.find('/', 1); ; slash = path.find('/', slash + 1)) {
		std::string partial = slash == std::string::npos ? path : path.substr(0, slash);
		if (mkdir(partial.c_str(), 0775) != 0 && errno != EEXIST) {
			Fatal("could not create the folder %s: %s", partial.c_str(), strerror(errno));
		}
		if (slash == std::string::npos) {
			return;
		}
	}
}

void CopyFile(const std::string& from, const std::string& to) {
	std::ifstream in(from, std::ios::binary);
	if (!in) {
		Fatal("could not read %s", from.c_str());
	}
	size_t slash = to.rfind('/');
	if (slash != std::string::npos && slash > 0) {
		MakeDirectories(to.substr(0, slash));
	}
	std::ofstream out(to, std::ios::binary | std::ios::trunc);
	if (!out) {
		Fatal("could not write %s", to.c_str());
	}
	// Streaming an empty file sets the failbit; only copy what is there.
	if (in.peek() != std::ifstream::traits_type::eof()) {
		out << in.rdbuf();
	}
	if (!out) {
		Fatal("could not write %s", to.c_str());
	}
}

void CopyTree(const std::string& from, const std::string& to) {
	if (!IsDirectory(from)) {
		CopyFile(from, to);
		return;
	}
	MakeDirectories(to);
	DIR* dir = opendir(from.c_str());
	if (!dir) {
		Fatal("could not read the folder %s: %s", from.c_str(), strerror(errno));
	}
	while (dirent* entry = readdir(dir)) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		CopyTree(from + "/" + entry->d_name, to + "/" + entry->d_name);
	}
	closedir(dir);
}

void RemoveTree(const std::string& path) {
	struct stat info;
	if (lstat(path.c_str(), &info) != 0) {
		return;
	}
	if (S_ISDIR(info.st_mode)) {
		if (DIR* dir = opendir(path.c_str())) {
			while (dirent* entry = readdir(dir)) {
				if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
					RemoveTree(path + "/" + entry->d_name);
				}
			}
			closedir(dir);
		}
		rmdir(path.c_str());
		return;
	}
	unlink(path.c_str());
}

static void Link(const std::string& target, const std::string& link) {
	if (PathExists(link)) {
		Fatal("%s is given twice (as %s)", BaseName(link).c_str(), target.c_str());
	}
	if (symlink(target.c_str(), link.c_str()) != 0) {
		Fatal("could not link %s to %s: %s", link.c_str(), target.c_str(), strerror(errno));
	}
}

static std::string sFatalRemoval;
static pid_t sFatalRemover = 0;

static void RemoveOnFatal() {
	if (getpid() == sFatalRemover) {
		RemoveTree(sFatalRemoval);
	}
}

TemporaryFolder::TemporaryFolder(bool keep) : keep_(keep), owner_(getpid()) {
	const char* base = getenv("TMPDIR");
	std::string pattern = std::string(base && *base ? base : "/tmp") + "/smrunner.XXXXXX";
	std::vector<char> buffer(pattern.begin(), pattern.end());
	buffer.push_back('\0');
	if (!mkdtemp(buffer.data())) {
		Fatal("could not create a temporary folder from %s: %s", pattern.c_str(), strerror(errno));
	}
	path_ = buffer.data();
	if (keep_) {
		return;
	}
	sFatalRemoval = path_;
	sFatalRemover = owner_;
	SetFatalCleanup(RemoveOnFatal);
}

TemporaryFolder::~TemporaryFolder() {
	if (keep_ || getpid() != owner_) {
		return;
	}
	SetFatalCleanup(nullptr);
	RemoveTree(path_);
}

SandboxRoot TemporaryFolder::Root() const {
	return {path_, keep_ ? SandboxOwnership::Kept : SandboxOwnership::Removed};
}

Sandbox::Sandbox(const SandboxSpec& spec) {
	MakeDirectories(spec.root.path);
	root_ = AbsolutePath(spec.root.path);
	if (PathExists(root_ + "/tf/addons/sourcemod")) {
		Fatal("the sandbox %s is not empty; give a new folder", root_.c_str());
	}
	std::string sm = SourceModDir();
	std::string share = spec.shareDir + "/sourcemod";
	if (!IsDirectory(share + "/configs") || !IsDirectory(share + "/translations")) {
		Fatal("the runner's SourceMod files are missing from %s", share.c_str());
	}
	for (const char* folder : {"configs", "gamedata", "translations"}) {
		CopyTree(share + "/" + folder, sm + "/" + folder);
	}
	for (const char* folder : {"extensions/x64", "plugins", "data", "logs"}) {
		MakeDirectories(sm + "/" + folder);
	}
	MakeDirectories(GameDir() + "/cfg/sourcemod");
	if (IsDirectory(share + "/cfg")) {
		CopyTree(share + "/cfg", GameDir() + "/cfg/sourcemod");
	}
	MakeDirectories(GameDir() + "/maps");
	MakeDirectories(GameDir() + "/bin");
	for (const char* binary : {"server_srv.so", "engine_srv.so"}) {
		Link(spec.libDir + "/" + binary, GameDir() + "/bin/" + binary);
	}
	std::ofstream(GameDir() + "/cfg/server.cfg") << "// server.cfg of the smrunner sandbox\n";

	AddExtensions(spec);
	AddPlugins(spec.plugins, share + "/plugins");
	for (const auto& file : spec.files) {
		std::string destination = GameDir() + "/" + file.second;
		CopyTree(AbsolutePath(file.first), destination);
	}
	Note("sandbox in %s", root_.c_str());
}

void Sandbox::AddExtensions(const SandboxSpec& spec) {
	std::string extensions = SourceModDir() + "/extensions";
	for (const std::string& given : spec.extensionDirs) {
		std::string dir = AbsolutePath(given);
		for (const std::string& folder : {std::string(""), std::string("/x64")}) {
			DIR* listing = opendir((dir + folder).c_str());
			if (!listing) {
				if (folder.empty()) {
					Fatal("could not read the extension folder %s", dir.c_str());
				}
				continue;
			}
			while (dirent* entry = readdir(listing)) {
				std::string name = entry->d_name;
				if (name == "." || name == ".." || IsDirectory(dir + folder + "/" + name)) {
					continue;
				}
				Link(dir + folder + "/" + name, extensions + folder + "/" + name);
			}
			closedir(listing);
		}
	}
#if defined(__x86_64__)
	std::string ownFolder = extensions + "/x64";
#else
	std::string ownFolder = extensions;
#endif
	for (const std::string& file : spec.extensionFiles) {
		std::string path = AbsolutePath(file);
		Link(path, ownFolder + "/" + BaseName(path));
	}
	if (spec.steamworks) {
		std::string steamworks = ownFolder + "/SteamWorks.ext.so";
		if (PathExists(steamworks)) {
			Fatal("--steamworks and a SteamWorks extension of --extensions or --extension: give one of them");
		}
		Link(spec.libDir + "/SteamWorks.ext.so", steamworks);
	}
	// SourceMod ships Top Menus; a given one takes its place.
	std::string topMenus = ownFolder + "/topmenus.ext.so";
	if (!PathExists(topMenus)) {
		Link(spec.libDir + "/topmenus.ext.so", topMenus);
	}
}

// A plugin file, or a bare name of one of SourceMod's stock plugins.
static std::string PluginPath(const std::string& given, const std::string& stockPlugins) {
	if (PathExists(given) || given.find('/') != std::string::npos) {
		return AbsolutePath(given);
	}
	bool hasSuffix = given.size() >= 4 && given.compare(given.size() - 4, 4, ".smx") == 0;
	std::string stock = stockPlugins + "/" + given + (hasSuffix ? "" : ".smx");
	if (!PathExists(stock)) {
		Fatal("the plugin %s is neither a file nor one of SourceMod's stock plugins", given.c_str());
	}
	return stock;
}

void Sandbox::AddPlugins(const std::vector<std::string>& plugins, const std::string& stockPlugins) {
	for (const std::string& plugin : plugins) {
		std::string path = PluginPath(plugin, stockPlugins);
		std::string name = BaseName(path);
		if (name.size() < 5 || name.compare(name.size() - 4, 4, ".smx") != 0) {
			Fatal("%s is not a plugin (.smx)", plugin.c_str());
		}
		Link(path, SourceModDir() + "/plugins/" + name);
		pluginNames_.push_back(name);
	}
}

}  // namespace smrunner
