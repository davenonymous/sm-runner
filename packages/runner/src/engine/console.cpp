// Copyright 2026 davenonymous
#include "console.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <algorithm>
#include <list>

static ConVar::ChangeCallback sGlobalChangeCallback;

ConVar::ConVar(const std::string& name, const std::string& defaultValue, int flags, const std::string& help,
               bool hasMin, float min, bool hasMax, float max)
	: name_(name), default_(defaultValue), help_(help), flags_(flags), hasMin_(hasMin), min_(min), hasMax_(hasMax),
	  max_(max) {
	// The engine's ConVar::Create: the default is set without callbacks.
	string_ = default_;
	float_ = static_cast<float>(atof(string_.c_str()));
	if (Clamp(float_)) {
		char clamped[32];
		snprintf(clamped, sizeof(clamped), "%f", float_);
		string_ = clamped;
	}
	int_ = static_cast<int>(float_);
}

void ConVar::SetGlobalChangeCallback(ChangeCallback callback) {
	sGlobalChangeCallback = std::move(callback);
}

bool ConVar::GetMin(float& min) const {
	min = min_;
	return hasMin_;
}

bool ConVar::GetMax(float& max) const {
	max = max_;
	return hasMax_;
}

bool ConVar::Clamp(float& value) const {
	if (hasMin_ && value < min_) {
		value = min_;
		return true;
	}
	if (hasMax_ && value > max_) {
		value = max_;
		return true;
	}
	return false;
}

void ConVar::ChangeString(const char* value, float oldFloat) {
	std::string old = string_;
	string_ = value;
	if (old == string_ || !sGlobalChangeCallback) {
		return;
	}
	sGlobalChangeCallback(this, old.c_str(), oldFloat);
}

// ConVar::InternalSetValue
void ConVar::SetValue(const char* value) {
	float oldFloat = float_;
	float newFloat = static_cast<float>(atof(value));
	char clamped[32];
	if (Clamp(newFloat)) {
		snprintf(clamped, sizeof(clamped), "%f", newFloat);
		value = clamped;
	}
	float_ = newFloat;
	int_ = static_cast<int>(newFloat);
	ChangeString(value, oldFloat);
}

// ConVar::InternalSetIntValue
void ConVar::SetValue(int value) {
	if (value == int_) {
		return;
	}
	float newFloat = static_cast<float>(value);
	if (Clamp(newFloat)) {
		value = static_cast<int>(newFloat);
	}
	float oldFloat = float_;
	float_ = newFloat;
	int_ = value;
	char formatted[32];
	snprintf(formatted, sizeof(formatted), "%d", int_);
	ChangeString(formatted, oldFloat);
}

// ConVar::InternalSetFloatValue
void ConVar::SetValue(float value) {
	if (value == float_) {
		return;
	}
	Clamp(value);
	float oldFloat = float_;
	float_ = value;
	int_ = static_cast<int>(value);
	char formatted[32];
	snprintf(formatted, sizeof(formatted), "%f", float_);
	ChangeString(formatted, oldFloat);
}

namespace smrunner {

// CCommand's default break set, "{}()':".
static bool IsBreakChar(char c) {
	return c != '\0' && strchr("{}()':", c) != nullptr;
}

namespace {

// A read-only text CUtlBuffer, as far as CCommand::Tokenize and
// CCommandBuffer::ParseArgV0 read one: reading past the end marks it
// invalid (GET_OVERFLOW), whitespace is isspace, and chars are signed.
class TextReader {
 public:
	explicit TextReader(const std::string& text) : text_(text) {}

	bool IsValid() const { return !overflowed_; }
	size_t TellGet() const { return get_; }
	size_t Remaining() const { return text_.size() - get_; }

