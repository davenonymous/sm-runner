// Copyright 2026 davenonymous
//
// SourceMod's KeyValues natives and the "KeyValues" handle type, ported from
// core/smn_keyvalues.cpp of SourceMod 1.12 (GPLv3, AlliedModders LLC), on a
// KeyValues tree of the runner's own that behaves like Valve's tier1
// KeyValues (tier1/KeyValues.cpp of the TF2 SDK) as plugins see it: key
// names are symbols of one process-wide, case-insensitive table whose first
// spelling of a name wins, the data types and their conversions (including
// the ones Valve's value union makes visible), paths in key names, the
// traversal stack of SourceMod's KeyValueStack, and the text format with
// quoted and unquoted tokens, // comments, escape sequences and the
// conditionals of a Linux server.
//
// Where SourceMod's natives would corrupt memory (deleting a key the
// traversal stack still holds, KvDeleteKey on the current key or on a key
// below a subkey) the natives fail with an error instead. #include and #base
// are skipped with a note on the console, and of several top-level sections
// in one text only the first is read.
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/core.h"
#include "diagnostics.h"
#include "testing/plugin_memory.h"

using namespace SourceMod;
using namespace SourcePawn;

namespace smrunner {

// ------------------------------------------------------------------ symbols

// KeyValuesSystem's symbol table: one id per name, compared without ASCII
// case; a name keeps the spelling it was first interned with. Ids start at 1
// since SourceMod reads a symbol of 0 as none.
class KeySymbols {
 public:
	static KeySymbols& Get() {
		static KeySymbols symbols;
		return symbols;
	}

	int Intern(const std::string& name) {
		std::string folded = Fold(name);
		auto found = byFoldedName_.find(folded);
		if (found != byFoldedName_.end()) {
			return found->second;
		}
		spellings_.push_back(name);
		int symbol = static_cast<int>(spellings_.size());
		byFoldedName_.emplace(std::move(folded), symbol);
		return symbol;
	}

	std::optional<int> Find(const std::string& name) const {
		auto found = byFoldedName_.find(Fold(name));
		if (found == byFoldedName_.end()) {
			return std::nullopt;
		}
		return found->second;
	}

	const std::string& Spelling(int symbol) const { return spellings_.at(static_cast<size_t>(symbol) - 1); }

 private:
	static std::string Fold(const std::string& name) {
		std::string folded = name;
		for (char& c : folded) {
			if (c >= 'A' && c <= 'Z') {
				c = static_cast<char>(c - 'A' + 'a');
			}
		}
		return folded;
	}

	std::unordered_map<std::string, int> byFoldedName_;
	std::vector<std::string> spellings_;
};

// -------------------------------------------------------------------- nodes

// KvDataTypes of keyvalues.inc, KeyValues::types_t of the SDK. Ptr and
// WString exist for their numbers only: no native sets them.
enum class KvType : cell_t { None = 0, String, Int, Float, Ptr, WString, Color, UInt64 };

// The C conversion of a float to an int as x86 does it: out of range and NaN
// become INT_MIN.
static int TruncateToInt(float value) {
	if (!(value > -2147483904.0f && value < 2147483648.0f)) {
		return INT_MIN;
	}
	return static_cast<int>(value);
}

static std::string FormatValue(const char* format, ...) __attribute__((format(printf, 1, 2)));
static std::string FormatValue(const char* format, ...) {
	char buffer[64];
	va_list ap;
	va_start(ap, format);
	vsnprintf(buffer, sizeof(buffer), format, ap);
	va_end(ap);
	return buffer;
}

// V_atoi64 of tier1/strtools.cpp: a sign, then hex after 0x, a quoted
// character or decimal digits up to the first other character.
static int64_t ParseInt64LikeValve(const char* text) {
	int64_t sign = 1;
	if (*text == '-') {
		sign = -1;
		text++;
	} else if (*text == '+') {
		text++;
	}
	uint64_t value = 0;
	if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
		for (text += 2;; text++) {
			char c = *text;
			if (c >= '0' && c <= '9') {
				value = (value << 4) + static_cast<uint64_t>(c - '0');
			} else if (c >= 'a' && c <= 'f') {
				value = (value << 4) + static_cast<uint64_t>(c - 'a' + 10);
			} else if (c >= 'A' && c <= 'F') {
				value = (value << 4) + static_cast<uint64_t>(c - 'A' + 10);
			} else {
				return static_cast<int64_t>(value) * sign;
			}
		}
	}
	if (text[0] == '\'') {
		return sign * text[1];
	}
	for (; *text >= '0' && *text <= '9'; text++) {
		value = value * 10 + static_cast<uint64_t>(*text - '0');
	}
	return static_cast<int64_t>(value) * sign;
}

struct Rgba {
	uint8_t channel[4] = {0, 0, 0, 0};
};

// One key: a section (subkeys) and/or a value, like Valve's KeyValues. Int,
// Float and Color share one 32-bit cell as Valve's union does, so a getter
// that does not convert a type reads what the last of them stored.
class KvNode {
 public:
	KvNode(int nameSymbol, bool escapes) : name(nameSymbol), escapes(escapes) {}

	int name;
	KvType type = KvType::None;
	bool escapes;
	KvNode* parent = nullptr;
	std::vector<std::unique_ptr<KvNode>> children;

	const std::string& Name() const { return KeySymbols::Get().Spelling(name); }

	// FindKey(const char*, bool): an empty or null name is this key, '/'
	// separates the keys of a path; creating a key makes its parent a section.
	KvNode* FindKey(const char* keyName, bool create) {
		if (!keyName || !*keyName) {
			return this;
		}
		const char* slash = strchr(keyName, '/');
		std::string segment = slash ? std::string(keyName, std::min<size_t>(slash - keyName, 255)) : keyName;
		std::optional<int> symbol = create ? KeySymbols::Get().Intern(segment) : KeySymbols::Get().Find(segment);
		if (!symbol) {
			return nullptr;
		}
		KvNode* found = FindChild(*symbol);
		if (!found) {
			if (!create) {
				return nullptr;
			}
			found = &AppendChild(*symbol);
			type = KvType::None;
		}
		return slash ? found->FindKey(slash + 1, create) : found;
	}

