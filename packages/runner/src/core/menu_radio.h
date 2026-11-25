// Copyright 2026 davenonymous
//
// The radio menu style, TF2's default, ported from core/MenuStyle_Radio.h of
// SourceMod 1.12 (GPLv3, AlliedModders LLC): numbered lines sent as ShowMenu
// user messages, sent again every RadioMenuTimeout seconds so the client
// keeps showing them, and keys pressed coming back as "menuselect N". Each
// ShowMenu carries the display's info (engine/clients.h) to the engine,
// which records a display when it is sent, once, not with its refreshes.
#ifndef SMRUNNER_CORE_MENU_RADIO_H
#define SMRUNNER_CORE_MENU_RADIO_H

#include <sh_stack.h>

#include <string>

#include "core/menu_base.h"
#include "core/menus.h"
#include "core/usermsgs.h"
#include "engine/console.h"

namespace smrunner {

class CRadioDisplay;
class CRadioMenu;

class CRadioMenuPlayer : public CBaseMenuPlayer {
 public:
	void Radio_Init(int keys, const char* title, const char* buffer);
	bool Radio_NeedsRefresh();
	void Radio_Refresh();
	void Radio_SetIndex(unsigned int index);
	// The keys of the display's menu that move through it or leave it.
	void Radio_SetNavigationKeys(int keys) { display_info.navigationKeys = keys; }

 private:
	unsigned int m_index;
	size_t display_len;
	char display_pkt[512];
	int display_keys;
	float display_last_refresh;
	// What goes to the engine with the display's ShowMenu messages; refresh
	// once the display was sent.
	ShowMenuInfo display_info;
};

class CRadioStyle : public BaseMenuStyle, public SMGlobalClass, public UserMessageListener {
 public:
	CRadioStyle();

 public:   // SMGlobalClass
	void OnSourceModAllInitialized() override;
	void OnSourceModLevelChange(const char* mapName) override;
	void OnSourceModShutdown() override;

 public:   // BaseMenuStyle
	CBaseMenuPlayer* GetMenuPlayer(int client) override;
	void SendDisplay(int client, SourceMod::IMenuPanel* display) override;
	void ProcessWatchList() override;

 public:   // IMenuStyle
	const char* GetStyleName() override;
	SourceMod::IMenuPanel* CreatePanel() override;
	SourceMod::IBaseMenu* CreateMenu(SourceMod::IMenuHandler* pHandler,
	                                 SourceMod::IdentityToken_t* pOwner) override;
	unsigned int GetMaxPageItems() override;
	unsigned int GetApproxMemUsage() override;

 public:   // UserMessageListener
	void OnUserMessage(int msg_id, const BitWriter& message, const MessageRecipients& recipients) override;
	void OnUserMessageSent(int msg_id) override;

 public:
	bool IsSupported() override;
	// A client command: true when it was a key pressed in a menu.
	bool OnClientCommand(int client, const CommandArgs& args);

 public:
	CRadioDisplay* MakeRadioDisplay(CRadioMenu* menu = nullptr);
	void FreeRadioDisplay(CRadioDisplay* display);
	CRadioMenuPlayer* GetRadioMenuPlayer(int client);

 private:
	CRadioMenuPlayer* m_players;
	SourceHook::CStack<CRadioDisplay*> m_FreeDisplays;
};

class CRadioDisplay : public SourceMod::IMenuPanel {
	friend class CRadioStyle;

 public:
	CRadioDisplay();
	explicit CRadioDisplay(CRadioMenu* menu);

 public:   // IMenuPanel
	SourceMod::IMenuStyle* GetParentStyle() override;
	void Reset() override;
	void DrawTitle(const char* text, bool onlyIfEmpty = false) override;
	unsigned int DrawItem(const SourceMod::ItemDrawInfo& item) override;
	bool DrawRawLine(const char* rawline) override;
	bool SetExtOption(SourceMod::MenuOption option, const void* valuePtr) override;
	bool CanDrawItem(unsigned int drawFlags) override;
	bool SendDisplay(int client, SourceMod::IMenuHandler* handler, unsigned int time) override;
	void DeleteThis() override;
	void SendRawDisplay(int client, unsigned int time);
	bool SetSelectableKeys(unsigned int keymap) override;
	unsigned int GetCurrentKey() override;
	bool SetCurrentKey(unsigned int key) override;
	int GetAmountRemaining() override;
	unsigned int GetApproxMemUsage() override;
	bool DirectSet(const char* str) override;

 private:
	std::string m_BufferText;
	std::string m_Title;
	unsigned int m_NextPos;
	int keys;
};

class CRadioMenu : public CBaseMenu {
 public:
	CRadioMenu(SourceMod::IMenuHandler* pHandler, SourceMod::IdentityToken_t* pOwner);

 public:
	bool SetExtOption(SourceMod::MenuOption option, const void* valuePtr) override;
	SourceMod::IMenuPanel* CreatePanel() override;
	bool Display(int client, unsigned int time, SourceMod::IMenuHandler* alt_handler = nullptr) override;
	bool DisplayAtItem(int client, unsigned int time, unsigned int start_item,
	                   SourceMod::IMenuHandler* alt_handler = nullptr) override;
	bool SetPagination(unsigned int itemsPerPage) override;
	void Cancel_Finally() override;
	unsigned int GetApproxMemUsage() override;
};

extern CRadioStyle g_RadioMenuStyle;

}  // namespace smrunner

#endif
