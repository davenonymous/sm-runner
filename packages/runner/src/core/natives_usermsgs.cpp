// Copyright 2026 davenonymous
//
// The user message and bit buffer natives, ported from core/smn_usermsgs.cpp
// and core/smn_bitbuffer.cpp of SourceMod 1.12 (GPLv3, AlliedModders LLC):
// plugins start, write and end messages and hook them (intercept, post and
// a notification after the message), reading a hooked message through the
// one reader handle SourceMod shares between all hooks.
#include <map>
#include <memory>

#include "core/core.h"
#include "core/players.h"
#include "core/usermsgs.h"
#include "testing/plugin_memory.h"

using namespace SourceMod;
using namespace SourcePawn;

namespace smrunner {

static HandleType_t sWriterType = NO_HANDLE_TYPE;
static HandleType_t sReaderType = NO_HANDLE_TYPE;
static Handle_t sReaderHandle = BAD_HANDLE;
static BitReader sReader;
static Handle_t sCurrentMessage = BAD_HANDLE;
static bool sMessageInExec = false;

// MsgListenerWrapper: one hook of a plugin.
class PluginMessageHook : public UserMessageListener {
 public:
	PluginMessageHook(int id, IPluginFunction* hook, IPluginFunction* notify, bool intercept)
		: id_(id), hook_(hook), notify_(notify), intercept_(intercept) {}

	int id() const { return id_; }
	IPluginFunction* hook() const { return hook_; }
	bool intercept() const { return intercept_; }

	void OnUserMessage(int id, const BitWriter& message, const MessageRecipients& recipients) override {
		cell_t result;
		Push(id, message, recipients);
		hook_->Execute(&result);
	}
	ResultType InterceptUserMessage(int id, const BitWriter& message, const MessageRecipients& recipients) override {
		cell_t result = Pl_Continue;
		Push(id, message, recipients);
		hook_->Execute(&result);
		return static_cast<ResultType>(result);
	}
	void OnPostUserMessage(int id, bool sent) override {
		if (!notify_) {
			return;
		}
		cell_t result;
		notify_->PushCell(id);
		notify_->PushCell(sent ? 1 : 0);
		notify_->Execute(&result);
	}

 private:
	void Push(int id, const BitWriter& message, const MessageRecipients& recipients) {
		sReader.StartReading(message.Data(), message.BytesWritten());
		// The VM copies a pushed array when the function is executed, so it
		// must outlive this call.
		players_.assign(recipients.clients.begin(), recipients.clients.end());
		hook_->PushCell(id);
		hook_->PushCell(sReaderHandle);
		hook_->PushArray(players_.data(), static_cast<unsigned int>(players_.size()));
		hook_->PushCell(static_cast<cell_t>(players_.size()));
		hook_->PushCell(recipients.reliable);
		hook_->PushCell(recipients.init);
	}

	int id_;
	IPluginFunction* hook_;
	IPluginFunction* notify_;
	bool intercept_;
	std::vector<cell_t> players_;   // the recipients pushed to hook_
};

class UserMessageNatives : public SMGlobalClass, public IHandleTypeDispatch, public IPluginsListener {
 public:
	void OnSourceModAllInitialized() override {
		HandleAccess access;
		handlesys->InitAccessDefaults(nullptr, &access);
		access.access[HandleAccess_Delete] = HANDLE_RESTRICT_IDENTITY;
		sWriterType = handlesys->CreateType("BitBufWriter", this, 0, nullptr, nullptr, g_pCoreIdent, nullptr);
		sReaderType = handlesys->CreateType("BitBufReader", this, 0, nullptr, &access, g_pCoreIdent, nullptr);
		sReaderHandle = handlesys->CreateHandle(sReaderType, &sReader, nullptr, g_pCoreIdent, nullptr);
		scripts->AddPluginsListener(this);
	}
	void OnSourceModShutdown() override {
		HandleSecurity security(nullptr, g_pCoreIdent);
		handlesys->FreeHandle(sReaderHandle, &security);
		handlesys->RemoveType(sWriterType, g_pCoreIdent);
		handlesys->RemoveType(sReaderType, g_pCoreIdent);
		sWriterType = NO_HANDLE_TYPE;
		sReaderType = NO_HANDLE_TYPE;
	}
	void OnHandleDestroy(HandleType_t, void*) override {}
	bool GetHandleApproxSize(HandleType_t, void*, unsigned int* size) override {
		*size = sizeof(BitReader);
		return true;
	}
	void OnPluginUnloaded(IPlugin* plugin) override {
		auto found = hooks_.find(plugin);
		if (found == hooks_.end()) {
			return;
		}
		for (const auto& hook : found->second) {
			Core::Get().userMessages().Unhook(hook->id(), hook.get(), hook->intercept(), true);
		}
		hooks_.erase(found);
	}

