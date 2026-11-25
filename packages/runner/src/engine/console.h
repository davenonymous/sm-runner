// Copyright 2026 davenonymous
//
// The simulated engine console: console variables, console commands, the
// command buffer and the tokenizer. It follows the Source engine's tier1
// behaviour that plugins can observe:
//   - cvar and command names are looked up case-insensitively and share one
//     namespace;
//   - a cvar keeps the string it was set to; numeric setters format it, and a
//     value outside the bounds is clamped and formatted with "%f";
//   - change callbacks run only when the string actually changed;
//   - a value set for a name no cvar has yet waits for the cvar: the first
//     cvar made with the name starts with it;
//   - command lines split at '\n' and at ';' outside quotes, not at '\r':
//     the '\r' of a CRLF line stays in the command, where the tokenizer
//     takes it as whitespace and ArgS keeps it; a "//" outside quotes starts
//     a comment up to the newline, in which ';' splits nothing; the
//     tokenizer is CCommand::Tokenize: whitespace is isspace, a word ends
//     at the break characters {}()':, a quote or a char <= ' ' compared
//     signed (so unquoted UTF-8 characters split a word into several
//     arguments), at most 64 arguments ("Clamped!"), a command whose
//     arguments overflow the 512-byte argument buffer has none;
//   - a cvar set from the console takes the whole ArgS without its
//     surrounding quotes and trailing whitespace (CCvarUtilities::IsCommand);
//   - a command of 511 characters or more is refused: the command buffer
//     drops it and the rest of its text ("WARNING: Command too long...
//     ignoring!", "Cbuf_AddText: buffer overflow"), the tokenizer skips it
//     ("CCommand::Tokenize: Encountered command which overflows the
//     tokenizer buffer.. Skipping!");
//   - a client runs only the commands made for clients (the game's own,
//     here SourceMod's); everything else of a client is the game's;
//   - the command buffer is tier1's CCommandBuffer: each run of it
//     (Cbuf_Execute: once a frame and at every ServerExecute) is one tick
//     and runs the commands of its tick. A "wait" in text being added
//     delays the commands after it by its argument or one tick, while
//     sv_allow_wait_command allows waits (otherwise it is a command). Text
//     added while the buffer runs (ServerCommand, InsertServerCommand, exec)
//     goes right after the running command; text added otherwise runs at the
//     next run; a run of the buffer while it runs does nothing.
#ifndef SMRUNNER_ENGINE_CONSOLE_H
#define SMRUNNER_ENGINE_CONSOLE_H

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

// The logic library and the bridge know the engine's ConVar only by name,
// in the global namespace; this is the runner's definition of it.
class ConVar {
 public:
	ConVar(const std::string& name, const std::string& defaultValue, int flags, const std::string& help,
	       bool hasMin, float min, bool hasMax, float max);

	const char* GetName() const { return name_.c_str(); }
	const char* GetHelpText() const { return help_.c_str(); }
	const char* GetDefault() const { return default_.c_str(); }
	const char* GetString() const { return string_.c_str(); }
	float GetFloat() const { return float_; }
	int GetInt() const { return int_; }
	bool GetBool() const { return int_ != 0; }
	int GetFlags() const { return flags_; }
	void SetFlags(int flags) { flags_ = flags; }
	bool IsFlagSet(int flag) const { return (flags_ & flag) != 0; }
	bool GetMin(float& min) const;
	bool GetMax(float& max) const;
	void SetMin(bool has, float min) { hasMin_ = has; min_ = min; }
	void SetMax(bool has, float max) { hasMax_ = has; max_ = max; }

	void SetValue(const char* value);
	void SetValue(int value);
	void SetValue(float value);
	void Revert() { SetValue(default_.c_str()); }

	// Called after every change of the string, with the old string and float.
	using ChangeCallback = std::function<void(ConVar* cvar, const char* oldValue, float oldFloat)>;
	static void SetGlobalChangeCallback(ChangeCallback callback);

 private:
	bool Clamp(float& value) const;
	void ChangeString(const char* value, float oldFloat);

	std::string name_;
	std::string default_;
	std::string help_;
	std::string string_;
	float float_ = 0.0f;
	int int_ = 0;
	int flags_ = 0;
	bool hasMin_ = false;
	float min_ = 0.0f;
	bool hasMax_ = false;
	float max_ = 0.0f;
};

namespace smrunner {

// One tokenized command line, like tier1's CCommand.
class CommandArgs {
 public:
	static constexpr size_t kMaxLength = 512;
	static constexpr size_t kMaxArgs = 64;

	static constexpr const char* kOverflowWarning =
		"CCommand::Tokenize: Encountered command which overflows the tokenizer buffer.. Skipping!\n";

	explicit CommandArgs(const std::string& line);

	static constexpr const char* kClampedWarning =
		"CCommand::Tokenize: Encountered command which overflows the argument buffer.. Clamped!\n";

	// A line of kMaxLength - 1 characters or more has no arguments.
	bool Overflowed() const { return overflowed_; }
	// Whether the arguments stopped at kMaxArgs.
	bool Clamped() const { return clamped_; }
	int ArgC() const { return static_cast<int>(args_.size()); }
	const char* Arg(int index) const;
	const char* ArgS() const { return argS_.c_str(); }
	const std::string& Line() const { return line_; }

 private:
	void Reset();

