// switchbot.cpp -- Local Development Bot Framework (server-side).
//
// NO anti-detection / NO evasion. IsSwitchbot() flag is always visible.
// Every toggle writes to LogManager::GMCommandLog.
//
// Architecture: CSwitchbotManager owns a pid->SBotEntry map.
// A single recurring event calls Pulse() every SWITCHBOT_PULSE_TICKS ticks.
// Per pulse, each pid is validated via CHARACTER_MANAGER::FindByPID() so that
// no raw CHARACTER* is stored — this avoids dangling pointers without
// touching char.cpp destructor at all.

#include "stdafx.h"
#include "switchbot.h"
#include "switchbot_log.h"
#include "char.h"
#include "char_manager.h"
#include "desc_client.h"
#include "item.h"
#include "log.h"
#include "event.h"
#include "utils.h"
#include "config.h"

#include <sstream>
#include <algorithm>
#include <cmath>

// Pulse interval in seconds.  PASSES_PER_SEC is runtime, not constexpr.
#define SWITCHBOT_PULSE_TICKS PASSES_PER_SEC(2)

// ============================================================================
// Pulse event
// ============================================================================
EVENTINFO(TSwitchbotPulseInfo)
{
    CSwitchbotManager* mgr = nullptr;
};

EVENTFUNC(switchbot_pulse_event)
{
    if (!event) return 0;
    const TSwitchbotPulseInfo* info =
        dynamic_cast<TSwitchbotPulseInfo*>(event->info);
    if (!info || !info->mgr) return 0;

    try
    {
        info->mgr->Pulse();
    }
    catch (const std::exception& ex)
    {
        sys_err("switchbot_pulse_event exception: %s", ex.what());
    }

    return SWITCHBOT_PULSE_TICKS;
}

// ============================================================================
// CSwitchbotManager ctor / dtor
// ============================================================================
CSwitchbotManager::CSwitchbotManager()
    : m_pPulseEvent(nullptr)
{
    // Start the recurring pulse event immediately.
    TSwitchbotPulseInfo* info = AllocEventInfo<TSwitchbotPulseInfo>();
    info->mgr = this;
    m_pPulseEvent = event_create(switchbot_pulse_event, info, SWITCHBOT_PULSE_TICKS);
    sys_log(0, "CSwitchbotManager: pulse event created");
}

CSwitchbotManager::~CSwitchbotManager()
{
    if (m_pPulseEvent)
        event_cancel(&m_pPulseEvent);

    // Flush remaining bot metrics before shutdown.
    for (auto& kv : m_bots)
    {
        SBotEntry& entry = kv.second;
        LPCHARACTER ch = CHARACTER_MANAGER::instance().FindByPID(entry.pid);
        if (ch)
            FlushMetric(entry, ch, "TOGGLE", "shutdown_flush");
    }
    m_bots.clear();
}

// ============================================================================
// Toggle
// ============================================================================
void CSwitchbotManager::Toggle(LPCHARACTER ch, bool enable)
{
    if (!ch) return;

    const DWORD pid = ch->GetPlayerID();
    const char* name = ch->GetName();

    if (enable)
    {
        if (m_bots.count(pid))
        {
            ch->ChatPacket(CHAT_TYPE_INFO, "[BOT] Already active.");
            return;
        }

        SBotEntry entry(pid);
        strncpy(entry.metric.name, name, sizeof(entry.metric.name) - 1);
        entry.metric.scenario         = BOT_SCENARIO_FARM;
        entry.metric.scenario_started = time(nullptr);
        entry.next_rotate             = time(nullptr) + SWITCHBOT_SCENARIO_ROTATE_SEC;
        entry.metric.flush_at         = time(nullptr);

        // Baseline for exploit detection (types match getter returns).
        entry.metric.last_exp   = static_cast<uint32_t>(ch->GetExp());
        entry.metric.last_level = ch->GetLevel();
        entry.metric.last_gold  = ch->GetGold();

        m_bots.emplace(pid, entry);

        // Persist via quest-flag (TPlayerTable unchanged).
        ch->SetQuestFlag("switchbot.enabled", 1);

        // Audit trail (GMCommandLog: pid, name, ip, channel, command).
        {
            const char* ip = ch->GetDesc() ? ch->GetDesc()->GetHostName() : "0.0.0.0";
            LogManager::instance().GMCommandLog(pid, name, ip, g_bChannel, "switchbot ON");
        }
        LogManager::instance().CharLog(ch, 0, "SWITCHBOT_ON", "");

        ch->ChatPacket(CHAT_TYPE_INFO,
            "[BOT] Switchbot ON. Scenario: %s. Rotate every %ds.",
            BotScenarioName(entry.metric.scenario),
            SWITCHBOT_SCENARIO_ROTATE_SEC);
    }
    else
    {
        auto it = m_bots.find(pid);
        if (it == m_bots.end())
        {
            ch->ChatPacket(CHAT_TYPE_INFO, "[BOT] Not active.");
            return;
        }

        FlushMetric(it->second, ch, "TOGGLE", "bot_off");
        m_bots.erase(it);

        ch->SetQuestFlag("switchbot.enabled", 0);

        {
            const char* ip = ch->GetDesc() ? ch->GetDesc()->GetHostName() : "0.0.0.0";
            LogManager::instance().GMCommandLog(pid, name, ip, g_bChannel, "switchbot OFF");
        }
        LogManager::instance().CharLog(ch, 0, "SWITCHBOT_OFF", "");

        ch->ChatPacket(CHAT_TYPE_INFO, "[BOT] Switchbot OFF.");
    }
}

