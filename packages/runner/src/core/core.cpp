// Copyright 2026 davenonymous
//
// The core side of SourceMod for the runner. The startup, map and frame
// sequences follow core/sourcemod.cpp, core/logic_bridge.cpp,
// core/frame_hooks.cpp and core/HalfLife2.cpp of SourceMod 1.12 (GPLv3,
// AlliedModders LLC).
#include "core/core.h"
#include "core/runner_host.h"

#include <IGameHelpers.h>
#include <IMenuManager.h>
#include <bridge/include/BridgeAPI.h>
#include <bridge/include/IFileSystemBridge.h>
#include <bridge/include/IPlayerInfoBridge.h>
#include <bridge/include/IVEngineServerBridge.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <sourcemod_version.h>
#include <fnmatch.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <algorithm>
#include <map>
#include <mutex>

#include "core/concmds.h"
#include "core/convars.h"
#include "core/coreconfig.h"
#include "core/menu_radio.h"
#include "core/menus.h"
#include "core/nextmap.h"
#include "core/players.h"
#include "core/sourcemod_api.h"
#include "core/timers.h"
#include "core/usermsgs.h"
#include "diagnostics.h"
#include "testing/plugin_memory.h"

using namespace SourceMod;
using namespace SourcePawn;

// ------------------------------------------------------- the core's globals

SMGlobalClass* SMGlobalClass::head = nullptr;
ISourcePawnEngine* g_pSourcePawn = nullptr;
ISourcePawnEngine2* g_pSourcePawn2 = nullptr;
IdentityToken_t* g_pCoreIdent = nullptr;
IThreader* g_pThreader = nullptr;
ITextParsers* textparsers = nullptr;

sm_logic_t logicore;
IShareSys* sharesys = nullptr;
IHandleSys* handlesys = nullptr;
IForwardManager* forwardsys = nullptr;
IScriptManager* scripts = nullptr;
IExtensionSys* extsys = nullptr;
IAdminSystem* adminsys = nullptr;
ILogger* logger = nullptr;
IRootConsole* rootmenu = nullptr;
ITranslator* translator = nullptr;

void CoreNativesToAdd::OnSourceModAllInitialized() {
	logicore.AddNatives(m_NativeList);
}

namespace smrunner {

// ------------------------------------------------------------------ bridges

class EngineBridge : public IVEngineServerBridge {
 public:
	bool IsDedicatedServer() override { return true; }
	void InsertServerCommand(const char* command) override { Core::Get().engine().console().InsertIntoBuffer(command); }
	void ServerCommand(const char* command) override { Core::Get().engine().console().AppendToBuffer(command); }
	void ServerExecute() override { Core::Get().engine().console().ExecuteBuffer(); }
	// CVEngineServer::GetClientConVarValue: "" for an index without a slot
	// and for a cvar the client does not have.
	const char* GetClientConVarValue(int client, const char* name) override {
		SimulatedClient* slot = Core::Get().engine().clients().Slot(client);
		return slot ? slot->GetConVar(name) : "";
	}
	void ClientCommand(edict_t* edict, const char* command) override {
		Core::Get().engine().clients().SendCommand(edict ? edict->index : 0, command);
	}
	void FakeClientCommand(edict_t* edict, const char* command) override {
		Core::Get().players().ExecuteClientCommand(edict ? edict->index : 0, command);
	}
};

// The Valve file system on the game folder alone: every path ID means the
// mod folder, as on a server without custom content.
class FileSystemBridge : public IFileSystemBridge {
 public:
	const char* FindFirstEx(const char* wildcard, const char*, FileFindHandle_t* handle) override {
		std::string pattern = wildcard;
		size_t slash = pattern.rfind('/');
		std::string folder = slash == std::string::npos ? "" : pattern.substr(0, slash);
		std::string mask = slash == std::string::npos ? pattern : pattern.substr(slash + 1);
		Search search;
		search.folder = Resolve(folder);
		if (DIR* dir = opendir(search.folder.c_str())) {
			while (dirent* entry = readdir(dir)) {
				if (fnmatch(mask.c_str(), entry->d_name, 0) == 0) {
					search.names.push_back(entry->d_name);
				}
			}
			closedir(dir);
		}
		if (search.names.empty()) {
			*handle = -1;
			return nullptr;
		}
		int id = nextSearch_++;
		searches_[id] = search;
		*handle = id;
		return searches_[id].names[0].c_str();
	}
	const char* FindNext(FileFindHandle_t handle) override {
		auto found = searches_.find(handle);
		if (found == searches_.end() || ++found->second.position >= found->second.names.size()) {
			return nullptr;
		}
		return found->second.names[found->second.position].c_str();
	}
	bool FindIsDirectory(FileFindHandle_t handle) override {
		auto found = searches_.find(handle);
		if (found == searches_.end() || found->second.position >= found->second.names.size()) {
			return false;
		}
		struct stat info;
		std::string path = found->second.folder + "/" + found->second.names[found->second.position];
		return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
	}
	void FindClose(FileFindHandle_t handle) override { searches_.erase(handle); }
	FileHandle_t Open(const char* name, const char* options, const char*) override {
		return fopen(Resolve(name).c_str(), options);
	}
	void Close(FileHandle_t file) override { fclose(static_cast<FILE*>(file)); }
	char* ReadLine(char* output, int maxChars, FileHandle_t file) override {
		return fgets(output, maxChars, static_cast<FILE*>(file));
	}
	bool EndOfFile(FileHandle_t file) override { return feof(static_cast<FILE*>(file)) != 0; }
	bool FileExists(const char* name, const char*) override {
		struct stat info;
		return stat(Resolve(name).c_str(), &info) == 0;
	}
	unsigned int Size(FileHandle_t file) override {
		FILE* stream = static_cast<FILE*>(file);
		long position = ftell(stream);
		fseek(stream, 0, SEEK_END);
		long size = ftell(stream);
		fseek(stream, position, SEEK_SET);
		return static_cast<unsigned int>(size);
	}
	unsigned int Size(const char* name, const char*) override {
		struct stat info;
		return stat(Resolve(name).c_str(), &info) == 0 ? static_cast<unsigned int>(info.st_size) : 0;
	}
	int Read(void* output, int size, FileHandle_t file) override {
		return static_cast<int>(fread(output, 1, size, static_cast<FILE*>(file)));
	}
	int Write(const void* input, int size, FileHandle_t file) override {
		return static_cast<int>(fwrite(input, 1, size, static_cast<FILE*>(file)));
	}
	void Seek(FileHandle_t file, int position, int seekType) override {
		fseek(static_cast<FILE*>(file), position, seekType);
	}
	unsigned int Tell(FileHandle_t file) override { return static_cast<unsigned int>(ftell(static_cast<FILE*>(file))); }
	int FPrint(FileHandle_t file, const char* data) override { return fputs(data, static_cast<FILE*>(file)); }
	void Flush(FileHandle_t file) override { fflush(static_cast<FILE*>(file)); }
	bool IsOk(FileHandle_t file) override { return file != nullptr && !ferror(static_cast<FILE*>(file)); }
	void RemoveFile(const char* path, const char*) override { remove(Resolve(path).c_str()); }
	void RenameFile(const char* oldPath, const char* newPath, const char*) override {
		rename(Resolve(oldPath).c_str(), Resolve(newPath).c_str());
	}
	bool IsDirectory(const char* name, const char*) override {
		struct stat info;
		return stat(Resolve(name).c_str(), &info) == 0 && S_ISDIR(info.st_mode);
	}
	void CreateDirHierarchy(const char* path, const char*) override {
		std::string full = Resolve(path);
		for (size_t slash = full.find('/', 1); slash != std::string::npos; slash = full.find('/', slash + 1)) {
			mkdir(full.substr(0, slash).c_str(), 0775);
		}
		mkdir(full.c_str(), 0775);
	}
	// The game folder for content; GAMEBIN and EXECUTABLE_PATH both name its
	// bin/ folder, which holds the stand-in game binaries.
	int GetSearchPath(const char* pathID, bool, char* path, int maxLength) override {
		std::string folder = Core::Get().engine().config().gameDir + "/";
		if (pathID && (strcmp(pathID, "GAMEBIN") == 0 || strcmp(pathID, "EXECUTABLE_PATH") == 0)) {
			folder += "bin/";
		}
		ke::SafeStrcpy(path, maxLength, folder.c_str());
		return static_cast<int>(folder.size()) + 1;
	}

