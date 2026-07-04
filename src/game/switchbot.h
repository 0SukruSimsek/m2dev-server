#pragma once
// switchbot.h -- Local Development Bot Framework (server-side).
//
// Allows up to N GM/test characters to be toggled into automated simulation
// mode via the /switchbot command.  NO anti-detection, NO evasion.
// IsSwitchbot() is always visible; all toggles are written to GMCommandLog.
//
// Design notes: see memory/agents/server-src-uzmani/switchbot_design.md

#ifndef __INC_SWITCHBOT_H__
#define __INC_SWITCHBOT_H__

#include "switchbot_log.h"
#include <unordered_map>

// Forward declarations (char.h already included transitively via stdafx in
// translation units that use this header, but keep it clean here).
class CHARACTER;
typedef CHARACTER* LPCHARACTER;

// ============================================================================
// Per-bot runtime state (extends SSwitchbotMetric with live-session data)
// ============================================================================
struct SBotEntry
{
    SSwitchbotMetric metric;
    DWORD            pid         = 0;
    bool             active      = true;

    // Scenario rotation timer.
    time_t           next_rotate = 0;

    // --- Farm-only runtime state (NOT part of metric — metric is Flush/StuckCheck owned) ---

    // F1: Farm warp counter — incremented by FarmWander when BotMoveStep returns
    // no-op (position unchanged). NOT metric.stuck_events (which belongs to
    // StuckCheck + FlushMetric.Reset()). Reset to 0 after warp fires.
    int              farm_stuck_score = 0;

    // F3: Time when the farm last saw zero mobs in scan radius.
    // Set on first empty scan, cleared on mob found, warp fires at 90s elapsed.
    time_t           no_mob_since = 0;

    SBotEntry() = default;
    explicit SBotEntry(DWORD p) : pid(p) { metric.pid = p; }
};

// ============================================================================
// CSwitchbotManager -- singleton, owns all active bots.
// Pulse timer drives all per-bot ticks (see switchbot.cpp).
// ============================================================================
class CSwitchbotManager : public singleton<CSwitchbotManager>
{
public:
    CSwitchbotManager();
    ~CSwitchbotManager();

    // Must be called after thecore_init (start() has run) to start the pulse event.
    void Initialize();

    // Enable / disable bot mode for a character.
    // Writes to GMCommandLog, sets quest-flag "switchbot.enabled".
    void Toggle(LPCHARACTER ch, bool enable);

    // Force a specific scenario on a bot character.
    void ForceScenario(LPCHARACTER ch, EBotScenario scenario);

    // Called once per idle() cycle via event_create.
    void Pulse();

    // Returns true when ch is registered and active.
    bool IsSwitchbot(DWORD pid) const;

    // Status string for /switchbot status  (written to chat).
    std::string StatusString(LPCHARACTER ch) const;

    // Number of currently active bots.
    size_t GetBotCount() const { return m_bots.size(); }

private:
    // Per-bot tick — called from Pulse() for each validated bot.
    void  BotTick(SBotEntry& entry, LPCHARACTER ch);

    // Scenario dispatchers.
    void  ScenarioTick_Farm    (SBotEntry& entry, LPCHARACTER ch);
    void  ScenarioTick_Quest   (SBotEntry& entry, LPCHARACTER ch);
    void  ScenarioTick_Dungeon (SBotEntry& entry, LPCHARACTER ch);
    void  ScenarioTick_PvP     (SBotEntry& entry, LPCHARACTER ch);
    void  ScenarioTick_Market  (SBotEntry& entry, LPCHARACTER ch);
    void  ScenarioTick_Idle    (SBotEntry& entry, LPCHARACTER ch);

    // Anomaly detectors (per-pulse, switchbot chars only).
    void  ExploitCheck (SBotEntry& entry, LPCHARACTER ch);
    void  StuckCheck   (SBotEntry& entry, LPCHARACTER ch);

    // Rotate to next scenario if rotation timer has elapsed.
    void  MaybeRotateScenario(SBotEntry& entry, LPCHARACTER ch);

    // Flush accumulated metrics to log DB and reset counters.
    void  FlushMetric  (SBotEntry& entry, LPCHARACTER ch, const char* event_type, const char* detail = "");

    // Log a single event row immediately.
    void  LogEvent     (const SBotEntry& entry, LPCHARACTER ch, const char* event_type, const char* detail);

    // pid -> SBotEntry map (no raw pointer storage — pid used to look up ch
    // via CHARACTER_MANAGER each pulse, avoiding dangling-pointer issues and
    // making char.cpp modifications unnecessary).
    std::unordered_map<DWORD, SBotEntry> m_bots;

    // Pulse event handle (thecore event system).
    LPEVENT m_pPulseEvent;
};

#endif // __INC_SWITCHBOT_H__