	// FindKey(int): a direct subkey by its name's symbol.
	KvNode* FindChild(int symbol) const {
		for (const auto& child : children) {
			if (child->name == symbol) {
				return child.get();
			}
		}
		return nullptr;
	}

	KvNode& AppendChild(int symbol) { return AdoptChild(std::make_unique<KvNode>(symbol, escapes)); }

	KvNode& AdoptChild(std::unique_ptr<KvNode> child) {
		child->parent = this;
		child->siblingIndex_ = children.size();
		children.push_back(std::move(child));
		return *children.back();
	}

	size_t IndexInParent() const {
		const auto& siblings = parent->children;
		if (siblingIndex_ < siblings.size() && siblings[siblingIndex_].get() == this) {
			return siblingIndex_;
		}
		for (size_t index = 0; index < siblings.size(); index++) {
			if (siblings[index].get() == this) {
				siblingIndex_ = index;
				return index;
			}
		}
		Fatal("a KeyValues key is not among its parent's subkeys");
	}

	KvNode* NextKey() const {
		if (!parent) {
			return nullptr;
		}
		size_t next = IndexInParent() + 1;
		return next < parent->children.size() ? parent->children[next].get() : nullptr;
	}

	KvNode* FirstSubKey() const { return children.empty() ? nullptr : children.front().get(); }

	// The "true" subkeys are the sections: keys of type None.
	KvNode* FirstTrueSubKey() const {
		KvNode* key = FirstSubKey();
		while (key && key->type != KvType::None) {
			key = key->NextKey();
		}
		return key;
	}

	KvNode* NextTrueSubKey() const {
		KvNode* key = NextKey();
		while (key && key->type != KvType::None) {
			key = key->NextKey();
		}
		return key;
	}

	bool HasChild(const KvNode* candidate) const {
		return std::any_of(children.begin(), children.end(),
		                   [candidate](const std::unique_ptr<KvNode>& child) { return child.get() == candidate; });
	}

	std::unique_ptr<KvNode> TakeChild(const KvNode* child) {
		auto found = std::find_if(children.begin(), children.end(),
		                          [child](const std::unique_ptr<KvNode>& entry) { return entry.get() == child; });
		std::unique_ptr<KvNode> taken = std::move(*found);
		children.erase(found);
		taken->parent = nullptr;
		return taken;
	}

	// ------------------------------------------------------------ setters

	void SetString(const std::string& value) {
		text_ = value;
		type = KvType::String;
	}
	void SetInt(int value) {
		memcpy(&cell_, &value, sizeof(cell_));
		type = KvType::Int;
	}
	void SetFloat(float value) {
		memcpy(&cell_, &value, sizeof(cell_));
		type = KvType::Float;
	}
	void SetColor(const Rgba& color) {
		memcpy(&cell_, color.channel, sizeof(cell_));
		type = KvType::Color;
	}
	void SetUInt64(uint64_t value) {
		wide_ = value;
		type = KvType::UInt64;
	}

	// ------------------------------------------------------------ getters

	int GetInt() const {
		switch (type) {
			case KvType::String:
				return atoi(text_.c_str());
			case KvType::Float:
				return TruncateToInt(CellAsFloat());
			case KvType::UInt64:
				return 0;
			default:
				return CellAsInt();
		}
	}

	uint64_t GetUInt64() const {
		switch (type) {
			case KvType::String:
				return static_cast<uint64_t>(ParseInt64LikeValve(text_.c_str()));
			case KvType::Float:
				return static_cast<uint64_t>(static_cast<int64_t>(TruncateToInt(CellAsFloat())));
			case KvType::UInt64:
				return wide_;
			default:
				return static_cast<uint64_t>(static_cast<int64_t>(CellAsInt()));
		}
	}

	float GetFloat() const {
		switch (type) {
			case KvType::String:
				return static_cast<float>(atof(text_.c_str()));
			case KvType::Float:
				return CellAsFloat();
			case KvType::Int:
				return static_cast<float>(CellAsInt());
			case KvType::UInt64:
				return static_cast<float>(wide_);
			default:
				return 0.0f;
		}
	}

	// GetString: numbers are formatted and the key becomes a string key, as
	// Valve's GetString does; sections and colors have no string.
	const std::string* GetString() {
		switch (type) {
			case KvType::String:
				break;
			case KvType::Int:
				SetString(FormatValue("%d", CellAsInt()));
				break;
			case KvType::Float:
				SetString(FormatValue("%f", CellAsFloat()));
				break;
			case KvType::UInt64:
				SetString(FormatValue("%lld", static_cast<long long>(wide_)));
				break;
			default:
				return nullptr;
		}
		return &text_;
	}

	Rgba GetColor() const {
		Rgba color;
		switch (type) {
			case KvType::Color:
				memcpy(color.channel, &cell_, sizeof(cell_));
				break;
			case KvType::Float:
				color.channel[0] = static_cast<uint8_t>(TruncateToInt(CellAsFloat()));
				break;
			case KvType::Int:
				color.channel[0] = static_cast<uint8_t>(CellAsInt());
				break;
			case KvType::String: {
				float parts[4] = {0.0f, 0.0f, 0.0f, 0.0f};
				sscanf(text_.c_str(), "%f %f %f %f", &parts[0], &parts[1], &parts[2], &parts[3]);
				for (int i = 0; i < 4; i++) {
					color.channel[i] = static_cast<uint8_t>(TruncateToInt(parts[i]));
				}
				break;
			}
			default:
				break;
		}
		return color;
	}

	// The raw values for the text writer.
	const std::string& RawString() const { return text_; }
	int RawInt() const { return CellAsInt(); }
	float RawFloat() const { return CellAsFloat(); }
	uint64_t RawUInt64() const { return wide_; }

	// MakeCopy: name, escapes, type, the value and the subkeys; a section's
	// cell is not copied.
	std::unique_ptr<KvNode> Copy() const {
		auto copy = std::make_unique<KvNode>(name, escapes);
		copy->type = type;
		copy->text_ = text_;
		copy->wide_ = wide_;
		copy->cell_ = type == KvType::None ? 0 : cell_;
		for (const auto& child : children) {
			copy->AdoptChild(child->Copy());
		}
		return copy;
	}

