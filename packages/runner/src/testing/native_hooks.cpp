// Copyright 2026 davenonymous
#include "testing/native_hooks.h"

#include <IForwardSys.h>
#include <amtl/am-refcounting.h>

#include <algorithm>

#include "core/core.h"
#include "core/globals.h"
#include "diagnostics.h"
#include "testing/plugin_memory.h"
#include "testing/test_plugins.h"

using namespace SourcePawn;
using namespace SourceMod;

namespace smrunner {

namespace {

// vm/plugin-runtime.h: struct NativeEntry : public sp_native_t
// { SPVM_NATIVE_FUNC legacy_fn; RefPtr<INativeCallback> callback; };
struct VmNativeEntry : public sp_native_t {
	SPVM_NATIVE_FUNC legacy_fn;
	INativeCallback* callback;
};

struct Original {
	SPVM_NATIVE_FUNC function = nullptr;
	ke::RefPtr<INativeCallback> callback;
	uint32_t flags = 0;
	void* user = nullptr;

	cell_t Invoke(IPluginContext* context, const cell_t* params) const {
		if (callback) {
			return callback->Invoke(context, params);
		}
		return function(context, params);
	}
};

}  // namespace

class NativeWrapper : public INativeCallback {
 public:
	NativeWrapper(HookedNative* native, Original original) : native_(native), original_(std::move(original)) {}
	void AddRef() override { references_++; }
	void Release() override {
		if (--references_ == 0) {
			delete this;
		}
	}
	int Invoke(IPluginContext* context, const cell_t* params) override;
	const Original& original() const { return original_; }

 private:
	HookedNative* native_;
	Original original_;
	uintptr_t references_ = 0;
};

// The native in one plugin; without a wrapper while the native is not bound
// there (an optional native, a provider unloaded), wrapped as soon as
// SourceMod binds it.
struct HookedNativeBinding {
	IPluginRuntime* runtime;
	uint32_t index;
	ke::RefPtr<NativeWrapper> wrapper;
};

class HookedNative {
 public:
	HookedNative(NativeHooks* hooks, std::string name) : hooks_(hooks), name_(std::move(name)) {}

	using Binding = HookedNativeBinding;

	const std::string& name() const { return name_; }
	NativeHooks& hooks() const { return *hooks_; }
	IPluginFunction* pre = nullptr;
	IPluginFunction* post = nullptr;
	IPluginFunction* mock = nullptr;
	int calls = 0;
	std::vector<Binding> bindings;

