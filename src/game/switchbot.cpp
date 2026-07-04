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
#include "item_manager.h"
#include "log.h"
#include "event.h"
#include "utils.h"
#include "config.h"
#include "sectree.h"
#include "sectree_manager.h"
#include "entity.h"

#include <sstream>
#include <algorithm>
#include <cmath>
#include <vector>

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
    // NOTE: event_create MUST NOT be called here — thecore_heart is NULL
    // until thecore_init() runs inside start().  Call Initialize() after
    // start() returns successfully (see main.cpp).
}

void CSwitchbotManager::Initialize()
{
    if (m_pPulseEvent)
        return; // already initialized

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
// SWITCHBOT FARM AUTOMATION — Wave-2 Faz C+ (2026-07-03)
//
// Tasarim kurallari:
//   1. Tum yeni kod IsSwitchbot yolunun icinde kalir (BotTick->ScenarioTick_Farm).
//      Switchbot olmayan karakterlere SIFIR etki.
//   2. Phase VII primitifleri yeniden kullanilir: sectree ForEachAround (mob-bul),
//      ch->Attack() / ch->BotMoveStep() (hareket/kavga), ch->BotInternalHeal()
//      (potion), ch->PickupItem() (loot). Yeni kod YAZILMAZ.
//   3. Mevcut stuck/exploit dedektorlerine dokunulmaz.
//   4. Karakterin olmu olmasi (ch->IsDead()) durumunda idle — crash yok.
// ============================================================================

namespace
{
    // -------------------------------------------------------------------------
    // FFarmFindMob — Sectree iter: en yakin hedef MOB / STONE bul
    // Skoring: canli + pc degil + boss degil (mob_rank<3) = temel skor 100.
    // Yakinlik: mesafe kucukse skor artar. (Phase VII FFindClosestMob benzeri)
    // -------------------------------------------------------------------------
    struct FFarmFindMob
    {
        LPCHARACTER  me;
        LPCHARACTER  best       = nullptr;
        long long    best_score = -1;
        static const int FARM_SCAN_RADIUS = 5000;  // u (world units)
        static const long long kRadSq = (long long)FARM_SCAN_RADIUS * FARM_SCAN_RADIUS;

        void operator()(LPENTITY ent)
        {
            if (!ent || !ent->IsType(ENTITY_CHARACTER)) return;
            LPCHARACTER c = (LPCHARACTER)ent;
            if (c == me)           return;
            if (c->IsDead())       return;
            if (c->IsPC())         return;           // gercek oyunculara dokunma
            if (c->IsServerSideBot()) return;        // diger bot'lara dokunma
            if (!c->IsMonster() && !c->IsStone()) return;

            long dx = c->GetX() - me->GetX();
            long dy = c->GetY() - me->GetY();
            long long dist_sq = (long long)dx * dx + (long long)dy * dy;
            if (dist_sq > kRadSq) return;

            // Skor: yakinlik + seviye (fark +-10 tercih)
            int lvl_diff = std::abs((int)c->GetLevel() - (int)me->GetLevel());
            int lvl_score = (lvl_diff <= 10) ? 50 : 0;
            long long score = lvl_score * 1000000LL - dist_sq;  // yakin + uygun seviye

            if (score > best_score)
            {
                best_score = score;
                best       = c;
            }
        }
    };

    // -------------------------------------------------------------------------
    // FFarmFindItem — Sectree iter: 3000u icinde loot item/gold ara
    // -------------------------------------------------------------------------
    struct FFarmFindItem
    {
        LPCHARACTER             me;
        std::vector<DWORD>      vids;
        static const int LOOT_RANGE_SQ = 3000 * 3000;

        void operator()(LPENTITY ent)
        {
            if (!ent || !ent->IsType(ENTITY_ITEM)) return;
            LPITEM item = (LPITEM)ent;
            if (!item->GetSectree()) return;
            int32_t dx = item->GetX() - me->GetX();
            int32_t dy = item->GetY() - me->GetY();
            if ((int64_t)dx * dx + (int64_t)dy * dy < LOOT_RANGE_SQ)
                vids.push_back(item->GetVID());
        }
    };

    // -------------------------------------------------------------------------
    // ApplySwitchbotJitter — Hedef vektoru ±max_deg kadar rastgele saptir.
    // "Insan eli" efekti — bot tam dogru cizgide degil. (Phase VII B1 kaynakli)
    // -------------------------------------------------------------------------
    inline void ApplySwitchbotJitter(long& dx, long& dy, DWORD seed, float max_deg = 8.0f)
    {
        int bucket = (int)(seed % 11) - 5;  // -5..+5
        float angle_deg = bucket * (max_deg / 5.0f);
        float rad = angle_deg * 3.14159265358979f / 180.0f;
        float c = std::cos(rad);
        float s = std::sin(rad);
        float ndx = c * (float)dx - s * (float)dy;
        float ndy = s * (float)dx + c * (float)dy;
        dx = (long)ndx;
        dy = (long)ndy;
    }
}  // namespace

// ============================================================================
// ScenarioTick_Farm — Gercek farm dongusu
//
// Her 2sn Pulse'ta:
//   (A) Karakter oluyse: idle (crash yok, respawn server'a birakilir).
//   (B) Yakin item/gold varsa: yerde bekleyen lootu topla (LootPickupAction ornek).
//   (C) Mevcut hedef canli + yakindaysa: saldir (Attack).
//   (D) Hedef yok/oldu:
//       (D1) Sektorde mob ara (FFarmFindMob).
//       (D2) Mob bulunduysa: chase/melee.
//       (D3) Mob yoksa: jitterli wander + NO_MOB_90s -> respawn bolgesine warp.
//   (E) HP/SP dusukse: BotInternalHeal (server-side, paket yok).
//
// Metrik: summary_kills (hedef olu oldugunda artir), summary_enc (encounter).
// ============================================================================
void CSwitchbotManager::ScenarioTick_Farm(SBotEntry& entry, LPCHARACTER ch)
{
    // --- GUARD A: karakter olu ---
    if (ch->IsDead())
    {
        // Idle — hicbir aksiyon yok; olum-respawn server mantigi calissin.
        sys_log(1, "switchbot FARM PID=%u: char dead, idle", entry.pid);
        return;
    }

    const DWORD now_ms = get_dword_time();
    const DWORD seed   = now_ms ^ entry.pid;

    // --- GUARD E: HP/SP kritikse once iyiles (hedef aramadan once) ---
    {
        int hp     = ch->GetHP();
        int max_hp = ch->GetMaxHP();
        int sp     = ch->GetSP();
        int max_sp = ch->GetMaxSP();
        int hp_pct = (max_hp > 0) ? (hp * 100 / max_hp) : 100;
        int sp_pct = (max_sp > 0) ? (sp * 100 / max_sp) : 100;

        if (hp_pct < 30)
        {
            ch->BotInternalHeal(POINT_HP);
            sys_log(1, "switchbot FARM PID=%u: HP=%d%% -> BotInternalHeal(HP)", entry.pid, hp_pct);
        }
        if (sp_pct < 30)
        {
            ch->BotInternalHeal(POINT_SP);
            sys_log(1, "switchbot FARM PID=%u: SP=%d%% -> BotInternalHeal(SP)", entry.pid, sp_pct);
        }
    }

    // --- BÖLÜM B: Yakin item/gold lootu topla ---
    // F4 FIX: yaklaşma eşiği 250u (BotMoveStep step_size=300 > eşik=250, boşluk yok).
    // Eski: eşik=300 / step=250 → 300u'daki item için ne-yaklaş-ne-topla aralığı vardı.
    LPSECTREE sec = ch->GetSectree();
    if (sec)
    {
        FFarmFindItem loot_scan{ ch, {} };
        sec->ForEachAround(loot_scan);
        if (!loot_scan.vids.empty())
        {
            // En yakin item'a git (ilk VID)
            LPITEM nearest_item = ITEM_MANAGER::instance().FindByVID(loot_scan.vids[0]);
            if (nearest_item)
            {
                long idx = nearest_item->GetX() - ch->GetX();
                long idy = nearest_item->GetY() - ch->GetY();
                long idist_sq = idx * idx + idy * idy;
                if (idist_sq > 250L * 250L)  // F4: 300 -> 250 (step_size=300 ile hizali)
                {
                    // Once yaklas
                    ApplySwitchbotJitter(idx, idy, seed ^ 0xC0DE, 3.0f);
                    ch->BotMoveStep(ch->GetX() + idx, ch->GetY() + idy, 300.0, "SB_LOOT_APPROACH");
                }
                else
                {
                    // Pickup loop (Phase VII B2 LootPickupAction pattern)
                    for (DWORD vid : loot_scan.vids)
                    {
                        ch->PickupItem(vid);
                    }
                    sys_log(1, "switchbot FARM PID=%u: loot pickup %zu items",
                        entry.pid, loot_scan.vids.size());
                }
            }
            // F2 FIX: encounters sadece yeni-hedef-edinme aninda artar (D bolumu).
            // Loot tick'inde artirma — sayac semantigi: "yeni hedef kac kez edindi".
            return;  // erken don (bir tick bir eylem)
        }
    }

    // --- BÖLÜM C: Mevcut hedef varsa saldir / kovala ---
    LPCHARACTER target = ch->GetBotTarget();
    if (target)
    {
        if (target->IsDead())
        {
            // Hedef yeni oldu -> kill say, hedef sifirla.
            // F2 FIX: encounters burada artmaz — kill saymak kills'in isi.
            entry.metric.kills++;
            ch->SetBotTarget(nullptr);
            sys_log(1, "switchbot FARM PID=%u: target '%s' died, kill++",
                entry.pid, target->GetName());
            target = nullptr;
        }
        else
        {
            // Hedef canli: mesafe kontrol
            long tdx = target->GetX() - ch->GetX();
            long tdy = target->GetY() - ch->GetY();
            long long dist_sq = (long long)tdx * tdx + (long long)tdy * tdy;

            if (dist_sq <= 280LL * 280LL)
            {
                // Melee mesafesi — saldir (Phase VII AttackAction gibi)
                // F2 FIX: encounters burada artmaz — saldiri sadece combat eylemi.
                ch->EnterCombat();
                ch->SetTarget(target);
                ch->Attack(target, 0);  // PHYSICAL
                sys_log(1, "switchbot FARM PID=%u: ATTACK target='%s'", entry.pid, target->GetName());
            }
            else if (dist_sq < (long long)FFarmFindMob::FARM_SCAN_RADIUS * FFarmFindMob::FARM_SCAN_RADIUS)
            {
                // Chase range — yaklas (jitter ile insan hissi)
                // F2 FIX: encounters burada artmaz — chase sadece hareket eylemi.
                ApplySwitchbotJitter(tdx, tdy, seed ^ 0xBEEF, 5.0f);
                ch->BotMoveStep(ch->GetX() + tdx, ch->GetY() + tdy, 300.0, "SB_CHASE");
            }
            else
            {
                // Hedef cok uzakta — birak, yenisini ara
                ch->SetBotTarget(nullptr);
                target = nullptr;
            }

            if (target) return;  // eylem yapildi
        }
    }

    // --- BÖLÜM D: Hedef yok — yeni mob ara ---
    if (sec)
    {
        FFarmFindMob mob_scan{ ch };
        sec->ForEachAround(mob_scan);

        if (mob_scan.best)
        {
            ch->SetBotTarget(mob_scan.best);
            // F2 FIX: encounters SADECE yeni-hedef-edinme aninda artar.
            // Semantik: "kac kez yeni bir mob hedeflendi" = leveling ilerleme proxy.
            entry.metric.encounters++;
            // F3: Mob bulundu — no_mob_since damgasini sifirla.
            entry.no_mob_since = 0;
            sys_log(1, "switchbot FARM PID=%u: new target='%s' lvl=%d",
                entry.pid, mob_scan.best->GetName(), (int)mob_scan.best->GetLevel());
            // Bir sonraki tick'te saldiracak (C bolumu)
        }
        else
        {
            // D3: Mob yok — wander
            // F3 FIX: no_mob_since ile gercek 90sn NO_MOB warp.
            // (Eski: stuck_events > 3 kullaniyordu — metric alani, cakisma.)
            const time_t now_t = time(nullptr);
            if (entry.no_mob_since == 0)
                entry.no_mob_since = now_t;  // ilk bos scan damgasi

            long dx = (long)(seed % 2000) - 1000;
            long dy = (long)((seed >> 13) % 2000) - 1000;
            double rdist = std::sqrt((double)dx * dx + (double)dy * dy);
            if (rdist < 1.0) rdist = 1.0;
            ApplySwitchbotJitter(dx, dy, seed ^ 0xDEAD, 8.0f);
            long wander_x = ch->GetX() + (long)(dx * 1000.0 / rdist);
            long wander_y = ch->GetY() + (long)(dy * 1000.0 / rdist);
            bool moved = ch->BotMoveStep(wander_x, wander_y, 150.0, "SB_WANDER");

            // F1 FIX: farm_stuck_score — AYRI runtime alan, metric.stuck_events DEGIL.
            // Artirma kriteri: BotMoveStep no-op dondu (hareket olmadi = pozisyon degismedi).
            if (!moved)
                entry.farm_stuck_score++;

            // F3: 90sn mob bulunamazsa spawn bolgesine warp.
            bool no_mob_warp = (entry.no_mob_since > 0 &&
                                (now_t - entry.no_mob_since) >= 90);

            // F1: farm_stuck_score > 3 ise warp (metric.stuck_events'e dokunma).
            bool stuck_warp = (entry.farm_stuck_score > 3);

            if (no_mob_warp || stuck_warp)
            {
                long wx = ch->GetX() + (long)(seed % 6000) - 3000;
                long wy = ch->GetY() + (long)((seed * 2654435769u) % 6000) - 3000;
                ch->BotMoveStep(wx, wy, 500.0, "SB_STUCK_WARP");
                entry.farm_stuck_score = 0;   // F1: kendi sayacini sifirla
                entry.no_mob_since     = 0;   // F3: no_mob damgasini sifirla
                sys_log(0, "switchbot FARM PID=%u: warp trigger no_mob=%s stuck=%s -> (%ld,%ld)",
                    entry.pid,
                    no_mob_warp ? "YES" : "NO",
                    stuck_warp  ? "YES" : "NO",
                    wx, wy);
            }
        }
    }
}

// ============================================================================
// ScenarioTick_Quest — Farm-fallback (quest otomasyonu buyuk ayri is)
//
// Mission: "leveling, quest, zone farming" — Quest otomasyonu (NPC dialog,
// objective tracking, quest-item pickup) ayri bir Wave'de ele alinacak.
// Bu turda: Quest senaryosunda Farm davranisi devreye girer (kill+loot devam
// eder, quest metrikleri ise "not implemented" yorumuyla loglanir).
// ============================================================================
void CSwitchbotManager::ScenarioTick_Quest(SBotEntry& entry, LPCHARACTER ch)
{
    // Quest otomasyonu (NPC dialog + objective tracking) bu turda implement edilmedi.
    // Farm davranisini delege et: kill/loot calissin, quest-specifik adimlar yok.
    // metric: encounters artir (Farm ile ayni sayac, Quest ozel sayac yok bu turda)
    ScenarioTick_Farm(entry, ch);

    // Quest-ozel log: 60sn'de bir bildir (FlushMetric zaten TICK_OK loglar)
    static thread_local time_t s_quest_warn = 0;
    if (time(nullptr) - s_quest_warn > 60)
    {
        sys_log(0, "switchbot QUEST PID=%u: quest-specific steps NOT IMPLEMENTED "
            "(NPC dialog / objective tracking); delegating to Farm. "
            "Implement in Wave-3 quest-automation iz.", entry.pid);
        s_quest_warn = time(nullptr);
    }
}

// ============================================================================
// ScenarioTick_Dungeon — Farm-fallback (dungeon navigasyon buyuk ayri is)
//
// Dungeon otomasyonu (portal + boss + dungeon-specific spawn) ayri Wave'de.
// Bu turda: Dungeon senaryosunda Farm davranisi devreye girer.
// ============================================================================
void CSwitchbotManager::ScenarioTick_Dungeon(SBotEntry& entry, LPCHARACTER ch)
{
    // Dungeon otomasyonu (portal bulma, boss sirali kill, ozel spawn)
    // bu turda implement edilmedi. Farm davranisini delege et.
    ScenarioTick_Farm(entry, ch);

    static thread_local time_t s_dung_warn = 0;
    if (time(nullptr) - s_dung_warn > 60)
    {
        sys_log(0, "switchbot DUNGEON PID=%u: dungeon-specific steps NOT IMPLEMENTED "
            "(portal/boss/spawn); delegating to Farm. "
            "Implement in Wave-3 dungeon-automation iz.", entry.pid);
        s_dung_warn = time(nullptr);
    }
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