 private:
	int CellAsInt() const {
		int value;
		memcpy(&value, &cell_, sizeof(value));
		return value;
	}
	float CellAsFloat() const {
		float value;
		memcpy(&value, &cell_, sizeof(value));
		return value;
	}

	std::string text_;
	uint32_t cell_ = 0;
	uint64_t wide_ = 0;
	mutable size_t siblingIndex_ = 0;
};

// SourceMod's KeyValueStack: the tree and the traversal stack, whose last
// entry is the current key. Subtrees that KvCopySubkeys replaced while the
// stack still held keys in them are kept detached (Valve leaks them).
struct KeyValueStack {
	std::unique_ptr<KvNode> base;
	std::vector<KvNode*> path;
	std::vector<std::unique_ptr<KvNode>> detached;

	KvNode* Current() const { return path.back(); }

	bool Holds(const KvNode* top) const {
		return std::any_of(path.begin(), path.end(), [top](const KvNode* entry) {
			for (const KvNode* node = entry; node; node = node->parent) {
				if (node == top) {
					return true;
				}
			}
			return false;
		});
	}

	// Frees subtrees, or detaches the ones the stack still holds keys in.
	void Release(std::vector<std::unique_ptr<KvNode>> subtrees) {
		for (auto& subtree : subtrees) {
			subtree->parent = nullptr;
			if (Holds(subtree.get())) {
				detached.push_back(std::move(subtree));
			}
		}
	}
};

// -------------------------------------------------------------- text format

static void AppendIndent(std::string& out, int level) {
	out.append(static_cast<size_t>(level), '\t');
}

// WriteConvertedString: quotes always get a backslash, backslashes only
// with escape sequences.
static void AppendConverted(std::string& out, const std::string& text, bool escapes) {
	for (char c : text) {
		if (c == '"' || (escapes && c == '\\')) {
			out += '\\';
		}
		out += c;
	}
}

static void AppendSection(std::string& out, const KvNode& section, int level);

// SaveKeyToFile: sections recurse; empty strings, empty sections and colors
// are not written; only string keys get their name converted.
static void AppendKey(std::string& out, const KvNode& section, const KvNode& key, int level) {
	if (!key.children.empty()) {
		AppendSection(out, key, level + 1);
		return;
	}
	std::string value;
	switch (key.type) {
		case KvType::String:
			if (key.RawString().empty()) {
				return;
			}
			AppendIndent(out, level + 1);
			out += '"';
			AppendConverted(out, key.Name(), section.escapes);
			out += "\"\t\t\"";
			AppendConverted(out, key.RawString(), section.escapes);
			out += "\"\n";
			return;
		case KvType::Int:
			value = FormatValue("%d", key.RawInt());
			break;
		case KvType::UInt64:
			value = FormatValue("0x%016llX", static_cast<unsigned long long>(key.RawUInt64()));
			break;
		case KvType::Float:
			value = FormatValue("%f", key.RawFloat());
			break;
		default:
			return;
	}
	AppendIndent(out, level + 1);
	out += '"' + key.Name() + "\"\t\t\"" + value + "\"\n";
}

// RecursiveSaveToFile.
static void AppendSection(std::string& out, const KvNode& section, int level) {
	AppendIndent(out, level);
	out += '"';
	AppendConverted(out, section.Name(), section.escapes);
	out += "\"\n";
	AppendIndent(out, level);
	out += "{\n";
	for (const auto& key : section.children) {
		AppendKey(out, section, *key, level);
	}
	AppendIndent(out, level);
	out += "}\n";
}

static std::string ExportText(const KvNode& section) {
	std::string text;
	AppendSection(text, section, 0);
	return text;
}

// EvaluateConditional on a dedicated Linux server: $WIN32 means any PC.
static bool EvaluateConditional(const std::string& token) {
	const char* text = token.c_str();
	if (*text == '[') {
		text++;
	}
	bool negated = *text == '!';
	struct Platform {
		const char* name;
		bool present;
	};
	static const Platform kPlatforms[] = {
		{"$DECK", false}, {"$X360", false}, {"$WIN32", true},  {"$WINDOWS", false},
		{"$OSX", false},  {"$LINUX", true}, {"$POSIX", true},
	};
	std::string upper = text;
	std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return toupper(c); });
	for (const Platform& platform : kPlatforms) {
		if (upper.find(platform.name) != std::string::npos) {
			return platform.present != negated;
		}
	}
	return false;
}

// LoadFromBuffer and RecursiveLoadFromBuffer over a text, with CUtlBuffer's
// way of running out: reading past the end makes the buffer invalid.
class KvTextReader {
 public:
	// Valve reads the text as a C string: it ends at its first NUL.
	KvTextReader(const std::string& text, const char* resourceName)
		: text_(text.c_str()), resourceName_(resourceName) {}

	// Reads the first top-level section into target: it takes the section's
	// name and gets its keys appended to the ones it has.
	void LoadInto(KvNode& target, KeyValueStack& stack) {
		bool readSection = false;
		do {
			std::optional<Token> token = ReadToken(target.escapes);
			if (!valid_ || !token || token->text.empty()) {
				break;
			}
			if (strcasecmp(token->text.c_str(), "#include") == 0 || strcasecmp(token->text.c_str(), "#base") == 0) {
				SkipInclude(token->text, target.escapes);
				continue;
			}
			std::unique_ptr<KvNode> ignored;
			KvNode* section = &target;
			int symbol = KeySymbols::Get().Intern(token->text);
			if (readSection) {
				ignored = std::make_unique<KvNode>(symbol, target.escapes);
				section = ignored.get();
			} else {
				target.name = symbol;
			}
			bool accepted = true;
			std::optional<Token> brace = ReadToken(target.escapes);
			if (brace && brace->conditional) {
				accepted = EvaluateConditional(brace->text);
				brace = ReadToken(target.escapes);
			}
			if (brace && brace->text == "{" && !brace->quoted) {
				LoadSection(*section, 1);
			} else {
				ReportError("LoadFromBuffer: missing {");
			}
			if (accepted) {
				readSection = true;
			} else if (section == &target) {
				stack.Release(std::move(target.children));
				target.children.clear();
				target.type = KvType::None;
			}
		} while (valid_);
	}