 private:
	struct Search {
		std::string folder;
		std::vector<std::string> names;
		size_t position = 0;
	};
	static std::string Resolve(const std::string& path) {
		if (!path.empty() && path[0] == '/') {
			return path;
		}
		return Core::Get().engine().GamePath(path);
	}
	std::map<int, Search> searches_;
	int nextSearch_ = 1;
};

class PlayerInfoBridge : public IPlayerInfoBridge {
 public:
	bool IsObserver(IPlayerInfo* info) override { return info->observer; }
	int GetTeamIndex(IPlayerInfo* info) override { return info->team; }
	int GetFragCount(IPlayerInfo* info) override { return info->frags; }
	int GetDeathCount(IPlayerInfo* info) override { return info->deaths; }
	int GetArmorValue(IPlayerInfo* info) override { return info->armor; }
	void GetAbsOrigin(IPlayerInfo* info, float* x, float* y, float* z) override { Copy(info->origin, x, y, z); }
	void GetAbsAngles(IPlayerInfo* info, float* x, float* y, float* z) override { Copy(info->angles, x, y, z); }
	void GetPlayerMins(IPlayerInfo* info, float* x, float* y, float* z) override { Copy(info->mins, x, y, z); }
	void GetPlayerMaxs(IPlayerInfo* info, float* x, float* y, float* z) override { Copy(info->maxs, x, y, z); }
	const char* GetWeaponName(IPlayerInfo* info) override { return info->weapon.c_str(); }
	const char* GetModelName(IPlayerInfo* info) override { return info->model.c_str(); }
	int GetHealth(IPlayerInfo* info) override { return info->health; }
	void ChangeTeam(IPlayerInfo* info, int team) override { info->team = team; }

