// Copyright 2026 davenonymous
#include "testing/debugger.h"

#include <string.h>

#include <functional>

#include "core/globals.h"
#include "diagnostics.h"
#include "sandbox.h"
#include "testing/plugin_memory.h"
#include "testing/test_plugins.h"

using namespace SourcePawn;
using namespace SourceMod;

namespace smrunner {

// The first argument of a SourcePawn frame is 12 bytes above the frame
// pointer (saved frame, return address, argument count).
static constexpr int32_t kFirstArgument = 12;

// The debugger the VM's debug break handler calls; the VM has only one.
static Debugger* sBreakHandler = nullptr;

Debugger::Debugger(const TestPlugins& testPlugins) : testPlugins_(testPlugins) {}

Debugger::~Debugger() = default;

void Debugger::Start() {
	if (started_) {
		return;
	}
	if (sBreakHandler) {
		Fatal("a debugger handles the VM's debug breaks already");
	}
	started_ = true;
	sBreakHandler = this;
	scripts->AddPluginsListener(this);
	if (g_pSourcePawn->SetDebugBreakHandler(OnBreak) != SP_ERROR_NONE) {
		Fatal("the SourcePawn VM did not accept the debug break handler");
	}
}

void Debugger::Stop() {
	if (!started_) {
		return;
	}
	started_ = false;
	sBreakHandler = nullptr;
	g_pSourcePawn->SetDebugBreakHandler(nullptr);
	scripts->RemovePluginsListener(this);
}

void Debugger::OnBreak(IPluginContext* context, sp_debug_break_info_t& info, const IErrorReport* report) {
	if (report != nullptr || !sBreakHandler) {
		return;
	}
	sBreakHandler->Hit(context, info.cip, info.frm);
}

static bool FileMatches(const std::string& debugFile, const std::string& wanted) {
	if (debugFile == wanted) {
		return true;
	}
	return debugFile.size() > wanted.size() &&
	       debugFile.compare(debugFile.size() - wanted.size(), wanted.size(), wanted) == 0 &&
	       debugFile[debugFile.size() - wanted.size() - 1] == '/';
}

bool Debugger::Applies(const Breakpoint& breakpoint, IPlugin* plugin) const {
	if (!breakpoint.plugin.empty()) {
		return BaseName(plugin->GetFilename()) == BaseName(breakpoint.plugin);
	}
	return !testPlugins_.Contains(plugin);
}

void Debugger::Resolve(Breakpoint& breakpoint, IPlugin* plugin) {
	if (!Applies(breakpoint, plugin)) {
		return;
	}
	IPluginDebugInfo* debug = plugin->GetRuntime()->GetDebugInfo();
	if (!debug) {
		return;
	}
	if (breakpoint.file.empty()) {
		ResolveFunction(breakpoint, plugin, debug);
		return;
	}
	for (size_t i = 0; i < debug->NumFiles(); i++) {
		const char* name = debug->GetFileName(i);
		if (!name || !FileMatches(name, breakpoint.file)) {
			continue;
		}
		// The VM's line table counts from 0: LookupLine adds 1 to what it
		// reports, LookupLineAddress takes the line as stored.
		ucell_t address;
		if (debug->LookupLineAddress(static_cast<uint32_t>(breakpoint.line - 1), name, &address) == SP_ERROR_NONE) {
			addresses_[plugin->GetBaseContext()].emplace(static_cast<cell_t>(address), breakpoint.id);
			breakpoint.resolved = true;
		}
	}
}

// The function is looked up in the runner's own reading of the plugin, not
// by the VM alone: with rtti.methods, SmxV1Image::LookupFunctionAddress
// answers a name no function has with the plugin's first statement. The
// VM's answer is taken only when it is a statement of that very function.
void Debugger::ResolveFunction(Breakpoint& breakpoint, IPlugin* plugin, IPluginDebugInfo* debug) {
	std::string error;
	const SmxFile* smx = Symbols(plugin, &error);
	if (!smx) {
		return;
	}
	for (const SmxFunction& function : smx->Functions()) {
		if (function.name != breakpoint.function) {
			continue;
		}
		const char* file = nullptr;
		if (debug->LookupFile(function.codeStart, &file) != SP_ERROR_NONE || !file) {
			continue;
		}
		ucell_t address;
		if (debug->LookupFunctionAddress(function.name.c_str(), file, &address) != SP_ERROR_NONE ||
		    address < function.codeStart || address >= function.codeEnd) {
			continue;   // the function has no statement to stop at
		}
		const char* found = nullptr;
		if (debug->LookupFunction(address, &found) != SP_ERROR_NONE || !found || function.name != found) {
			continue;
		}
		addresses_[plugin->GetBaseContext()].emplace(static_cast<cell_t>(address), breakpoint.id);
		breakpoint.resolved = true;
	}
}

bool Debugger::AnyPluginUnderTest() const {
	bool any = false;
	IPluginIterator* iterator = scripts->GetPluginIterator();
	for (; iterator->MorePlugins(); iterator->NextPlugin()) {
		if (!testPlugins_.Contains(iterator->GetPlugin())) {
			any = true;
		}
	}
	iterator->Release();
	return any;
}

static void ResolveInLoadedPlugins(const std::function<void(IPlugin*)>& resolve) {
	IPluginIterator* iterator = scripts->GetPluginIterator();
	for (; iterator->MorePlugins(); iterator->NextPlugin()) {
		resolve(iterator->GetPlugin());
	}
	iterator->Release();
}

int Debugger::AddLine(const std::string& file, int line, const std::string& plugin, IPluginFunction* callback,
                      cell_t data, std::string* error) {
	if (file.empty() || line < 1) {
		*error = "a breakpoint needs a file and a line number of 1 or more";
		return 0;
	}
	Breakpoint breakpoint{nextId_++, file, line, "", plugin, callback, data};
	ResolveInLoadedPlugins([&](IPlugin* loaded) { Resolve(breakpoint, loaded); });
	if (!breakpoint.resolved && AnyPluginUnderTest()) {
		*error = "no plugin" + (plugin.empty() ? std::string() : " " + plugin) + " has a statement at or after " +
		         file + ":" + std::to_string(line);
		return 0;
	}
	breakpoints_.push_back(breakpoint);
	return breakpoint.id;
}

int Debugger::AddFunction(const std::string& function, const std::string& plugin, IPluginFunction* callback,
                          cell_t data, std::string* error) {
	if (function.empty()) {
		*error = "a breakpoint needs a function name";
		return 0;
	}
	Breakpoint breakpoint{nextId_++, "", 0, function, plugin, callback, data};
	ResolveInLoadedPlugins([&](IPlugin* loaded) { Resolve(breakpoint, loaded); });
	if (!breakpoint.resolved && AnyPluginUnderTest()) {
		*error = "no plugin" + (plugin.empty() ? std::string() : " " + plugin) + " has a function " + function;
		return 0;
	}
	breakpoints_.push_back(breakpoint);
	return breakpoint.id;
}

bool Debugger::Remove(int id) {
	for (auto it = breakpoints_.begin(); it != breakpoints_.end(); ++it) {
		if (it->id != id) {
			continue;
		}
		breakpoints_.erase(it);
		for (auto& entry : addresses_) {
			for (auto address = entry.second.begin(); address != entry.second.end();) {
				address = address->second == id ? entry.second.erase(address) : std::next(address);
			}
		}
		return true;
	}
	return false;
}

int Debugger::Hits(int id) const {
	for (const Breakpoint& breakpoint : breakpoints_) {
		if (breakpoint.id == id) {
			return breakpoint.hits;
		}
	}
	return -1;
}

void Debugger::OnPluginLoaded(IPlugin* plugin) {
	for (Breakpoint& breakpoint : breakpoints_) {
		Resolve(breakpoint, plugin);
	}
}

void Debugger::OnPluginDestroyed(IPlugin* plugin) {
	addresses_.erase(plugin->GetBaseContext());
	symbols_.erase(plugin);
}

void Debugger::Hit(IPluginContext* context, cell_t cip, cell_t frm) {
	if (inCallback_) {
		return;
	}
	auto found = addresses_.find(context);
	if (found == addresses_.end()) {
		return;
	}
	auto range = found->second.equal_range(cip);
	if (range.first == range.second) {
		return;
	}
	std::vector<int> ids;
	for (auto it = range.first; it != range.second; ++it) {
		ids.push_back(it->second);
	}
	IPlugin* plugin = scripts->FindPluginByContext(context->GetContext());
	std::string error;
	const SmxFile* smx = plugin ? Symbols(plugin, &error) : nullptr;
	for (int id : ids) {
		Breakpoint* breakpoint = nullptr;
		for (Breakpoint& candidate : breakpoints_) {
			if (candidate.id == id) {
				breakpoint = &candidate;
			}
		}
		if (!breakpoint) {
			continue;
		}
		breakpoint->hits++;
		BreakFrame frame{context, plugin, smx, cip, frm, id};
		frames_.push_back(&frame);
		inCallback_ = true;
		IPluginFunction* callback = breakpoint->callback;
		cell_t data = breakpoint->data;
		callback->PushCell(static_cast<cell_t>(frames_.size()));
		callback->PushCell(data);
		callback->Execute(nullptr);
		inCallback_ = false;
		frames_.pop_back();
	}
}

BreakFrame* Debugger::FrameById(cell_t id) {
	if (id < 1 || id > static_cast<cell_t>(frames_.size())) {
		return nullptr;
	}
	return frames_[id - 1];
}

const SmxFile* Debugger::Symbols(IPlugin* plugin, std::string* error) {
	auto found = symbols_.find(plugin);
	if (found != symbols_.end()) {
		return found->second.get();
	}
	std::unique_ptr<SmxFile> smx = SmxFile::Load(plugin->GetRuntime()->GetFilename(), error);
	const SmxFile* result = smx.get();
	if (smx) {
		symbols_[plugin] = std::move(smx);
	}
	return result;
}

uint32_t BreakFrame::Line() const {
	IPluginDebugInfo* debug = context->GetRuntime()->GetDebugInfo();
	uint32_t line = 0;
	if (debug) {
		debug->LookupLine(static_cast<ucell_t>(cip), &line);
	}
	return line;
}

std::string BreakFrame::File() const {
	IPluginDebugInfo* debug = context->GetRuntime()->GetDebugInfo();
	const char* file = nullptr;
	if (debug) {
		debug->LookupFile(static_cast<ucell_t>(cip), &file);
	}
	return file ? file : "";
}

std::string BreakFrame::Function() const {
	IPluginDebugInfo* debug = context->GetRuntime()->GetDebugInfo();
	const char* name = nullptr;
	if (debug) {
		debug->LookupFunction(static_cast<ucell_t>(cip), &name);
	}
	return name ? name : "";
}

std::optional<PluginVariable> Debugger::Resolve(const BreakFrame& frame, const std::string& name, std::string* error) {
	if (!frame.smx) {
		*error = "the plugin has no debug information";
		return std::nullopt;
	}
	const SmxVariable* variable = frame.smx->FindLocal(static_cast<uint32_t>(frame.cip), name);
	if (!variable) {
		if (!frame.plugin) {
			*error = "the stopped plugin has no local " + name + " and is unknown to SourceMod";
			return std::nullopt;
		}
		return ResolveGlobal(frame.plugin, name, error);
	}
	bool isArray = !variable->type.dims.empty();
	bool held = isArray;   // a local array's slot holds the address of its storage
	if (variable->scope == SmxVariable::Scope::Argument) {
		const SmxFunction* function = frame.smx->FunctionAt(static_cast<uint32_t>(frame.cip));
		size_t index = static_cast<size_t>((variable->address - kFirstArgument) / 4);
		held = isArray || (function && index < function->byRefArguments.size() && function->byRefArguments[index]);
	} else if (variable->scope != SmxVariable::Scope::Local) {
		// Statics are stored in place, as globals are.
		return PluginVariable(frame.context, variable, variable->address, false);
	}
	return PluginVariable(frame.context, variable, frame.frm + variable->address, held);
}

std::optional<PluginVariable> Debugger::ResolveGlobal(IPlugin* plugin, const std::string& name, std::string* error) {
	const SmxFile* smx = Symbols(plugin, error);
	if (!smx) {
		return std::nullopt;
	}
	const SmxVariable* variable = smx->FindGlobal(name);
	if (!variable) {
		*error = std::string(plugin->GetFilename()) + " has no variable " + name +
		         (smx->Globals().empty() ? " (it has no debug variables: compile it with spcomp 1.11 or later)" : "");
		return std::nullopt;
	}
	return PluginVariable(plugin->GetBaseContext(), variable, variable->address, false);
}

std::string PluginVariable::NotInMemory() const {
	return variable_->name + " is not in the plugin's memory";
}

bool PluginVariable::Storage(cell_t* address, std::string* error) const {
	if (!held_) {
		*address = slot_;
		return true;
	}
	cell_t* holder = PluginCells(context_, slot_, 1);
	if (!holder) {
		*error = NotInMemory();
		return false;
	}
	*address = *holder;
	return true;
}

cell_t* PluginVariable::Element(cell_t index, std::string* error) const {
	const SmxType& type = variable_->type;
	if (type.dims.size() > 1) {
		*error = variable_->name + " is a " + type.Describe() +
		         "; only single values and one-dimensional arrays can be read";
		return nullptr;
	}
	if (type.IsString()) {
		*error = variable_->name + " is a string; use GetString and SetString";
		return nullptr;
	}
	bool isArray = !type.dims.empty();
	bool outOfBounds = index < 0 || (!isArray && index != 0) ||
	                   (isArray && type.dims[0] != 0 && static_cast<uint32_t>(index) >= type.dims[0]);
	if (outOfBounds) {
		*error = "index " + std::to_string(index) + " is out of the bounds of " + variable_->name + " (" +
		         type.Describe() + ")";
		return nullptr;
	}
	cell_t address;
	if (!Storage(&address, error)) {
		return nullptr;
	}
	cell_t* cell = PluginCells(context_, address + index * 4, 1);
	if (!cell) {
		*error = NotInMemory();
	}
	return cell;
}

bool PluginVariable::ReadCell(cell_t index, cell_t* value, std::string* error) const {
	cell_t* cell = Element(index, error);
	if (!cell) {
		return false;
	}
	*value = *cell;
	return true;
}

bool PluginVariable::WriteCell(cell_t index, cell_t value, std::string* error) const {
	cell_t* cell = Element(index, error);
	if (!cell) {
		return false;
	}
	*cell = value;
	return true;
}

bool PluginVariable::StringOrError(std::string* error) const {
	if (variable_->type.IsString()) {
		return true;
	}
	*error = variable_->name + " is not a string (" + variable_->type.Describe() + ")";
	return false;
}

bool PluginVariable::ReadString(std::string* text, std::string* error) const {
	cell_t address;
	if (!StringOrError(error) || !Storage(&address, error)) {
		return false;
	}
	uint32_t size = variable_->type.dims[0];
	if (size == 0) {
		// No fixed size (const char[] arguments): the string ends at its NUL.
		char* chars = PluginChars(context_, address);
		if (!chars) {
			*error = NotInMemory();
			return false;
		}
		*text = chars;
		return true;
	}
	char* chars = PluginBytes(context_, address, static_cast<cell_t>(size));
	if (!chars) {
		*error = NotInMemory();
		return false;
	}
	*text = std::string(chars, strnlen(chars, size));
	return true;
}

bool PluginVariable::WriteString(const std::string& text, std::string* error) const {
	cell_t address;
	if (!StringOrError(error) || !Storage(&address, error)) {
		return false;
	}
	uint32_t size = variable_->type.dims[0];
	if (size == 0) {
		*error = variable_->name + " has no fixed size (" + variable_->type.Describe() + "); it cannot be written";
		return false;
	}
	if (!PluginBytes(context_, address, static_cast<cell_t>(size)) ||
	    context_->StringToLocalUTF8(address, size, text.c_str(), nullptr) != SP_ERROR_NONE) {
		*error = NotInMemory();
		return false;
	}
	return true;
}

}  // namespace smrunner
