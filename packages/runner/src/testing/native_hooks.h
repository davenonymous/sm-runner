// Copyright 2026 davenonymous
//
// Hooks and mocks of natives for tests. A hooked native is rebound, in every
// plugin that is not a test plugin, to a wrapper that runs the test's pre
// hook, the original binding (unless the hook handled the call or a mock
// replaces it) and the post hook, and counts the calls. Plugins loaded
// later are wrapped when they load (IPluginsListener::OnPluginLoaded, after
// SourceMod bound their natives and before their OnPluginStart). Whenever
// SourceMod binds a hooked native anew, unbinds it (a provider plugin
// unloaded) or binds it late (an optional native whose provider loads), the
// patched VM tells the hooks at once and the new binding is wrapped, so a
// hook outlives a provider's reload; Unhook gives back only bindings that
// are still the wrapper's.
//
// The original binding is read from the VM's native table: SourcePawn keeps
// it right after the public sp_native_t (struct NativeEntry of
// vm/plugin-runtime.h). The layout is checked once, at the first hook,
// before any binding is read that way: the calling plugin's binding of
// Runner.HookNative (or Runner.MockNative) must be the runner's function,
// an object bound in its place must show where an object binding is kept,
// and the runner's function is bound again. A VM laid out otherwise is
// fatal.
#ifndef SMRUNNER_TESTING_NATIVE_HOOKS_H
#define SMRUNNER_TESTING_NATIVE_HOOKS_H

#include <IPluginSys.h>
#include <sp_vm_api.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace smrunner {

// One call of a hooked native, as the test's callbacks see it: its
// arguments (counting from 1) and their memory in the calling plugin. The
// accessors check what they are given and the caller's memory and say what
// is wrong in error instead.
class NativeCall {
 public:
	using Binding = std::function<cell_t(SourcePawn::IPluginContext*, const cell_t*)>;

	NativeCall(std::string name, SourcePawn::IPluginContext* caller, const cell_t* params, Binding original);

	const std::string& Name() const { return name_; }
	SourcePawn::IPluginContext* Caller() const { return caller_; }
	int ArgCount() const { return static_cast<int>(params_.size()) - 1; }
	cell_t Result() const { return result_; }
	void SetResult(cell_t result) { result_ = result; }

	// The argument as passed: a cell, or the address of a reference or array.
	bool Arg(cell_t n, cell_t* value, std::string* error) const;
	bool SetArg(cell_t n, cell_t value, std::string* error);
	// The cell a by-reference argument refers to.
	bool ArgRef(cell_t n, cell_t* value, std::string* error) const;
	bool SetArgRef(cell_t n, cell_t value, std::string* error);
	bool ArgString(cell_t n, std::string* text, std::string* error) const;
	// Writes text into a string buffer argument of size bytes.
	bool SetArgString(cell_t n, const std::string& text, cell_t size, std::string* error);
	// Copies count cells of an array argument into cells, or from cells.
	bool GetArgArray(cell_t n, cell_t* cells, cell_t count, std::string* error) const;
	bool SetArgArray(cell_t n, const cell_t* cells, cell_t count, std::string* error);

	// The binding the plugin had, with the arguments as they are now; 0
	// when it threw, which the caller then sees.
	cell_t CallOriginal();
	// Whether the last CallOriginal threw.
	bool OriginalThrew() const { return originalThrew_; }

 private:
	bool HasArg(cell_t n, std::string* error) const;
	cell_t* ArgCells(cell_t n, cell_t count, const char* what, std::string* error) const;

	std::string name_;
	SourcePawn::IPluginContext* caller_;
	std::vector<cell_t> params_;   // params_[0] is the count, as the VM passes them
	Binding original_;
	cell_t result_ = 0;
	bool originalThrew_ = false;
};

class HookedNative;
struct HookedNativeBinding;
class TestPlugins;

// natives_calls.cpp: the natives a plugin hooks and mocks with, whose
// binding the layout check knows.
cell_t Runner_HookNative(SourcePawn::IPluginContext* context, const cell_t* params);
cell_t Runner_MockNative(SourcePawn::IPluginContext* context, const cell_t* params);

class NativeHooks : public SourceMod::IPluginsListener {
 public:
	// Test plugins are never wrapped.
	explicit NativeHooks(const TestPlugins& testPlugins);
	~NativeHooks();

	void Start();   // after SourceMod started, before plugins load
	void Stop();    // after SourceMod shut down

	// caller is the plugin calling Runner.HookNative or Runner.MockNative.
	// pre and post may be null; a mock replaces the native.
	void Hook(SourcePawn::IPluginContext* caller, const std::string& name, SourcePawn::IPluginFunction* pre,
	          SourcePawn::IPluginFunction* post);
	void Mock(SourcePawn::IPluginContext* caller, const std::string& name, SourcePawn::IPluginFunction* mock);
	void Unhook(const std::string& name);
	int Calls(const std::string& name) const;

	// The call a hook or mock callback was given, while it runs.
	NativeCall* CallById(cell_t id);

	// IPluginsListener
	void OnPluginLoaded(SourceMod::IPlugin* plugin) override;
	void OnPluginDestroyed(SourceMod::IPlugin* plugin) override;

 private:
	friend class HookedNative;
	friend class NativeWrapper;
	cell_t Dispatch(HookedNative* native, NativeCall::Binding original, SourcePawn::IPluginContext* context,
	                const cell_t* params);
	// Records the native's binding in the plugin and wraps it when bound.
	void Wrap(SourceMod::IPlugin* plugin, HookedNative* native);
	void WrapBinding(HookedNative* native, HookedNativeBinding& binding);
	// The VM changed the binding of a native of runtime.
	void Rewrap(SourcePawn::IPluginRuntime* runtime, uint32_t index);
	static void OnNativeBindingChanged(SourcePawn::IPluginRuntime* runtime, uint32_t index);
	void VerifyVmLayout(SourcePawn::IPluginContext* caller);
	HookedNative* Find(const std::string& name) const;
	HookedNative* Ensure(const std::string& name);

	const TestPlugins& testPlugins_;
	std::map<std::string, std::unique_ptr<HookedNative>> natives_;
	std::vector<NativeCall*> calls_;
	bool started_ = false;
	bool layoutVerified_ = false;
	bool rebinding_ = false;   // the hooks change a binding themselves
};

}  // namespace smrunner

#endif
