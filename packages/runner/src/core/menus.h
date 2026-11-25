// Copyright 2026 davenonymous
//
// SourceMod's menu manager, ported from core/MenuManager.cpp of SourceMod
// 1.12 (GPLv3, AlliedModders LLC): the menu and style handle types, the
// styles (radio, and a stand-in for valve that the runner does not
// simulate), rendering a page of a menu with its navigation keys, and votes
// through the vote handler. Menu sounds are not played: the simulated
// server has no sounds.
#ifndef SMRUNNER_CORE_MENUS_H
#define SMRUNNER_CORE_MENUS_H

#include <IMenuManager.h>
#include <sh_vector.h>

#include "core/globals.h"

namespace smrunner {

class SimPlayer;

// The simulated server, as the ported menu code reads it.
float GameTime();
int MaxClients();
SimPlayer* PlayerOf(int client);
// A key of SourceMod's core game config (core.games), or nullptr.
const char* CoreGameConfigValue(const char* key);

class MenuManager : public SourceMod::IMenuManager,
                    public SMGlobalClass,
                    public SourceMod::IHandleTypeDispatch {
	friend class BroadcastHandler;
	friend class VoteHandler;
	friend class CBaseMenu;
	friend class BaseMenuStyle;
	friend class ValveStandInStyle;

 public:
	MenuManager();

 public:   // SMGlobalClass
	void OnSourceModAllInitialized() override;
	void OnSourceModAllShutdown() override;

 public:   // IMenuManager
	const char* GetInterfaceName() override { return SMINTERFACE_MENUMANAGER_NAME; }
	unsigned int GetInterfaceVersion() override { return SMINTERFACE_MENUMANAGER_VERSION; }
	unsigned int GetStyleCount();
	SourceMod::IMenuStyle* GetStyle(unsigned int index);
	SourceMod::IMenuStyle* FindStyleByName(const char* name) override;
	SourceMod::IMenuStyle* GetDefaultStyle() override;
	void AddStyle(SourceMod::IMenuStyle* style);
	bool SetDefaultStyle(SourceMod::IMenuStyle* style);
	SourceMod::IMenuPanel* RenderMenu(int client, SourceMod::menu_states_t& states,
	                                  SourceMod::ItemOrder order) override;
	void CancelMenu(SourceMod::IBaseMenu* menu) override;
	bool StartVote(SourceMod::IBaseMenu* menu, unsigned int num_clients, int clients[], unsigned int max_time,
	               unsigned int flags = 0) override;
	bool IsVoteInProgress() override;
	void CancelVoting() override;
	unsigned int GetRemainingVoteDelay() override;
	bool IsClientInVotePool(int client) override;
	bool RedrawClientVoteMenu(int client) override;
	bool RedrawClientVoteMenu2(int client, bool revote) override;

 public:   // IHandleTypeDispatch
	void OnHandleDestroy(SourceMod::HandleType_t type, void* object) override;
	bool GetHandleApproxSize(SourceMod::HandleType_t type, void* object, unsigned int* pSize) override;

 public:
	SourceMod::HandleError ReadMenuHandle(SourceMod::Handle_t handle, SourceMod::IBaseMenu** menu);
	SourceMod::HandleError ReadStyleHandle(SourceMod::Handle_t handle, SourceMod::IMenuStyle** style);

 protected:
	SourceMod::Handle_t CreateMenuHandle(SourceMod::IBaseMenu* menu, SourceMod::IdentityToken_t* pOwner);
	SourceMod::Handle_t CreateStyleHandle(SourceMod::IMenuStyle* style);

 private:
	SourceMod::IMenuStyle* m_pDefaultStyle;
	SourceHook::CVector<SourceMod::IMenuStyle*> m_Styles;
	SourceMod::HandleType_t m_StyleType;
	SourceMod::HandleType_t m_MenuType;
};

extern MenuManager g_Menus;

}  // namespace smrunner

#endif
