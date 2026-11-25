// Copyright 2026 davenonymous
//
// Ported from core/MenuVoting.cpp of SourceMod 1.12 (GPLv3, AlliedModders
// LLC). The sm_vote_* cvars are the core's cvars of the simulated console.
#include "core/menu_voting.h"

#include <am-string.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/convars.h"
#include "core/core.h"
#include "core/menus.h"
#include "core/players.h"
#include "core/timers.h"
#include "core/usermsgs.h"
#include "diagnostics.h"

namespace smrunner {

// The core's cvars, created with the simulated console.
static ConVar& CoreConVar(const char* name) {
	ConVar* cvar = Core::Get().engine().console().FindConVar(name);
	if (!cvar) {
		Fatal("the core cvar %s is missing", name);
	}
	return *cvar;
}

float g_next_vote = 0.0f;

#define VOTE_NOT_VOTING -2
#define VOTE_PENDING -1

#define sm_vote_hintbox CoreConVar("sm_vote_progress_hintbox")
#define sm_vote_chat CoreConVar("sm_vote_progress_chat")
#define sm_vote_console CoreConVar("sm_vote_progress_console")
#define sm_vote_client_console CoreConVar("sm_vote_progress_client_console")
#define sm_vote_delay CoreConVar("sm_vote_delay")

static void OnVoteDelayChange(ConVar *cvar, const char *value, float flOldValue)
{
	/* See if the new vote delay isn't something we need to account for */
	if (sm_vote_delay.GetFloat() < 1.0f)
	{
		g_next_vote = 0.0f;
		return;
	}

	/* If there was never a last vote, ignore this change */
	if (g_next_vote < 0.1f)
	{
		return;
	}

	/* Subtract the original value, then add the new one. */
	g_next_vote -= (float)atof(value);
	g_next_vote += sm_vote_delay.GetFloat();
}

unsigned int VoteMenuHandler::GetRemainingVoteDelay()
{
	if (g_next_vote <= GameTime())
	{
		return 0;
	}

	return (unsigned int)(g_next_vote - GameTime());
}

void VoteMenuHandler::OnSourceModAllInitialized()
{
	Core::Get().players().AddClientListener(this);
	Core::Get().convars().AddChangeListener("sm_vote_delay", this, OnVoteDelayChange);
}

void VoteMenuHandler::OnSourceModShutdown()
{
	Core::Get().players().RemoveClientListener(this);
}

void VoteMenuHandler::OnSourceModLevelChange(const char *mapName)
{
	g_next_vote = 0.0f;
}

unsigned int VoteMenuHandler::GetMenuAPIVersion2()
{
	return m_pHandler->GetMenuAPIVersion2();
}

void VoteMenuHandler::OnClientDisconnected(int client)
{
	if (!IsVoteInProgress())
	{
		return;
	}

	/* Wipe out their vote if they had one.  We have to make sure the the the
	 * newly connected client is not allowed to vote. 
	 */
	int item;
	if ((item = m_ClientVotes[client]) >= VOTE_PENDING)
	{
		if (item >= 0)
		{
			assert((unsigned)item < m_Items);
			assert(m_Votes[item] > 0);
			m_Votes[item]--;
		}
		m_ClientVotes[client] = VOTE_NOT_VOTING;
	}
}

bool VoteMenuHandler::IsVoteInProgress()
{
	return (m_pCurMenu != NULL);
}

bool VoteMenuHandler::StartVote(IBaseMenu *menu,
								unsigned int num_clients,
								int clients[],
								unsigned int max_time,
								unsigned int flags/* =0 */)
{
	if (!InitializeVoting(menu, menu->GetHandler(), max_time, flags))
	{
		return false;
	}

	/* Note: we can use game time and not universal time because 
	 * if we're voting then players are in-game.
	 */

	float fVoteDelay = sm_vote_delay.GetFloat();
	if (fVoteDelay < 1.0)
	{
		g_next_vote = 0.0;
	}
	else
	{
		/* This little trick breaks for infinite votes!
		 * However, we just ignore that since those 1) shouldn't exist and 
		 * 2) people must be checking IsVoteInProgress() beforehand anyway.
		 */
		g_next_vote = GameTime() + fVoteDelay + (float)max_time;
	}

	m_fStartTime = GameTime();
	m_nMenuTime = max_time;

	for (unsigned int i=0; i<num_clients; i++)
	{
		if (clients[i] < 1 || clients[i] > 256)
		{
			continue;
		}
		menu->Display(clients[i], max_time, this);
	}

	StartVoting();

	return true;
}

bool VoteMenuHandler::IsClientInVotePool(int client)
{
	if (client < 1 
		|| client > MaxClients() 
		|| m_pCurMenu == NULL)
	{
		return false;
	}

	return (m_ClientVotes[client] > VOTE_NOT_VOTING);
}

bool VoteMenuHandler::GetClientVoteChoice(int client, unsigned int *pItem)
{
	if (!IsClientInVotePool(client)
		|| m_ClientVotes[client] == VOTE_PENDING)
	{
		return false;
	}

	*pItem = m_ClientVotes[client];

	return true;
}

bool VoteMenuHandler::RedrawToClient(int client, bool revotes)
{
	unsigned int time_limit;

	if (!IsClientInVotePool(client))
	{
		return false;
	}

	if (m_ClientVotes[client] >= 0)
	{
		if ((m_VoteFlags & VOTEFLAG_NO_REVOTES) == VOTEFLAG_NO_REVOTES || !revotes)
		{
			return false;
		}
		assert((unsigned)m_ClientVotes[client] < m_Items);
		assert(m_Votes[m_ClientVotes[client]] > 0);
		m_Votes[m_ClientVotes[client]]--;
		m_ClientVotes[client] = VOTE_PENDING;
		m_Revoting[client] = true;
		m_NumVotes--;
	}

	if (m_nMenuTime == MENU_TIME_FOREVER)
	{
		time_limit = m_nMenuTime;
	}
	else
	{
		time_limit = (int)((float)m_nMenuTime - (GameTime() - m_fStartTime));
		
		/* Make sure this doesn't round to zero */
		if (time_limit == MENU_TIME_FOREVER)
		{
			time_limit = 1;
		}
	}

	return m_pCurMenu->Display(client, time_limit, this);
}

bool VoteMenuHandler::InitializeVoting(IBaseMenu *menu, 
									   IMenuHandler *handler,
									   unsigned int time,
									   unsigned int flags)
{
	if (IsVoteInProgress())
	{
		return false;
	}

	InternalReset();

	/* Mark all clients as not voting */
	for (int i=1; i<=MaxClients(); i++)
	{
		m_ClientVotes[i] = VOTE_NOT_VOTING;
		m_Revoting[i] = false;
	}

	m_Items = menu->GetItemCount();

	if (m_Votes.size() < (size_t)m_Items)
	{
		/* Only clear the items we need to... */
		size_t size = m_Votes.size();
		for (size_t i=0; i<size; i++)
		{
			m_Votes[i] = 0;
		}
		m_Votes.resize(m_Items, 0);
	}
	else
	{
		for (unsigned int i=0; i<m_Items; i++)
		{
			m_Votes[i] = 0;
		}
	}

	m_pCurMenu = menu;
	m_VoteTime = time;
	m_VoteFlags = flags;
	m_pHandler = handler;

	m_pHandler->OnMenuStart(m_pCurMenu);

	return true;
}

void VoteMenuHandler::StartVoting()
{
	if (!m_pCurMenu)
	{
		return;
	}

	m_bStarted = true;

	m_pHandler->OnMenuVoteStart(m_pCurMenu);

	m_displayTimer = Core::Get().timers().CreateTimer(this, 1.0, NULL, TIMER_FLAG_REPEAT|TIMER_FLAG_NO_MAPCHANGE);

	/* By now we know how many clients were set.  
	 * If there are none, we should end IMMEDIATELY.
	 */
	if (m_Clients == 0)
	{
		EndVoting();
	}

	m_TotalClients = m_Clients;
}

void VoteMenuHandler::DecrementPlayerCount()
{
	assert(m_Clients > 0);

	m_Clients--;

	if (m_bStarted && m_Clients == 0)
	{
		EndVoting();
	}
}

int SortVoteItems(const void *item1, const void *item2)
{
	return ((menu_vote_result_t::menu_item_vote_t *)item2)->count 
		- ((menu_vote_result_t::menu_item_vote_t *)item1)->count;
}

void VoteMenuHandler::EndVoting()
{
	/* Set when the next delay ends.  We ignore cancellation because a menu
	 * was, at one point, displayed, which is all that counts.  However, we
	 * do re-calculate the time just in case the menu had no time limit.
	 */
	float fVoteDelay = sm_vote_delay.GetFloat();
	if (fVoteDelay < 1.0)
	{
		g_next_vote = 0.0;
	}
	else
	{
		g_next_vote = GameTime() + fVoteDelay;
	}

	if (m_displayTimer)
	{
		Core::Get().timers().KillTimer(m_displayTimer);
	}

	if (m_bCancelled)
	{
		/* If we were cancelled, don't bother tabulating anything.
		 * Reset just in case someone tries to redraw, which means
		 * we need to save our states.
		 */
		IBaseMenu *menu = m_pCurMenu;
		IMenuHandler *handler = m_pHandler;
		InternalReset();
		handler->OnMenuVoteCancel(menu, VoteCancel_Generic);
		handler->OnMenuEnd(menu, MenuEnd_VotingCancelled);
		return;
	}

	menu_vote_result_t vote;
	menu_vote_result_t::menu_client_vote_t client_vote[256];
	menu_vote_result_t::menu_item_vote_t item_vote[256];

	memset(&vote, 0, sizeof(vote));

	/* Build the item list */
	for (unsigned int i=0; i<m_Items; i++)
	{
		if (m_Votes[i] > 0)
		{
			item_vote[vote.num_items].count = m_Votes[i];
			item_vote[vote.num_items].item = i;
			vote.num_votes += m_Votes[i];
			vote.num_items++;
		}
	}
	vote.item_list = item_vote;

	if (!vote.num_votes)
	{
		IBaseMenu *menu = m_pCurMenu;
		IMenuHandler *handler = m_pHandler;
		InternalReset();
		handler->OnMenuVoteCancel(menu, VoteCancel_NoVotes);
		handler->OnMenuEnd(menu, MenuEnd_VotingCancelled);
		return;
	}

	/* Build the client list */
	for (int i=1; i<=MaxClients(); i++)
	{
		if (m_ClientVotes[i] >= VOTE_PENDING)
		{
			client_vote[vote.num_clients].client = i;
			client_vote[vote.num_clients].item = m_ClientVotes[i];
			vote.num_clients++;
		}
	}
	vote.client_list = client_vote;

	/* Sort the item list descending like we promised */
	qsort(item_vote,
		vote.num_items,
		sizeof(menu_vote_result_t::menu_item_vote_t),
		SortVoteItems);

	/* Save states, then clear what we've saved.
	 * This makes us re-entrant, which is always the safe way to go.
	 */
	IBaseMenu *menu = m_pCurMenu;
	IMenuHandler *handler = m_pHandler;
	InternalReset();

	/* Send vote info */
	handler->OnMenuVoteResults(menu, &vote);
	handler->OnMenuEnd(menu, MenuEnd_VotingDone);
}

void VoteMenuHandler::OnMenuStart(IBaseMenu *menu)
{
	m_Clients++;
}

void VoteMenuHandler::OnMenuEnd(IBaseMenu *menu, MenuEndReason reason)
{
	DecrementPlayerCount();
}

void VoteMenuHandler::OnMenuCancel(IBaseMenu *menu, int client, MenuCancelReason reason)
{
	m_pHandler->OnMenuCancel(menu, client, reason);
}

void VoteMenuHandler::OnMenuDisplay(IBaseMenu *menu, int client, IMenuPanel *display)
{
	m_ClientVotes[client] = VOTE_PENDING;
	m_pHandler->OnMenuDisplay(menu, client, display);
}

unsigned int VoteMenuHandler::OnMenuDisplayItem(IBaseMenu *menu, int client, IMenuPanel *panel, unsigned int item, const ItemDrawInfo &dr)
{
	return m_pHandler->OnMenuDisplayItem(menu, client, panel, item, dr);
}

void VoteMenuHandler::OnMenuDrawItem(IBaseMenu *menu, int client, unsigned int item, unsigned int &style)
{
	m_pHandler->OnMenuDrawItem(menu, client, item, style);
}

void VoteMenuHandler::OnMenuSelect(IBaseMenu *menu, int client, unsigned int item)
{
	/* Check by our item count, NOT the vote array size */
	if (item < m_Items)
	{
		unsigned int index = menu->GetRealItemIndex(client, item);
		m_ClientVotes[client] = index;
		m_Votes[index]++;
		m_NumVotes++;

		if (sm_vote_chat.GetBool() || sm_vote_console.GetBool() || sm_vote_client_console.GetBool())
		{
			static char buffer[1024];
			ItemDrawInfo dr;
			menu->GetItemInfo(item, &dr, client);

			if (sm_vote_console.GetBool())
			{
				int target = SOURCEMOD_SERVER_LANGUAGE;
				logicore.CoreTranslate(buffer, sizeof(buffer), "[SM] %T", 4, NULL, "Voted For",
				                       &target, PlayerOf(client)->GetName(), dr.display);
				Core::Get().engine().console().Printf("%s\n", buffer);
			}
			
			if (sm_vote_chat.GetBool() || sm_vote_client_console.GetBool())
			{
				int maxclients = MaxClients();
				for (int i=1; i<=maxclients; i++)
				{	
					SimPlayer *pPlayer = PlayerOf(i);
					assert(pPlayer);

					if (pPlayer->IsInGame() && !pPlayer->IsFakeClient())
					{
						if (m_Revoting[client])
						{
							logicore.CoreTranslate(buffer, sizeof(buffer), "[SM] %T", 4, NULL, "Changed Vote",
							                       &i, PlayerOf(client)->GetName(), dr.display);
						}
						else
						{
							logicore.CoreTranslate(buffer, sizeof(buffer), "[SM] %T", 4, NULL, "Voted For",
							                       &i, PlayerOf(client)->GetName(), dr.display);
						}

						if (sm_vote_chat.GetBool())
						{
							SendTextMsg(i, kHudPrintTalk, buffer);
						}

						if (sm_vote_client_console.GetBool())
						{
							pPlayer->PrintToConsole((std::string(buffer) + "\n").c_str());
						}		
					}
				}
			}
		}

		BuildVoteLeaders();
		DrawHintProgress();
	}

	m_pHandler->OnMenuSelect(menu, client, item);
}

void VoteMenuHandler::OnMenuSelect2(IBaseMenu *menu, int client, unsigned int item, unsigned int item_on_page)
{
	if (m_pHandler->GetMenuAPIVersion2() >= 13)
	{
		m_pHandler->OnMenuSelect2(menu, client, item, item_on_page);
	}
}

void VoteMenuHandler::InternalReset()
{
	m_Clients = 0;
	m_Items = 0;
	m_bStarted = false;
	m_pCurMenu = NULL;
	m_NumVotes = 0;
	m_bCancelled = false;
	m_pHandler = NULL;
	m_leaderList[0] = '\0';
	m_displayTimer = NULL;
	m_TotalClients = 0;
}

void VoteMenuHandler::CancelVoting()
{
	if (m_bCancelled || !m_pCurMenu)
	{
		return;
	}
	m_bCancelled = true;
	m_pCurMenu->Cancel();
}

IBaseMenu *VoteMenuHandler::GetCurrentMenu()
{
	return m_pCurMenu;
}

bool VoteMenuHandler::IsCancelling()
{
	return m_bCancelled;
}

void VoteMenuHandler::DrawHintProgress()
{
	if (!sm_vote_hintbox.GetBool())
	{
		return;
	}

	static char buffer[1024];

	float timeRemaining = (m_fStartTime + m_nMenuTime) - GameTime();
	if (timeRemaining < 0)
	{
		timeRemaining = 0.0;
	}

	int iTimeRemaining = static_cast<int>(roundf(timeRemaining));
	
	int maxclients = MaxClients();
	for (int i=1; i<=maxclients; i++)
	{
		if (PlayerOf(i)->IsInGame())
		{
			logicore.CoreTranslate(buffer, sizeof(buffer), "%T%s", 6, NULL, "Vote Count",
			                       &i, &m_NumVotes, &m_TotalClients, &iTimeRemaining, &m_leaderList);
			SendHintText(i, buffer);
		}
	}
}

void VoteMenuHandler::BuildVoteLeaders()
{
	if (m_NumVotes == 0 || !sm_vote_hintbox.GetBool())
	{
		return;
	}

	menu_vote_result_t vote;
	menu_vote_result_t::menu_item_vote_t item_vote[256];

	memset(&vote, 0, sizeof(vote));

	/* Build the item list */
	for (unsigned int i=0; i<m_Items; i++)
	{
		if (m_Votes[i] > 0)
		{
			item_vote[vote.num_items].count = m_Votes[i];
			item_vote[vote.num_items].item = i;
			vote.num_votes += m_Votes[i];
			vote.num_items++;
		}
	}
	vote.item_list = item_vote;
	assert(vote.num_votes);

	/* Sort the item list descending */
	qsort(item_vote,
		vote.num_items,
		sizeof(menu_vote_result_t::menu_item_vote_t),
		SortVoteItems);

	/* Take the top 3 (if applicable) and draw them */
	int len = 0;
	for (unsigned int i=0; i<vote.num_items && i<3; i++)
	{
		int curItem = vote.item_list[i].item;
		ItemDrawInfo dr;
		m_pCurMenu->GetItemInfo(curItem, &dr);
		len += ke::SafeSprintf(m_leaderList + len, sizeof(m_leaderList) - len, "\n%i. %s: (%i)", i+1, dr.display, vote.item_list[i].count);
	}
}

SourceMod::ResultType VoteMenuHandler::OnTimer(ITimer *pTimer, void *pData)
{
	DrawHintProgress();

	return Pl_Continue;
}

void VoteMenuHandler::OnTimerEnd(ITimer *pTimer, void *pData)
{
	m_displayTimer = NULL;
}

}  // namespace smrunner
