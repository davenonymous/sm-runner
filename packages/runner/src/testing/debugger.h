// Copyright 2026 davenonymous
//
// Breakpoints and variable access for tests, on the VM's debug break (the
// VM calls back at every statement once debug breaks are on): a breakpoint
// is a source line (file and line) or a function's first statement, in
// every plugin that is not a test plugin or in one named plugin; when a
// plugin reaches it, the test's callback runs while the plugin waits, and
// reads and writes the plugin's locals, arguments and globals by name, from
// the plugin's debug information (SmxFile). Globals of any plugin can also
// be read and written outside of breakpoints.
#ifndef SMRUNNER_TESTING_DEBUGGER_H
#define SMRUNNER_TESTING_DEBUGGER_H

#include <IPluginSys.h>
#include <sp_vm_api.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "smx/smx_file.h"

namespace smrunner {

// A variable of a plugin, found by name, and the rules of reading and
// writing it: single values and cells of one-dimensional arrays within their
// bounds, strings within their size; every access checked against the
// plugin's memory, with what is wrong in error instead.
class PluginVariable {
 public:
	// slot is the address of the value or of an array's first element, or,
	// when held, of the cell that holds that address (a local array or a
	// by-reference argument).
	PluginVariable(SourcePawn::IPluginContext* context, const SmxVariable* variable, cell_t slot, bool held)
		: context_(context), variable_(variable), slot_(slot), held_(held) {}

	const std::string& Name() const { return variable_->name; }

	bool ReadCell(cell_t index, cell_t* value, std::string* error) const;
	bool WriteCell(cell_t index, cell_t value, std::string* error) const;
	bool ReadString(std::string* text, std::string* error) const;
	// Only strings of a fixed size can be written; longer text is cut.
	bool WriteString(const std::string& text, std::string* error) const;

 private:
	bool Storage(cell_t* address, std::string* error) const;
	cell_t* Element(cell_t index, std::string* error) const;
	bool StringOrError(std::string* error) const;
	std::string NotInMemory() const;

	SourcePawn::IPluginContext* context_;
	const SmxVariable* variable_;
	cell_t slot_;
	bool held_;
};

// A plugin stopped at a breakpoint.
struct BreakFrame {
	SourcePawn::IPluginContext* context;
	SourceMod::IPlugin* plugin;   // null when SourceMod knows no plugin of the context
	const SmxFile* smx;
	cell_t cip;
	cell_t frm;
	int breakpoint;

	// Where the plugin stopped, from its debug information: 0 and empty
	// without one.
	uint32_t Line() const;
	std::string File() const;
	std::string Function() const;
};

class TestPlugins;

class Debugger : public SourceMod::IPluginsListener {
 public:
	// Breakpoints without a named plugin skip the test plugins.
	explicit Debugger(const TestPlugins& testPlugins);
	~Debugger();

	// After SourceMod started, before plugins load: the VM's one debug break
	// handler is this debugger's until Stop.
	void Start();
	void Stop();

	// Returns the breakpoint's id, or 0 with a reason in error.
	int AddLine(const std::string& file, int line, const std::string& plugin, SourcePawn::IPluginFunction* callback,
	            cell_t data, std::string* error);
	int AddFunction(const std::string& function, const std::string& plugin, SourcePawn::IPluginFunction* callback,
	                cell_t data, std::string* error);
	bool Remove(int id);
	int Hits(int id) const;

	BreakFrame* FrameById(cell_t id);

	// The plugin's debug information, read once.
	const SmxFile* Symbols(SourceMod::IPlugin* plugin, std::string* error);

	// A variable visible in a frame (locals first), or a global of a plugin;
	// none with the reason in error.
	std::optional<PluginVariable> Resolve(const BreakFrame& frame, const std::string& name, std::string* error);
	std::optional<PluginVariable> ResolveGlobal(SourceMod::IPlugin* plugin, const std::string& name,
	                                            std::string* error);

	// IPluginsListener
	void OnPluginLoaded(SourceMod::IPlugin* plugin) override;
	void OnPluginDestroyed(SourceMod::IPlugin* plugin) override;

 private:
	struct Breakpoint {
		int id;
		std::string file;       // empty for a function breakpoint
		int line;
		std::string function;
		std::string plugin;     // empty: every plugin that is not a test plugin
		SourcePawn::IPluginFunction* callback;
		cell_t data;
		int hits = 0;
		bool resolved = false;  // in at least one plugin
	};

	static void OnBreak(SourcePawn::IPluginContext* context, sp_debug_break_info_t& info,
	                    const SourcePawn::IErrorReport* report);
	void Hit(SourcePawn::IPluginContext* context, cell_t cip, cell_t frm);
	void Resolve(Breakpoint& breakpoint, SourceMod::IPlugin* plugin);
	void ResolveFunction(Breakpoint& breakpoint, SourceMod::IPlugin* plugin, SourcePawn::IPluginDebugInfo* debug);
	bool Applies(const Breakpoint& breakpoint, SourceMod::IPlugin* plugin) const;
	bool AnyPluginUnderTest() const;

	const TestPlugins& testPlugins_;

	std::vector<Breakpoint> breakpoints_;
	// The breakpoints' addresses per plugin context: cip -> breakpoint ids.
	std::map<SourcePawn::IPluginContext*, std::multimap<cell_t, int>> addresses_;
	std::map<SourceMod::IPlugin*, std::unique_ptr<SmxFile>> symbols_;
	std::vector<BreakFrame*> frames_;
	int nextId_ = 1;
	bool started_ = false;
	bool inCallback_ = false;
};

}  // namespace smrunner

#endif