 private:
	static void Copy(const float* from, float* x, float* y, float* z) {
		*x = from[0];
		*y = from[1];
		*z = from[2];
	}
};

// ------------------------------------------------------------ game helpers

// IGameHelpers without entities, send tables or data maps: the simulated
// server has no game DLL. What it has: text messages to clients (as user
// messages), map names, delayed kicks and entity references of client
// indexes.
class GameHelpers : public IGameHelpers {
 public:
	SendProp* FindInSendTable(const char*, const char*) override { return nullptr; }
	ServerClass* FindServerClass(const char*) override { return nullptr; }
	typedescription_t* FindInDataMap(datamap_t*, const char*) override { return nullptr; }
	datamap_t* GetDataMap(CBaseEntity*) override { return nullptr; }
	void SetEdictStateChanged(edict_t*, unsigned short) override {}
	bool TextMsg(int client, int dest, const char* message) override { return SendTextMsg(client, dest, message); }
	bool IsLANServer() override {
		ConVar* lan = Core::Get().engine().console().FindConVar("sv_lan");
		return lan && lan->GetBool();
	}
	bool FindSendPropInfo(const char*, const char*, sm_sendprop_info_t*) override { return false; }
	edict_t* EdictOfIndex(int index) override {
		SimPlayer* player = Core::Get().players().Player(index);
		return player ? player->GetEdict() : nullptr;
	}
	int IndexOfEdict(edict_t* edict) override { return edict ? edict->index : -1; }
	edict_t* GetHandleEntity(CBaseHandle&) override { return nullptr; }
	void SetHandleEntity(CBaseHandle&, edict_t*) override {}
	const char* GetCurrentMap() override { return Core::Get().engine().MapName().c_str(); }
	void ServerCommand(const char* buffer) override { Core::Get().engine().console().AppendToBuffer(buffer); }
	CBaseEntity* ReferenceToEntity(cell_t) override { return nullptr; }
	cell_t EntityToReference(CBaseEntity*) override { return -1; }
	cell_t EntityToBCompatRef(CBaseEntity*) override { return -1; }
	cell_t IndexToReference(int index) override { return index; }
	int ReferenceToIndex(cell_t reference) override {
		if (reference == -1) {
			return -1;
		}
		if (reference & (1 << 31)) {
			return reference & 0xFFF;
		}
		return reference;
	}
	cell_t ReferenceToBCompatRef(cell_t reference) override { return reference; }
	void* GetGlobalEntityList() override { return nullptr; }
	void AddDelayedKick(int client, int userid, const char* message) override {
		Core::Get().players().AddDelayedKick(client, userid, message);
	}
	int GetSendPropOffset(SendProp*) override { return -1; }
	bool HintTextMsg(int client, const char* message) override { return SendHintText(client, message); }
	ICommandLine* GetValveCommandLine() override { return nullptr; }
	const char* GetEntityClassname(edict_t* edict) override { return edict ? "player" : nullptr; }
	const char* GetEntityClassname(CBaseEntity*) override { return nullptr; }
	bool IsMapValid(const char* map) override { return Core::Get().engine().MapExists(map); }
	bool FindDataMapInfo(datamap_t*, const char*, sm_datatable_info_t*) override { return false; }
	bool GetServerSteam3Id(char* out, size_t length) const override {
		ke::SafeStrcpy(out, length, "[G:1:1]");
		return true;
	}
	uint64_t GetServerSteamId64() const override { return 85568392920039425ULL; }
	ServerClass* FindEntityServerClass(CBaseEntity*) override { return nullptr; }
};

// ----------------------------------------------------------- CoreProvider

static ServerGlobals sServerGlobals;

static const char* GetCoreConfigValueCallback(const char* key) {
	return Core::Get().config().GetCoreConfigValue(key);
}
static void DoGlobalPluginLoadsCallback() {
	Core::Get().LoadPlugins();
}
static bool AreConfigsExecutedCallback() {
	return Core::Get().config().AreConfigsExecuted();
}
static void ExecuteConfigsCallback(IPluginContext* context) {
	Core::Get().config().ExecuteForPlugin(context);
}
static void GetDBInfoFromKeyValuesCallback(KeyValues*, DatabaseInfo*) {
	Fatal("database configurations from KeyValues are not supported by smrunner");
}
static int GetActivityFlagsCallback() {
	ConVar* activity = Core::Get().engine().console().FindConVar("sm_show_activity");
	return activity ? activity->GetInt() : 13;
}
static int GetImmunityModeCallback() {
	ConVar* mode = Core::Get().engine().console().FindConVar("sm_immunity_mode");
	return mode ? mode->GetInt() : 1;
}
static void UpdateAdminCmdFlagsCallback(const char* command, OverrideType type, FlagBits bits, bool remove) {
	Core::Get().commands().UpdateAdminCmdFlags(command, type, bits, remove);
}
static bool LookForCommandAdminFlagsCallback(const char* command, FlagBits* flags) {
	return Core::Get().commands().LookForCommandAdminFlags(command, flags);
}
static int GetGlobalTargetCallback() {
	return static_cast<int>(Core::Get().sourcemod().GetGlobalTarget());
}

class CoreProviderImpl : public CoreProvider {
 public:
	CoreProviderImpl(ISourceMod* sourcemod, ITimerSystem* timers, IPlayerManager* players, IGameHelpers* helpers,
	                 IMenuManager* menuManager) {
		sm = sourcemod;
		engine = &engineBridge_;
		filesystem = &fileSystemBridge_;
		playerInfo = &playerInfoBridge_;
		timersys = timers;
		playerhelpers = players;
		gamehelpers = helpers;
		menus = menuManager;
		spe1 = &g_pSourcePawn;
		spe2 = &g_pSourcePawn2;
		gamesuffix = "2.tf2";
		serverGlobals = &sServerGlobals;
		listeners = nullptr;
		GetCoreConfigValue = GetCoreConfigValueCallback;
		DoGlobalPluginLoads = DoGlobalPluginLoadsCallback;
		AreConfigsExecuted = AreConfigsExecutedCallback;
		ExecuteConfigs = ExecuteConfigsCallback;
		GetDBInfoFromKeyValues = GetDBInfoFromKeyValuesCallback;
		GetActivityFlags = GetActivityFlagsCallback;
		GetImmunityMode = GetImmunityModeCallback;
		UpdateAdminCmdFlags = UpdateAdminCmdFlagsCallback;
		LookForCommandAdminFlags = LookForCommandAdminFlagsCallback;
		GetGlobalTarget = GetGlobalTargetCallback;
	}

