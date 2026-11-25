// Copyright 2026 davenonymous
//
// Ported from core/MenuStyle_Radio.cpp of SourceMod 1.12 (GPLv3,
// AlliedModders LLC), the bit buffer path only.
#include "core/menu_radio.h"

#include <AutoHandleRooter.h>
#include <stdlib.h>
#include <string.h>

#include "core/core.h"
#include "core/players.h"

using namespace SourceMod;

namespace smrunner {

extern const char* g_RadioNumTable[11];

CRadioStyle g_RadioMenuStyle;
int g_ShowMenuId = -1;
bool g_bRadioInit = false;
unsigned int g_RadioMenuTimeout = 0;

// back, next, exit
#define MAX_PAGINATION_OPTIONS 3

#define MAX_MENUSLOT_KEYS 10

static unsigned int s_RadioMaxPageItems = MAX_MENUSLOT_KEYS;
static bool s_RadioClosesOnInvalidSlot = false;

CRadioStyle::CRadioStyle()
{
	m_players = new CRadioMenuPlayer[256+1];
	for (size_t i = 0; i < 256+1; i++)
	{
		m_players[i].Radio_SetIndex(i);
	}
}

void CRadioStyle::OnSourceModAllInitialized()
{
	Core::Get().players().AddClientListener(this);
}

void CRadioStyle::OnSourceModLevelChange(const char *mapName)
{
	if (g_bRadioInit)
	{
		return;
	}

	g_bRadioInit = true;

	// Always register the style. Use IsSupported() to check for validity before use.
	g_Menus.AddStyle(this);

	const char *msg = CoreGameConfigValue("HudRadioMenuMsg");
	if (!msg || msg[0] == '\0')
	{
		return;
	}

	g_ShowMenuId = Core::Get().userMessages().GetMessageIndex(msg);

	if (!IsSupported())
	{
		return;
	}

	const char *val = CoreGameConfigValue("RadioMenuTimeout");
	if (val != NULL)
	{
		g_RadioMenuTimeout = atoi(val);
	}
	else
	{
		g_RadioMenuTimeout = 0;
	}

	const char *items = CoreGameConfigValue("RadioMenuMaxPageItems");
	if (items != NULL)
	{
		int value = atoi(items);

		// Only override the mostly-safe default if it's a sane value
		if (value > MAX_PAGINATION_OPTIONS && value <= MAX_MENUSLOT_KEYS)
		{
			s_RadioMaxPageItems = value;
		}
	}

	const char *closes = CoreGameConfigValue("RadioMenuClosesOnInvalidSlot");
	if (closes != nullptr && strcmp(closes, "yes") == 0)
	{
		s_RadioClosesOnInvalidSlot = true;
	}

	g_Menus.SetDefaultStyle(this);

	Core::Get().userMessages().Hook(g_ShowMenuId, this, false, false);
}

void CRadioStyle::OnSourceModShutdown()
{
	Core::Get().players().RemoveClientListener(this);
	Core::Get().userMessages().Unhook(g_ShowMenuId, this, false, false);

	while (!m_FreeDisplays.empty())
	{
		delete m_FreeDisplays.front();
		m_FreeDisplays.pop();
	}
}

bool CRadioStyle::IsSupported()
{
	return (g_ShowMenuId != -1);
}

bool CRadioStyle::OnClientCommand(int client, const CommandArgs &cmd)
{
	if (cmd.ArgC() > 0 && strcmp(cmd.Arg(0), "menuselect") == 0)
	{
		if (!m_players[client].bInMenu)
		{
			m_players[client].bInExternMenu = false;
			return false;
		}

		int arg = atoi(cmd.Arg(1));
		ClientPressedKey(client, arg);
		return true;
	}

	return false;
}

static unsigned int g_last_holdtime = 0;
static unsigned int g_last_client_count = 0;
static int g_last_clients[256];

void CRadioStyle::OnUserMessage(int msg_id, const BitWriter &message, const MessageRecipients &recipients)
{
	BitReader br(message.Data(), message.BytesWritten() < 3 ? message.BytesWritten() : 3);

	br.ReadWord();
	int c = br.ReadChar();

	g_last_holdtime = (c == -1) ? 0 : (unsigned)c;

	for (int client : recipients.clients)
	{
		g_last_clients[g_last_client_count++] = client;
	}
}

void CRadioStyle::OnUserMessageSent(int msg_id)
{
	for (unsigned int i=0; i<g_last_client_count; i++)
	{
		int client = g_last_clients[i];
#if defined MENU_DEBUG
		logger->LogMessage("[SM_MENU] CRadioStyle got ShowMenu (client %d) (bInMenu %d)",
			client,
			m_players[client].bInExternMenu);
#endif
		if (m_players[client].bInMenu)
		{
			_CancelClientMenu(client, MenuCancel_Interrupted, true);
		}
		m_players[client].bInExternMenu = true;
		m_players[client].menuHoldTime = g_last_holdtime;
	}
	g_last_client_count = 0;
}

void CRadioStyle::SendDisplay(int client, IMenuPanel *display)
{
	CRadioDisplay *rDisplay = (CRadioDisplay *)display;
	rDisplay->SendRawDisplay(client, m_players[client].menuHoldTime);
}

IMenuPanel *CRadioStyle::CreatePanel()
{
	return g_RadioMenuStyle.MakeRadioDisplay();
}

IBaseMenu *CRadioStyle::CreateMenu(IMenuHandler *pHandler, IdentityToken_t *pOwner)
{
	return new CRadioMenu(pHandler, pOwner);
}

unsigned int CRadioStyle::GetMaxPageItems()
{
	return s_RadioMaxPageItems;
}

const char *CRadioStyle::GetStyleName()
{
	return "radio";
}

CBaseMenuPlayer *CRadioStyle::GetMenuPlayer(int client)
{
	return &m_players[client];
}

CRadioDisplay *CRadioStyle::MakeRadioDisplay(CRadioMenu *menu)
{
	CRadioDisplay *display;
	if (m_FreeDisplays.empty())
	{
		display = new CRadioDisplay();
	}
	else
	{
		display = m_FreeDisplays.front();
		m_FreeDisplays.pop();
		display->Reset();
	}
	return display;
}

void CRadioStyle::FreeRadioDisplay(CRadioDisplay *display)
{
	m_FreeDisplays.push(display);
}

CRadioMenuPlayer *CRadioStyle::GetRadioMenuPlayer(int client)
{
	return &m_players[client];
}

void CRadioStyle::ProcessWatchList()
{
	if (!g_RadioMenuTimeout)
	{
		BaseMenuStyle::ProcessWatchList();
		return;
	}

	BaseMenuStyle::ProcessWatchList();

	CRadioMenuPlayer *pPlayer;
	unsigned int max_clients = MaxClients();
	for (unsigned int i = 1; i <= max_clients; i++)
	{
		pPlayer = GetRadioMenuPlayer(i);
		if (!pPlayer->bInMenu || pPlayer->bInExternMenu)
		{
			continue;
		}
		if (pPlayer->Radio_NeedsRefresh())
		{
			pPlayer->Radio_Refresh();
		}
	}
}

unsigned int CRadioStyle::GetApproxMemUsage()
{
	return sizeof(CRadioStyle) + (sizeof(CRadioMenuPlayer) * 257);
}

CRadioDisplay::CRadioDisplay()
{
	Reset();
}

CRadioDisplay::CRadioDisplay(CRadioMenu *menu)
{
	Reset();
}

bool CRadioDisplay::DrawRawLine(const char *rawline)
{
	m_BufferText.append(rawline);
	m_BufferText.append("\n");
	return true;
}

void CRadioDisplay::Reset()
{
	m_BufferText.assign("");
	m_Title.assign("");
	m_NextPos = 1;
	keys = 0;
}

bool CRadioDisplay::DirectSet(const char *str)
{
	m_Title.clear();
	m_BufferText.assign(str);
	
	return true;
}

unsigned int CRadioDisplay::GetCurrentKey()
{
	return m_NextPos;
}

bool CRadioDisplay::SetCurrentKey(unsigned int key)
{
	if (key < m_NextPos || m_NextPos > s_RadioMaxPageItems)
	{
		return false;
	}

	m_NextPos = key;

	return true;
}

bool CRadioDisplay::SendDisplay(int client, IMenuHandler *handler, unsigned int time)
{
	return g_RadioMenuStyle.DoClientMenu(client, this, handler, time);
}

bool CRadioDisplay::SetExtOption(MenuOption option, const void *valuePtr)
{
	return false;
}

IMenuStyle *CRadioDisplay::GetParentStyle()
{
	return &g_RadioMenuStyle;
}

void CRadioDisplay::DrawTitle(const char *text, bool onlyIfEmpty/* =false */)
{
	if (m_Title.size() != 0 && onlyIfEmpty)
	{
		return;
	}
	m_Title.assign(text);
}

unsigned int CRadioDisplay::DrawItem(const ItemDrawInfo &item)
{
	if (m_NextPos > s_RadioMaxPageItems || !CanDrawItem(item.style))
	{
		return 0;
	}

	if (item.style & ITEMDRAW_RAWLINE)
	{
		if (item.style & ITEMDRAW_SPACER)
		{
			m_BufferText.append(" \n");
		} else {
			m_BufferText.append(item.display);
			m_BufferText.append("\n");
		}
		return 0;
	} else if (item.style & ITEMDRAW_SPACER) {
		m_BufferText.append(" \n");
		return m_NextPos++;
	} else if (item.style & ITEMDRAW_NOTEXT) {
		return m_NextPos++;
	}

	if (item.style & ITEMDRAW_DISABLED)
	{
		m_BufferText.append(g_RadioNumTable[m_NextPos]);
		m_BufferText.append(item.display);
		m_BufferText.append("\n");
	} else {
		m_BufferText.append("->");
		m_BufferText.append(g_RadioNumTable[m_NextPos]);
		m_BufferText.append(item.display);
		m_BufferText.append("\n");
		keys |= (1<<(m_NextPos-1));
	}

	return m_NextPos++;
}

bool CRadioDisplay::CanDrawItem(unsigned int drawFlags)
{
	if ((drawFlags & ITEMDRAW_IGNORE) == ITEMDRAW_IGNORE)
	{
		return false;
	}

	if ((drawFlags & ITEMDRAW_DISABLED) && (drawFlags & ITEMDRAW_CONTROL))
	{
		return false;
	}

	return true;
}

// The keys of a menu's page that move through it or leave it; a panel has
// none (its states belong to no menu).
static int NavigationKeys(const menu_states_t &states)
{
	if (!states.menu)
	{
		return 0;
	}
	int keys = 0;
	for (int key = 1; key <= 10; key++)
	{
		ItemSelection type = states.slots[key].type;
		if (type == ItemSel_Back || type == ItemSel_Next || type == ItemSel_Exit || type == ItemSel_ExitBack)
		{
			keys |= 1 << (key - 1);
		}
	}
	return keys;
}

void CRadioDisplay::SendRawDisplay(int client, unsigned int time)
{
	int _sel_keys = (keys == 0) ? (1<<9) : keys;
	CRadioMenuPlayer *pPlayer = g_RadioMenuStyle.GetRadioMenuPlayer(client);
	pPlayer->Radio_Init(_sel_keys, m_Title.c_str(), m_BufferText.c_str());
	pPlayer->Radio_SetNavigationKeys(NavigationKeys(pPlayer->states));
	pPlayer->Radio_Refresh();
}

void CRadioMenuPlayer::Radio_SetIndex(unsigned int index)
{
	m_index = index;
}

bool CRadioMenuPlayer::Radio_NeedsRefresh()
{
	return (GameTime() - display_last_refresh >= g_RadioMenuTimeout);
}

void CRadioMenuPlayer::Radio_Init(int keys, const char *title, const char *text)
{
	if (title[0] != '\0')
	{
		display_len = ke::SafeSprintf(display_pkt, 
			sizeof(display_pkt), 
			"%s\n%s", 
			title,
			text);
	}
	else
	{
		display_len = ke::SafeStrcpy(display_pkt, 
			sizeof(display_pkt), 
			text);
	}

	// Some games have implemented CHudMenu::SelectMenuItem to close the menu
	// even if an invalid slot has been selected, which causes us a problem as
	// we'll never get any notification from the client and we'll keep the menu
	// alive on our end indefinitely. For these games, pretend that every slot
	// is valid for selection so we're guaranteed to get a menuselect command.
	// We don't want to do this for every game as the common SelectMenuItem
	// implementation ignores invalid selections and keeps the menu open, which
	// is a much nicer user experience.
	display_keys = s_RadioClosesOnInvalidSlot ? 0x7ff : keys;

	display_info = ShowMenuInfo();
	display_info.title = title;
}

void CRadioMenuPlayer::Radio_Refresh()
{
	cell_t players[1] = { (cell_t)m_index };
	char *ptr = display_pkt;
	char save = 0;
	size_t len = display_len;
	unsigned int time;

	/* Compute the new time */
	if (menuHoldTime == 0)
	{
		time = 0;
	}
	else
	{
		time = menuHoldTime - (unsigned int)(GameTime() - menuStartTime);
	}

	while (true)
	{
		if (len > 240)
		{
			save = ptr[240];
			ptr[240] = '\0';
		}

		UserMessages &messages = Core::Get().userMessages();
		display_info.holdTime = time;
		BitWriter *buffer = messages.StartMessage(g_ShowMenuId, players, 1, USERMSG_BLOCKHOOKS, &display_info);
		if (!buffer)
		{
			if (len > 240)
			{
				ptr[240] = save;
			}
			/* A message is being sent or hooked. SourceMod writes into the
			 * null buffer here and crashes the server; a display is a plugin
			 * error, the runner's own periodic refresh comes again later. */
			if (!display_info.refresh)
			{
				Core::Get().ReportPluginError(
					"a menu was displayed to client %u while a user message was being sent or hooked; "
					"SourceMod's radio menu style crashes the server there (it gets no ShowMenu message)",
					m_index);
			}
			return;
		}
		buffer->WriteWord(display_keys);
		buffer->WriteChar(time ? time : -1);
		buffer->WriteByte( (len > 240) ? 1 : 0 );
		buffer->WriteString(ptr);
		messages.EndMessage();
		if (len > 240)
		{
			ptr[240] = save;
			ptr = &ptr[240];
			len -= 240;
		}
		else
		{
			break;
		}
	}

	display_last_refresh = GameTime();
	display_info.refresh = true;
}

int CRadioDisplay::GetAmountRemaining()
{
	size_t amt = m_Title.size() + 1 + m_BufferText.size();
	if (amt >= 511)
	{
		return 0;
	}
	return (int)(511 - amt);
}

void CRadioDisplay::DeleteThis()
{
	delete this;
}

bool CRadioDisplay::SetSelectableKeys(unsigned int keymap)
{
	keys = (signed)keymap;
	return true;
}

unsigned int CRadioDisplay::GetApproxMemUsage()
{
	return sizeof(CRadioDisplay)
		+ m_BufferText.size()
		+ m_Title.size();
}

CRadioMenu::CRadioMenu(IMenuHandler *pHandler, IdentityToken_t *pOwner) : 
CBaseMenu(pHandler, &g_RadioMenuStyle, pOwner)
{
	m_Pagination = s_RadioMaxPageItems - MAX_PAGINATION_OPTIONS;
}

bool CRadioMenu::SetExtOption(MenuOption option, const void *valuePtr)
{
	return false;
}

IMenuPanel *CRadioMenu::CreatePanel()
{
	return g_RadioMenuStyle.MakeRadioDisplay(this);
}

bool CRadioMenu::Display(int client, unsigned int time, IMenuHandler *alt_handler)
{
	return DisplayAtItem(client, time, 0, alt_handler);
}

bool CRadioMenu::DisplayAtItem(int client,
							   unsigned int time,
							   unsigned int start_item,
							   IMenuHandler *alt_handler)
{
#if defined MENU_DEBUG
	logger->LogMessage("[SM_MENU] CRadioMenu::Display(%p) (client %d) (time %d)",
		this,
		client,
		time);
#endif
	if (m_bCancelling)
	{
		return false;
	}

	AutoHandleRooter ahr(this->GetHandle());
	return g_RadioMenuStyle.DoClientMenu(client,
		this,
		start_item,
		alt_handler ? alt_handler : m_pHandler, 
		time);
}

bool CRadioMenu::SetPagination(unsigned int itemsPerPage)
{
	const unsigned int maxPerPage = s_RadioMaxPageItems - MAX_PAGINATION_OPTIONS;
	if (itemsPerPage > maxPerPage)
	{
		return false;
	}

	return CBaseMenu::SetPagination(itemsPerPage);
}

void CRadioMenu::Cancel_Finally()
{
	g_RadioMenuStyle.CancelMenu(this);
}

unsigned int CRadioMenu::GetApproxMemUsage()
{
	return sizeof(CRadioMenu) + GetBaseMemUsage();
}

const char* g_RadioNumTable[11] = 
{
	"0. ", "1. ", "2. ", "3. ", "4. ", "5. ", "6. ", "7. ", "8. ", "9. ", "0. "
};

}  // namespace smrunner