 private:
	struct Token {
		std::string text;
		bool quoted = false;
		bool conditional = false;
	};

	static constexpr size_t kTokenSize = 4096;
	static constexpr int kMaxDepth = 100;

	bool AtEnd() const { return position_ >= text_.size(); }

	// CUtlBuffer::GetChar: past the end it yields 0 and invalidates.
	char GetChar() {
		if (AtEnd()) {
			valid_ = false;
			return '\0';
		}
		return text_[position_++];
	}

	void EatWhiteSpace() {
		while (!AtEnd() && isspace(static_cast<unsigned char>(text_[position_]))) {
			position_++;
		}
		if (AtEnd()) {
			valid_ = false;
		}
	}

	bool EatComment() {
		if (text_.compare(position_, 2, "//") != 0) {
			return false;
		}
		position_ += 2;
		char c = '\0';
		while (valid_ && c != '\n') {
			c = GetChar();
		}
		return true;
	}

	// The escape sequences of GetCStringCharConversion; an unknown one, and
	// the DEL character without escape sequences, yield 0, which ends the
	// token's text.
	char GetDelimitedChar(bool escapes) {
		char c = GetChar();
		char escapeChar = escapes ? '\\' : '\x7f';
		if (!valid_ || c != escapeChar) {
			return c;
		}
		if (AtEnd() || !escapes) {
			return '\0';
		}
		static const char kFrom[] = "ntvbrfa\\?'\"";
		static const char kTo[] = "\n\t\v\b\r\f\a\\?'\"";
		const char* found = strchr(kFrom, text_[position_]);
		if (!found) {
			return '\0';
		}
		position_++;
		return kTo[found - kFrom];
	}

	std::optional<Token> ReadToken(bool escapes) {
		if (!valid_) {
			return std::nullopt;
		}
		while (true) {
			EatWhiteSpace();
			if (!valid_) {
				return std::nullopt;
			}
			if (!EatComment()) {
				break;
			}
		}
		Token token;
		char first = text_[position_];
		if (first == '"') {
			token.quoted = true;
			position_++;
			std::string raw;
			while (valid_) {
				if (!AtEnd() && text_[position_] == '"') {
					position_++;
					break;
				}
				char c = GetDelimitedChar(escapes);
				if (raw.size() < kTokenSize) {
					raw += c;
				}
			}
			raw.resize(std::min(raw.size(), kTokenSize - 1));
			token.text = raw.c_str();
			return token;
		}
		if (first == '{' || first == '}') {
			token.text = first;
			position_++;
			return token;
		}
		bool bracketOpened = false;
		bool overflowReported = false;
		while (!AtEnd()) {
			char c = text_[position_];
			if (c == '"' || c == '{' || c == '}' || isspace(static_cast<unsigned char>(c))) {
				break;
			}
			if (c == '[') {
				bracketOpened = true;
			}
			if (c == ']' && bracketOpened) {
				token.conditional = true;
			}
			if (token.text.size() < kTokenSize - 1) {
				token.text += c;
			} else if (!overflowReported) {
				overflowReported = true;
				ReportError(" ReadToken overflow");
			}
			position_++;
		}
		return token;
	}

	void SkipInclude(const std::string& directive, bool escapes) {
		std::optional<Token> file = ReadToken(escapes);
		if (!file || file->text.empty()) {
			ReportError((directive + " is NULL ").c_str());
			return;
		}
		Core::Get().engine().console().Printf("KeyValues: %s \"%s\" in file %s is not supported by smrunner, skipped\n",
		                                      directive.c_str(), file->text.c_str(), resourceName_.c_str());
	}

	// The value types the parser recognizes: empty and other text is a
	// string, "0x" with 16 hex digits an uint64, a number with a fraction or
	// exponent a float and an integer an int. A long of 32 bits reports an
	// integer that does not fit (a string then); one of 64 bits hands it
	// back, cut to an int, as on a 64-bit server.
	static void SetParsedValue(KvNode& key, const std::string& value) {
		const char* text = value.c_str();
		const char* end = text + value.size();
		if (value.empty()) {
			key.SetString(value);
			return;
		}
		if (value.size() == 18 && value[0] == '0' && value[1] == 'x') {
			uint64_t parsed = 0;
			for (size_t i = 2; i < 18; i++) {
				char digit = value[i];
				if (digit >= 'a') {
					digit = static_cast<char>(digit - ('a' - ('9' + 1)));
				} else if (digit >= 'A') {
					digit = static_cast<char>(digit - ('A' - ('9' + 1)));
				}
				parsed = parsed * 16 + static_cast<uint64_t>(digit - '0');
			}
			key.SetUInt64(parsed);
			return;
		}
		char* intEnd;
		char* floatEnd;
		errno = 0;
		long parsedLong = strtol(text, &intEnd, 10);
		bool overflow = errno == ERANGE && sizeof(long) == sizeof(int);
		float parsedFloat = static_cast<float>(strtod(text, &floatEnd));
		if (value.size() > 1 && tolower(static_cast<unsigned char>(value[1])) == 'x') {
			parsedFloat = 0.0f;
			floatEnd = const_cast<char*>(text);
		}
		if (floatEnd > intEnd && floatEnd == end) {
			key.SetFloat(parsedFloat);
		} else if (intEnd == end && !overflow) {
			key.SetInt(static_cast<int>(parsedLong));
		} else {
			key.SetString(value);
		}
	}