// ============================================================================
// ForceScenario
// ============================================================================
void CSwitchbotManager::ForceScenario(LPCHARACTER ch, EBotScenario scenario)
{
    if (!ch) return;
    auto it = m_bots.find(ch->GetPlayerID());
    if (it == m_bots.end())
    {
        ch->ChatPacket(CHAT_TYPE_INFO, "[BOT] Not active. Use /switchbot on first.");
        return;
    }

    SBotEntry& entry = it->second;
    FlushMetric(entry, ch, "SCENARIO_CHANGE",
        BotScenarioName(entry.metric.scenario));

    entry.metric.scenario         = scenario;
    entry.metric.scenario_started = time(nullptr);
    entry.next_rotate             = time(nullptr) + SWITCHBOT_SCENARIO_ROTATE_SEC;

    LogEvent(entry, ch, "SCENARIO_CHANGE", BotScenarioName(scenario));
    ch->ChatPacket(CHAT_TYPE_INFO, "[BOT] Scenario set to %s.",
        BotScenarioName(scenario));
}

// ============================================================================
// IsSwitchbot
// ============================================================================
bool CSwitchbotManager::IsSwitchbot(DWORD pid) const
{
    return m_bots.count(pid) != 0;
}

// ============================================================================
// StatusString
// ============================================================================
std::string CSwitchbotManager::StatusString(LPCHARACTER ch) const
{
    if (!ch) return "";
    auto it = m_bots.find(ch->GetPlayerID());
    if (it == m_bots.end()) return "[BOT] Inactive.";

    const SBotEntry& e = it->second;
    std::ostringstream oss;
    oss << "[BOT] Active | Scenario: " << BotScenarioName(e.metric.scenario)
        << " | Kills: " << e.metric.kills
        << " | Enc: "   << e.metric.encounters
        << " | Stuck: " << e.metric.stuck_events
        << " | Err: "   << e.metric.error_events;
    return oss.str();
}

// ============================================================================
// Pulse -- called every SWITCHBOT_PULSE_TICKS from the event system
// ============================================================================
void CSwitchbotManager::Pulse()
{
    if (m_bots.empty()) return;

    std::vector<DWORD> expired;

    for (auto& kv : m_bots)
    {
        SBotEntry& entry = kv.second;
        const DWORD pid  = entry.pid;

        // Validity check: no raw pointer stored — resolve each pulse.
        LPCHARACTER ch = CHARACTER_MANAGER::instance().FindByPID(pid);
        if (!ch)
        {
            // Character no longer exists (logged out / destroyed).
            // Final flush already happened in Toggle(off) or was skipped.
            expired.push_back(pid);
            continue;
        }

        try
        {
            BotTick(entry, ch);
        }
        catch (const std::exception& ex)
        {
            sys_err("switchbot BotTick PID=%u exception: %s", pid, ex.what());
            entry.metric.error_events++;
            LogEvent(entry, ch, "CRASH", ex.what());
        }
    }

    for (DWORD pid : expired)
    {
        auto it = m_bots.find(pid);
        if (it != m_bots.end())
        {
            // Quest-flag reset is not possible without ch; leave as-is.
            sys_log(0, "switchbot: PID %u vanished, removing from bot set", pid);
            m_bots.erase(it);
        }
    }
}

