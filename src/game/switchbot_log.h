#pragma once
// switchbot_log.h -- Per-bot in-memory metric aggregate.
// Flushed to log_switchbot_dev (db: log) every SWITCHBOT_FLUSH_INTERVAL pulses
// or when the bot is toggled off.
//
// NO anti-detection / NO evasion logic.
// IsSwitchbot() flag is always visible; every Toggle writes to GMCommandLog.

#include <cstdint>
#include <cstring>
#include <ctime>

// How often (in seconds) accumulated metrics are flushed to the log DB.
static constexpr int SWITCHBOT_FLUSH_INTERVAL = 60;

// Scenario IDs for rotation.
enum EBotScenario : uint8_t
{
    BOT_SCENARIO_FARM     = 0,
    BOT_SCENARIO_QUEST    = 1,
    BOT_SCENARIO_DUNGEON  = 2,
    BOT_SCENARIO_PVP      = 3,
    BOT_SCENARIO_MARKET   = 4,
    BOT_SCENARIO_IDLE     = 5,
    BOT_SCENARIO_COUNT    = 6,
};

// Default scenario rotation interval in seconds.
static constexpr int SWITCHBOT_SCENARIO_ROTATE_SEC = 300;

// Recent position history depth for stuck detection.
static constexpr int SWITCHBOT_POS_HISTORY = 10;
// Distance threshold: if all last N positions are within ±STUCK_RADIUS the bot
// is considered stuck.
static constexpr int SWITCHBOT_STUCK_RADIUS = 50;

inline const char* BotScenarioName(EBotScenario s)
{
    switch (s)
    {
        case BOT_SCENARIO_FARM:    return "FARM";
        case BOT_SCENARIO_QUEST:   return "QUEST";
        case BOT_SCENARIO_DUNGEON: return "DUNGEON";
        case BOT_SCENARIO_PVP:     return "PVP";
        case BOT_SCENARIO_MARKET:  return "MARKET";
        case BOT_SCENARIO_IDLE:    return "IDLE";
        default:                   return "UNKNOWN";
    }
}

inline EBotScenario BotScenarioFromName(const char* name)
{
    if (!name) return BOT_SCENARIO_FARM;
    if (0 == strcasecmp(name, "FARM"))    return BOT_SCENARIO_FARM;
    if (0 == strcasecmp(name, "QUEST"))   return BOT_SCENARIO_QUEST;
    if (0 == strcasecmp(name, "DUNGEON")) return BOT_SCENARIO_DUNGEON;
    if (0 == strcasecmp(name, "PVP"))     return BOT_SCENARIO_PVP;
    if (0 == strcasecmp(name, "MARKET"))  return BOT_SCENARIO_MARKET;
    if (0 == strcasecmp(name, "IDLE"))    return BOT_SCENARIO_IDLE;
    return BOT_SCENARIO_FARM;
}

// Per-bot in-memory metric aggregate (no DB dependency).
struct SSwitchbotMetric
{
    DWORD           pid          = 0;
    char            name[25]     = {};    // CHARACTER_NAME_MAX_LEN + 1

    // Counters accumulated since last flush.
    uint32_t        kills        = 0;
    uint32_t        encounters   = 0;
    uint32_t        stuck_events = 0;
    uint32_t        error_events = 0;

    // Scenario state.
    EBotScenario    scenario     = BOT_SCENARIO_FARM;
    time_t          scenario_started = 0;

    // Stuck detection: circular buffer of last N positions.
    int32_t         pos_x[SWITCHBOT_POS_HISTORY] = {};
    int32_t         pos_y[SWITCHBOT_POS_HISTORY] = {};
    int             pos_head     = 0;
    int             pos_count    = 0;

    // Exploit detection baselines (set at Toggle-on, refreshed each pulse).
    // Types match CHARACTER getter return types to avoid sign-extension warnings.
    uint32_t        last_exp     = 0; // GetExp() returns DWORD
    int             last_level   = 0;
    int             last_gold    = 0; // GetGold() returns INT

    // Flush bookkeeping.
    time_t          flush_at     = 0;  // epoch of last flush
    int32_t         last_x       = 0;
    int32_t         last_y       = 0;

    void Reset()
    {
        kills = encounters = stuck_events = error_events = 0;
        flush_at = time(nullptr);
    }
};