	void LoadSection(KvNode& section, int depth) {
		if (depth > kMaxDepth) {
			ReportError("RecursiveLoadFromBuffer:  recursion overflow");
			return;
		}
		while (true) {
			std::optional<Token> name = ReadToken(section.escapes);
			if (!name) {
				ReportError("RecursiveLoadFromBuffer:  got EOF instead of keyname");
				return;
			}
			if (name->text.empty()) {
				ReportError("RecursiveLoadFromBuffer:  got empty keyname");
				return;
			}
			if (name->text == "}" && !name->quoted) {
				return;
			}
			KvNode& key = section.AppendChild(KeySymbols::Get().Intern(name->text));
			bool accepted = true;
			std::optional<Token> value = ReadToken(section.escapes);
			if (value && value->conditional) {
				accepted = EvaluateConditional(value->text);
				value = ReadToken(section.escapes);
			}
			if (!value) {
				ReportError("RecursiveLoadFromBuffer:  got NULL key");
				return;
			}
			if (value->text == "}" && !value->quoted) {
				ReportError("RecursiveLoadFromBuffer:  got } in key");
				return;
			}
			if (value->text == "{" && !value->quoted) {
				LoadSection(key, depth + 1);
			} else {
				if (value->conditional) {
					ReportError("RecursiveLoadFromBuffer:  got conditional between key and value");
					return;
				}
				SetParsedValue(key, value->text);
				size_t position = position_;
				bool valid = valid_;
				std::optional<Token> peek = ReadToken(section.escapes);
				if (peek && peek->conditional) {
					accepted = EvaluateConditional(peek->text);
				} else {
					position_ = position;
					valid_ = valid;
				}
			}
			if (!accepted) {
				section.children.pop_back();
			}
		}
	}

	void ReportError(const char* error) const {
		Core::Get().engine().console().Printf("KeyValues Error: %s in file %s\n", error, resourceName_.c_str());
	}

	std::string text_;
	std::string resourceName_;
	size_t position_ = 0;
	bool valid_ = true;
};

// ------------------------------------------------------------------- files

// The Valve file system with every path ID on the mod folder; absolute
// paths are used as they are.
static std::string GameFilePath(const char* path) {
	if (path[0] == '/') {
		return path;
	}
	return Core::Get().engine().GamePath(path);
}

static std::optional<std::string> ReadGameFile(const char* path) {
	FILE* file = fopen(GameFilePath(path).c_str(), "rb");
	if (!file) {
		return std::nullopt;
	}
	std::string content;
	char chunk[8192];
	size_t read;
	while ((read = fread(chunk, 1, sizeof(chunk), file)) > 0) {
		content.append(chunk, read);
	}
	bool failed = ferror(file) != 0;
	fclose(file);
	if (failed) {
		return std::nullopt;
	}
	return content;
}

static bool WriteGameFile(const char* path, const std::string& content) {
	FILE* file = fopen(GameFilePath(path).c_str(), "wb");
	if (!file) {
		return false;
	}
	fwrite(content.data(), 1, content.size(), file);
	fclose(file);
	return true;
}

// ------------------------------------------------------------- handle type

static HandleType_t g_keyValuesType = 0;

class KeyValueNatives : public SMGlobalClass, public IHandleTypeDispatch {
 public:
	void OnSourceModAllInitialized() override {
		g_keyValuesType = handlesys->CreateType("KeyValues", this, 0, nullptr, nullptr, g_pCoreIdent, nullptr);
	}
	void OnSourceModShutdown() override {
		handlesys->RemoveType(g_keyValuesType, g_pCoreIdent);
		g_keyValuesType = 0;
	}
	void OnHandleDestroy(HandleType_t, void* object) override { delete static_cast<KeyValueStack*>(object); }
	bool GetHandleApproxSize(HandleType_t, void* object, unsigned int* size) override {
		auto stack = static_cast<KeyValueStack*>(object);
		*size = static_cast<unsigned int>(sizeof(KeyValueStack) + stack->path.size() * sizeof(KvNode*) +
		                                  ExportText(*stack->base).size());
		return true;
	}
};

static KeyValueNatives s_keyValueNatives;

static KeyValueStack* ReadStack(IPluginContext* context, cell_t value) {
	Handle_t handle = static_cast<Handle_t>(value);
	HandleSecurity security(nullptr, g_pCoreIdent);
	KeyValueStack* stack;
	HandleError error = handlesys->ReadHandle(handle, g_keyValuesType, &security, reinterpret_cast<void**>(&stack));
	if (error != HandleError_None) {
		context->ThrowNativeError("Invalid key value handle %x (error %d)", handle, error);
		return nullptr;
	}
	return stack;
}

// ------------------------------------------------------------------ setters

static cell_t KvSetString(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	char* keyName;
	if (!PluginCharsOrNullArg(context, params[2], &keyName)) {
		return 0;
	}
	char* value = PluginCharsArg(context, params[3]);
	if (!value) {
		return 0;
	}
	stack->Current()->FindKey(keyName, true)->SetString(value);
	return 1;
}

static cell_t KvSetNum(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	char* keyName;
	if (!PluginCharsOrNullArg(context, params[2], &keyName)) {
		return 0;
	}
	stack->Current()->FindKey(keyName, true)->SetInt(params[3]);
	return 1;
}

static cell_t KvSetUInt64(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	char* keyName;
	if (!PluginCharsOrNullArg(context, params[2], &keyName)) {
		return 0;
	}
	cell_t* cells = PluginCellsArg(context, params[3], 2);
	if (!cells) {
		return 0;
	}
	uint64_t value;
	memcpy(&value, cells, sizeof(value));
	stack->Current()->FindKey(keyName, true)->SetUInt64(value);
	return 1;
}

static cell_t KvSetFloat(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	char* keyName;
	if (!PluginCharsOrNullArg(context, params[2], &keyName)) {
		return 0;
	}
	stack->Current()->FindKey(keyName, true)->SetFloat(sp_ctof(params[3]));
	return 1;
}

static cell_t KvSetColor(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	char* keyName;
	if (!PluginCharsOrNullArg(context, params[2], &keyName)) {
		return 0;
	}
	Rgba color;
	for (int i = 0; i < 4; i++) {
		color.channel[i] = static_cast<uint8_t>(params[3 + i]);
	}
	stack->Current()->FindKey(keyName, true)->SetColor(color);
	return 1;
}

static cell_t KvSetVector(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	char* keyName;
	if (!PluginCharsOrNullArg(context, params[2], &keyName)) {
		return 0;
	}
	cell_t* vector = PluginCellsArg(context, params[3], 3);
	if (!vector) {
		return 0;
	}
	std::string value =
		FormatValue("%f %f %f", sp_ctof(vector[0]), sp_ctof(vector[1]), sp_ctof(vector[2]));
	stack->Current()->FindKey(keyName, true)->SetString(value);
	return 1;
}