// ============================================================================
// BotTick
// ============================================================================
void CSwitchbotManager::BotTick(SBotEntry& entry, LPCHARACTER ch)
{
    // Exploit check before any action.
    ExploitCheck(entry, ch);

    // Stuck detection.
    StuckCheck(entry, ch);

    // Update position history.
    const int32_t cx = ch->GetX();
    const int32_t cy = ch->GetY();
    int head = entry.metric.pos_head;
    entry.metric.pos_x[head] = cx;
    entry.metric.pos_y[head] = cy;
    entry.metric.pos_head    = (head + 1) % SWITCHBOT_POS_HISTORY;
    if (entry.metric.pos_count < SWITCHBOT_POS_HISTORY)
        entry.metric.pos_count++;

    entry.metric.last_x = cx;
    entry.metric.last_y = cy;

    // Maybe rotate scenario.
    MaybeRotateScenario(entry, ch);

    // Dispatch scenario tick.
    switch (entry.metric.scenario)
    {
        case BOT_SCENARIO_FARM:    ScenarioTick_Farm   (entry, ch); break;
        case BOT_SCENARIO_QUEST:   ScenarioTick_Quest  (entry, ch); break;
        case BOT_SCENARIO_DUNGEON: ScenarioTick_Dungeon(entry, ch); break;
        case BOT_SCENARIO_PVP:     ScenarioTick_PvP    (entry, ch); break;
        case BOT_SCENARIO_MARKET:  ScenarioTick_Market (entry, ch); break;
        case BOT_SCENARIO_IDLE:    ScenarioTick_Idle   (entry, ch); break;
        default: break;
    }

    // Periodic flush.
    if (time(nullptr) - entry.metric.flush_at >= SWITCHBOT_FLUSH_INTERVAL)
        FlushMetric(entry, ch, "TICK_OK", "");
}

// ============================================================================
// Scenario ticks (stubs; GM/QA team fills in real logic)
// ============================================================================
void CSwitchbotManager::ScenarioTick_Farm(SBotEntry& entry, LPCHARACTER ch)
{
    // FARM: move randomly in a small radius, simulate mob encounter.
    // Stub: increment encounter counter to test metric pipeline.
    entry.metric.encounters++;
    // Real implementation: ch->MoveToRandom(), trigger attack, pick up drops.
    (void)ch;
}

void CSwitchbotManager::ScenarioTick_Quest(SBotEntry& entry, LPCHARACTER ch)
{
    // QUEST: stub — simulate quest interaction tick.
    entry.metric.encounters++;
    (void)ch;
}

void CSwitchbotManager::ScenarioTick_Dungeon(SBotEntry& entry, LPCHARACTER ch)
{
    // DUNGEON: stub — simulate dungeon navigation tick.
    entry.metric.encounters++;
    (void)ch;
}

void CSwitchbotManager::ScenarioTick_PvP(SBotEntry& entry, LPCHARACTER ch)
{
    // PVP: stub — simulate PvP engagement tick.
    entry.metric.encounters++;
    (void)ch;
}

void CSwitchbotManager::ScenarioTick_Market(SBotEntry& entry, LPCHARACTER ch)
{
    // MARKET: stub — simulate shop browsing tick.
    (void)entry; (void)ch;
}

void CSwitchbotManager::ScenarioTick_Idle(SBotEntry& entry, LPCHARACTER ch)
{
    // IDLE: do nothing; just collect metrics.
    (void)entry; (void)ch;
}

// ============================================================================
// ExploitCheck
// ============================================================================
void CSwitchbotManager::ExploitCheck(SBotEntry& entry, LPCHARACTER ch)
{
    bool flagged = false;
    char detail[256] = {};

    // EXP anomaly: exp decreased (rollback / overflow exploit).
    const uint32_t cur_exp   = ch->GetExp();
    const int      cur_level = ch->GetLevel();
    const int      cur_gold  = ch->GetGold();

    if (cur_exp < entry.metric.last_exp)
    {
        flagged = true;
        snprintf(detail, sizeof(detail),
            "EXPLOIT:exp_anomaly prev=%u cur=%u",
            entry.metric.last_exp, cur_exp);
    }
    else if (cur_level > 250 || cur_level < 0)
    {
        flagged = true;
        snprintf(detail, sizeof(detail),
            "EXPLOIT:level_oob level=%d", cur_level);
    }
    else if (cur_gold > 2000000000 || cur_gold < 0)
    {
        flagged = true;
        snprintf(detail, sizeof(detail),
            "EXPLOIT:gold_oob gold=%d", cur_gold);
    }
    else
    {
        // Inventory: duplicate unique vnum check.
        // Scan inventory slots for duplicate vnums marked as unique.
        std::unordered_map<DWORD,int> vnumCount;
        for (int i = 0; i < INVENTORY_MAX_NUM; ++i)
        {
            LPITEM item = ch->GetInventoryItem(static_cast<WORD>(i));
            if (!item) continue;
            // Only flag items whose vnum appears in more than one slot.
            uint32_t vnum = item->GetVnum();
            if (++vnumCount[vnum] > 1)
            {
                flagged = true;
                snprintf(detail, sizeof(detail),
                    "EXPLOIT:dup_unique vnum=%u slot=%d", vnum, i);
                break;
            }
        }
    }

    // Update baselines regardless.
    entry.metric.last_exp   = cur_exp;
    entry.metric.last_level = cur_level;
    entry.metric.last_gold  = cur_gold;

    if (flagged)
    {
        entry.metric.error_events++;
        sys_err("switchbot ExploitCheck PID=%u: %s", entry.pid, detail);
        LogEvent(entry, ch, "EXPLOIT", detail);
    }
}