 private:
	NativeHooks* hooks_;
	std::string name_;
};

int NativeWrapper::Invoke(IPluginContext* context, const cell_t* params) {
	// A callback may unhook the native, which drops the bindings' references
	// to this wrapper while it runs; the VM holds none.
	ke::RefPtr<NativeWrapper> self(this);
	Original original = original_;
	return native_->hooks().Dispatch(
		native_, [original](IPluginContext* caller, const cell_t* args) { return original.Invoke(caller, args); },
		context, params);
}

NativeHooks::NativeHooks(const TestPlugins& testPlugins) : testPlugins_(testPlugins) {}

NativeHooks::~NativeHooks() = default;

// The one hooks object the VM's binding listener reports to.
static NativeHooks* sBindingListener = nullptr;

void NativeHooks::Start() {
	if (started_) {
		return;
	}
	if (sBindingListener) {
		Fatal("native hooks follow the VM's native bindings already");
	}
	started_ = true;
	sBindingListener = this;
	Core::Get().SetNativeBindingListener(OnNativeBindingChanged);
	scripts->AddPluginsListener(this);
}

void NativeHooks::Stop() {
	if (!started_) {
		return;
	}
	started_ = false;
	scripts->RemovePluginsListener(this);
	Core::Get().SetNativeBindingListener(nullptr);
	sBindingListener = nullptr;
}

void NativeHooks::OnNativeBindingChanged(IPluginRuntime* runtime, uint32_t index) {
	if (sBindingListener && !sBindingListener->rebinding_) {
		sBindingListener->Rewrap(runtime, index);
	}
}

HookedNative* NativeHooks::Find(const std::string& name) const {
	auto found = natives_.find(name);
	return found == natives_.end() ? nullptr : found->second.get();
}

static const VmNativeEntry* EntryOf(IPluginRuntime* runtime, uint32_t index) {
	return static_cast<const VmNativeEntry*>(runtime->GetNative(index));
}

static bool IsWrappedBy(const VmNativeEntry* entry, const NativeWrapper* wrapper) {
	return wrapper && entry && entry->status == SP_NATIVE_BOUND && entry->legacy_fn == nullptr &&
	       entry->callback == wrapper;
}

void NativeHooks::Wrap(IPlugin* plugin, HookedNative* native) {
	if (testPlugins_.Contains(plugin)) {
		return;
	}
	IPluginRuntime* runtime = plugin->GetRuntime();
	uint32_t index;
	if (runtime->FindNativeByName(native->name().c_str(), &index) != SP_ERROR_NONE) {
		return;
	}
	for (HookedNative::Binding& binding : native->bindings) {
		if (binding.runtime == runtime && binding.index == index) {
			WrapBinding(native, binding);
			return;
		}
	}
	native->bindings.push_back({runtime, index, nullptr});
	WrapBinding(native, native->bindings.back());
}

void NativeHooks::WrapBinding(HookedNative* native, HookedNativeBinding& binding) {
	const VmNativeEntry* entry = EntryOf(binding.runtime, binding.index);
	if (IsWrappedBy(entry, binding.wrapper.get())) {
		return;
	}
	binding.wrapper = nullptr;
	if (!entry || entry->status != SP_NATIVE_BOUND) {
		return;   // wrapped when SourceMod binds it
	}
	if (!layoutVerified_) {
		Fatal("native %s was to be wrapped before the SourcePawn VM's native table was checked",
		      native->name().c_str());
	}
	Original original;
	original.function = entry->legacy_fn;
	original.callback = entry->callback;
	original.flags = entry->flags;
	original.user = entry->user;
	ke::RefPtr<NativeWrapper> wrapper = new NativeWrapper(native, original);
	rebinding_ = true;
	binding.runtime->UpdateNativeBindingObject(binding.index, wrapper.get(), entry->flags, entry->user);
	rebinding_ = false;
	binding.wrapper = wrapper;
}

void NativeHooks::Rewrap(IPluginRuntime* runtime, uint32_t index) {
	for (auto& entry : natives_) {
		for (HookedNative::Binding& binding : entry.second->bindings) {
			if (binding.runtime == runtime && binding.index == index) {
				WrapBinding(entry.second.get(), binding);
			}
		}
	}
}

// A probe binding for the layout check: never called.
class LayoutProbe : public INativeCallback {
 public:
	void AddRef() override { references_++; }
	void Release() override {
		if (--references_ == 0) {
			delete this;
		}
	}
	int Invoke(IPluginContext* context, const cell_t* params) override {
		return context->ThrowNativeError("smrunner's layout probe was called");
	}