// ------------------------------------------------------------------ getters

static cell_t KvGetString(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	char* defaultValue = PluginCharsArg(context, params[5]);
	if (!defaultValue) {
		return 0;
	}
	char* keyName;
	if (!PluginCharsOrNullArg(context, params[2], &keyName)) {
		return 0;
	}
	KvNode* key = stack->Current()->FindKey(keyName, false);
	const std::string* value = key ? key->GetString() : nullptr;
	return WritePluginString(context, params[3], params[4], value ? value->c_str() : defaultValue) ? 1 : 0;
}

static cell_t KvGetNum(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	char* keyName;
	if (!PluginCharsOrNullArg(context, params[2], &keyName)) {
		return 0;
	}
	KvNode* key = stack->Current()->FindKey(keyName, false);
	return key ? key->GetInt() : params[3];
}

static cell_t KvGetFloat(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	char* keyName;
	if (!PluginCharsOrNullArg(context, params[2], &keyName)) {
		return 0;
	}
	KvNode* key = stack->Current()->FindKey(keyName, false);
	return key ? sp_ftoc(key->GetFloat()) : params[3];
}

static cell_t KvGetColor(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	char* keyName;
	if (!PluginCharsOrNullArg(context, params[2], &keyName)) {
		return 0;
	}
	cell_t* channels[4];
	for (int i = 0; i < 4; i++) {
		if (!(channels[i] = PluginCellsArg(context, params[3 + i], 1))) {
			return 0;
		}
	}
	KvNode* key = stack->Current()->FindKey(keyName, false);
	Rgba color = key ? key->GetColor() : Rgba();
	for (int i = 0; i < 4; i++) {
		*channels[i] = color.channel[i];
	}
	return 1;
}

// The default is read from its first cell only, as SourceMod does.
static cell_t KvGetUInt64(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	char* keyName;
	if (!PluginCharsOrNullArg(context, params[2], &keyName)) {
		return 0;
	}
	cell_t* output = PluginCellsArg(context, params[3], 2);
	cell_t* defaultValue = output ? PluginCellsArg(context, params[4], 1) : nullptr;
	if (!defaultValue) {
		return 0;
	}
	KvNode* key = stack->Current()->FindKey(keyName, false);
	uint64_t value = key ? key->GetUInt64() : static_cast<uint64_t>(static_cast<int64_t>(*defaultValue));
	memcpy(output, &value, sizeof(value));
	return 1;
}

// SourceMod's own reader of "x y z": optional minus, digits, a fraction.
static void ParseVector(const char* value, cell_t* output) {
	for (int component = 0; *value && component < 3; component++) {
		while (*value == ' ') {
			value++;
		}
		bool negative = *value == '-';
		if (negative) {
			value++;
		}
		float number = 0.0f;
		for (; *value && isdigit(static_cast<unsigned char>(*value)); value++) {
			number = number * 10.0f + static_cast<float>(*value - '0');
		}
		if (*value == '.') {
			value++;
			float factor = 0.1f;
			for (; *value && isdigit(static_cast<unsigned char>(*value)); value++) {
				number += static_cast<float>(*value - '0') * factor;
				factor *= 0.1f;
			}
		}
		output[component] = sp_ftoc(negative ? -number : number);
	}
}

static cell_t KvGetVector(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	char* keyName;
	if (!PluginCharsOrNullArg(context, params[2], &keyName)) {
		return 0;
	}
	cell_t* output = PluginCellsArg(context, params[3], 3);
	cell_t* defaultVector = output ? PluginCellsArg(context, params[4], 3) : nullptr;
	if (!defaultVector) {
		return 0;
	}
	std::string defaultValue =
		FormatValue("%f %f %f", sp_ctof(defaultVector[0]), sp_ctof(defaultVector[1]), sp_ctof(defaultVector[2]));
	KvNode* key = stack->Current()->FindKey(keyName, false);
	const std::string* value = key ? key->GetString() : nullptr;
	ParseVector(value ? value->c_str() : defaultValue.c_str(), output);
	return 1;
}

static cell_t CreateKeyValues(IPluginContext* context, const cell_t* params) {
	char* name = PluginCharsArg(context, params[1]);
	if (!name) {
		return 0;
	}
	char* firstKey = PluginCharsArg(context, params[2]);
	if (!firstKey) {
		return 0;
	}
	char* firstValue = PluginCharsArg(context, params[3]);
	if (!firstValue) {
		return 0;
	}
	auto stack = new KeyValueStack();
	stack->base = std::make_unique<KvNode>(KeySymbols::Get().Intern(name), false);
	// KeyValues(name, firstKey, firstValue) sets the first key even when it
	// is empty, so a new tree's root starts as an empty string key; without
	// a first key the first value is ignored.
	stack->base->FindKey(firstKey, true)->SetString(*firstKey ? firstValue : "");
	stack->path.push_back(stack->base.get());
	Handle_t handle = handlesys->CreateHandle(g_keyValuesType, stack, context->GetIdentity(), g_pCoreIdent, nullptr);
	if (handle == BAD_HANDLE) {
		delete stack;
	}
	return handle;
}

// ---------------------------------------------------------------- traversal

static cell_t KvJumpToKey(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	char* name = PluginCharsArg(context, params[2]);
	if (!name) {
		return 0;
	}
	KvNode* key = stack->Current()->FindKey(name, params[3] != 0);
	if (!key) {
		return 0;
	}
	stack->path.push_back(key);
	return 1;
}

static cell_t KvJumpToKeySymbol(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	KvNode* key = stack->Current()->FindChild(params[2]);
	if (!key) {
		return 0;
	}
	stack->path.push_back(key);
	return 1;
}

static cell_t KvGotoFirstSubKey(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	bool sectionsOnly = params[2] != 0;
	KvNode* key = sectionsOnly ? stack->Current()->FirstTrueSubKey() : stack->Current()->FirstSubKey();
	if (!key) {
		return 0;
	}
	stack->path.push_back(key);
	return 1;
}

