/**
 * Copyright 2026 davenonymous
 *
 * Self tests of user messages: the core's text messages, messages of
 * plugins, what clients are recorded to receive, and hooks.
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <smrunner>

int g_hookCalls;
int g_seenDest;
char g_seenText[256];
bool g_sentNotified;
bool g_sent;
Action g_interceptResult;

Action Hook_TextMsg(UserMsg id, BfRead message, const int[] players, int playersNum, bool reliable, bool init) {
	g_hookCalls++;
	g_seenDest = message.ReadByte();
	message.ReadString(g_seenText, sizeof(g_seenText));
	return g_interceptResult;
}

void Post_TextMsg(UserMsg id, bool sent) {
	g_sentNotified = true;
	g_sent = sent;
}

void SendSayText2(int client, const char[] format, const char[] first = "", const char[] second = "") {
	BfWrite message = UserMessageToBfWrite(StartMessageOne("SayText2", client, USERMSG_RELIABLE));
	message.WriteByte(client);
	message.WriteByte(true);
	message.WriteString(format);
	message.WriteString(first);
	message.WriteString(second);
	EndMessage();
}

public void Test_TheMessageTableIsTF2s() {
	Assert.Equal(GetUserMessageId("SayText2"), 4);
	Assert.Equal(GetUserMessageId("TextMsg"), 5);
	char name[32];
	GetUserMessageName(view_as<UserMsg>(9), name, sizeof(name));
	Assert.StrEqual(name, "ShowMenu");
	Assert.Equal(GetUserMessageId("NoSuchMessage"), -1);
	Assert.Equal(GetUserMessageType(), UM_BitBuf);
}

public void Test_PrintToChatIsATextMsg() {
	int client = Runner.ConnectClient("alice");
	HookUserMessage(GetUserMessageId("TextMsg"), Hook_TextMsg);
	PrintToChat(client, "hello %s", "there");
	Assert.Equal(g_hookCalls, 1);
	Assert.Equal(g_seenDest, 3, "HUD_PRINTTALK");
	Assert.StrEqual(g_seenText, "hello there");
	Assert.ClientReceived(client, RunnerMessage_Chat, "hello there");
}

public void Test_CenterAndHintTextAreTheirKinds() {
	int client = Runner.ConnectClient("alice");
	PrintCenterText(client, "in the middle");
	PrintHintText(client, "a hint");
	Assert.ClientReceived(client, RunnerMessage_Center, "in the middle");
	Assert.ClientReceived(client, RunnerMessage_Hint, "a hint");
}

public void Test_SayText2IsChatAndMatchesWithoutColors() {
	int client = Runner.ConnectClient("alice");
	SendSayText2(client, "\x07FF0000Red\x01 and \x08FFFFFF80faded\x01 text");
	Assert.ClientReceived(client, RunnerMessage_Chat, "Red and faded text");
	Assert.ClientReceived(client, RunnerMessage_Chat, "\x07FF0000Red", "as it was sent, too");
	char text[256];
	Runner.GetClientMessage(client, Runner.ClientMessageCount(client) - 1, text, sizeof(text));
	Assert.StrEqual(text, "\x07FF0000Red\x01 and \x08FFFFFF80faded\x01 text", "the message stays raw");
}

public void Test_ParametersAreSubstitutedAndTokensKept() {
	int client = Runner.ConnectClient("alice");
	SendSayText2(client, "%s1 says %s2", "alice", "hi");
	SendSayText2(client, "#TF_Chat_All", "alice", "hi");
	Assert.ClientReceived(client, RunnerMessage_Chat, "alice says hi");
	Assert.ClientReceived(client, RunnerMessage_Chat, "#TF_Chat_All (alice, hi)");
}

public void Test_OtherMessagesAreRecordedByName() {
	int client = Runner.ConnectClient("alice");
	BfWrite message = UserMessageToBfWrite(StartMessageOne("Shake", client));
	message.WriteByte(0);
	message.WriteFloat(5.0);
	message.WriteFloat(1.0);
	message.WriteFloat(1.0);
	EndMessage();
	Assert.ClientReceived(client, RunnerMessage_UserMessage, "Shake");
	Assert.Equal(Runner.ClientMessageCount(client, RunnerMessage_Chat), 0);
}

public void Test_BotsAndOtherClientsReceiveNothing() {
	int alice = Runner.ConnectClient("alice");
	int bob = Runner.ConnectClient("bob");
	int bot = Runner.ConnectBot("bot");
	PrintToChat(alice, "for alice");
	PrintToChat(bot, "for the bot");
	Assert.Equal(Runner.ClientMessageCount(bob, RunnerMessage_Chat), 0);
	Assert.Equal(Runner.ClientMessageCount(bot), 0);
}

public void Test_AnInterceptHookCanBlockAMessage() {
	int client = Runner.ConnectClient("alice");
	g_interceptResult = Plugin_Stop;
	HookUserMessage(GetUserMessageId("TextMsg"), Hook_TextMsg, true, Post_TextMsg);
	PrintToChat(client, "blocked");
	Assert.Equal(g_hookCalls, 1);
	Assert.StrEqual(g_seenText, "blocked");
	Assert.False(Runner.ClientReceived(client, RunnerMessage_Chat, "blocked"));
	Assert.True(g_sentNotified);
	Assert.False(g_sent, "the post hook is told the message was not sent");
}

public void Test_AnInterceptHookThatContinuesLetsTheMessageThrough() {
	int client = Runner.ConnectClient("alice");
	g_interceptResult = Plugin_Continue;
	HookUserMessage(GetUserMessageId("TextMsg"), Hook_TextMsg, true, Post_TextMsg);
	PrintToChat(client, "passes");
	Assert.ClientReceived(client, RunnerMessage_Chat, "passes");
	Assert.True(g_sent);
}

public void Test_UnhookedHooksAreNotCalled() {
	int client = Runner.ConnectClient("alice");
	UserMsg id = GetUserMessageId("TextMsg");
	HookUserMessage(id, Hook_TextMsg);
	UnhookUserMessage(id, Hook_TextMsg);
	PrintToChat(client, "unhooked");
	Assert.Equal(g_hookCalls, 0);
}

public void Test_BlockHooksPassesHooksBy() {
	int client = Runner.ConnectClient("alice");
	HookUserMessage(GetUserMessageId("SayText2"), Hook_TextMsg);
	BfWrite message = UserMessageToBfWrite(StartMessageOne("SayText2", client, USERMSG_BLOCKHOOKS));
	message.WriteByte(client);
	message.WriteByte(true);
	message.WriteString("unseen by hooks");
	EndMessage();
	Assert.Equal(g_hookCalls, 0);
	Assert.ClientReceived(client, RunnerMessage_Chat, "unseen by hooks");
}

float g_readFloat;
int g_readNum;
float g_readCoord;
float g_readAngles[3];
float g_readNormal[3];
char g_readString[32];

Action Hook_ReadEverything(UserMsg id, BfRead message, const int[] players, int playersNum, bool reliable, bool init) {
	g_readNum = message.ReadNum();
	g_readFloat = message.ReadFloat();
	g_readCoord = message.ReadCoord();
	message.ReadAngles(g_readAngles);
	message.ReadVecNormal(g_readNormal);
	message.ReadString(g_readString, sizeof(g_readString));
	return Plugin_Continue;
}

public void Test_HooksReadWhatWasWritten() {
	int client = Runner.ConnectClient("alice");
	HookUserMessage(GetUserMessageId("Shake"), Hook_ReadEverything);
	BfWrite message = UserMessageToBfWrite(StartMessageOne("Shake", client));
	message.WriteNum(-123456);
	message.WriteFloat(2.5);
	message.WriteCoord(-100.25);
	message.WriteAngles({10.0, -20.5, 0.0});
	message.WriteVecNormal({0.0, 0.6, -0.8});
	message.WriteString("the end");
	EndMessage();
	Assert.Equal(g_readNum, -123456);
	Assert.FloatEqual(g_readFloat, 2.5);
	Assert.FloatEqual(g_readCoord, -100.25, 0.04);
	Assert.FloatEqual(g_readAngles[1], -20.5, 0.04);
	Assert.FloatEqual(g_readNormal[1], 0.6, 0.001);
	Assert.FloatEqual(g_readNormal[2], -0.8, 0.001);
	Assert.StrEqual(g_readString, "the end");
}

Action Hook_ReadWithANegativeLength(UserMsg id, BfRead message, const int[] players, int playersNum, bool reliable,
                                    bool init) {
	char text[8];
	message.ReadString(text, -1);
	return Plugin_Continue;
}

public void Test_ReadStringNeedsABuffer() {
	int client = Runner.ConnectClient("alice");
	HookUserMessage(GetUserMessageId("TextMsg"), Hook_ReadWithANegativeLength);
	Runner.ExpectError("BfReadString needs a buffer of at least 1 byte, not -1");
	PrintToChat(client, "a message to read");
}

public void Test_StartMessageRefusesANegativeClientCount() {
	Runner.ExpectError("the number of clients (-1) is negative");
	int clients[1];
	StartMessage("SayText2", clients, -1);
}

public void Test_AnOverflowingMessageIsAnError() {
	int client = Runner.ConnectClient("alice");
	Runner.ExpectError("The user message overflowed");
	char text[300];
	for (int i = 0; i < sizeof(text) - 1; i++) {
		text[i] = 'x';
	}
	BfWrite message = UserMessageToBfWrite(StartMessageOne("SayText2", client));
	message.WriteByte(client);
	message.WriteByte(true);
	message.WriteString(text);
	EndMessage();
}