	std::string line_;
	bool overflowed_;
	bool clamped_ = false;
	std::string argS_;
	std::vector<std::string> args_;
};

// client 0 is the server console.
using CommandHandler = std::function<void(int client, const CommandArgs& args)>;

struct ConsoleCommand {
	std::string name;
	std::string help;
	int flags = 0;
	CommandHandler handler;
	// Whether clients may run it, as the engine runs a client's commands of
	// the game DLL (FCVAR_GAMEDLL); the engine's own commands are the
	// server's.
	bool clientsMayRun = false;
};

// Decides before a command runs whether it runs at all: return true to
// block it. client is 0 for the server console. Used for SourceMod's
// command listeners.
using CommandFilter = std::function<bool(int client, const CommandArgs& args)>;

class Console {
 public:
	// Lines printed to the server console go to this sink.
	using PrintSink = std::function<void(const char* text)>;
	explicit Console(PrintSink sink);

	void Print(const char* text) const { sink_(text); }
	void Printf(const char* format, ...) const __attribute__((format(printf, 2, 3)));

	ConVar* CreateConVar(const std::string& name, const std::string& defaultValue, int flags, const std::string& help,
	                     bool hasMin, float min, bool hasMax, float max);
	ConVar* FindConVar(const std::string& name) const;
	// Sets the cvar of the name; a name no cvar has yet keeps the value, and
	// the cvar made with it first gets it through SetValue once it was made
	// with its own default, bounds, help and flags.
	void SetConVarValue(const std::string& name, const std::string& value);
	// The names still keeping a value for a cvar never made, as given.
	std::vector<std::string> PendingConVarNames() const;

	// Fails when the name is taken by a cvar or a command.
	bool AddCommand(ConsoleCommand command);
	void RemoveCommand(const std::string& name);
	ConsoleCommand* FindCommand(const std::string& name);

	// A command or cvar as an iteration over the console sees it.
	struct Entry {
		std::string name;
		bool isCommand;
		int flags;
		std::string help;
	};
	// Every command and cvar, in the order they were made.
	std::vector<Entry> Entries() const;

	void SetCommandFilter(CommandFilter filter) { filter_ = std::move(filter); }

	// The command buffer, filled as Cbuf_AddText fills it (Cbuf_InsertText
	// is the same): into the commands being run (those of ExecuteNow while
	// it runs) or the buffer.
	void AppendToBuffer(const std::string& text);
	void InsertIntoBuffer(const std::string& text);
	// Cbuf_Execute: runs the commands of the buffer's tick, after which the
	// buffer is a tick further; nothing while commands run.
	void ExecuteBuffer();
	// Runs text's commands at once, apart from the buffer, as a run of a
	// buffer of their own: with what they add while they run (exec'd files,
	// ServerCommand), up to a "wait" of their own; what waits goes into the
	// buffer, a tick of its own being the buffer's next run. The buffered
	// commands and their ticks are left as they are.
	void ExecuteNow(const std::string& text);

	// One command a client sent, at once: a command clients may run runs
	// through the filter and its handler; false when there is none of the
	// name (unknown, a cvar, or the server's), which is then the game's.
	bool ExecuteClientCommand(int client, const CommandArgs& args);

	// Runs body with args as the current command, for commands that do not
	// go through the console (a client's commands for the game).
	void ExecuteNowWith(int client, const CommandArgs& args, const std::function<void()>& body);

	// A command line split as the engine and the game's client split it:
	// one command per line or per ';' outside quotes and comments, without
	// the comments, those without a first token dropped.
	static std::vector<std::string> SplitCommandLine(const std::string& text);

	// Whether a command's first token is "wait" (any case).
	static bool IsWaitCommand(const std::string& line);
	// sv_allow_wait_command, which the engine and its clients follow.
	bool WaitsAllowed() const;

	// The command being run, innermost first; null outside of commands.
	const CommandArgs* CurrentCommand() const;

 private:
	struct SplitCommand {
		std::string line;
		size_t start;   // where it starts in the text
	};
	static std::vector<SplitCommand> SplitCommands(const std::string& text);

	// tier1's CCommandBuffer: commands ordered by the tick they run in.
	class CommandBuffer {
	 public:
		struct Command {
			std::string line;
			int tick;
		};
		int CurrentTick() const { return currentTick_; }
		// The tick the next Process runs.
		int NextRunTick() const { return processing_ ? lastTickToProcess_ + 1 : currentTick_; }
		void Insert(std::string line, int tick);
		// BeginProcessingCommands(1), every command of the tick, then
		// EndProcessingCommands.
		void Process(const std::function<void(const std::string&)>& run);
		std::list<Command> TakeAll();

	 private:
		std::list<Command> commands_;
		std::list<Command>::iterator next_ = commands_.end();   // while processing
		int currentTick_ = 0;
		int lastTickToProcess_ = -1;
		bool processing_ = false;
	};
	void AddText(const std::string& text, CommandBuffer& target);
	CommandBuffer& TextTarget();
	void TakeWaitSetting();
	void Process(CommandBuffer& commands);
	void ExecuteServerCommand(const std::string& line);
	// The filter, then the handler, with args as the current command.
	void Dispatch(int client, const CommandArgs& args, const ConsoleCommand& command);
	static std::string Fold(const std::string& name);

	PrintSink sink_;
	std::map<std::string, std::unique_ptr<ConVar>> cvars_;
	struct PendingValue {
		std::string name;
		std::string value;
	};
	std::map<std::string, PendingValue> pending_;   // by folded name
	std::map<std::string, ConsoleCommand> commands_;
	std::vector<std::string> order_;   // the folded names, in the order made
	CommandFilter filter_;
	CommandBuffer buffer_;
	// The buffers being run, innermost last.
	std::vector<CommandBuffer*> processing_;
	bool waitEnabled_ = true;
	struct Running {
		const CommandArgs* args;
		int client;
	};
	std::vector<Running> running_;
};

}  // namespace smrunner

#endif