	ConVar* FindConVar(const char* name) override { return Core::Get().engine().console().FindConVar(name); }
	const char* GetCvarString(ConVar* cvar) override { return cvar->GetString(); }
	bool GetCvarBool(ConVar* cvar) override { return cvar->GetBool(); }
	void DefineCommand(const char* name, const char* help, const CommandFunc& callback) override {
		CommandFunc handler = callback;
		Core::Get().engine().console().AddCommand({name, help, 0, [handler](int client, const CommandArgs& args) {
			                                           CommandArgsAdapter adapter(args);
			                                           handler(client, &adapter);
		                                           }});
	}
	bool GetGameName(char* buffer, size_t maxlength) override {
		ke::SafeStrcpy(buffer, maxlength, Core::Get().engine().config().gameDescription.c_str());
		return true;
	}
	const char* GetGameDescription() override { return Core::Get().engine().config().gameDescription.c_str(); }
	const char* GetSourceEngineName() override { return "tf2"; }
	bool SymbolsAreHidden() override { return true; }
	bool IsMapLoading() override { return Core::Get().IsMapLoading(); }
	bool IsMapRunning() override { return Core::Get().IsMapStarted(); }
	int MaxClients() override { return Core::Get().players().GetMaxClients(); }
	bool DescribePlayer(int index, const char** namep, const char** authp, int* useridp) override {
		SimPlayer* player = Core::Get().players().Player(index);
		if (!player || !player->IsConnected()) {
			return false;
		}
		if (namep) {
			*namep = player->GetName();
		}
		if (authp) {
			const char* auth = player->GetAuthString(false);
			*authp = (auth && *auth) ? auth : "STEAM_ID_PENDING";
		}
		if (useridp) {
			*useridp = player->GetUserId();
		}
		return true;
	}
	void LogToGame(const char* message) override { Core::Get().engine().console().Printf("L %s", message); }
	void ConPrint(const char* message) override { Core::Get().engine().console().Print(message); }
	void ConsolePrint(const char* format, ...) override {
		va_list ap;
		va_start(ap, format);
		ConsolePrintVa(format, ap);
		va_end(ap);
	}
	// UTIL_ConsolePrintVa: one line with a line break added.
	void ConsolePrintVa(const char* format, va_list ap) override {
		char buffer[3072];
		size_t length = ke::SafeVsprintf(buffer, sizeof(buffer) - 2, format, ap);
		buffer[length++] = '\n';
		buffer[length] = '\0';
		Core::Get().engine().console().Print(buffer);
	}
	void FormatSourceBinaryName(const char* basename, char* buffer, size_t maxlength) override {
		bool prefix = strcasecmp(basename, "tier0") == 0 || strcasecmp(basename, "vstdlib") == 0;
		ke::SafeSprintf(buffer, maxlength, "%s%s_srv.so", prefix ? "lib" : "", basename);
	}
	bool IsClientConVarQueryingSupported() override { return true; }
	int QueryClientConVar(int, const char*) override { return -1; }
	int LoadMMSPlugin(const char* file, bool* ok, char* error, size_t maxlength) override {
		*ok = false;
		ke::SafeSprintf(error, maxlength, "Metamod:Source plugins are not supported by smrunner (%s)", file);
		return 0;
	}
	void UnloadMMSPlugin(int) override {}

 private:
	class CommandArgsAdapter : public ICommandArgs {
	 public:
		explicit CommandArgsAdapter(const CommandArgs& args) : args_(args) {}
		const char* Arg(int n) const override { return args_.Arg(n); }
		int ArgC() const override { return args_.ArgC(); }
		const char* ArgS() const override { return args_.ArgS(); }

	 private:
		const CommandArgs& args_;
	};