// ============================================================================
// StuckCheck
// ============================================================================
void CSwitchbotManager::StuckCheck(SBotEntry& entry, LPCHARACTER ch)
{
    if (entry.metric.pos_count < SWITCHBOT_POS_HISTORY) return;

    // All positions in the history within ±STUCK_RADIUS?
    int32_t base_x = entry.metric.pos_x[0];
    int32_t base_y = entry.metric.pos_y[0];
    bool stuck = true;

    for (int i = 1; i < SWITCHBOT_POS_HISTORY; ++i)
    {
        if (std::abs(entry.metric.pos_x[i] - base_x) > SWITCHBOT_STUCK_RADIUS ||
            std::abs(entry.metric.pos_y[i] - base_y) > SWITCHBOT_STUCK_RADIUS)
        {
            stuck = false;
            break;
        }
    }

    if (stuck)
    {
        entry.metric.stuck_events++;
        char detail[64];
        snprintf(detail, sizeof(detail), "x=%d y=%d", base_x, base_y);
        sys_log(0, "switchbot StuckCheck PID=%u stuck at %s", entry.pid, detail);
        LogEvent(entry, ch, "STUCK", detail);
    }

    (void)ch;
}

// ============================================================================
// MaybeRotateScenario
// ============================================================================
void CSwitchbotManager::MaybeRotateScenario(SBotEntry& entry, LPCHARACTER ch)
{
    if (time(nullptr) < entry.next_rotate) return;

    const EBotScenario prev = entry.metric.scenario;
    const int next_idx = (static_cast<int>(prev) + 1) % BOT_SCENARIO_COUNT;
    entry.metric.scenario = static_cast<EBotScenario>(next_idx);
    entry.metric.scenario_started = time(nullptr);
    entry.next_rotate = time(nullptr) + SWITCHBOT_SCENARIO_ROTATE_SEC;

    LogEvent(entry, ch, "SCENARIO_CHANGE", BotScenarioName(entry.metric.scenario));
    ch->ChatPacket(CHAT_TYPE_INFO, "[BOT] Scenario rotated -> %s.",
        BotScenarioName(entry.metric.scenario));
}

// ============================================================================
// FlushMetric
// ============================================================================
void CSwitchbotManager::FlushMetric(SBotEntry& entry, LPCHARACTER ch,
    const char* event_type, const char* detail)
{
    const time_t now = time(nullptr);
    const uint32_t dur = (uint32_t)(now - entry.metric.flush_at);

    LogManager::instance().SwitchbotLog(
        entry.pid,
        entry.metric.name,
        g_bChannel,
        ch ? ch->GetMapIndex() : 0,
        BotScenarioName(entry.metric.scenario),
        event_type,
        detail ? detail : "",
        entry.metric.last_x,
        entry.metric.last_y,
        dur,
        entry.metric.kills,
        entry.metric.encounters,
        entry.metric.stuck_events,
        entry.metric.error_events);

    entry.metric.Reset();
}

// ============================================================================
// LogEvent  (single-event immediate insert, no counter reset)
// ============================================================================
void CSwitchbotManager::LogEvent(const SBotEntry& entry, LPCHARACTER ch,
    const char* event_type, const char* detail)
{
    LogManager::instance().SwitchbotLog(
        entry.pid,
        entry.metric.name,
        g_bChannel,
        ch ? ch->GetMapIndex() : 0,
        BotScenarioName(entry.metric.scenario),
        event_type,
        detail ? detail : "",
        entry.metric.last_x,
        entry.metric.last_y,
        0,   // duration_sec 0 for single events
        0, 0, 0, 0);  // summary zeroes for event rows
}