static cell_t KvGotoNextKey(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	bool sectionsOnly = params[2] != 0;
	KvNode* key = sectionsOnly ? stack->Current()->NextTrueSubKey() : stack->Current()->NextKey();
	if (!key) {
		return 0;
	}
	stack->path.back() = key;
	return 1;
}

static cell_t KvGoBack(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	if (stack->path.size() == 1) {
		return 0;
	}
	stack->path.pop_back();
	return 1;
}

// Older plugins pass no clearHistory, which means true.
static cell_t KvRewind(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	bool clearHistory = params[0] < 2 || params[2] != 0;
	if (clearHistory) {
		stack->path.resize(1);
	} else {
		stack->path.push_back(stack->path.front());
	}
	return 1;
}

static cell_t KvSavePosition(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	if (stack->path.size() < 2) {
		return 0;
	}
	stack->path.push_back(stack->Current());
	return 1;
}

static cell_t KvNodesInStack(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	return static_cast<cell_t>(stack->path.size() - 1);
}

static cell_t KvGetSectionName(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	return WritePluginString(context, params[2], params[3], stack->Current()->Name()) ? 1 : 0;
}

static cell_t KvSetSectionName(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	char* name = PluginCharsArg(context, params[2]);
	if (!name) {
		return 0;
	}
	stack->Current()->name = KeySymbols::Get().Intern(name);
	return 1;
}

static cell_t KvGetDataType(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	char* name = PluginCharsArg(context, params[2]);
	if (!name) {
		return 0;
	}
	KvNode* key = stack->Current()->FindKey(name, false);
	return static_cast<cell_t>(key ? key->type : KvType::None);
}

static cell_t KvSetEscapeSequences(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	stack->Current()->escapes = params[2] != 0;
	return 1;
}

// ----------------------------------------------------------------- deletion

static bool ThrowIfStackHolds(IPluginContext* context, const KeyValueStack& stack, const KvNode& key,
                              const char* native) {
	if (!stack.Holds(&key)) {
		return false;
	}
	context->ThrowNativeError(
		"%s would delete key \"%s\" while the traversal stack still holds it or a key below it "
		"(SourceMod would leave the stack on freed memory)",
		native, key.Name().c_str());
	return true;
}

// 1: deleted, the next key is current; -1: deleted, no next key, the parent
// is current; 0: nothing deleted.
static cell_t KvDeleteThis(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	if (stack->path.size() < 2) {
		return 0;
	}
	KvNode* doomed = stack->Current();
	stack->path.pop_back();
	KvNode* parent = stack->Current();
	if (!parent->HasChild(doomed)) {
		stack->path.push_back(doomed);
		return 0;
	}
	if (ThrowIfStackHolds(context, *stack, *doomed, "KvDeleteThis")) {
		stack->path.push_back(doomed);
		return 0;
	}
	KvNode* next = doomed->NextKey();
	parent->TakeChild(doomed);
	if (!next) {
		return -1;
	}
	stack->path.push_back(next);
	return 1;
}

static cell_t KvDeleteKey(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	char* name = PluginCharsArg(context, params[2]);
	if (!name) {
		return 0;
	}
	if (stack->path.size() < 2) {
		return 0;
	}
	KvNode* section = stack->Current();
	KvNode* doomed = section->FindKey(name, false);
	if (!doomed) {
		return 0;
	}
	if (doomed == section) {
		return context->ThrowNativeError("KvDeleteKey cannot delete the current key (an empty key name)");
	}
	if (!section->HasChild(doomed)) {
		return context->ThrowNativeError(
			"KvDeleteKey(\"%s\") names a key below a subkey; SourceMod only deletes direct subkeys and would "
			"corrupt the tree",
			name);
	}
	if (ThrowIfStackHolds(context, *stack, *doomed, "KvDeleteKey")) {
		return 0;
	}
	section->TakeChild(doomed);
	return 1;
}

// ------------------------------------------------------------------ copying

// KeyValues::CopySubkeys: the destination's subkeys are replaced by copies of
// the origin's; an origin without subkeys changes nothing.
static void CopySubkeys(const KvNode& origin, KvNode& destination, KeyValueStack& destinationStack) {
	if (origin.children.empty()) {
		return;
	}
	std::unique_ptr<KvNode> copies = origin.Copy();
	destinationStack.Release(std::move(destination.children));
	destination.children.clear();
	for (auto& copy : copies->children) {
		destination.AdoptChild(std::move(copy));
	}
}

static cell_t KvCopySubkeys(IPluginContext* context, const cell_t* params) {
	KeyValueStack* origin = ReadStack(context, params[1]);
	if (!origin) {
		return 0;
	}
	KeyValueStack* destination = ReadStack(context, params[2]);
	if (!destination) {
		return 0;
	}
	CopySubkeys(*origin->Current(), *destination->Current(), *destination);
	return 1;
}

// KeyValues.Import takes (destination, origin).
static cell_t KeyValuesImport(IPluginContext* context, const cell_t* params) {
	const cell_t swapped[3] = {2, params[2], params[1]};
	return KvCopySubkeys(context, swapped);
}

// ------------------------------------------------------------------ symbols

// SourceMod only answers below the root.
static cell_t KvGetNameSymbol(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	char* name = PluginCharsArg(context, params[2]);
	if (!name) {
		return 0;
	}
	if (stack->path.size() < 2) {
		return 0;
	}
	cell_t* id = PluginCellsArg(context, params[3], 1);
	if (!id) {
		return 0;
	}
	KvNode* key = stack->Current()->FindKey(name, false);
	if (!key) {
		return 0;
	}
	*id = key->name;
	return 1;
}

static cell_t KvFindKeyById(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	KvNode* key = stack->Current()->FindChild(params[2]);
	if (!key) {
		return 0;
	}
	return WritePluginString(context, params[3], params[4], key->Name()) ? 1 : 0;
}

static cell_t KvGetSectionSymbol(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	cell_t* symbol = PluginCellsArg(context, params[2], 1);
	if (!symbol) {
		return 0;
	}
	*symbol = stack->Current()->name;
	return 1;
}

// --------------------------------------------------------- text and files