	EngineBridge engineBridge_;
	FileSystemBridge fileSystemBridge_;
	PlayerInfoBridge playerInfoBridge_;
};

// -------------------------------------------------------------- ISourceMod

SourceModImpl::SourceModImpl(const std::string& gameDir, const std::string& smDir)
	: gameDir_(gameDir), smDir_(smDir) {
	size_t slash = gameDir_.rfind('/');
	gameFolder_ = slash == std::string::npos ? gameDir_ : gameDir_.substr(slash + 1);
}

size_t SourceModImpl::BuildPath(PathType type, char* buffer, size_t maxlength, const char* format, ...) {
	char path[PLATFORM_MAX_PATH];
	va_list ap;
	va_start(ap, format);
	vsnprintf(path, sizeof(path), format, ap);
	va_end(ap);
	// ke::path::Format turns backslashes into slashes on Linux.
	for (char* c = path; *c; c++) {
		if (*c == '\\') {
			*c = '/';
		}
	}
	if (type != Path_SM_Rel && strncmp(path, "file://", 7) == 0) {
		return ke::SafeSprintf(buffer, maxlength, "%s", &path[7]);
	}
	const char* base = nullptr;
	if (type == Path_Game) {
		base = gameDir_.c_str();
	} else if (type == Path_SM) {
		base = smDir_.c_str();
	} else if (type == Path_SM_Rel) {
		base = RelativeSourceModPath();
	}
	if (base) {
		return ke::SafeSprintf(buffer, maxlength, "%s/%s", base, path);
	}
	return ke::SafeSprintf(buffer, maxlength, "%s", path);
}

void SourceModImpl::LogMessage(IExtension* extension, const char* format, ...) {
	char buffer[2048];
	va_list ap;
	va_start(ap, format);
	vsnprintf(buffer, sizeof(buffer), format, ap);
	va_end(ap);
	const char* tag = extension ? extension->GetAPI()->GetExtensionTag() : nullptr;
	if (tag) {
		logger->LogMessage("[%s] %s", tag, buffer);
	} else {
		logger->LogMessage("%s", buffer);
	}
}

void SourceModImpl::LogError(IExtension* extension, const char* format, ...) {
	char buffer[2048];
	va_list ap;
	va_start(ap, format);
	vsnprintf(buffer, sizeof(buffer), format, ap);
	va_end(ap);
	const char* tag = extension ? extension->GetAPI()->GetExtensionTag() : nullptr;
	if (tag) {
		logger->LogError("[%s] %s", tag, buffer);
	} else {
		logger->LogError("%s", buffer);
	}
}

size_t SourceModImpl::FormatString(char* buffer, size_t maxlength, IPluginContext* context, const cell_t* params,
                                   unsigned int param) {
	char* format = PluginCharsArg(context, params[param]);
	if (!format) {
		if (maxlength > 0) {
			buffer[0] = '\0';
		}
		return 0;
	}
	int next = static_cast<int>(++param);
	return logicore.atcprintf(buffer, maxlength, format, context, params, &next);
}

KeyValues* SourceModImpl::ReadKeyValuesHandle(Handle_t, HandleError* error, bool) {
	if (error) {
		*error = HandleError_Type;
	}
	return nullptr;
}

time_t SourceModImpl::GetAdjustedTime() {
	ConVar* adjustment = Core::Get().engine().console().FindConVar("sm_time_adjustment");
	return time(nullptr) + (adjustment ? adjustment->GetInt() : 0);
}

unsigned int SourceModImpl::SetGlobalTarget(unsigned int index) {
	unsigned int old = target_;
	target_ = index;
	return old;
}

void SourceModImpl::AddGameFrameHook(GAME_FRAME_HOOK hook) {
	frameHooks_.push_back(hook);
}

void SourceModImpl::RemoveGameFrameHook(GAME_FRAME_HOOK hook) {
	for (auto it = frameHooks_.begin(); it != frameHooks_.end(); ++it) {
		if (*it == hook) {
			frameHooks_.erase(it);
			return;
		}
	}
}

void SourceModImpl::ProcessGameFrameHooks(bool simulating) {
	// A hook may remove itself; run over a copy.
	std::vector<GAME_FRAME_HOOK> hooks = frameHooks_;
	for (GAME_FRAME_HOOK hook : hooks) {
		hook(simulating);
	}
}

size_t SourceModImpl::Format(char* buffer, size_t maxlength, const char* format, ...) {
	va_list ap;
	va_start(ap, format);
	size_t length = FormatArgs(buffer, maxlength, format, ap);
	va_end(ap);
	return length;
}

size_t SourceModImpl::FormatArgs(char* buffer, size_t maxlength, const char* format, va_list ap) {
	return ke::SafeVsprintf(buffer, maxlength, format, ap);
}

void SourceModImpl::AddFrameAction(FRAMEACTION action, void* data) {
	Core::Get().AddFrameAction(action, data);
}

const char* SourceModImpl::GetCoreConfigValue(const char* key) {
	return Core::Get().config().GetCoreConfigValue(key);
}

bool SourceModImpl::IsMapRunning() {
	return Core::Get().IsMapStarted();
}

// -------------------------------------------------------------------- Core

static std::mutex sFrameActionMutex;

Core& Core::Get() {
	// Never destroyed: extensions may still run threads when the process exits.
	static Core* core = new Core();
	return *core;
}

Core::Core() = default;

void Core::NotifyGlobalClasses(const std::function<void(SMGlobalClass*)>& call) {
	for (SMGlobalClass* global = SMGlobalClass::head; global; global = global->m_pGlobalClassNext) {
		call(global);
	}
}

void Core::LoadVm() {
	std::string path = options_.libDir + "/libsourcepawn.so";
	vmLibrary_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
	if (!vmLibrary_) {
		Fatal("could not load the SourcePawn VM %s: %s", path.c_str(), dlerror());
	}
	auto factoryFn = reinterpret_cast<GetSourcePawnFactoryFn>(dlsym(vmLibrary_, "GetSourcePawnFactory"));
	ISourcePawnFactory* factory = factoryFn ? factoryFn(SOURCEPAWN_API_VERSION) : nullptr;
	if (!factory) {
		Fatal("the SourcePawn VM %s is out of date", path.c_str());
	}
	setNativeBindingListener_ = reinterpret_cast<void (*)(NativeBindingListener)>(
		dlsym(vmLibrary_, "SmRunnerSetNativeBindingListener"));
	if (!setNativeBindingListener_) {
		Fatal("the SourcePawn VM %s was built without smrunner's patches (patches/)", path.c_str());
	}
	pawnEnv_ = factory->NewEnvironment();
	if (!pawnEnv_) {
		Fatal("could not create a SourcePawn environment");
	}
	g_pSourcePawn = pawnEnv_->APIv1();
	g_pSourcePawn2 = pawnEnv_->APIv2();
	// Breakpoints need the VM's debug break, which must be on before any
	// plugin is loaded.
	if (!pawnEnv_->EnableDebugBreak()) {
		Fatal("the SourcePawn VM refused to enable debug breaks");
	}
	g_pSourcePawn2->SetJitEnabled(options_.jit);
	pawnEnv_->SetDebugMetadataFlags(0);
}

void Core::LoadLogic() {
	std::string path = options_.libDir + "/sourcemod.logic.so";
	logicLibrary_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
	if (!logicLibrary_) {
		Fatal("could not load SourceMod's logic library %s: %s", path.c_str(), dlerror());
	}
	auto load = reinterpret_cast<LogicLoadFunction>(dlsym(logicLibrary_, "logic_load"));
	auto getTextParsers = reinterpret_cast<GetITextParsers>(dlsym(logicLibrary_, "get_textparsers"));
	if (!load || !getTextParsers) {
		Fatal("%s is not SourceMod's logic library", path.c_str());
	}
	LogicInitFunction init = load(SM_LOGIC_MAGIC);
	if (!init) {
		Fatal("%s does not match the runner's SourceMod version (component version mismatch)", path.c_str());
	}
	textparsers = getTextParsers();

	// The engine's own globals; the logic library only reads them, through
	// the bridge's non-const pointers.
	const EngineGlobals& globals = engine_->globals();
	sServerGlobals.universalTime = timers_->UniversalTime();
	sServerGlobals.frametime = const_cast<float*>(&globals.frametime);
	sServerGlobals.interval_per_tick = const_cast<float*>(&globals.interval_per_tick);
	provider_->listeners = SMGlobalClass::head;
	init(provider_.get(), &logicore);

	// Join the logic library's global classes to the core's.
	SMGlobalClass* last = SMGlobalClass::head;
	while (last->m_pGlobalClassNext) {
		last = last->m_pGlobalClassNext;
	}
	last->m_pGlobalClassNext = logicore.head;

	g_pThreader = logicore.threader;
	translator = logicore.translator;
	scripts = logicore.scripts;
	sharesys = logicore.sharesys;
	extsys = logicore.extsys;
	g_pCoreIdent = logicore.core_ident;
	handlesys = logicore.handlesys;
	forwardsys = logicore.forwardsys;
	adminsys = logicore.adminsys;
	logger = logicore.logger;
	rootmenu = logicore.rootmenu;
}

// The cvars SourceMod's core defines (ConVar globals of core/*.cpp), with
// their flags, help texts and bounds.
static void AddCoreConVars(Console& console) {
	struct Definition {
		const char* name;
		const char* value;
		int flags;
		const char* help;
		bool hasMin = false;
		float min = 0.0f;
		bool hasMax = false;
		float max = 0.0f;
	};
	static const Definition kCoreConVars[] = {
		{"sourcemod_version", SOURCEMOD_VERSION, (1 << 6) | (1 << 8), "SourceMod Version"},
		{"sm_basepath", "addons/sourcemod", 0, "SourceMod base path (set via command line)"},
		{"sm_corecfgfile", "addons/sourcemod/configs/core.cfg", 0, "SourceMod core configuration file"},
		{"sm_nextmap", "", 1 << 8, ""},
		{"sm_maphistory_size", "20", 0, ""},
		{"sm_time_adjustment", "0", 0, "Adjusts the server time in seconds"},
		{"sm_debug_connect", "1", 0, "Log Debug information about potential connection issues."},
		{"sm_menu_sounds", "1", 0, "Sets whether SourceMod menus play trigger sounds"},
		{"sm_vote_progress_hintbox", "0", 0, "Show current vote progress in a hint box", true, 0.0f, true, 1.0f},
		{"sm_vote_progress_chat", "0", 0, "Show current vote progress as chat messages", true, 0.0f, true, 1.0f},
		{"sm_vote_progress_console", "0", 0, "Show current vote progress as console messages", true, 0.0f, true, 1.0f},
		{"sm_vote_progress_client_console", "0", 0, "Show current vote progress as console messages to clients", true,
		 0.0f, true, 1.0f},
		{"sm_vote_delay", "30", 0, "Sets the recommended time in between public votes"},
		{"sm_show_activity", "13", 1 << 6, "Activity display setting (see sourcemod.cfg)"},
		{"sm_immunity_mode", "1", 1 << 6, "Mode for deciding immunity protection"},
		{"sm_datetime_format", "%m/%d/%Y - %H:%M:%S", 0, "Default formatting time rules"},
	};
	for (const Definition& cvar : kCoreConVars) {
		console.CreateConVar(cvar.name, cvar.value, cvar.flags, cvar.help, cvar.hasMin, cvar.min, cvar.hasMax,
		                     cvar.max);
	}
}

void Core::Boot(const CoreOptions& options, Engine& engine) {
	if (booted_) {
		Fatal("SourceMod was started twice");
	}
	booted_ = true;
	options_ = options;
	engine_ = &engine;

	// The core's global classes; each joins SMGlobalClass::head.
	timers_ = std::make_unique<TimerSystem>();
	players_ = std::make_unique<PlayerManager>(engine.clients());
	convars_ = std::make_unique<ConVarManager>();
	commands_ = std::make_unique<ConCmdManager>();
	config_ = std::make_unique<CoreConfig>();
	nextMap_ = std::make_unique<NextMapManager>();
	sourcemod_ = std::make_unique<SourceModImpl>(options.gameDir, options.smDir);
	gameHelpers_ = std::make_unique<GameHelpers>();
	userMessages_ = std::make_unique<UserMessages>();
	provider_ = std::make_unique<CoreProviderImpl>(sourcemod_.get(), timers_.get(), players_.get(), gameHelpers_.get(),
	                                               &g_Menus);

	// The engine keeps the game's binaries loaded; SourceMod's game configs
	// look for them in memory.
	for (const char* binary : {"server_srv.so", "engine_srv.so"}) {
		std::string path = options.gameDir + "/bin/" + binary;
		if (!dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL)) {
			Fatal("could not load the stand-in game binary %s: %s", path.c_str(), dlerror());
		}
	}
	LoadVm();
	LoadLogic();
	g_pSourcePawn2->SetDebugListener(logicore.debugger);