	// CUtlBuffer::ParseToken with the default break set and comments:
	// the token's length, or -1 when none is left. A token of maxLength
	// characters or more is cut and reported with maxLength.
	int ParseToken(std::string* token, int maxLength) {
		token->clear();
		while (true) {
			if (!IsValid()) {
				return -1;
			}
			EatWhiteSpace();
			if (!EatCppComment()) {
				break;
			}
		}
		char c = GetChar();
		if (c == '\0') {
			return -1;
		}
		if (c == '"') {
			int length = 0;
			while (IsValid()) {
				c = GetChar();
				if (c == '"' || c == '\0') {
					return length;
				}
				*token += c;
				if (++length == maxLength) {
					token->pop_back();
					return maxLength;
				}
			}
			return length;
		}
		if (IsBreakChar(c)) {
			*token = c;
			return 1;
		}
		int length = 0;
		while (true) {
			*token += c;
			if (++length == maxLength) {
				token->pop_back();
				return maxLength;
			}
			c = GetChar();
			if (!IsValid()) {
				break;
			}
			// The engine's char is signed: a byte of a UTF-8 character ends a
			// word as a control character does.
			if (IsBreakChar(c) || c == '"' || static_cast<signed char>(c) <= ' ') {
				get_--;
				break;
			}
		}
		return length;
	}

 private:
	// CUtlBuffer::GetChar through Scanf("%c").
	char GetChar() {
		if (overflowed_) {
			return '\0';
		}
		if (get_ >= text_.size()) {
			overflowed_ = true;
			return '\0';
		}
		return text_[get_++];
	}

	void EatWhiteSpace() {
		if (!IsValid()) {
			return;
		}
		while (true) {
			if (get_ >= text_.size()) {
				overflowed_ = true;   // CheckGet
				return;
			}
			if (!isspace(static_cast<unsigned char>(text_[get_]))) {
				return;
			}
			get_++;
		}
	}

	bool EatCppComment() {
		if (!IsValid() || get_ + 2 > text_.size() || text_[get_] != '/' || text_[get_ + 1] != '/') {
			return false;
		}
		get_ += 2;
		for (char c = GetChar(); IsValid(); c = GetChar()) {
			if (c == '\n') {
				break;
			}
		}
		return true;
	}

