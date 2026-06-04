// ============================================================================
// bot_ai_utility.h — v13 Utility AI mimari
//
// Linear if-else BotTick (char_bot.cpp 1015 satir) yerine Action-based dispatch.
// Her tick:
//   1. BuildContext (HP%, SP%, target, sector, persona, fatigue, ...)
//   2. Her IBotAction icin Score() cagir (0.0..1.0)
//   3. En yuksek skor + > 0 olan Execute() cagir
//   4. Cooldown ve fatigue update
//
// Avantajlar:
//   - Yeni davranis = yeni IBotAction subclass (50-120 satir tek dosya)
//   - Test edilebilirlik: action izole, mock context ile birim test mumkun
//   - Davranis ceshitliligi: weight'leri persona-bazli ayarla
//
// Literatur: Dave Mark "Behavioral Mathematics for Game AI" (Utility Theory)
// gameaipro Ch04 (Behavior Selection) + Ch09 (Utility Theory)
// ============================================================================
#pragma once

#include "char.h"
#include "bot_combat_plan.h"   // v14 class-aware combat
#include <vector>
#include <memory>

class CHARACTER;
class SECTREE;

// ============================================================================
// BotActionContext — her tick basinda bir kez doldurulur, IBotAction'lara aktarilir
// ============================================================================
struct BotActionContext
{
    CHARACTER*  bot;
    DWORD       now;              // get_dword_time()
    DWORD       seed;             // per-tick random seed (deterministik)

    // Stat snapshot
    int         hp;
    int         max_hp;
    int         sp;
    int         max_sp;
    int         hp_pct;           // 0..100
    int         sp_pct;           // 0..100
    int         inv_used;         // v16 Adim 6.2 inventory dolu slot sayisi
    int         inv_pct;          // 0..100 (90 slot baz)

    // Target
    CHARACTER*  target;           // mevcut hedef (m_pBotTarget)
    int         target_dist_sq;   // hedef varsa
    bool        target_in_melee;  // 280u icinde mi

    // Sectree
    LPSECTREE   sec;
    bool        nearby_pc;        // 1500u icinde gercek oyuncu var mi (chat icin)

    // Bot durumu
    BYTE        persona;
    BYTE        fatigue;
    bool        in_afk;           // m_dwBotIdleUntil > now mi
    DWORD       last_action;      // m_dwBotLastActionTime

    // Yardimcilar
    BYTE        empire;
    BYTE        level;

    // v14 class-aware combat plan (BuildContext'te doldurur)
    CombatPlan  combat_plan;
};

// ============================================================================
// IBotAction — action interface. Tum davranislar bunu implement eder.
//
// Score() 0.0..1.0:
//   - 0.0: bu davranis uygun degil
//   - 0.5: norm/standart
//   - 1.0: kritik/zorunlu (HP=0% -> HealAction 1.0)
//
// Execute() context'e gore action calistirir. true dondurursen action gercekten
// calisti (cooldown reset vs), false ise no-op (debug log atilabilir).
// ============================================================================
class IBotAction
{
public:
    virtual ~IBotAction() = default;
    virtual float       Score(const BotActionContext& ctx) const = 0;
    virtual bool        Execute(BotActionContext& ctx) = 0;
    virtual const char* Name() const = 0;
};

// Action registry — char_bot.cpp BotTick disinda init edilir, runtime'da read-only
extern std::vector<std::unique_ptr<IBotAction>> g_bot_actions;

// Initial action listesi olustur (main.cpp BotInit veya benzer noktada cagrilir)
void BotAI_InitActions();

// BotTick'in icinden cagrilir — score+execute dispatch
// Donen deger: secilen action'in adi (log icin), nullptr=hicbiri secilmedi
const char* BotAI_TickDispatch(CHARACTER* bot);

// Yardimci: context insa et (BotTick basindan once)
BotActionContext BotAI_BuildContext(CHARACTER* bot, DWORD seed);
