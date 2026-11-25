// Copyright 2026 davenonymous
#include "engine/clients.h"

#include "diagnostics.h"
#include "engine/engine.h"

namespace smrunner {

// --------------------------------------------------------- SimulatedClient

const char* SimulatedClient::GetConVar(const std::string& name) const {
	auto found = cvars_.find(name);
	return found == cvars_.end() ? "" : found->second.c_str();
}

// CBaseClient::SetName: Q_strncpy into the name buffer.
static std::string EngineName(const std::string& name) {
	return name.substr(0, kMaxPlayerNameLength - 1);
}

void SimulatedClient::SetConVar(const std::string& name, const std::string& value) {
	cvars_[name] = strcasecmp(name.c_str(), "name") == 0 ? EngineName(value) : value;
	userInfoChanged_ = true;
}

const MenuDisplay* SimulatedClient::CurrentMenu() const {
	if (!hasMenu_) {
		return nullptr;
	}
	if (menu_.holdTime != 0 && globals_->curtime > menu_.shownAt + static_cast<float>(menu_.holdTime)) {
		return nullptr;
	}
	return &menu_;
}

void SimulatedClient::PressMenuKey(int key) {
	if (!CurrentMenu() || key < 0 || key > 10) {
		return;
	}
	if (menu_.keys & MenuKeyBit(key == 0 ? 10 : key)) {
		hasMenu_ = false;
	}
}

// CHudMenu::MsgFunc_ShowMenu: the keys, the display time, whether more of
// the text follows, the text; a display is complete with its last part.
void SimulatedClient::ReceiveShowMenu(const BitWriter& message, const ShowMenuInfo& info) {
	BitReader reader(message.Data(), message.BytesWritten());
	int keys = reader.ReadWord();
	reader.ReadChar();   // the display time, wrapped; the info has it whole
	bool more = reader.ReadByte() != 0;
	menuChunks_ += reader.ReadString();
	if (more) {
		return;
	}
	menu_.text = menuChunks_;
	menu_.title = info.title;
	menu_.keys = keys;
	menu_.navigationKeys = info.navigationKeys;
	menu_.holdTime = info.holdTime;
	menu_.shownAt = globals_->curtime;
	hasMenu_ = true;
	menuChunks_.clear();
	if (!info.refresh) {
		Record(ClientMessage::Menu, menu_.text);
	}
}

// ------------------------------------------------------------- ClientTable

ClientTable::ClientTable(int maxClients, const EngineGlobals& globals) {
	if (maxClients < 1 || maxClients > kMaxClients) {
		Fatal("maxplayers must be between 1 and %d, not %d", kMaxClients, maxClients);
	}
	slots_.resize(static_cast<size_t>(maxClients) + 1);
	for (size_t index = 0; index < slots_.size(); index++) {
		slots_[index].edict_.index = static_cast<int>(index);
		slots_[index].globals_ = &globals;
	}
}

SimulatedClient* ClientTable::Slot(int client) {
	if (client < 1 || client > MaxClients()) {
		return nullptr;
	}
	return &slots_[static_cast<size_t>(client)];
}

SimulatedClient* ClientTable::Connect(const ClientIdentity& identity, double now) {
	for (int client = 1; client <= MaxClients(); client++) {
		SimulatedClient& slot = slots_[static_cast<size_t>(client)];
		if (slot.connected_) {
			continue;
		}
		const EngineGlobals* globals = slot.globals_;
		slot = SimulatedClient();
		slot.globals_ = globals;
		slot.edict_.index = client;
		slot.connected_ = true;
		slot.identity_ = identity;
		slot.userId_ = nextUserId_++;
		slot.connectedAt_ = now;
		slot.cvars_ = identity.cvars;
		slot.cvars_["name"] = EngineName(identity.name);
		slot.cvars_["cl_language"] = identity.language;
		slot.userInfoChanged_ = true;
		return &slot;
	}
	return nullptr;
}

bool ClientTable::TakeUserInfoChange(int client) {
	SimulatedClient* slot = Slot(client);
	if (!slot || !slot->userInfoChanged_) {
		return false;
	}
	slot->userInfoChanged_ = false;
	return true;
}

std::vector<int> ClientTable::TakeUserInfoChanges() {
	std::vector<int> changed;
	for (int client = 1; client <= MaxClients(); client++) {
		SimulatedClient& slot = slots_[static_cast<size_t>(client)];
		if (slot.connected_ && slot.userInfoChanged_) {
			changed.push_back(client);
		}
		slot.userInfoChanged_ = false;
	}
	return changed;
}

void ClientTable::PutInServer(int client) {
	if (SimulatedClient* slot = Slot(client)) {
		slot->active_ = slot->connected_;
	}
}

void ClientTable::Disconnect(int client) {
	SimulatedClient* slot = Slot(client);
	if (!slot) {
		return;
	}
	slot->connected_ = false;
	slot->active_ = false;
	slot->hasMenu_ = false;
	slot->menuChunks_.clear();
}

void ClientTable::Print(int client, const std::string& text) {
	SimulatedClient* slot = Slot(client);
	if (slot && slot->NetworkReceives()) {
		slot->Record(ClientMessage::Console, text);
	}
}

void ClientTable::SendCommand(int client, const std::string& command) {
	SimulatedClient* slot = Slot(client);
	if (slot && slot->NetworkReceives()) {
		slot->Record(ClientMessage::Command, command);
	}
}

void ClientTable::SendKick(int client, const std::string& reason) {
	SimulatedClient* slot = Slot(client);
	if (slot && slot->IsConnected()) {
		slot->Record(ClientMessage::Kick, reason);
	}
}

// ---------------------------------------------------------- user messages

static std::vector<std::string> ReadParameters(BitReader& reader) {
	std::vector<std::string> parameters;
	while (parameters.size() < 4 && reader.BitsLeft() >= 8) {
		parameters.push_back(reader.ReadString());
	}
	return parameters;
}

static ClientMessage::Kind KindOfTextMsgDestination(int dest) {
	if (dest == kHudPrintTalk) {
		return ClientMessage::Chat;
	}
	if (dest == kHudPrintCenter) {
		return ClientMessage::Center;
	}
	return ClientMessage::Console;
}

// TF2's client: TextMsg, SayText, SayText2 and HintText (a string alone)
// are text; every other message is told by its name.
static ClientMessage Decode(const std::string& name, const BitWriter& message) {
	BitReader reader(message.Data(), message.BytesWritten());
	if (name == "TextMsg") {
		int dest = reader.ReadByte();
		std::string format = reader.ReadString();
		return {KindOfTextMsgDestination(dest), ComposeClientText(format, ReadParameters(reader))};
	}
	if (name == "SayText") {
		reader.ReadByte();   // the author
		return {ClientMessage::Chat, reader.ReadString()};
	}
	if (name == "SayText2") {
		reader.ReadByte();   // the author
		reader.ReadByte();   // whether it is chat
		std::string format = reader.ReadString();
		return {ClientMessage::Chat, ComposeClientText(format, ReadParameters(reader))};
	}
	if (name == "HintText") {
		return {ClientMessage::Hint, reader.ReadString()};
	}
	return {ClientMessage::UserMessage, name};
}

void ClientTable::SendUserMessage(const std::vector<int>& clients, const std::string& name,
                                  const BitWriter& message, const ShowMenuInfo* menu) {
	bool isMenuDisplay = menu != nullptr && name == "ShowMenu";
	ClientMessage decoded = isMenuDisplay ? ClientMessage{ClientMessage::Menu, ""} : Decode(name, message);
	for (int client : clients) {
		SimulatedClient* slot = Slot(client);
		if (!slot || !slot->IsActive() || !slot->NetworkReceives()) {
			continue;
		}
		if (isMenuDisplay) {
			slot->ReceiveShowMenu(message, *menu);
		} else {
			slot->Record(decoded.kind, decoded.text);
		}
	}
}

std::string ComposeClientText(const std::string& format, const std::vector<std::string>& parameters) {
	if (!format.empty() && format[0] == '#') {
		size_t used = parameters.size();
		while (used > 0 && parameters[used - 1].empty()) {
			used--;
		}
		if (used == 0) {
			return format;
		}
		std::string text = format + " (";
		for (size_t i = 0; i < used; i++) {
			text += (i > 0 ? ", " : "") + parameters[i];
		}
		return text + ")";
	}
	std::string text;
	for (size_t i = 0; i < format.size(); i++) {
		bool isParameter = i + 2 < format.size() && format.compare(i, 2, "%s") == 0 && format[i + 2] >= '1' &&
		                   format[i + 2] <= '4';
		if (!isParameter) {
			text += format[i];
			continue;
		}
		size_t index = static_cast<size_t>(format[i + 2] - '1');
		if (index < parameters.size()) {
			text += parameters[index];
		}
		i += 2;
	}
	return text;
}

std::string StripChatColors(const std::string& text) {
	std::string stripped;
	for (size_t i = 0; i < text.size(); i++) {
		unsigned char c = static_cast<unsigned char>(text[i]);
		if (c == 7) {
			i += 6;
		} else if (c == 8) {
			i += 8;
		} else if (c < 1 || c > 6) {
			stripped += text[i];
		}
	}
	return stripped;
}

}  // namespace smrunner