	const std::string& text_;
	size_t get_ = 0;
	bool overflowed_ = false;
};

}  // namespace

// CCommand::Tokenize with the default break set: a line too long for its
// buffer is not tokenized at all, nor one whose tokens overflow the
// argument buffer; the 64th argument is the last.
CommandArgs::CommandArgs(const std::string& line) : line_(line), overflowed_(line.size() >= kMaxLength - 1) {
	if (overflowed_) {
		return;
	}
	TextReader reader(line_);
	size_t argvBytes = 0;
	while (reader.IsValid() && args_.size() < kMaxArgs) {
		if (argvBytes >= kMaxLength) {
			Reset();
			return;
		}
		int maxLength = static_cast<int>(kMaxLength - argvBytes);
		size_t start = reader.TellGet();
		std::string token;
		int size = reader.ParseToken(&token, maxLength);
		if (size < 0) {
			break;
		}
		if (size >= maxLength) {
			Reset();
			return;
		}
		if (args_.size() == 1) {
			// ArgS starts at the second argument, at its quote when quoted.
			size_t argv0Size = reader.TellGet();
			if (line_[argv0Size - 1] == '"') {
				argv0Size--;
			}
			argv0Size -= static_cast<size_t>(size);
			if (argv0Size > start && line_[argv0Size - 1] == '"') {
				argv0Size--;
			}
			argS_ = line_.substr(argv0Size);
		}
		args_.push_back(token);
		if (args_.size() >= kMaxArgs) {
			clamped_ = true;
		}
		argvBytes += static_cast<size_t>(size) + 1;
	}
	// ArgS runs to the end of the command, with its trailing whitespace (the
	// '\r' of a CRLF line too).
}

void CommandArgs::Reset() {
	args_.clear();
	argS_.clear();
	clamped_ = false;
}

const char* CommandArgs::Arg(int index) const {
	if (index < 0 || index >= ArgC()) {
		return "";
	}
	return args_[index].c_str();
}

Console::Console(PrintSink sink) : sink_(std::move(sink)) {}

void Console::Printf(const char* format, ...) const {
	char buffer[4096];
	va_list ap;
	va_start(ap, format);
	vsnprintf(buffer, sizeof(buffer), format, ap);
	va_end(ap);
	sink_(buffer);
}

std::string Console::Fold(const std::string& name) {
	std::string folded = name;
	std::transform(folded.begin(), folded.end(), folded.begin(), [](unsigned char c) { return tolower(c); });
	return folded;
}

ConVar* Console::CreateConVar(const std::string& name, const std::string& defaultValue, int flags,
                              const std::string& help, bool hasMin, float min, bool hasMax, float max) {
	std::string key = Fold(name);
	if (cvars_.count(key) != 0 || commands_.count(key) != 0) {
		return nullptr;
	}
	auto cvar = std::make_unique<ConVar>(name, defaultValue, flags, help, hasMin, min, hasMax, max);
	ConVar* created = cvar.get();
	cvars_.emplace(key, std::move(cvar));
	order_.push_back(key);
	auto pending = pending_.find(key);
	if (pending != pending_.end()) {
		std::string value = pending->second.value;
		pending_.erase(pending);
		created->SetValue(value.c_str());
	}
	return created;
}

void Console::SetConVarValue(const std::string& name, const std::string& value) {
	if (ConVar* cvar = FindConVar(name)) {
		cvar->SetValue(value.c_str());
		return;
	}
	pending_[Fold(name)] = {name, value};
}

std::vector<std::string> Console::PendingConVarNames() const {
	std::vector<std::string> names;
	for (const auto& entry : pending_) {
		names.push_back(entry.second.name);
	}
	return names;
}

ConVar* Console::FindConVar(const std::string& name) const {
	auto found = cvars_.find(Fold(name));
	return found == cvars_.end() ? nullptr : found->second.get();
}

bool Console::AddCommand(ConsoleCommand command) {
	std::string key = Fold(command.name);
	if (cvars_.count(key) != 0 || commands_.count(key) != 0) {
		return false;
	}
	commands_.emplace(key, std::move(command));
	order_.push_back(key);
	return true;
}

void Console::RemoveCommand(const std::string& name) {
	std::string key = Fold(name);
	if (commands_.erase(key) != 0) {
		order_.erase(std::remove(order_.begin(), order_.end(), key), order_.end());
	}
}

std::vector<Console::Entry> Console::Entries() const {
	std::vector<Entry> entries;
	for (const std::string& key : order_) {
		auto command = commands_.find(key);
		if (command != commands_.end()) {
			entries.push_back({command->second.name, true, command->second.flags, command->second.help});
			continue;
		}
		const ConVar* cvar = cvars_.at(key).get();
		entries.push_back({cvar->GetName(), false, cvar->GetFlags(), cvar->GetHelpText()});
	}
	return entries;
}

ConsoleCommand* Console::FindCommand(const std::string& name) {
	auto found = commands_.find(Fold(name));
	return found == commands_.end() ? nullptr : &found->second;
}

// CCommandBuffer::GetNextCommandLength: a command ends at a line break, or
// at ';' outside quotes and outside a "//" comment, which runs to the line
// break and is not part of the command.
std::vector<Console::SplitCommand> Console::SplitCommands(const std::string& text) {
	std::vector<SplitCommand> commands;
	std::string current;
	size_t start = 0;
	bool quoted = false;
	bool commented = false;
	for (size_t i = 0; i < text.size(); i++) {
		char c = text[i];
		if (c == '\n' || (c == ';' && !quoted && !commented)) {
			commands.push_back({current, start});
			current.clear();
			start = i + 1;
			quoted = false;
			commented = false;
			continue;
		}
		if (commented) {
			continue;
		}
		if (c == '"') {
			quoted = !quoted;
		} else if (c == '/' && !quoted && i + 1 < text.size() && text[i + 1] == '/') {
			commented = true;
			i++;
			continue;
		}
		current += c;
	}
	commands.push_back({current, start});
	return commands;
}

// CCommandBuffer::ParseArgV0: the command's first token, as CCommand reads
// it, and the text after it (empty when nothing follows the token).
static bool ParseArgV0(const std::string& line, std::string* argv0, std::string* rest) {
	TextReader reader(line);
	int size = reader.ParseToken(argv0, static_cast<int>(line.size()) + 1);
	if (size <= 0) {
		argv0->clear();
		return false;
	}
	*rest = line.substr(reader.TellGet());
	return true;
}

bool Console::IsWaitCommand(const std::string& line) {
	std::string argv0;
	std::string rest;
	return ParseArgV0(line, &argv0, &rest) && strcasecmp(argv0.c_str(), "wait") == 0;
}

bool Console::WaitsAllowed() const {
	const ConVar* allowWait = FindConVar("sv_allow_wait_command");
	return !allowWait || allowWait->GetBool();
}

std::vector<std::string> Console::SplitCommandLine(const std::string& text) {
	std::vector<std::string> lines;
	std::string argv0;
	std::string rest;
	for (const SplitCommand& command : SplitCommands(text)) {
		if (ParseArgV0(command.line, &argv0, &rest)) {
			lines.push_back(command.line);
		}
	}
	return lines;
}

void Console::CommandBuffer::Insert(std::string line, int tick) {
	// CCommandBuffer::InsertCommand: while commands run, one for the tick
	// being run goes right after the running command.
	if (processing_ && tick <= currentTick_) {
		commands_.insert(next_, {std::move(line), tick});
		return;
	}
	auto at = std::find_if(commands_.begin(), commands_.end(),
	                       [tick](const Command& command) { return command.tick > tick; });
	commands_.insert(at, {std::move(line), tick});
}

void Console::CommandBuffer::Process(const std::function<void(const std::string&)>& run) {
	processing_ = true;
	lastTickToProcess_ = currentTick_;
	while (!commands_.empty() && commands_.front().tick <= lastTickToProcess_) {
		currentTick_ = commands_.front().tick;
		std::string line = std::move(commands_.front().line);
		commands_.pop_front();
		next_ = commands_.begin();
		run(line);
	}
	processing_ = false;
	currentTick_ = lastTickToProcess_ + 1;
	next_ = commands_.end();
}

std::list<Console::CommandBuffer::Command> Console::CommandBuffer::TakeAll() {
	std::list<Command> taken;
	taken.swap(commands_);
	next_ = commands_.end();
	return taken;
}

// CCommandBuffer::AddText under Cbuf_AddText: commands go in at the
// buffer's tick, a "wait" (while waits are allowed) delays the ones after it
// by its argument or one tick, and a command too long for the tokenizer is
// refused with the rest of the text, the commands before it staying.
void Console::AddText(const std::string& text, CommandBuffer& target) {
	int tick = target.CurrentTick();
	std::string argv0;
	std::string rest;
	for (const SplitCommand& command : SplitCommands(text)) {
		if (!ParseArgV0(command.line, &argv0, &rest)) {
			continue;
		}
		if (waitEnabled_ && strcasecmp(argv0.c_str(), "wait") == 0) {
			tick += rest.empty() ? 1 : atoi(rest.c_str());
			continue;
		}
		if (command.line.size() >= CommandArgs::kMaxLength - 1) {
			Printf("WARNING: Command too long... ignoring!\n%s\n", text.c_str() + command.start);
			Print("Cbuf_AddText: buffer overflow\n");
			return;
		}
		target.Insert(command.line, tick);
	}
}

Console::CommandBuffer& Console::TextTarget() {
	return processing_.empty() ? buffer_ : *processing_.back();
}

void Console::AppendToBuffer(const std::string& text) {
	AddText(text, TextTarget());
}

void Console::InsertIntoBuffer(const std::string& text) {
	AddText(text, TextTarget());
}

// Cbuf_Execute: sv_allow_wait_command decides whether text added from now on
// may wait.
void Console::TakeWaitSetting() {
	waitEnabled_ = WaitsAllowed();
}

void Console::Process(CommandBuffer& commands) {
	processing_.push_back(&commands);
	commands.Process([this](const std::string& line) { ExecuteServerCommand(line); });
	processing_.pop_back();
}

void Console::ExecuteBuffer() {
	// Text added while commands run is run with them already.
	if (!processing_.empty()) {
		return;
	}
	TakeWaitSetting();
	Process(buffer_);
}

void Console::ExecuteNow(const std::string& text) {
	TakeWaitSetting();
	CommandBuffer own;
	AddText(text, own);
	Process(own);
	// What waits left goes to the buffer: one tick of its own is the next
	// time the buffer runs.
	int nextRun = buffer_.NextRunTick();
	for (CommandBuffer::Command& command : own.TakeAll()) {
		buffer_.Insert(std::move(command.line), nextRun + command.tick - own.CurrentTick());
	}
}

bool Console::ExecuteClientCommand(int client, const CommandArgs& args) {
	const ConsoleCommand* command = args.ArgC() > 0 ? FindCommand(args.Arg(0)) : nullptr;
	if (!command || !command->clientsMayRun) {
		return false;
	}
	Dispatch(client, args, *command);
	return true;
}

void Console::ExecuteNowWith(int client, const CommandArgs& args, const std::function<void()>& body) {
	running_.push_back({&args, client});
	body();
	running_.pop_back();
}

const CommandArgs* Console::CurrentCommand() const {
	return running_.empty() ? nullptr : running_.back().args;
}

void Console::Dispatch(int client, const CommandArgs& args, const ConsoleCommand& command) {
	// The handler may remove the command; keep it alive for the call.
	CommandHandler handler = command.handler;
	ExecuteNowWith(client, args, [&]() {
		if (filter_ && filter_(client, args)) {
			return;
		}
		handler(client, args);
	});
}

// CCvarUtilities::IsCommand: a cvar is set to the whole ArgS, without a
// leading quote, trailing whitespace (a CRLF line's '\r' too) and then a
// closing quote. The engine compares chars, which are signed: a trailing
// byte of a UTF-8 character goes with the whitespace.
static std::string ConVarValueOf(const std::string& argS) {
	bool quoted = !argS.empty() && argS[0] == '"';
	std::string value = quoted ? argS.substr(1) : argS;
	while (!value.empty() && static_cast<signed char>(value.back()) <= ' ') {
		value.pop_back();
	}
	if (quoted && !value.empty() && value.back() == '"') {
		value.pop_back();
	}
	return value;
}

void Console::ExecuteServerCommand(const std::string& line) {
	CommandArgs args(line);
	if (args.Overflowed()) {
		Print(CommandArgs::kOverflowWarning);
		return;
	}
	if (args.Clamped()) {
		Print(CommandArgs::kClampedWarning);
	}
	if (args.ArgC() == 0) {
		return;
	}
	if (const ConsoleCommand* command = FindCommand(args.Arg(0))) {
		Dispatch(0, args, *command);
		return;
	}
	ConVar* cvar = FindConVar(args.Arg(0));
	if (!cvar) {
		Printf("Unknown command \"%s\"\n", args.Arg(0));
		return;
	}
	ExecuteNowWith(0, args, [&]() {
		if (args.ArgC() == 1) {
			Printf("\"%s\" = \"%s\" ( def. \"%s\" )\n - %s\n", cvar->GetName(), cvar->GetString(),
			       cvar->GetDefault(), cvar->GetHelpText());
			return;
		}
		cvar->SetValue(ConVarValueOf(args.ArgS()).c_str());
	});
}

}  // namespace smrunner