	AddCoreConVars(engine.console());

	// SourceModBase::StartSourceMod
	config_->Initialize(options.smDir + "/configs/core.cfg", options.coreConfig);
	NotifyGlobalClasses([](SMGlobalClass* global) { global->OnSourceModStartup(false); });
	NotifyGlobalClasses([](SMGlobalClass* global) { global->OnSourceModAllInitialized(); });
	NotifyGlobalClasses([](SMGlobalClass* global) { global->OnSourceModAllInitialized_Post(); });
	sharesys->AddInterface(nullptr, sourcemod_.get());
	sharesys->AddInterface(nullptr, gameHelpers_.get());
	runnerHost_ = std::make_unique<RunnerHost>(options.steamworks);
	sharesys->AddInterface(nullptr, runnerHost_.get());
	if (options.watchdogSeconds > 0) {
		g_pSourcePawn2->InstallWatchdogTimer(static_cast<size_t>(options.watchdogSeconds) * 1000);
	}
	// Metamod's AllPluginsLoaded comes before the first map.
	NotifyGlobalClasses([](SMGlobalClass* global) { global->OnSourceModGameInitialized(); });
	Note("SourceMod started (game folder %s)", options.gameDir.c_str());
}

std::string Core::ChangeMap(const std::string& map) {
	if (IsLevelLoaded()) {
		EndMap();
	}
	StartMap(map);
	bool executed = RunFramesUntil([this]() { return AreConfigsExecuted(); }, kConfigFrames);
	if (executed || engine_->QuitRequested()) {
		return std::string();
	}
	return "the configs of map " + map + " were not executed within " + std::to_string(kConfigFrames) + " frames";
}

