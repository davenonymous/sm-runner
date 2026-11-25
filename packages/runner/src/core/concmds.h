// Copyright 2026 davenonymous
//
// SourceMod's console command layer, ported from core/ConCmdManager.cpp,
// core/ConsoleDetours.cpp and the command natives of core/smn_console.cpp of
// SourceMod 1.12 (GPLv3, AlliedModders LLC):
//   - RegServerCmd, RegConsoleCmd and RegAdminCmd hook a command; one that
//     does not exist yet is created, one of the engine is hooked and blocked
//     when a hook returns Plugin_Handled or more;
//   - server commands run only from the server console, client and admin
//     commands get the client, admin commands check the client's access;
//   - command listeners (AddCommandListener) see every existing command
//     before its hooks, "" listens to all of them;
//   - a plugin's hooks go when the plugin goes, and a created command goes
//     with its last hook;
//   - command iterators walk SourceMod's commands (CommandIterator) or every
//     command and cvar of the console, in the order they were made
//     (FindFirstConCommand).
#ifndef SMRUNNER_CORE_CONCMDS_H
#define SMRUNNER_CORE_CONCMDS_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/globals.h"
#include "engine/console.h"

namespace smrunner {

class ConCmdManager : public SMGlobalClass, public SourceMod::IPluginsListener, public SourceMod::IRootConsoleCommand {
 public:
	struct Hook;
	struct Info {
		std::string name;
		std::string help;
		bool sourceMod = false;   // created by SourceMod, not an engine command
		SourceMod::IPlugin* plugin = nullptr;
		SourceMod::FlagBits eflags = 0;
		std::vector<Hook*> hooks;
	};
	struct Hook {
		enum Type { Server, Client } type;
		Info* info;
		SourcePawn::IPluginFunction* function;
		std::string help;
		SourceMod::IPlugin* plugin;
		bool isAdmin = false;
		std::string group;
		SourceMod::FlagBits flags = 0;
		SourceMod::FlagBits eflags = 0;
	};

	// SMGlobalClass
	void OnSourceModAllInitialized() override;
	void OnSourceModShutdown() override;

	// IPluginsListener
	void OnPluginDestroyed(SourceMod::IPlugin* plugin) override;

	// IRootConsoleCommand: sm cmds <plugin>
	void OnRootConsoleCommand(const char* name, const SourceMod::ICommandArgs* args) override;

	// Every command SourceMod knows of, sorted by name.
	const std::vector<Info*>& CommandList() const { return list_; }

	// Whether SourceMod hooks the command, so that clients may run it.
	bool IsHooked(const char* command) const;

	bool AddServerCommand(SourcePawn::IPluginFunction* function, const char* name, const char* help, int flags,
	                      SourceMod::IPlugin* plugin);
	bool AddAdminCommand(SourcePawn::IPluginFunction* function, const char* name, const char* group, int adminFlags,
	                     const char* help, int flags, SourceMod::IPlugin* plugin);
	bool AddListener(SourcePawn::IPluginFunction* function, const char* command);
	bool RemoveListener(SourcePawn::IPluginFunction* function, const char* command);

	// The console's command filter: listeners, then the hooks. Returns true
	// to block the command.
	bool Dispatch(int client, const CommandArgs& args);
	// Command listeners alone (ConsoleDetours::InternalDispatch).
	cell_t RunListeners(int client, const CommandArgs& args);
	// What a client command runs through when no console command took it
	// (IServerGameClients::ClientCommand): client hooks only.
	SourceMod::ResultType DispatchClientCommand(int client, const char* command, int args,
	                                           SourceMod::ResultType type);

	void UpdateAdminCmdFlags(const char* command, SourceMod::OverrideType type, SourceMod::FlagBits bits, bool remove);
	bool LookForCommandAdminFlags(const char* command, SourceMod::FlagBits* flags);

	// SM_REPLY_CONSOLE or SM_REPLY_CHAT for the command being run.
	unsigned int ReplyTo() const { return replyTo_; }
	unsigned int SetReplyTo(unsigned int replyTo) {
		unsigned int old = replyTo_;
		replyTo_ = replyTo;
		return old;
	}

 private:
	Info* AddOrFindCommand(const char* name, const char* help, int flags, SourceMod::IPlugin* plugin);
	void RemoveCommand(Info* info);
	bool CheckAccess(int client, const char* command, const Hook& hook);
	bool InternalDispatch(int client, const CommandArgs& args, Info* info);

	std::map<std::string, Info*> commands_;   // by lower-case name
	std::vector<Info*> list_;                 // sorted by name
	SourceMod::IChangeableForward* anyCommand_ = nullptr;
	std::map<std::string, SourceMod::IChangeableForward*> listeners_;
	unsigned int replyTo_ = SM_REPLY_CONSOLE;
};

}  // namespace smrunner

#endif
