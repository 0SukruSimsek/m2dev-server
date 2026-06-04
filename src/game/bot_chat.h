// bot_chat.h — stub (Phase VII R7 chat system reverted)
// Provides no-op declarations for functions referenced in bot_ai_utility.cpp.
// ChatAction will silently skip (BotChat_PickLineDedup returns nullptr -> Execute returns false).
#pragma once

class CHARACTER;

// Returns nullptr always (no chat lines available).
inline const char* BotChat_PickMentionReply(BYTE /*persona*/, DWORD /*seed*/)
{
    return nullptr;
}

// No-op broadcast.
inline void BotChat_Broadcast(CHARACTER* /*bot*/, const char* /*msg*/)
{
}

// Returns nullptr always (no chat lines available).
inline const char* BotChat_PickLineDedup(CHARACTER* /*bot*/, BYTE /*persona*/, DWORD /*seed*/)
{
    return nullptr;
}

// Returns false always (no nearby PC detection without chat system).
inline bool BotChat_NearbyPC(CHARACTER* /*bot*/)
{
    return false;
}