void Core::StartMap(const std::string& map) {
	engine_->BeginMap(map);

	// SourceModBase::LevelInit
	mapLoading_ = true;
	NotifyGlobalClasses([&map](SMGlobalClass* global) { global->OnSourceModLevelChange(map.c_str()); });
	LoadPlugins();
	mapLoading_ = false;
	NotifyGlobalClasses([](SMGlobalClass* global) { global->OnSourceModPluginsLoaded(); });
	if (!onMapInit_) {
		onMapInit_ = forwardsys->CreateForward("OnMapInit", ET_Ignore, 1, nullptr, Param_String);
	}
	if (!onMapEnd_) {
		onMapEnd_ = forwardsys->CreateForward("OnMapEnd", ET_Ignore, 0, nullptr);
	}
	levelEndBarrier_ = true;
	int status;
	size_t position;
	logicore.ParseEntityLumpString("{\n\"classname\" \"worldspawn\"\n}\n", status, position);
	logicore.SetEntityLumpWritable(true);
	onMapInit_->PushString(map.c_str());
	onMapInit_->Execute();
	logicore.SetEntityLumpWritable(false);
	engine_->ActivateMap();

	// PlayerManager::OnServerActivate
	players_->OnServerActivate();
	NotifyGlobalClasses([](SMGlobalClass* global) { global->OnSourceModLevelActivated(); });
	config_->ExecuteAllConfigs();
	ConVar* serverCfg = engine_->console().FindConVar("servercfgfile");
	engine_->console().AppendToBuffer(std::string("exec ") + (serverCfg ? serverCfg->GetString() : "server.cfg") +
	                                  "\n");
	Note("map %s started", map.c_str());
}

void Core::EndMap() {
	// SourceModBase::LevelShutdown
	if (levelEndBarrier_) {
		NotifyGlobalClasses([](SMGlobalClass* global) { global->OnSourceModLevelEnd(); });
		onMapEnd_->Execute();
		extsys->CallOnCoreMapEnd();
		timers_->RemoveMapChangeTimers();
		levelEndBarrier_ = false;
	}
	mapStarted_ = false;
	scripts->RefreshAll();
	engine_->EndMap();
}

// A plugin file kept from LoadAll's scan: renamed, for the time of one
// LoadAll, to a name without ".smx", which LoadPluginsFromDir passes over.
static std::string HiddenPluginPath(const std::string& path) {
	return path + ".smrunner-failed";
}

std::vector<std::string> Core::LoadGivenPluginsInOrder() {
	IPluginManager* pluginsys = nullptr;
	if (!sharesys->RequestInterface(SMINTERFACE_PLUGINSYSTEM_NAME, SMINTERFACE_PLUGINSYSTEM_VERSION, nullptr,
	                                reinterpret_cast<SMInterface**>(&pluginsys))) {
		Fatal("SourceMod's plugin system is missing");
	}
	std::vector<std::string> hidden;
	for (const std::string& plugin : options_.plugins) {
		if (std::find(hidden.begin(), hidden.end(), plugin) != hidden.end()) {
			continue;
		}
		char error[256];
		bool loaded;
		if (pluginsys->LoadPlugin(plugin.c_str(), false, PluginType_MapUpdated, error, sizeof(error), &loaded)) {
			continue;
		}
		// LoadAutoPlugin's line, as when LoadAll had tried it.
		logger->LogError("[SM] Failed to load plugin \"%s\": %s.", plugin.c_str(), error);
		orderedLoadFailures_.push_back({plugin, error});
		char path[PLATFORM_MAX_PATH];
		sourcemod_->BuildPath(Path_SM, path, sizeof(path), "plugins/%s", plugin.c_str());
		if (rename(path, HiddenPluginPath(path).c_str()) != 0) {
			Fatal("could not keep %s from SourceMod's plugin scan: %s", path, strerror(errno));
		}
		hidden.push_back(plugin);
	}
	return hidden;
}