static cell_t KeyValuesToFile(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	char* path = PluginCharsArg(context, params[2]);
	if (!path) {
		return 0;
	}
	return WriteGameFile(path, ExportText(*stack->Current()));
}

static cell_t FileToKeyValues(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	char* path = PluginCharsArg(context, params[2]);
	if (!path) {
		return 0;
	}
	std::optional<std::string> content = ReadGameFile(path);
	if (!content) {
		return 0;
	}
	KvTextReader(*content, path).LoadInto(*stack->Current(), *stack);
	return 1;
}

static cell_t StringToKeyValues(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	char* text = PluginCharsArg(context, params[2]);
	if (!text) {
		return 0;
	}
	char* resourceName = PluginCharsArg(context, params[3]);
	if (!resourceName) {
		return 0;
	}
	KvTextReader(text, resourceName)
		.LoadInto(*stack->Current(), *stack);
	return 1;
}

// Copies what fits as bytes and returns the whole length, as SourceMod does
// with its CUtlBuffer.
static cell_t KeyValuesExportToString(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	if (params[3] <= 0) {
		return context->ThrowNativeError("ExportToString needs a buffer of at least 1 byte, not %d", params[3]);
	}
	char* buffer = PluginBytes(context, params[2], params[3]);
	if (!buffer) {
		return context->ThrowNativeError("a buffer of %d bytes does not fit the plugin's memory", params[3]);
	}
	std::string text = ExportText(*stack->Current());
	size_t copied = std::min(text.size(), static_cast<size_t>(params[3]) - 1);
	memcpy(buffer, text.data(), copied);
	buffer[copied] = '\0';
	return static_cast<cell_t>(text.size());
}

static cell_t KeyValuesExportLength(IPluginContext* context, const cell_t* params) {
	KeyValueStack* stack = ReadStack(context, params[1]);
	if (!stack) {
		return 0;
	}
	return static_cast<cell_t>(ExportText(*stack->Current()).size());
}

REGISTER_NATIVES(keyValueNatives) {
	{"KvSetString", KvSetString},
	{"KvSetNum", KvSetNum},
	{"KvSetUInt64", KvSetUInt64},
	{"KvSetFloat", KvSetFloat},
	{"KvSetColor", KvSetColor},
	{"KvGetString", KvGetString},
	{"KvGetNum", KvGetNum},
	{"KvGetFloat", KvGetFloat},
	{"KvGetColor", KvGetColor},
	{"KvGetUInt64", KvGetUInt64},
	{"CreateKeyValues", CreateKeyValues},
	{"KvJumpToKey", KvJumpToKey},
	{"KvJumpToKeySymbol", KvJumpToKeySymbol},
	{"KvGotoNextKey", KvGotoNextKey},
	{"KvGotoFirstSubKey", KvGotoFirstSubKey},
	{"KvGoBack", KvGoBack},
	{"KvRewind", KvRewind},
	{"KvGetSectionName", KvGetSectionName},
	{"KvSetSectionName", KvSetSectionName},
	{"KvGetDataType", KvGetDataType},
	{"KeyValuesToFile", KeyValuesToFile},
	{"FileToKeyValues", FileToKeyValues},
	{"StringToKeyValues", StringToKeyValues},
	{"KvSetEscapeSequences", KvSetEscapeSequences},
	{"KvDeleteThis", KvDeleteThis},
	{"KvDeleteKey", KvDeleteKey},
	{"KvNodesInStack", KvNodesInStack},
	{"KvSavePosition", KvSavePosition},
	{"KvCopySubkeys", KvCopySubkeys},
	{"KvFindKeyById", KvFindKeyById},
	{"KvGetNameSymbol", KvGetNameSymbol},
	{"KvGetSectionSymbol", KvGetSectionSymbol},
	{"KvGetVector", KvGetVector},
	{"KvSetVector", KvSetVector},

	{"KeyValues.KeyValues", CreateKeyValues},
	{"KeyValues.SetString", KvSetString},
	{"KeyValues.SetNum", KvSetNum},
	{"KeyValues.SetUInt64", KvSetUInt64},
	{"KeyValues.SetFloat", KvSetFloat},
	{"KeyValues.SetColor", KvSetColor},
	{"KeyValues.GetString", KvGetString},
	{"KeyValues.GetNum", KvGetNum},
	{"KeyValues.GetFloat", KvGetFloat},
	{"KeyValues.GetColor", KvGetColor},
	{"KeyValues.GetUInt64", KvGetUInt64},
	{"KeyValues.JumpToKey", KvJumpToKey},
	{"KeyValues.JumpToKeySymbol", KvJumpToKeySymbol},
	{"KeyValues.GotoNextKey", KvGotoNextKey},
	{"KeyValues.GotoFirstSubKey", KvGotoFirstSubKey},
	{"KeyValues.GoBack", KvGoBack},
	{"KeyValues.Rewind", KvRewind},
	{"KeyValues.GetSectionName", KvGetSectionName},
	{"KeyValues.SetSectionName", KvSetSectionName},
	{"KeyValues.GetDataType", KvGetDataType},
	{"KeyValues.SetEscapeSequences", KvSetEscapeSequences},
	{"KeyValues.DeleteThis", KvDeleteThis},
	{"KeyValues.DeleteKey", KvDeleteKey},
	{"KeyValues.NodesInStack", KvNodesInStack},
	{"KeyValues.SavePosition", KvSavePosition},
	{"KeyValues.FindKeyById", KvFindKeyById},
	{"KeyValues.GetNameSymbol", KvGetNameSymbol},
	{"KeyValues.GetSectionSymbol", KvGetSectionSymbol},
	{"KeyValues.GetVector", KvGetVector},
	{"KeyValues.SetVector", KvSetVector},
	{"KeyValues.Import", KeyValuesImport},
	{"KeyValues.ImportFromFile", FileToKeyValues},
	{"KeyValues.ImportFromString", StringToKeyValues},
	{"KeyValues.ExportToFile", KeyValuesToFile},
	{"KeyValues.ExportToString", KeyValuesExportToString},
	{"KeyValues.ExportLength.get", KeyValuesExportLength},
	{nullptr, nullptr},
};

}  // namespace smrunner