	PluginMessageHook* Add(IPlugin* plugin, std::unique_ptr<PluginMessageHook> hook) {
		hooks_[plugin].push_back(std::move(hook));
		return hooks_[plugin].back().get();
	}
	// The plugin's hook of this function on this message, or nullptr.
	PluginMessageHook* Find(IPlugin* plugin, int id, IPluginFunction* function, bool intercept) {
		for (const auto& hook : hooks_[plugin]) {
			if (hook->id() == id && hook->hook() == function && hook->intercept() == intercept) {
				return hook.get();
			}
		}
		return nullptr;
	}
	void Remove(IPlugin* plugin, PluginMessageHook* hook) {
		auto& list = hooks_[plugin];
		for (auto it = list.begin(); it != list.end(); ++it) {
			if (it->get() == hook) {
				list.erase(it);
				return;
			}
		}
	}

 private:
	std::map<IPlugin*, std::vector<std::unique_ptr<PluginMessageHook>>> hooks_;
};

static UserMessageNatives sUserMessageNatives;

// ---------------------------------------------------------- user messages

static UserMessages& Messages() {
	return Core::Get().userMessages();
}

static cell_t GetUserMessageType(IPluginContext*, const cell_t*) {
	return 0;   // UM_BitBuf
}

static cell_t GetUserMessageId(IPluginContext* context, const cell_t* params) {
	char* name = PluginCharsArg(context, params[1]);
	return name ? Messages().GetMessageIndex(name) : 0;
}

static cell_t GetUserMessageName(IPluginContext* context, const cell_t* params) {
	const char* name = Messages().GetMessageName(params[1]);
	if (!name) {
		return 0;
	}
	return WritePluginString(context, params[2], params[3], name, TextCut::AtByte) ? 1 : 0;
}

static bool ValidRecipients(IPluginContext* context, const cell_t* clients, unsigned int count) {
	for (unsigned int i = 0; i < count; i++) {
		SimPlayer* player = Core::Get().players().Player(clients[i]);
		if (!player) {
			context->ThrowNativeError("Client index %d is invalid", clients[i]);
			return false;
		}
		if (!player->IsConnected()) {
			context->ThrowNativeError("Client %d is not connected", clients[i]);
			return false;
		}
	}
	return true;
}

static cell_t StartById(IPluginContext* context, int id, const cell_t* params) {
	if (params[3] < 0) {
		return context->ThrowNativeError("the number of clients (%d) is negative", params[3]);
	}
	cell_t* clients = nullptr;
	if (params[3] > 0 && !(clients = PluginCells(context, params[2], params[3]))) {
		return context->ThrowNativeError("an array of %d clients does not fit the plugin's memory", params[3]);
	}
	unsigned int count = static_cast<unsigned int>(params[3]);
	if (!ValidRecipients(context, clients, count)) {
		return 0;
	}
	BitWriter* writer = Messages().StartMessage(id, clients, count, params[4]);
	if (!writer) {
		return context->ThrowNativeError("Unable to execute a new message while in hook");
	}
	sCurrentMessage = handlesys->CreateHandle(sWriterType, writer, context->GetIdentity(), g_pCoreIdent, nullptr);
	sMessageInExec = true;
	return sCurrentMessage;
}

static cell_t StartMessage(IPluginContext* context, const cell_t* params) {
	if (sMessageInExec) {
		return context->ThrowNativeError("Unable to execute a new message, there is already one in progress");
	}
	char* name = PluginCharsArg(context, params[1]);
	if (!name) {
		return 0;
	}
	int id = Messages().GetMessageIndex(name);
	if (id == kInvalidMessageId) {
		return context->ThrowNativeError("Invalid message name: \"%s\"", name);
	}
	return StartById(context, id, params);
}

static cell_t StartMessageEx(IPluginContext* context, const cell_t* params) {
	if (sMessageInExec) {
		return context->ThrowNativeError("Unable to execute a new message, there is already one in progress");
	}
	if (params[1] < 0 || params[1] >= 255) {
		return context->ThrowNativeError("Invalid message id supplied (%d)", params[1]);
	}
	return StartById(context, params[1], params);
}

static cell_t EndMessage(IPluginContext* context, const cell_t*) {
	if (!sMessageInExec) {
		return context->ThrowNativeError("Unable to end message, no message is in progress");
	}
	UserMessages::EndResult result = Messages().EndMessage();
	HandleSecurity security(context->GetIdentity(), g_pCoreIdent);
	handlesys->FreeHandle(sCurrentMessage, &security);
	sMessageInExec = false;
	if (result == UserMessages::EndResult::Overflowed) {
		return context->ThrowNativeError("The user message overflowed: it has more than %d bytes, the engine's limit",
		                                 static_cast<int>(kMaxUserMessageBytes));
	}
	return 1;
}

static IPlugin* PluginOf(IPluginContext* context) {
	return scripts->FindPluginByContext(context->GetContext());
}

static cell_t HookUserMessage(IPluginContext* context, const cell_t* params) {
	int id = params[1];
	if (id < 0 || id >= 255) {
		return context->ThrowNativeError("Invalid message id supplied (%d)", id);
	}
	IPluginFunction* hook = context->GetFunctionById(params[2]);
	if (!hook) {
		return context->ThrowNativeError("Invalid function id (%X)", params[2]);
	}
	IPluginFunction* notify = context->GetFunctionById(params[4]);
	bool intercept = params[3] != 0;
	PluginMessageHook* added = sUserMessageNatives.Add(
		PluginOf(context), std::make_unique<PluginMessageHook>(id, hook, notify, intercept));
	Messages().Hook(id, added, intercept, true);
	return 1;
}

static cell_t UnhookUserMessage(IPluginContext* context, const cell_t* params) {
	int id = params[1];
	if (id < 0 || id >= 255) {
		return context->ThrowNativeError("Invalid message id supplied (%d)", id);
	}
	IPluginFunction* function = context->GetFunctionById(params[2]);
	if (!function) {
		return context->ThrowNativeError("Invalid function id (%X)", params[2]);
	}
	bool intercept = params[3] != 0;
	IPlugin* plugin = PluginOf(context);
	PluginMessageHook* hook = sUserMessageNatives.Find(plugin, id, function, intercept);
	if (!hook || !Messages().Unhook(id, hook, intercept, true)) {
		return context->ThrowNativeError("Unable to unhook the current user message");
	}
	sUserMessageNatives.Remove(plugin, hook);
	return 1;
}

// ------------------------------------------------------------ bit buffers

static BitWriter* WriterOf(IPluginContext* context, cell_t handle) {
	BitWriter* writer = nullptr;
	HandleSecurity security(nullptr, g_pCoreIdent);
	HandleError error = handlesys->ReadHandle(handle, sWriterType, &security, reinterpret_cast<void**>(&writer));
	if (error != HandleError_None) {
		context->ThrowNativeError("Invalid bit buffer handle %x (error %d)", handle, error);
		return nullptr;
	}
	return writer;
}

static BitReader* ReaderOf(IPluginContext* context, cell_t handle) {
	BitReader* reader = nullptr;
	HandleSecurity security(nullptr, g_pCoreIdent);
	HandleError error = handlesys->ReadHandle(handle, sReaderType, &security, reinterpret_cast<void**>(&reader));
	if (error != HandleError_None) {
		context->ThrowNativeError("Invalid bit buffer handle %x (error %d)", handle, error);
		return nullptr;
	}
	return reader;
}

// The vector argument's 3 cells; false after the native error.
static bool ReadVector(IPluginContext* context, cell_t address, float out[3]) {
	cell_t* cells = PluginCellsArg(context, address, 3);
	if (!cells) {
		return false;
	}
	for (int i = 0; i < 3; i++) {
		out[i] = sp_ctof(cells[i]);
	}
	return true;
}

static void WriteVector(cell_t* cells, const float in[3]) {
	for (int i = 0; i < 3; i++) {
		cells[i] = sp_ftoc(in[i]);
	}
}

// A writer native: the handle checked, then the write.
#define WRITER_NATIVE(name, body)                                       \
	static cell_t name(IPluginContext* context, const cell_t* params) { \
		BitWriter* writer = WriterOf(context, params[1]);               \
		if (!writer) {                                                  \
			return 0;                                                   \
		}                                                               \
		body;                                                           \
		return 1;                                                       \
	}

WRITER_NATIVE(BfWriteBool, writer->WriteOneBit(params[2]))
WRITER_NATIVE(BfWriteByte, writer->WriteByte(params[2]))
WRITER_NATIVE(BfWriteChar, writer->WriteChar(params[2]))
WRITER_NATIVE(BfWriteShort, writer->WriteShort(params[2]))
WRITER_NATIVE(BfWriteWord, writer->WriteWord(params[2]))
WRITER_NATIVE(BfWriteNum, writer->WriteLong(params[2]))
WRITER_NATIVE(BfWriteFloat, writer->WriteFloat(sp_ctof(params[2])))
WRITER_NATIVE(BfWriteAngle, writer->WriteBitAngle(sp_ctof(params[2]), params[3]))
WRITER_NATIVE(BfWriteCoord, writer->WriteBitCoord(sp_ctof(params[2])))

static cell_t BfWriteString(IPluginContext* context, const cell_t* params) {
	BitWriter* writer = WriterOf(context, params[1]);
	if (!writer) {
		return 0;
	}
	char* text = PluginCharsArg(context, params[2]);
	if (!text) {
		return 0;
	}
	writer->WriteString(text);
	return 1;
}

static cell_t BfWriteEntity(IPluginContext* context, const cell_t* params) {
	BitWriter* writer = WriterOf(context, params[1]);
	if (!writer) {
		return 0;
	}
	cell_t reference = params[2];
	if (reference == -1) {
		return 0;
	}
	writer->WriteShort((reference & (1 << 31)) ? (reference & 0xFFF) : reference);
	return 1;
}

static cell_t BfWriteVecCoord(IPluginContext* context, const cell_t* params) {
	BitWriter* writer = WriterOf(context, params[1]);
	if (!writer) {
		return 0;
	}
	float vector[3];
	if (!ReadVector(context, params[2], vector)) {
		return 0;
	}
	writer->WriteBitVec3Coord(vector);
	return 1;
}

static cell_t BfWriteVecNormal(IPluginContext* context, const cell_t* params) {
	BitWriter* writer = WriterOf(context, params[1]);
	if (!writer) {
		return 0;
	}
	float vector[3];
	if (!ReadVector(context, params[2], vector)) {
		return 0;
	}
	writer->WriteBitVec3Normal(vector);
	return 1;
}

static cell_t BfWriteAngles(IPluginContext* context, const cell_t* params) {
	BitWriter* writer = WriterOf(context, params[1]);
	if (!writer) {
		return 0;
	}
	float angles[3];
	if (!ReadVector(context, params[2], angles)) {
		return 0;
	}
	writer->WriteBitAngles(angles);
	return 1;
}

// A reader native: the handle checked, then the value read.
#define READER_NATIVE(name, expression)                                 \
	static cell_t name(IPluginContext* context, const cell_t* params) { \
		BitReader* reader = ReaderOf(context, params[1]);               \
		if (!reader) {                                                  \
			return 0;                                                   \
		}                                                               \
		return expression;                                              \
	}

READER_NATIVE(BfReadBool, reader->ReadOneBit() ? 1 : 0)
READER_NATIVE(BfReadByte, reader->ReadByte())
READER_NATIVE(BfReadChar, reader->ReadChar())
READER_NATIVE(BfReadShort, reader->ReadShort())
READER_NATIVE(BfReadWord, reader->ReadWord())
READER_NATIVE(BfReadNum, reader->ReadLong())
READER_NATIVE(BfReadFloat, sp_ftoc(reader->ReadFloat()))
READER_NATIVE(BfReadEntity, reader->ReadShort())
READER_NATIVE(BfReadAngle, sp_ftoc(reader->ReadBitAngle(params[2])))
READER_NATIVE(BfReadCoord, sp_ftoc(reader->ReadBitCoord()))
READER_NATIVE(BfGetNumBytesLeft, static_cast<cell_t>(reader->BitsLeft() >> 3))

// The number of characters read; negative (-count - 1) when the message
// ended before the string did.
static cell_t BfReadString(IPluginContext* context, const cell_t* params) {
	BitReader* reader = ReaderOf(context, params[1]);
	if (!reader) {
		return 0;
	}
	if (params[3] <= 0) {
		return context->ThrowNativeError("BfReadString needs a buffer of at least 1 byte, not %d", params[3]);
	}
	char* buffer = PluginBytes(context, params[2], params[3]);
	if (!buffer) {
		return context->ThrowNativeError("a buffer of %d bytes does not fit the plugin's memory", params[3]);
	}
	size_t length = 0;
	reader->ReadString(buffer, static_cast<size_t>(params[3]), params[4] != 0, &length);
	cell_t count = static_cast<cell_t>(length);
	return reader->IsOverflowed() ? -count - 1 : count;
}

static cell_t BfReadVecCoord(IPluginContext* context, const cell_t* params) {
	BitReader* reader = ReaderOf(context, params[1]);
	if (!reader) {
		return 0;
	}
	cell_t* cells = PluginCellsArg(context, params[2], 3);
	if (!cells) {
		return 0;
	}
	float vector[3];
	reader->ReadBitVec3Coord(vector);
	WriteVector(cells, vector);
	return 1;
}

static cell_t BfReadVecNormal(IPluginContext* context, const cell_t* params) {
	BitReader* reader = ReaderOf(context, params[1]);
	if (!reader) {
		return 0;
	}
	cell_t* cells = PluginCellsArg(context, params[2], 3);
	if (!cells) {
		return 0;
	}
	float vector[3];
	reader->ReadBitVec3Normal(vector);
	WriteVector(cells, vector);
	return 1;
}

static cell_t BfReadAngles(IPluginContext* context, const cell_t* params) {
	BitReader* reader = ReaderOf(context, params[1]);
	if (!reader) {
		return 0;
	}
	cell_t* cells = PluginCellsArg(context, params[2], 3);
	if (!cells) {
		return 0;
	}
	float angles[3];
	reader->ReadBitAngles(angles);
	WriteVector(cells, angles);
	return 1;
}

REGISTER_NATIVES(userMessageNatives) {
	{"GetUserMessageType", GetUserMessageType},
	{"GetUserMessageId", GetUserMessageId},
	{"GetUserMessageName", GetUserMessageName},
	{"StartMessage", StartMessage},
	{"StartMessageEx", StartMessageEx},
	{"EndMessage", EndMessage},
	{"HookUserMessage", HookUserMessage},
	{"UnhookUserMessage", UnhookUserMessage},
	{nullptr, nullptr},
};

REGISTER_NATIVES(bitBufferNatives) {
	{"BfWriteBool", BfWriteBool},
	{"BfWriteByte", BfWriteByte},
	{"BfWriteChar", BfWriteChar},
	{"BfWriteShort", BfWriteShort},
	{"BfWriteWord", BfWriteWord},
	{"BfWriteNum", BfWriteNum},
	{"BfWriteFloat", BfWriteFloat},
	{"BfWriteString", BfWriteString},
	{"BfWriteEntity", BfWriteEntity},
	{"BfWriteAngle", BfWriteAngle},
	{"BfWriteCoord", BfWriteCoord},
	{"BfWriteVecCoord", BfWriteVecCoord},
	{"BfWriteVecNormal", BfWriteVecNormal},
	{"BfWriteAngles", BfWriteAngles},
	{"BfReadBool", BfReadBool},
	{"BfReadByte", BfReadByte},
	{"BfReadChar", BfReadChar},
	{"BfReadShort", BfReadShort},
	{"BfReadWord", BfReadWord},
	{"BfReadNum", BfReadNum},
	{"BfReadFloat", BfReadFloat},
	{"BfReadString", BfReadString},
	{"BfReadEntity", BfReadEntity},
	{"BfReadAngle", BfReadAngle},
	{"BfReadCoord", BfReadCoord},
	{"BfReadVecCoord", BfReadVecCoord},
	{"BfReadVecNormal", BfReadVecNormal},
	{"BfReadAngles", BfReadAngles},
	{"BfGetNumBytesLeft", BfGetNumBytesLeft},
	{"BfWrite.WriteBool", BfWriteBool},
	{"BfWrite.WriteByte", BfWriteByte},
	{"BfWrite.WriteChar", BfWriteChar},
	{"BfWrite.WriteShort", BfWriteShort},
	{"BfWrite.WriteWord", BfWriteWord},
	{"BfWrite.WriteNum", BfWriteNum},
	{"BfWrite.WriteFloat", BfWriteFloat},
	{"BfWrite.WriteString", BfWriteString},
	{"BfWrite.WriteEntity", BfWriteEntity},
	{"BfWrite.WriteAngle", BfWriteAngle},
	{"BfWrite.WriteCoord", BfWriteCoord},
	{"BfWrite.WriteVecCoord", BfWriteVecCoord},
	{"BfWrite.WriteVecNormal", BfWriteVecNormal},
	{"BfWrite.WriteAngles", BfWriteAngles},
	{"BfRead.ReadBool", BfReadBool},
	{"BfRead.ReadByte", BfReadByte},
	{"BfRead.ReadChar", BfReadChar},
	{"BfRead.ReadShort", BfReadShort},
	{"BfRead.ReadWord", BfReadWord},
	{"BfRead.ReadNum", BfReadNum},
	{"BfRead.ReadFloat", BfReadFloat},
	{"BfRead.ReadString", BfReadString},
	{"BfRead.ReadEntity", BfReadEntity},
	{"BfRead.ReadAngle", BfReadAngle},
	{"BfRead.ReadCoord", BfReadCoord},
	{"BfRead.ReadVecCoord", BfReadVecCoord},
	{"BfRead.ReadVecNormal", BfReadVecNormal},
	{"BfRead.ReadAngles", BfReadAngles},
	{"BfRead.BytesLeft.get", BfGetNumBytesLeft},
	{nullptr, nullptr},
};

}  // namespace smrunner