// SourceMod's DoGlobalPluginLoads, at each LevelInit.
void Core::LoadPlugins() {
	char configPath[PLATFORM_MAX_PATH];
	char pluginsPath[PLATFORM_MAX_PATH];
	sourcemod_->BuildPath(Path_SM, configPath, sizeof(configPath), "configs/plugin_settings.cfg");
	sourcemod_->BuildPath(Path_SM, pluginsPath, sizeof(pluginsPath), "plugins");
	extsys->TryAutoload();
	orderedLoadFailures_.clear();
	std::vector<std::string> hidden;
	if (!givenPluginsLoaded_) {
		givenPluginsLoaded_ = true;
		hidden = LoadGivenPluginsInOrder();
	}
	scripts->LoadAll(configPath, pluginsPath);
	for (const std::string& plugin : hidden) {
		char path[PLATFORM_MAX_PATH];
		sourcemod_->BuildPath(Path_SM, path, sizeof(path), "plugins/%s", plugin.c_str());
		if (rename(HiddenPluginPath(path).c_str(), path) != 0) {
			Fatal("could not put %s back after SourceMod's plugin scan: %s", path, strerror(errno));
		}
	}
}

bool Core::AreConfigsExecuted() const {
	return config_->AreConfigsExecuted();
}

void Core::AddFrameAction(FRAMEACTION action, void* data) {
	std::lock_guard<std::mutex> lock(sFrameActionMutex);
	frameActions_.emplace_back(action, data);
}

// core/frame_hooks.cpp RunFrameHooks
void Core::RunFrameHooks(bool simulating) {
	std::vector<std::pair<FRAMEACTION, void*>> actions;
	{
		std::lock_guard<std::mutex> lock(sFrameActionMutex);
		actions.swap(frameActions_);
	}
	for (auto& action : actions) {
		action.first(action.second);
	}
	players_->ProcessFakeClientCommands();
	players_->ProcessDelayedKicks();
	if (pendingInternalPush_) {
		pendingInternalPush_ = false;
		config_->InternalCmdTrigger();
	}
	sourcemod_->ProcessGameFrameHooks(simulating);

	static double lastAuthCheck = 0.0;
	static double lastMenuCheck = 0.0;
	double now = *timers_->UniversalTime();
	if (now - lastMenuCheck >= 1.0) {
		g_RadioMenuStyle.ProcessWatchList();
		lastMenuCheck = now;
	}
	if (players_->HasPendingAuth() && now - lastAuthCheck >= 0.7) {
		players_->RunAuthChecks();
		lastAuthCheck = now;
	}
}

void Core::ConsolePrint(const char* format, ...) {
	char buffer[3072];
	va_list ap;
	va_start(ap, format);
	size_t length = ke::SafeVsprintf(buffer, sizeof(buffer) - 2, format, ap);
	va_end(ap);
	buffer[length++] = '\n';
	buffer[length] = '\0';
	engine_->console().Print(buffer);
}

void Core::ReportPluginError(const char* format, ...) {
	char message[1024];
	va_list ap;
	va_start(ap, format);
	vsnprintf(message, sizeof(message), format, ap);
	va_end(ap);
	IPluginContext* context = nullptr;
	IPluginIterator* iterator = scripts->GetPluginIterator();
	for (; iterator->MorePlugins() && !context; iterator->NextPlugin()) {
		context = iterator->GetPlugin()->GetBaseContext();
	}
	iterator->Release();
	if (!context) {
		Fatal("%s", message);
	}
	context->ReportError("%s", message);
}

bool Core::RunFrame() {
	if (engine_->QuitRequested()) {
		return false;
	}
	if (frameDepth_ > 0) {
		Fatal("a frame was started inside a frame");
	}
	frameDepth_++;
	frameNumber_++;
	engine_->RunFrame([this](bool simulating) {
		timers_->GameFrame(simulating);
		logicore.callbacks->OnThink(simulating);
	});
	frameDepth_--;
	if (engine_->PendingMap().empty()) {
		return true;
	}
	std::string next = engine_->PendingMap();
	engine_->ClearPendingMap();
	std::string error = ChangeMap(next);
	if (!error.empty()) {
		Fatal("%s", error.c_str());
	}
	return true;
}

void Core::RunFrames(int count) {
	for (int frame = 0; frame < count && RunFrame(); frame++) {
	}
}

bool Core::RunFramesUntil(const std::function<bool()>& done, int limit) {
	for (int frame = 0; frame < limit && !done(); frame++) {
		if (!RunFrame()) {
			break;
		}
	}
	return done();
}

void Core::Shutdown() {
	if (!booted_) {
		return;
	}
	if (IsLevelLoaded()) {
		EndMap();
	}
	// SourceModBase::ShutdownServices
	scripts->Shutdown();
	extsys->Shutdown();
	if (onMapInit_) {
		forwardsys->ReleaseForward(onMapInit_);
		onMapInit_ = nullptr;
	}
	if (onMapEnd_) {
		forwardsys->ReleaseForward(onMapEnd_);
		onMapEnd_ = nullptr;
	}
	NotifyGlobalClasses([](SMGlobalClass* global) { global->OnSourceModShutdown(); });
	NotifyGlobalClasses([](SMGlobalClass* global) { global->OnSourceModAllShutdown(); });
	booted_ = false;
}

}  // namespace smrunner