 private:
	uintptr_t references_ = 0;
};

void NativeHooks::VerifyVmLayout(IPluginContext* caller) {
	if (layoutVerified_) {
		return;
	}
	IPluginRuntime* runtime = caller->GetRuntime();
	const char* name = "Runner.HookNative";
	SPVM_NATIVE_FUNC function = Runner_HookNative;
	uint32_t index;
	if (runtime->FindNativeByName(name, &index) != SP_ERROR_NONE) {
		name = "Runner.MockNative";
		function = Runner_MockNative;
		if (runtime->FindNativeByName(name, &index) != SP_ERROR_NONE) {
			Fatal("a plugin hooks natives without Runner.HookNative or Runner.MockNative");
		}
	}
	auto fail = [name](const char* what) {
		Fatal("the SourcePawn VM's native table is not laid out as smrunner expects: %s (native %s)", what, name);
	};
	const VmNativeEntry* entry = EntryOf(runtime, index);
	if (!entry || entry->status != SP_NATIVE_BOUND || entry->legacy_fn != function || entry->callback != nullptr) {
		fail("the runner's function is not where a function binding is kept");
	}
	uint32_t flags = entry->flags;
	void* user = entry->user;
	ke::RefPtr<LayoutProbe> probe = new LayoutProbe;
	rebinding_ = true;
	runtime->UpdateNativeBindingObject(index, probe.get(), flags, user);
	bool objectInPlace = entry->legacy_fn == nullptr && entry->callback == probe.get();
	runtime->UpdateNativeBinding(index, function, flags, user);
	rebinding_ = false;
	if (!objectInPlace) {
		fail("an object bound in its place is not where an object binding is kept");
	}
	if (entry->legacy_fn != function || entry->callback != nullptr) {
		fail("the runner's function did not go back where a function binding is kept");
	}
	layoutVerified_ = true;
}

void NativeHooks::OnPluginLoaded(IPlugin* plugin) {
	for (auto& entry : natives_) {
		Wrap(plugin, entry.second.get());
	}
}

void NativeHooks::OnPluginDestroyed(IPlugin* plugin) {
	IPluginRuntime* runtime = plugin->GetRuntime();
	for (auto& entry : natives_) {
		auto& bindings = entry.second->bindings;
		bindings.erase(std::remove_if(bindings.begin(), bindings.end(),
		                              [runtime](const HookedNative::Binding& binding) {
			                              return binding.runtime == runtime;
		                              }),
		               bindings.end());
	}
}

HookedNative* NativeHooks::Ensure(const std::string& name) {
	auto& slot = natives_[name];
	if (!slot) {
		slot = std::make_unique<HookedNative>(this, name);
	}
	return slot.get();
}

void NativeHooks::Hook(IPluginContext* caller, const std::string& name, IPluginFunction* pre,
                       IPluginFunction* post) {
	VerifyVmLayout(caller);
	HookedNative* native = Ensure(name);
	native->pre = pre;
	native->post = post;
	native->mock = nullptr;
	IPluginIterator* iterator = scripts->GetPluginIterator();
	for (; iterator->MorePlugins(); iterator->NextPlugin()) {
		Wrap(iterator->GetPlugin(), native);
	}
	iterator->Release();
}

void NativeHooks::Mock(IPluginContext* caller, const std::string& name, IPluginFunction* mock) {
	Hook(caller, name, nullptr, nullptr);
	Find(name)->mock = mock;
}

void NativeHooks::Unhook(const std::string& name) {
	HookedNative* native = Find(name);
	if (!native) {
		return;
	}
	for (HookedNative::Binding& binding : native->bindings) {
		if (!IsWrappedBy(EntryOf(binding.runtime, binding.index), binding.wrapper.get())) {
			continue;   // SourceMod bound it anew or dropped it: that binding stays
		}
		const Original& original = binding.wrapper->original();
		rebinding_ = true;
		if (original.callback) {
			binding.runtime->UpdateNativeBindingObject(binding.index, original.callback.get(), original.flags,
			                                           original.user);
		} else {
			binding.runtime->UpdateNativeBinding(binding.index, original.function, original.flags, original.user);
		}
		rebinding_ = false;
	}
	native->bindings.clear();
	native->pre = native->post = native->mock = nullptr;
}

int NativeHooks::Calls(const std::string& name) const {
	HookedNative* native = Find(name);
	return native ? native->calls : 0;
}

NativeCall* NativeHooks::CallById(cell_t id) {
	if (id < 1 || id > static_cast<cell_t>(calls_.size())) {
		return nullptr;
	}
	return calls_[id - 1];
}

NativeCall::NativeCall(std::string name, IPluginContext* caller, const cell_t* params, Binding original)
	: name_(std::move(name)),
	  caller_(caller),
	  params_(params, params + params[0] + 1),
	  original_(std::move(original)) {}

bool NativeCall::HasArg(cell_t n, std::string* error) const {
	if (n >= 1 && n <= ArgCount()) {
		return true;
	}
	*error = name_ + " has no argument " + std::to_string(n) + " (it got " + std::to_string(ArgCount()) + ")";
	return false;
}

cell_t* NativeCall::ArgCells(cell_t n, cell_t count, const char* what, std::string* error) const {
	cell_t* cells = PluginCells(caller_, params_[n], count);
	if (!cells) {
		*error = "argument " + std::to_string(n) + " of " + name_ + " is not " + what;
	}
	return cells;
}

bool NativeCall::Arg(cell_t n, cell_t* value, std::string* error) const {
	if (!HasArg(n, error)) {
		return false;
	}
	*value = params_[n];
	return true;
}

bool NativeCall::SetArg(cell_t n, cell_t value, std::string* error) {
	if (!HasArg(n, error)) {
		return false;
	}
	params_[n] = value;
	return true;
}

bool NativeCall::ArgRef(cell_t n, cell_t* value, std::string* error) const {
	cell_t* cell = HasArg(n, error) ? ArgCells(n, 1, "a reference", error) : nullptr;
	if (!cell) {
		return false;
	}
	*value = *cell;
	return true;
}

bool NativeCall::SetArgRef(cell_t n, cell_t value, std::string* error) {
	cell_t* cell = HasArg(n, error) ? ArgCells(n, 1, "a reference", error) : nullptr;
	if (!cell) {
		return false;
	}
	*cell = value;
	return true;
}

bool NativeCall::ArgString(cell_t n, std::string* text, std::string* error) const {
	if (!HasArg(n, error)) {
		return false;
	}
	char* chars = PluginChars(caller_, params_[n]);
	if (!chars) {
		*error = "argument " + std::to_string(n) + " of " + name_ + " is not a string";
		return false;
	}
	*text = chars;
	return true;
}

bool NativeCall::SetArgString(cell_t n, const std::string& text, cell_t size, std::string* error) {
	if (!HasArg(n, error)) {
		return false;
	}
	if (size < 1) {
		*error = "the buffer of argument " + std::to_string(n) + " needs a size of 1 or more";
		return false;
	}
	if (!PluginBytes(caller_, params_[n], size) ||
	    caller_->StringToLocalUTF8(params_[n], size, text.c_str(), nullptr) != SP_ERROR_NONE) {
		*error = "argument " + std::to_string(n) + " of " + name_ + " is not a string buffer of " +
		         std::to_string(size) + " bytes";
		return false;
	}
	return true;
}

bool NativeCall::GetArgArray(cell_t n, cell_t* cells, cell_t count, std::string* error) const {
	if (!HasArg(n, error)) {
		return false;
	}
	if (count == 0) {
		return true;
	}
	cell_t* theirs = ArgCells(n, count, (std::to_string(count) + " cells long").c_str(), error);
	if (!theirs) {
		return false;
	}
	std::copy(theirs, theirs + count, cells);
	return true;
}

bool NativeCall::SetArgArray(cell_t n, const cell_t* cells, cell_t count, std::string* error) {
	if (!HasArg(n, error)) {
		return false;
	}
	if (count == 0) {
		return true;
	}
	cell_t* theirs = ArgCells(n, count, (std::to_string(count) + " cells long").c_str(), error);
	if (!theirs) {
		return false;
	}
	std::copy(cells, cells + count, theirs);
	return true;
}

cell_t NativeCall::CallOriginal() {
	DetectExceptions exceptions(caller_);
	cell_t result = original_(caller_, params_.data());
	originalThrew_ = exceptions.HasException();
	return originalThrew_ ? 0 : result;
}

cell_t NativeHooks::Dispatch(HookedNative* native, NativeCall::Binding original, IPluginContext* context,
                             const cell_t* params) {
	NativeCall call(native->name(), context, params, std::move(original));
	native->calls++;
	calls_.push_back(&call);
	cell_t id = static_cast<cell_t>(calls_.size());

	if (native->mock) {
		native->mock->PushCell(id);
		cell_t result = 0;
		if (native->mock->Execute(&result) == SP_ERROR_NONE) {
			call.SetResult(result);
		}
		calls_.pop_back();
		return call.Result();
	}
	bool handled = false;
	if (native->pre) {
		native->pre->PushCell(id);
		cell_t action = Pl_Continue;
		native->pre->Execute(&action);
		handled = action >= Pl_Handled;
	}
	if (!handled) {
		call.SetResult(call.CallOriginal());
		if (call.OriginalThrew()) {
			calls_.pop_back();
			return 0;
		}
	}
	if (native->post) {
		native->post->PushCell(id);
		native->post->Execute(nullptr);
	}
	calls_.pop_back();
	return call.Result();
}

}  // namespace smrunner
