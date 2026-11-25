// Copyright 2026 davenonymous
//
// A reader for what the runner needs from a compiled plugin (.smx) besides
// what the VM offers: its public functions (to find tests) and its debug
// variables, globals and locals with their scopes and types (to read and
// write variables at breakpoints). Entries the reader cannot use, such as
// built-in constants that spcomp records as globals without storage, are
// left out. The format is SourcePawn's (include/smx/smx-v1.h,
// smx-typeinfo.h).
#ifndef SMRUNNER_SMX_SMX_FILE_H
#define SMRUNNER_SMX_SMX_FILE_H

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

namespace smrunner {

// A variable's type as far as the runner reads and writes it.
struct SmxType {
	enum class Base { Bool, Int, Float, Char, Any, Function, Enum, EnumStruct, Other };
	Base base = Base::Other;
	// Array dimensions, outermost first; 0 is an unsized dimension, which is
	// held by reference.
	std::vector<uint32_t> dims;
	bool isConst = false;
	std::string name;   // of an enum or enum struct

	bool IsScalar() const { return dims.empty(); }
	bool IsString() const { return dims.size() == 1 && base == Base::Char; }
	std::string Describe() const;
};

struct SmxVariable {
	enum class Scope { Global, Local, Static, Argument };
	std::string name;
	Scope scope = Scope::Global;
	int32_t address = 0;   // data offset (global, static) or frame offset (local, argument)
	uint32_t codeStart = 0;
	uint32_t codeEnd = 0;
	SmxType type;
};

struct SmxFunction {
	std::string name;
	uint32_t codeStart = 0;   // [codeStart, codeEnd) in the code section
	uint32_t codeEnd = 0;
	bool isPublic = false;
	std::vector<bool> byRefArguments;   // by argument index, from the signature
	std::vector<SmxVariable> locals;
};

class SmxFile {
 public:
	// Reads a plugin file; returns null with a reason on error.
	static std::unique_ptr<SmxFile> Load(const std::string& path, std::string* error);

	const std::vector<std::string>& Publics() const { return publics_; }
	const std::vector<SmxFunction>& Functions() const { return functions_; }
	const std::vector<SmxVariable>& Globals() const { return globals_; }

	const SmxFunction* FunctionAt(uint32_t cip) const;
	const SmxFunction* FindFunction(const std::string& name) const;
	const SmxVariable* FindGlobal(const std::string& name) const;
	// The local or argument visible at cip with that name, innermost scope
	// first.
	const SmxVariable* FindLocal(uint32_t cip, const std::string& name) const;

 private:
	std::vector<std::string> publics_;
	std::vector<SmxFunction> functions_;
	std::vector<SmxVariable> globals_;
};

}  // namespace smrunner

#endif
