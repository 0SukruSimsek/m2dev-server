// ============================================================================
// bot_ai_utility.cpp — v13 Utility AI action implementations
//
// 9 action sinifi (8 mevcut davranis + 1 YENI FleeAction):
//   1. IdleAction         — AFK pencere icindeyse 1.0 override
//   2. HealAction         — HP<%30 veya SP<%30 (server-side PointChange)
//   3. FleeAction         — HP<%20 + agresif target (BotTeleport ters yon, YENI)
//   4. LevelUpWarpAction  — level threshold geciyse yeni farm pos
//   5. AttackAction       — target var (melee veya chase)
//   6. RestockAction      — envanter %80+ alt-tier drop
//   7. ChatAction         — nearby_pc + cooldown bitti
//   8. WanderAction       — hicbir sey yoksa rastgele yon
//   9. SpawnIdleAction    — fallback no-op (hicbir action skor vermezse)
//  10. LootPickupAction   — Phase VII B2 mob olu -> yerdeki item/gold topla
// ============================================================================
#include "stdafx.h"
#include "bot_ai_utility.h"
#include "bot_chat.h"               // v16 Adim 6.1 — ChatAction public helpers
#include "bot_visual.h"             // v17 — Atak/Skill animasyon broadcast
#include "bot_logger.h"             // v30 — Master CSV log
#include "char.h"
#include "char_manager.h"
#include "desc.h"
#include "desc_manager.h"
#include "item.h"
#include "item_manager.h"
#include "config.h"
#include "log.h"
#include "sectree.h"
#include "sectree_manager.h"
#include "entity.h"
#include "packet_structs.h"
#include "../common/packet_headers.h"
#include "../common/length.h"
#include "../common/tables.h"
#include <cmath>
#include <vector>
#include <memory>
#include <algorithm>

std::vector<std::unique_ptr<IBotAction>> g_bot_actions;

// char_bot.cpp icindeki static helper'lar buradan erisilemez (TU-local).
// Inline duplicate — degisiklikte iki yer guncellenir, gelecekte ortak header'a tasinabilir.
namespace
{
    void GetEmpireVillage_local(BYTE /*empire*/, long& x, long& y)
    {
        // v18: admin pos fallback — sadece job bilinmiyorsa
        x = 484096; y = 970001;
    }

    // v26 — Bot-ID bazli zon (hepsi warrior senaryosu icin, job degil pid)
    void GetClassFarmZone_local(BYTE /*job*/, long& x, long& y)
    {
        x = 484096; y = 970001;  // fallback (caller PID kullansin)
    }

    void GetBotZoneByPid_local(DWORD pid, long& x, long& y)
    {
        switch (pid % 4)
        {
            case 0: x = 488000; y = 970500; break;  // East
            case 1: x = 484000; y = 966000; break;  // South
            case 2: x = 480000; y = 970500; break;  // West
            case 3: x = 484000; y = 974500; break;  // North
            default: x = 484096; y = 970001; break;
        }
    }

    // v28 — Boids-lite separation: range 400u -> 800u (cluster onlemi)
    // 800u kadar yakin tum bot'larla aktif separation. Step 200u -> 300u (daha gucu agirlik)
    struct FBoidsSepFunctor
    {
        LPCHARACTER me;
        long sum_dx = 0;
        long sum_dy = 0;
        int  near_count = 0;
        static const long kRange = 800;   // v28: 400 -> 800u
        static const long kRangeSq = 800L * 800L;

        void operator()(LPENTITY ent)
        {
            if (!ent || ent == me) return;
            if (!ent->IsType(ENTITY_CHARACTER)) return;
            LPCHARACTER c = (LPCHARACTER)ent;
            if (!c->IsServerSideBot()) return;  // sadece bot'larla itme
            long dx = me->GetX() - c->GetX();
            long dy = me->GetY() - c->GetY();
            long dist_sq = dx * dx + dy * dy;
            if (dist_sq < 50LL * 50LL) { dx = (long)((me->GetPlayerID() & 1) ? 100 : -100); dy = 100; }
            else if (dist_sq > kRangeSq) return;
            // Ters yon vektoru — yakinlik agirligi (yakinsa daha buyuk ittirir)
            long weight = (kRangeSq - dist_sq) / 4000;  // 0..160 (kRangeSq 640000/4000=160)
            sum_dx += dx * weight;
            sum_dy += dy * weight;
            near_count++;
        }

        bool GetSeparationStep(long& out_dx, long& out_dy) const
        {
            if (near_count == 0) return false;
            double mag = std::sqrt((double)(sum_dx * sum_dx + sum_dy * sum_dy));
            if (mag < 1.0) return false;
            // v28: step 200 -> 300u (daha guclu kacis)
            out_dx = (long)(300.0 * sum_dx / mag);
            out_dy = (long)(300.0 * sum_dy / mag);
            return true;
        }
    };

    // ============================================================================
    // Phase VII B1 — ApplyHeadingJitter
    // Hedef vektoru (dx, dy) uzunlugunu koruyarak ±max_deg kadar rastgele donus
    // uygular. Box-Muller yerine basit tabanli: seed-driven [-max_deg, +max_deg].
    // "Insan eli" efekti: bot tam cizgide degil hafif kvriltmali yol izler.
    // ============================================================================
    void ApplyHeadingJitter(long& dx, long& dy, DWORD seed, float max_deg = 5.0f)
    {
        // seed-driven [-max_deg, +max_deg] radyan
        int bucket = (int)(seed % 11) - 5;  // -5 .. +5
        float angle_deg = bucket * (max_deg / 5.0f);
        float rad = angle_deg * 3.14159265358979f / 180.0f;
        float c = std::cos(rad);
        float s = std::sin(rad);
        float ndx = c * (float)dx - s * (float)dy;
        float ndy = s * (float)dx + c * (float)dy;
        dx = (long)ndx;
        dy = (long)ndy;
    }
}

// ============================================================================
// IdleAction — AFK pencere icindeyse her sey override edilir
// ============================================================================
class IdleAction : public IBotAction
{
public:
    float Score(const BotActionContext& ctx) const override
    {
        return ctx.in_afk ? 1.0f : 0.0f;
    }
    bool Execute(BotActionContext& ctx) override
    {
        // AFK aktif: target birak, hareket yok (BotTick zaten erken return ediyor v12'de)
        ctx.bot->SetBotTarget(nullptr);
        ctx.bot->SetVictim(nullptr);
        return true;
    }
    const char* Name() const override { return "Idle"; }
};

// ============================================================================
// v30 UnstuckAction — arastirici onerisi B6
// 8sn boyunca <150u hareket -> random 90deg rotation + 1500u step
// Score 0.92 (Heal 0.95'tan dusuk ama Flee 0.90'dan yuksek)
// ============================================================================
class UnstuckAction : public IBotAction
{
public:
    float Score(const BotActionContext& ctx) const override
    {
        // Bot'un kendi stuck sayaci (char_bot.cpp m_byBotStuckCount) varsa kullan.
        // Burada lightweight: 2+ STUCK sayaci ile tetikle.
        if (ctx.bot && ctx.bot->GetBotStuckCount() >= 2) return 0.92f;
        return 0.0f;
    }
    bool Execute(BotActionContext& ctx) override
    {
        if (!ctx.bot) return false;
        // Random direction ±90° rotation
        DWORD seed = get_dword_time() ^ ctx.bot->GetPlayerID();
        int angle_deg = (int)((seed % 180) - 90);   // -90..+90
        float current_rot = ctx.bot->GetRotation();
        float new_rot = current_rot + angle_deg;
        if (new_rot < 0) new_rot += 360;
        if (new_rot >= 360) new_rot -= 360;
        // 1500u step
        double rad = new_rot * 3.14159265358979 / 180.0;
        long tx = ctx.bot->GetX() + (long)(std::cos(rad) * 1500.0);
        long ty = ctx.bot->GetY() + (long)(std::sin(rad) * 1500.0);
        ctx.bot->BotMoveStep(tx, ty, 300.0, "UNSTUCK");
        sys_log(0, "BotAI[Unstuck]: %s stuck_count=%d -> angle=%d new_pos=(%ld,%ld)",
            ctx.bot->GetName(), (int)ctx.bot->GetBotStuckCount(), angle_deg, tx, ty);
        return true;
    }
    const char* Name() const override { return "Unstuck"; }
};

// ============================================================================
// v30 LeashReturnAction — arastirici onerisi B5
// Bot anchor (BotHome) ile distance > 4000u -> home pos'a don
// Score 0.50 (Attack 0.70'tan dusuk, ama Wander 0.40'tan yuksek)
// ============================================================================
class LeashReturnAction : public IBotAction
{
public:
    float Score(const BotActionContext& ctx) const override
    {
        if (!ctx.bot || !ctx.bot->HasBotHome()) return 0.0f;
        long hx, hy;
        ctx.bot->GetBotHome(hx, hy);
        long dx = ctx.bot->GetX() - hx;
        long dy = ctx.bot->GetY() - hy;
        long long dist_sq = (long long)dx*dx + (long long)dy*dy;
        if (dist_sq > 4000LL * 4000LL) return 0.50f;
        return 0.0f;
    }
    bool Execute(BotActionContext& ctx) override
    {
        long hx, hy;
        ctx.bot->GetBotHome(hx, hy);
        // Smooth step (BotMoveStep) — anchor yonune git
        ctx.bot->BotMoveStep(hx, hy, 300.0, "LEASH");
        sys_log(0, "BotAI[Leash]: %s -> home (%ld,%ld)",
            ctx.bot->GetName(), hx, hy);
        return true;
    }
    const char* Name() const override { return "LeashReturn"; }
};

// ============================================================================
// v30 PotionUseAction — arastirici onerisi B4
// HP<50% -> red potion auto-use (BotInternalHeal yapilmiyorsa)
// Score 0.92 (Heal 0.95 ile combine)
// NOT: Mevcut HealAction zaten HP<30% icin PointChange ile heal yapiyor.
// PotionUse 30-50% araliginda kullanilir, "iksir tüketme" simulasyonu icin gercek
// item drop yapilmiyor (server-side bot icin gerek yok).
// ============================================================================
class PotionUseAction : public IBotAction
{
public:
    float Score(const BotActionContext& ctx) const override
    {
        // 30-50% HP araliginda potion (Heal action 30 alti aktif)
        if (ctx.hp_pct >= 30 && ctx.hp_pct < 50) return 0.92f;
        if (ctx.sp_pct >= 30 && ctx.sp_pct < 50) return 0.85f;
        return 0.0f;
    }
    bool Execute(BotActionContext& ctx) override
    {
        bool used = false;
        if (ctx.hp_pct >= 30 && ctx.hp_pct < 50)
        {
            ctx.bot->BotInternalHeal(POINT_HP);
            sys_log(0, "BotAI[Potion]: %s HP %d%% -> potion", ctx.bot->GetName(), ctx.hp_pct);
            used = true;
        }
        if (ctx.sp_pct >= 30 && ctx.sp_pct < 50)
        {
            ctx.bot->BotInternalHeal(POINT_SP);
            used = true;
        }
        return used;
    }
    const char* Name() const override { return "PotionUse"; }
};

// ============================================================================
// HealAction — HP<%30 veya SP<%30 (server-side PointChange direkt)
// ============================================================================
class HealAction : public IBotAction
{
public:
    float Score(const BotActionContext& ctx) const override
    {
        // v13.1 DEBUG: HP/SP context degerleri yanlis mi? hp%=100'de Heal seciliyor bug
        float score = 0.0f;
        if (ctx.hp_pct < 30) score = 0.95f;       // kritik HP
        else if (ctx.sp_pct < 30) score = 0.85f;  // SP sonu

        if (score > 0.0f)
        {
            // Sadece score>0 oldugunda log (spam'i azalt)
            sys_log(0, "DBG_HealScore: %s hp=%d/%d (%d%%) sp=%d/%d (%d%%) score=%.2f",
                ctx.bot ? ctx.bot->GetName() : "(null)",
                ctx.hp, ctx.max_hp, ctx.hp_pct,
                ctx.sp, ctx.max_sp, ctx.sp_pct,
                score);
        }
        return score;
    }
    bool Execute(BotActionContext& ctx) override
    {
        // v16 — shared helper BotInternalHeal (char.cpp)
        bool healed = false;
        if (ctx.hp_pct < 30)
        {
            ctx.bot->BotInternalHeal(POINT_HP);
            healed = true;
        }
        if (ctx.sp_pct < 30)
        {
            ctx.bot->BotInternalHeal(POINT_SP);
            healed = true;
        }
        return healed;
    }
    const char* Name() const override { return "Heal"; }
};

// ============================================================================
// FleeAction YENI — HP<%20 + bot saldiriya maruz + target seviyesi yuksek
// Bot can sikintida -> village'a kac. Real-player feel: paniklemek.
// ============================================================================
class FleeAction : public IBotAction
{
public:
    float Score(const BotActionContext& ctx) const override
    {
        if (ctx.hp_pct >= 20) return 0.0f;
        if (!ctx.target) return 0.0f;
        // Hedef bot'tan 5+ level yuksekse veya bot lvl < 10 ise kacacak
        int target_lvl = ctx.target->GetLevel();
        if (target_lvl > ctx.level + 5 || ctx.level < 10)
            return 0.90f;  // HealAction'dan dusuk ama tetikleyici
        return 0.0f;
    }
    bool Execute(BotActionContext& ctx) override
    {
        // Hedef birak + village'a teleport
        ctx.bot->SetBotTarget(nullptr);
        ctx.bot->SetVictim(nullptr);
        long vx, vy;
        ctx.bot->GetBotHome(vx, vy);
        // jitter
        DWORD seed = get_dword_time() ^ ctx.bot->GetPlayerID();
        vx += (long)(seed % 2000) - 1000;
        vy += (long)((seed * 2654435769u) % 2000) - 1000;
        ctx.bot->BotTeleport(vx, vy);
        sys_log(0, "BotAI[Flee]: %s panik kacis -> class_zone (%ld,%ld) hp=%d/%d target_lvl=%d",
            ctx.bot->GetName(), vx, vy, ctx.hp, ctx.max_hp,
            ctx.target ? ctx.target->GetLevel() : 0);
        return true;
    }
    const char* Name() const override { return "Flee"; }
};

// ============================================================================
// AttackAction — target var (melee veya chase)
// Reaksiyon delay korunur: persona-bazli 120-320ms+N(0,60)
// ============================================================================
class AttackAction : public IBotAction
{
public:
    float Score(const BotActionContext& ctx) const override
    {
        if (!ctx.target || ctx.target->IsDead()) return 0.0f;
        if (ctx.target_in_melee) return 0.70f;        // Melee'de vur
        if (ctx.target_dist_sq < 5000LL * 5000LL) return 0.55f;  // Chase
        return 0.0f;
    }
    bool Execute(BotActionContext& ctx) override
    {
        if (!ctx.target) return false;
        if (ctx.target_in_melee)
        {
            ctx.bot->EnterCombat();
            // v33 — SetTarget geri ekledi (char.cpp:5496 DESC NULL guard, safe).
            // Native SendDamagePacket flow tetiklenir.
            ctx.bot->SetTarget(ctx.target);
            ctx.bot->Attack(ctx.target, 0);  // PHYSICAL
            BotEmitAttackVisual(ctx.bot, ctx.target);  // v17 (v32 FUNC_COMBO swing)

            // v28 Boids-lite separation: 2+ bot 800u icinde kacis (range 400 -> 800)
            if (auto sec = ctx.bot->GetSectree())
            {
                FBoidsSepFunctor sep{ ctx.bot, 0, 0, 0 };
                sec->ForEachAround(sep);
                long sx, sy;
                if (sep.near_count >= 2 && sep.GetSeparationStep(sx, sy))
                {
                    long tx = ctx.bot->GetX() + sx;
                    long ty = ctx.bot->GetY() + sy;
                    ctx.bot->BotMoveStep(tx, ty, 150.0, "SEPARATE");
                }
            }
            return true;
        }
        else
        {
            // v34 hiz fix: 200u -> 350u step (warrior ~1166u/sn @ 300ms duration)
            // Phase VII B1 — Path jitter: hedef vektoru ±5 deg jitter (insan eli efekti)
            long tdx = ctx.target->GetX() - ctx.bot->GetX();
            long tdy = ctx.target->GetY() - ctx.bot->GetY();
            DWORD jseed = ctx.seed ^ (ctx.now >> 8);
            ApplyHeadingJitter(tdx, tdy, jseed, 5.0f);
            long jx = ctx.bot->GetX() + tdx;
            long jy = ctx.bot->GetY() + tdy;
            sys_log(1, "BotAI[Chase/Jitter]: %s target=(%ld,%ld) jittered=(%ld,%ld)",
                ctx.bot->GetName(), ctx.target->GetX(), ctx.target->GetY(), jx, jy);
            return ctx.bot->BotMoveStep(jx, jy, 350.0, "CHASE");
        }
    }
    const char* Name() const override { return "Attack"; }
};

// ============================================================================
// v14 SkillCombatAction — class-aware skill rotation (AttackAction'in uzantisi)
// CombatPlan.preferred_skills priority queue, ilk uygun (cooldown bitti + SP yeter)
// kullanilir. Fallback: basic Attack.
// ============================================================================
class SkillCombatAction : public IBotAction
{
public:
    float Score(const BotActionContext& ctx) const override
    {
        if (!ctx.target || ctx.target->IsDead()) return 0.0f;
        if (!ctx.combat_plan.preferred_skills[0]) return 0.0f;
        // v16 Adim 6.5 fix — Per-bot 6sn cooldown (CanUseSkill cooldown check yok)
        if (!ctx.bot->IsBotSkillReady()) return 0.0f;
        // AttackAction (0.70)'tan yuksek olsun ki skill rotation tercih edilsin
        if (ctx.target_in_melee) return 0.75f;
        // Ranged class melee disinda bile skill atabilir
        if (ctx.combat_plan.optimal_range >= 800 &&
            ctx.target_dist_sq <= (long long)ctx.combat_plan.optimal_range * ctx.combat_plan.optimal_range)
            return 0.75f;
        return 0.0f;
    }
    bool Execute(BotActionContext& ctx) override
    {
        if (!ctx.target || !ctx.bot) return false;

        // Priority loop: 0 -> 1 -> 2 (ilk uygun skill)
        for (int i = 0; i < 3; ++i)
        {
            DWORD vnum = ctx.combat_plan.preferred_skills[i];
            if (vnum == 0) continue;

            const SkillMeta* meta = GetSkillMetaCached(vnum);
            if (!meta || !meta->valid) continue;

            // SP filter kaldirildi — CanUseSkill zaten dogru SP check yapar (bot skill lvl bazli)

            // SELFONLY skill -> SetVictim self (buff)
            if (meta->is_selfonly)
            {
                if (ctx.bot->CanUseSkill(vnum))
                {
                    ctx.bot->ComputeSkill(vnum, ctx.bot);
                    BotEmitSkillVisual(ctx.bot, vnum);  // v17 — viewer-side skill anim
                    ctx.bot->MarkBotSkillUsed();  // v16 Adim 6.5 fix per-bot 6sn cooldown
                    sys_log(0, "BOT_AI_COMBAT_SKILL: %s class=%s skill=%u (SELFONLY) target=self sp=%d",
                        ctx.bot->GetName(), ctx.combat_plan.class_name, vnum, ctx.bot->GetSP());
                    sys_log(0, "[BOT_COMBAT] %s class=%s skill=%u type=SELFONLY tgt=self sp=%d",
                        ctx.bot->GetName(), ctx.combat_plan.class_name, vnum, ctx.bot->GetSP());
                    return true;
                }
            }
            else
            {
                // Attack skill
                if (ctx.bot->CanUseSkill(vnum))
                {
                    // Hedefe donus + skill anim broadcast
                    ctx.bot->SetRotationToXY(ctx.target->GetX(), ctx.target->GetY());
                    ctx.bot->ComputeSkill(vnum, ctx.target);
                    BotEmitSkillVisual(ctx.bot, vnum);  // v17 — viewer-side skill anim
                    ctx.bot->MarkBotSkillUsed();  // v16 Adim 6.5 fix per-bot 6sn cooldown
                    sys_log(0, "BOT_AI_COMBAT_SKILL: %s class=%s skill=%u target=%s sp=%d",
                        ctx.bot->GetName(), ctx.combat_plan.class_name, vnum,
                        ctx.target->GetName(), ctx.bot->GetSP());
                    sys_log(0, "[BOT_COMBAT] %s class=%s skill=%u tgt=%s sp=%d",
                        ctx.bot->GetName(), ctx.combat_plan.class_name, vnum,
                        ctx.target->GetName(), ctx.bot->GetSP());
                    return true;
                }
            }
        }

        // Fallback: basic Attack (AttackAction'in mantigi)
        if (ctx.target_in_melee)
        {
            ctx.bot->EnterCombat();
            // v29.2 — SetTarget kaldirildi (DESC=null bot crash). char_battle.cpp:1627
            // SendDamagePacket kosulu gevsetildi: bot attacker icin PacketAround broadcast.
            ctx.bot->Attack(ctx.target, 0);
            BotEmitAttackVisual(ctx.bot, ctx.target);  // v17 — viewer-side anim
            return true;
        }
        return false;
    }
    const char* Name() const override { return "SkillCombat"; }
};

// ============================================================================
// v14 KiteAction — ranged class HP dustugunde target'tan ters yon kac
// (Goto kullanir, SetDest YOK — R4 NULL deref azaltma)
// ============================================================================
class KiteAction : public IBotAction
{
public:
    float Score(const BotActionContext& ctx) const override
    {
        // Sadece ranged class (optimal_range >= 800)
        if (ctx.combat_plan.optimal_range < 800) return 0.0f;
        if (ctx.hp_pct >= ctx.combat_plan.kite_threshold_hp) return 0.0f;
        // Sadece target melee'de yakindaysa kac (mesafedeyse Attack yeter)
        if (!ctx.target_in_melee) return 0.0f;
        return 0.65f;  // Heal (0.95) < FleeAction (0.90) < bu (0.65) < Attack (0.70)
    }
    bool Execute(BotActionContext& ctx) override
    {
        if (!ctx.target || !ctx.bot) return false;

        // v16 — Shared BotMoveStep delegate (char.cpp).
        // Ters yon target hesabi: target'tan uzaklas, target_x = bot + (bot-target) yonu uzanmasi
        long bot_x = ctx.bot->GetX();
        long bot_y = ctx.bot->GetY();
        long dx = bot_x - ctx.target->GetX();
        long dy = bot_y - ctx.target->GetY();
        double dist = std::sqrt((double)dx*dx + (double)dy*dy);
        if (dist < 1.0) dist = 1.0;
        // 1200u uzaktaki ters yon hedef
        long flee_x = bot_x + (long)((double)dx * 1200.0 / dist);
        long flee_y = bot_y + (long)((double)dy * 1200.0 / dist);

        // v34 hiz fix: 250u -> 350u step (KITE ranged class kacis hizi)
        bool moved = ctx.bot->BotMoveStep(flee_x, flee_y, 350.0, "KITE");
        if (moved)
        {
            sys_log(0, "BOT_AI_KITE: %s class=%s hp=%d%% threshold=%d%%",
                ctx.bot->GetName(), ctx.combat_plan.class_name,
                ctx.hp_pct, ctx.combat_plan.kite_threshold_hp);
        }
        return moved;
    }
    const char* Name() const override { return "Kite"; }
};

// ============================================================================
// WanderAction — hicbir sey yoksa rastgele yon
// ============================================================================
class WanderAction : public IBotAction
{
public:
    float Score(const BotActionContext& ctx) const override
    {
        if (ctx.target) return 0.0f;     // target varsa Attack one cikar
        if (ctx.in_afk) return 0.0f;     // AFK override edilmis
        return 0.30f;  // default low priority
    }
    bool Execute(BotActionContext& ctx) override
    {
        // v16 Adim 6.3 — NO_MOB_30s village fallback (char_bot.cpp eski 4d bloku tasindi)
        // Bot 30sn+ target bulamadiysa village'a teleport (sectree disi, bos alan kurtarma)
        if (ctx.bot->GetBotNoTargetMs() > 30000)
        {
            long vx, vy;
            ctx.bot->GetBotHome(vx, vy);
            // jitter (her bot ayni nokta yerine ±1500u)
            DWORD seed = get_dword_time() ^ ctx.bot->GetPlayerID();
            vx += (long)(seed % 3000) - 1500;
            vy += (long)((seed * 2654435769u) % 3000) - 1500;
            ctx.bot->BotTeleport(vx, vy);
            ctx.bot->MarkBotWanderReset();
            sys_log(0, "BotAI[Wander]: %s NO_MOB_30s -> class_zone (%ld,%ld) lvl=%d",
                ctx.bot->GetName(), vx, vy, ctx.bot->GetLevel());
            return true;
        }

        // v16 — Random wander BotMoveStep delegate
        long raw_dx = (long)(ctx.seed % 2000) - 1000;
        long raw_dy = (long)((ctx.seed / 2000) % 2000) - 1000;
        double rdist = std::sqrt((double)raw_dx*raw_dx + (double)raw_dy*raw_dy);
        if (rdist < 1.0) rdist = 1.0;
        // Phase VII B1 — Path jitter: wander yonune ±5 deg ek saptirma
        ApplyHeadingJitter(raw_dx, raw_dy, ctx.seed ^ 0xDEAD, 5.0f);
        long wander_x = ctx.bot->GetX() + (long)((double)raw_dx * 1000.0 / rdist);
        long wander_y = ctx.bot->GetY() + (long)((double)raw_dy * 1000.0 / rdist);

        return ctx.bot->BotMoveStep(wander_x, wander_y, 150.0, "WANDER");
    }
    const char* Name() const override { return "Wander"; }
};

// v16 — LevelUpWarpAction SILINDI (state-based, action mimarisine uymaz).
// Level change detection BotTick'te kalir (m_byBotLastKnownLevel polling).

// ============================================================================
// RestockAction — envanter %80+ alt-tier silah/zirh drop
// Mevcut AUTO-CLEAN mantigi (char_bot.cpp:567 satir civari) buraya tasinabilir
// V13'te basitlestirilmis: skor 0.6, simdilik no-op (Aşama D'de implement)
// ============================================================================
// v16 Adim 6.2 — RestockAction full impl (char_bot.cpp AUTO-CLEAN bloku tasindi)
// Per-bot cooldown 30sn (eski static s_lastAutoClean global'di — E5 fix).
// Score: inv_pct > 80 ise 0.55 (Heal/Skill/Attack altinda, Wander uzerinde).
// Execute: en alt-tier WEAPON/ARMOR item drop (potion korunur, vnum 27000-27999).
// Phase VII B3 genisleme: HP<50% + gold>1000 -> heal potion vnum=27001 in-place create.
static constexpr DWORD kHealPotionVnum  = 27001; // Small Healing Potion
static constexpr INT   kPotionCost      = 1000;  // yang maliyeti
class RestockAction : public IBotAction
{
public:
    float Score(const BotActionContext& ctx) const override
    {
        // Phase VII B3: HP<%50 + yeterli gold -> potion al (acil)
        if (ctx.hp_pct < 50 && ctx.bot->GetGold() > kPotionCost)
        {
            if (ctx.bot->IsBotRestockReady()) return 0.60f;
        }
        if (ctx.inv_pct < 80) return 0.0f;
        if (!ctx.bot->IsBotRestockReady()) return 0.0f;  // cooldown bitmedi
        return 0.55f;
    }
    bool Execute(BotActionContext& ctx) override
    {
        if (!ctx.bot) return false;
        ctx.bot->MarkBotRestockChecked();  // cooldown ata (30sn)

        // Phase VII B3: HP<%50 + gold>1000 -> heal potion in-place create
        if (ctx.hp_pct < 50 && ctx.bot->GetGold() > kPotionCost)
        {
            // inventory'de bos slot var mi?
            if (ctx.inv_pct < 90)
            {
                LPITEM potion = ctx.bot->AutoGiveItem(kHealPotionVnum, 1, -1, false);
                if (potion)
                {
                    ctx.bot->PointChange(POINT_GOLD, -kPotionCost);
                    BotLog_Inventory(ctx.bot, "RESTOCK_POTION",
                                     kHealPotionVnum, -1, 1);
                    sys_log(0, "[BOT_RESTOCK] %s gold=%d potion+1 (HP=%d%%)",
                        ctx.bot->GetName(),
                        (int)ctx.bot->GetGold(),
                        ctx.hp_pct);
                    return true;
                }
            }
        }

        // Envanter temizligi: en dusuk tier WEAPON/ARMOR bul (potion korunur)
        WORD drop_slot = INVENTORY_MAX_NUM;
        DWORD drop_vnum = 0;
        for (WORD i = 0; i < INVENTORY_MAX_NUM; ++i)
        {
            LPITEM it = ctx.bot->GetInventoryItem(i);
            if (!it) continue;
            DWORD vn = it->GetVnum();
            if (vn >= 27000 && vn < 28000) continue;  // potion koru
            BYTE tp = it->GetType();
            if (tp == ITEM_WEAPON || tp == ITEM_ARMOR)
            {
                drop_slot = i;
                drop_vnum = vn;
                break;  // ilk uygunu bul
            }
        }
        if (drop_slot >= INVENTORY_MAX_NUM) return false;  // dropable bulunamadi

        TItemPos cell;
        cell.window_type = INVENTORY;
        cell.cell = drop_slot;
        if (!ctx.bot->DropItem(cell)) return false;

        sys_log(0, "BotAI[Restock]: %s drop vnum=%u slot=%u inv=%d/90",
            ctx.bot->GetName(), drop_vnum, drop_slot, ctx.inv_used);
        return true;
    }
    const char* Name() const override { return "Restock"; }
};

// ============================================================================
// Phase VII B2 — LootPickupAction
// Mob olu veya 5000u icinde item var -> yere dusen item/gold topla
// Pickup pattern: char_bot.cpp FCollectDrops + CHARACTER::PickupItem(vid) referans
//
// Score mantigi:
//   - target NULL veya IsDead() + yakin item var: +1000
//   - 5000u icinde item var: +800
//   - inventory dolu (inv_pct >= 90): -2000
//
// Execute: sectree ForEachAround -> yakindaki item VID'leri topla,
//          her biri icin PickupItem(vid) cagir, BotLog_Inventory ile kaydet.
// ============================================================================
class LootPickupAction : public IBotAction
{
    // Sectree functor — bu class'a ozel, global FCollectDrops'tan bagimsiz
    struct FLootCollect
    {
        LPCHARACTER bot;
        std::vector<DWORD> vids;
        static constexpr int LOOT_RANGE_SQ = 5000 * 5000;

        void operator()(LPENTITY ent)
        {
            if (!ent || !ent->IsType(ENTITY_ITEM)) return;
            LPITEM item = (LPITEM)ent;
            if (!item->GetSectree()) return;
            int32_t dx = item->GetX() - bot->GetX();
            int32_t dy = item->GetY() - bot->GetY();
            if ((int64_t)dx * dx + (int64_t)dy * dy < LOOT_RANGE_SQ)
                vids.push_back(item->GetVID());
        }
    };

public:
    float Score(const BotActionContext& ctx) const override
    {
        // Envanter doluysa pickup yapma
        if (ctx.inv_pct >= 90) return -2000.0f;

        LPSECTREE sec = ctx.bot->GetSectree();
        if (!sec) return 0.0f;

        // Yakin item var mi kontrolu
        bool has_item = false;
        FLootCollect probe{ ctx.bot, {} };
        sec->ForEachAround(probe);
        if (!probe.vids.empty()) has_item = true;

        if (!has_item) return 0.0f;

        // Target olu ya da yok: yuksek oncelik (looting window)
        bool target_dead = (!ctx.target || ctx.target->IsDead());
        return target_dead ? 1000.0f : 800.0f;
    }

    bool Execute(BotActionContext& ctx) override
    {
        if (!ctx.bot) return false;

        LPSECTREE sec = ctx.bot->GetSectree();
        if (!sec) return false;

        FLootCollect collector{ ctx.bot, {} };
        sec->ForEachAround(collector);
        if (collector.vids.empty()) return false;

        // En yakin item'a yuru (ilk VID referans)
        LPITEM nearest = ITEM_MANAGER::instance().FindByVID(collector.vids[0]);
        if (!nearest) return false;

        // Mesafe > 300u ise once yaklas
        long dx = nearest->GetX() - ctx.bot->GetX();
        long dy = nearest->GetY() - ctx.bot->GetY();
        long dist_sq = dx * dx + dy * dy;
        if (dist_sq > 300LL * 300LL)
        {
            // Phase VII B1 jitter burada da uygula (dogal yaklasum)
            ApplyHeadingJitter(dx, dy, ctx.seed ^ 0xC0DE, 3.0f);
            long tx = ctx.bot->GetX() + dx;
            long ty = ctx.bot->GetY() + dy;
            ctx.bot->BotMoveStep(tx, ty, 250.0, "LOOT_APPROACH");
            return true;  // bir sonraki tick'te pickup yapilacak
        }

        // Pickup loop
        int picked = 0;
        for (DWORD vid : collector.vids)
        {
            LPITEM it = ITEM_MANAGER::instance().FindByVID(vid);
            if (!it) continue;
            DWORD ivnum = it->GetVnum();
            int   icount = it->GetCount();
            bool  isGold = (it->GetType() == ITEM_ELK);

            if (ctx.bot->PickupItem(vid))
            {
                picked++;
                BotLog_Inventory(ctx.bot, isGold ? "PICKUP_GOLD" : "PICKUP_ITEM",
                                 ivnum, -1, icount);
                sys_log(0, "[BOT_LOOT] %s picked %s vnum=%u cnt=%d pos=(%ld,%ld)",
                    ctx.bot->GetName(),
                    isGold ? "GOLD" : "ITEM",
                    ivnum, icount,
                    ctx.bot->GetX(), ctx.bot->GetY());
            }
        }

        if (picked > 0)
        {
            sys_log(0, "BotAI[LootPickup]: %s picked %d items",
                ctx.bot->GetName(), picked);
            return true;
        }
        return false;
    }

    const char* Name() const override { return "LootPickup"; }
};

// ============================================================================
// v22 PullAoeAction — 3+ mob 800u içindeyse class-spesifik AOE skill
// Warrior Ep1 14:25 pattern: pull aggregate -> AOE cast (Sangong/Yeonsa)
// Score 0.85 (SkillCombat 0.75'ten yuksek), AOE skill prio
// ============================================================================
class PullAoeAction : public IBotAction
{
public:
    float Score(const BotActionContext& ctx) const override
    {
        if (!ctx.target || ctx.target->IsDead()) return 0.0f;
        if (!ctx.bot->IsBotSkillReady()) return 0.0f;  // 6sn cooldown share

        // Sectree iter: 800u içinde kac mob var?
        int mob_count = 0;
        if (auto sec = ctx.bot->GetSectree())
        {
            struct FCount {
                LPCHARACTER me; int& count; long range_sq;
                void operator()(LPENTITY e) {
                    if (!e || e == me || !e->IsType(ENTITY_CHARACTER)) return;
                    LPCHARACTER c = (LPCHARACTER)e;
                    if (c->IsDead() || c->IsPC()) return;
                    if (!c->IsMonster() && !c->IsStone()) return;
                    long dx = c->GetX() - me->GetX();
                    long dy = c->GetY() - me->GetY();
                    if ((long long)dx*dx + (long long)dy*dy < range_sq) count++;
                }
            };
            FCount counter{ ctx.bot, mob_count, 800L*800L };
            sec->ForEachAround(counter);
        }
        if (mob_count < 3) return 0.0f;
        return 0.85f;  // SkillCombat 0.75'ten yuksek
    }

    bool Execute(BotActionContext& ctx) override
    {
        if (!ctx.target) return false;

        // v24 — CombatPlan'a delege (hardcoded vnum yerine, validated AOE skill)
        // SkillCombatAction zaten ayni listeyi kullaniyor — guvenli.
        // preferred_skills[1] genelde AOE (warrior: Palbang, sura: Hwayeompok vb).
        DWORD aoe_skill = 0;
        for (int i = 0; i < 3; ++i)
        {
            DWORD candidate = ctx.combat_plan.preferred_skills[i];
            if (candidate == 0) continue;
            const SkillMeta* m = GetSkillMetaCached(candidate);
            if (!m || !m->valid) continue;
            // SELFONLY veya non-attack atla (PullAoe = attack ONLY)
            if (m->is_selfonly) continue;
            if (!m->is_attack) continue;
            // AOE tercih (is_aoe varsa direkt sec, yoksa fallback)
            if (m->is_aoe) { aoe_skill = candidate; break; }
            if (aoe_skill == 0) aoe_skill = candidate;  // fallback
        }
        if (aoe_skill == 0) return false;
        if (!ctx.bot->CanUseSkill(aoe_skill)) return false;

        ctx.bot->SetRotationToXY(ctx.target->GetX(), ctx.target->GetY());
        ctx.bot->ComputeSkill(aoe_skill, ctx.target);
        BotEmitSkillVisual(ctx.bot, aoe_skill);
        ctx.bot->MarkBotSkillUsed();
        sys_log(0, "BOT_AI_PULLAOE: %s class=%s aoe_skill=%u tgt=%s",
            ctx.bot->GetName(), ctx.combat_plan.class_name, aoe_skill,
            ctx.target ? ctx.target->GetName() : "?");
        sys_log(0, "[BOT_COMBAT] %s class=%s skill=%u type=AOE tgt=%s",
            ctx.bot->GetName(), ctx.combat_plan.class_name, aoe_skill,
            ctx.target ? ctx.target->GetName() : "?");
        return true;
    }
    const char* Name() const override { return "PullAoe"; }
};

// ============================================================================
// v24 Sorun 2 — MentionReplyAction: PC chat'te bot adi gectiginde persona cevabi
// Score 0.55 (Chat 0.10'dan yuksek, Attack 0.70'ten dusuk: combat oncelik)
// 30sn icinde tek cevap (eski mention'lari clear)
// ============================================================================
class MentionReplyAction : public IBotAction
{
public:
    float Score(const BotActionContext& ctx) const override
    {
        if (!ctx.bot->HasBotMention()) return 0.0f;
        return 0.55f;
    }
    bool Execute(BotActionContext& ctx) override
    {
        const std::string& msg = ctx.bot->GetBotMention();
        DWORD seed = (DWORD)ctx.bot->GetPlayerID() ^ get_dword_time();
        const char* reply = BotChat_PickMentionReply(ctx.persona, seed);
        if (!reply) {
            ctx.bot->ClearBotMention();
            return false;
        }
        BotChat_Broadcast(ctx.bot, reply);
        ctx.bot->ClearBotMention();
        ctx.bot->SetBotChatCooldown(60000);  // 60sn idle chat sustur (mention'a cevap verdi)
        sys_log(0, "[BOT_MENTION_REPLY] %s -> \"%s\" (in reply to: \"%s\")",
            ctx.bot->GetName(), reply, msg.c_str());
        return true;
    }
    const char* Name() const override { return "MentionReply"; }
};

// ============================================================================
// v22 BuffCycleAction — class-spesifik self-buff her 5dk yenile
// Warrior Ep1 c-025 pattern: city return -> Geomkyung defense buff
// Score 0.60 (Idle 0 dan yuksek, Attack 0.70 dan dusuk — combat once buff)
// ============================================================================
class BuffCycleAction : public IBotAction
{
public:
    float Score(const BotActionContext& ctx) const override
    {
        // HP >= %80 (buff aktivite kabuldir), cooldown ready
        if (ctx.hp_pct < 80) return 0.0f;
        if (!ctx.bot->IsBotBuffReady()) return 0.0f;
        // Combat'ta degilse veya hedef yoksa
        if (ctx.target && ctx.target_dist_sq < 500LL*500LL) return 0.0f;  // melee'de degil
        return 0.60f;
    }

    bool Execute(BotActionContext& ctx) override
    {
        // v24 — CombatPlan.buff_skills'ten validated SELFONLY skill al
        // Hardcoded vnum hatasi (warrior=5 actually ATTACK skill) duzeltildi.
        DWORD buff_skill = 0;
        for (int i = 0; i < 2; ++i)
        {
            DWORD candidate = ctx.combat_plan.buff_skills[i];
            if (candidate == 0) continue;
            const SkillMeta* m = GetSkillMetaCached(candidate);
            if (!m || !m->valid) continue;
            // SELFONLY ve attack-degil olmali (gercek buff)
            if (!m->is_selfonly) continue;
            if (m->is_attack) continue;
            buff_skill = candidate;
            break;
        }
        if (buff_skill == 0) return false;
        if (!ctx.bot->CanUseSkill(buff_skill)) return false;

        ctx.bot->ComputeSkill(buff_skill, ctx.bot);
        BotEmitSkillVisual(ctx.bot, buff_skill);
        ctx.bot->MarkBotBuffUsed();
        sys_log(0, "BOT_AI_BUFFCYCLE: %s class=%s buff_skill=%u",
            ctx.bot->GetName(), ctx.combat_plan.class_name, buff_skill);
        sys_log(0, "[BOT_COMBAT] %s class=%s skill=%u type=BUFF_CYCLE",
            ctx.bot->GetName(), ctx.combat_plan.class_name, buff_skill);
        return true;
    }
    const char* Name() const override { return "BuffCycle"; }
};

// ============================================================================
// ChatAction — nearby_pc + cooldown bitti
// Cooldown m_dwBotNextChatTime BotTick'te zaten yonetiliyor — burada sadece
// score "yakinda PC var mi" check, Execute char_bot.cpp BotChatPacket'a delege.
// V13: bu action gercek chat broadcast'i BotTick'te yapar, burada placeholder
// ============================================================================
// v16 Adim 6.1 — ChatAction full impl (char_bot.cpp BotTick chat blok'u buraya tasindi)
// Cooldown per-bot: aggressive 30-180sn, cautious 120-600sn (sessiz), explorer/lazy 60-300sn, social 30-120sn
class ChatAction : public IBotAction
{
public:
    float Score(const BotActionContext& ctx) const override
    {
        if (!ctx.nearby_pc) return 0.0f;
        if (!ctx.bot->IsBotChatReady()) return 0.0f;
        // v16.2 fix — Score 0.10 (cok dusuk, sadece bos zamanda)
        // Eski 0.40 -> Heal/Flee/Skill/Attack hep oncelikli kalsin, chat seyrek tetiklensin
        return 0.10f;
    }
    bool Execute(BotActionContext& ctx) override
    {
        if (!ctx.bot) return false;
        // v16.2 fix — Seed cesitlilik: bot pid + zaman pencere (5dk buckets)
        // Ayni dakika icindeki bot'lar farkli cumle secsin
        DWORD time_bucket = ctx.now / (5 * 60 * 1000);  // 5dk pencere
        DWORD unique_seed = ((DWORD)ctx.bot->GetPlayerID() * 0x9E3779B9u) ^ time_bucket;
        // v24 Sorun 2 — Dedup ring buffer (son 5 cumle tekrarini engelle)
        const char* line = BotChat_PickLineDedup(ctx.bot, ctx.persona, unique_seed);
        if (!line) return false;
        BotChat_Broadcast(ctx.bot, line);

        // v16.2 fix — Cooldown 10x artirildi (kullanici feedback: spam tekrari)
        // Eski aggregat 30-600sn -> Yeni 5-30dk (cok daha seyrek, dogal sohbet ritmi)
        DWORD cd_min, cd_max;
        switch (ctx.persona)
        {
            case 0: cd_min = 300000;  cd_max = 1200000; break;  // aggressive 5-20dk
            case 1: cd_min = 900000;  cd_max = 3600000; break;  // cautious 15-60dk (sessiz)
            case 2: cd_min = 600000;  cd_max = 1800000; break;  // explorer 10-30dk
            case 3: cd_min = 600000;  cd_max = 1800000; break;  // lazy 10-30dk
            case 4: cd_min = 240000;  cd_max = 900000;  break;  // social 4-15dk
            default: cd_min = 600000; cd_max = 1800000; break;
        }
        DWORD cd = cd_min + (unique_seed % (cd_max - cd_min));
        ctx.bot->SetBotChatCooldown(cd);
        return true;
    }
    const char* Name() const override { return "Chat"; }
};

// ============================================================================
// Registry init — main.cpp BotInit veya benzeri tek seferlik cagri
// ============================================================================
void BotAI_InitActions()
{
    if (!g_bot_actions.empty()) return;  // already initialized
    g_bot_actions.emplace_back(std::make_unique<IdleAction>());
    g_bot_actions.emplace_back(std::make_unique<UnstuckAction>());      // v30 (score 0.92)
    g_bot_actions.emplace_back(std::make_unique<PotionUseAction>());    // v30 (score 0.92)
    g_bot_actions.emplace_back(std::make_unique<LeashReturnAction>()); // v30 (score 0.50)
    g_bot_actions.emplace_back(std::make_unique<HealAction>());
    g_bot_actions.emplace_back(std::make_unique<FleeAction>());
    // v24 — Re-enabled: char_affect.cpp NULL guard + CombatPlan delegation
    g_bot_actions.emplace_back(std::make_unique<PullAoeAction>());      // v22 video-trained (score 0.85)
    g_bot_actions.emplace_back(std::make_unique<BuffCycleAction>());    // v22 video-trained (score 0.60)
    g_bot_actions.emplace_back(std::make_unique<MentionReplyAction>()); // v24 Sorun 2 (score 0.55)
    g_bot_actions.emplace_back(std::make_unique<SkillCombatAction>());  // v14 class-aware (score 0.75)
    g_bot_actions.emplace_back(std::make_unique<KiteAction>());         // v14 ranged kite (score 0.65)
    g_bot_actions.emplace_back(std::make_unique<AttackAction>());       // basic fallback (score 0.70)
    g_bot_actions.emplace_back(std::make_unique<WanderAction>());
    // v16 — LevelUpWarpAction silindi (state-based, BotTick'te kalir)
    g_bot_actions.emplace_back(std::make_unique<RestockAction>());       // v16 impl + Phase VII B3 potion
    g_bot_actions.emplace_back(std::make_unique<LootPickupAction>());   // Phase VII B2 mob-drop pickup
    g_bot_actions.emplace_back(std::make_unique<ChatAction>());          // v16 impl
    sys_log(0, "BotAI_InitActions: %zu actions registered (v16 konsolide + PhaseVII-B)", g_bot_actions.size());
}

// ============================================================================
// BuildContext — char_bot.cpp BotTick basindan once cagrilir
// ============================================================================
BotActionContext BotAI_BuildContext(CHARACTER* bot, DWORD seed)
{
    BotActionContext ctx{};
    ctx.bot   = bot;
    ctx.now   = get_dword_time();
    ctx.seed  = seed;

    ctx.hp    = bot->GetHP();
    ctx.max_hp = bot->GetMaxHP();
    ctx.sp    = bot->GetSP();
    ctx.max_sp = bot->GetMaxSP();
    ctx.hp_pct = ctx.max_hp > 0 ? (ctx.hp * 100) / ctx.max_hp : 0;
    ctx.sp_pct = ctx.max_sp > 0 ? (ctx.sp * 100) / ctx.max_sp : 0;

    // v16 Adim 6.2 — Inventory dolu slot hesabi (RestockAction icin)
    ctx.inv_used = 0;
    for (WORD i = 0; i < INVENTORY_MAX_NUM; ++i)
    {
        if (bot->GetInventoryItem(i)) ctx.inv_used++;
    }
    ctx.inv_pct = (ctx.inv_used * 100) / 90;  // INVENTORY_MAX_NUM=90 baz

    ctx.target = bot->GetBotTarget();
    if (ctx.target && !ctx.target->IsDead())
    {
        long dx = ctx.target->GetX() - bot->GetX();
        long dy = ctx.target->GetY() - bot->GetY();
        ctx.target_dist_sq = (int)((long long)dx * dx + (long long)dy * dy);
        ctx.target_in_melee = ctx.target_dist_sq <= 280 * 280;
    }
    else
    {
        ctx.target = nullptr;
        ctx.target_dist_sq = 0;
        ctx.target_in_melee = false;
    }

    ctx.sec = bot->GetSectree();
    // v16 Adim 6.1 — ChatAction icin gercek nearby_pc check (1500u sectree iter)
    ctx.nearby_pc = BotChat_NearbyPC(bot);

    ctx.persona = bot->GetBotPersonality();
    ctx.fatigue = bot->GetBotFatigue();   // v16 — gercek getter (E1)
    ctx.in_afk  = bot->IsBotIdleNow();    // v16 — gercek AFK pencere check (E2)
    ctx.last_action = 0;

    ctx.empire = bot->GetEmpire();
    ctx.level  = bot->GetLevel();

    // v14 — Class-aware combat plan
    ctx.combat_plan = BuildCombatPlanForBot(bot);

    return ctx;
}

// ============================================================================
// TickDispatch — char_bot.cpp BotTick'te cagrilir
// Score+Execute, en yuksek skoru bul ve isle. Tie -> registry sirasi.
// ============================================================================
const char* BotAI_TickDispatch(CHARACTER* bot)
{
    if (g_bot_actions.empty())
    {
        BotAI_InitActions();
    }
    DWORD seed = (DWORD)bot->GetPlayerID() ^ get_dword_time();
    BotActionContext ctx = BotAI_BuildContext(bot, seed);

    IBotAction* best = nullptr;
    float       best_score = 0.0f;
    IBotAction* runner_up = nullptr;
    float       runner_up_score = 0.0f;
    for (auto& act : g_bot_actions)
    {
        float s = act->Score(ctx);
        if (s > best_score)
        {
            runner_up = best;
            runner_up_score = best_score;
            best_score = s;
            best = act.get();
        }
        else if (s > runner_up_score)
        {
            runner_up = act.get();
            runner_up_score = s;
        }
    }

    if (best && best_score > 0.0f)
    {
        // v30 — Master log: AI_DECISION (her 10 tick'te 1, spam azalt)
        static thread_local DWORD s_log_skip = 0;
        if ((++s_log_skip % 10) == 0)
        {
            BotLog_AI(bot, best->Name(), best_score,
                runner_up ? runner_up->Name() : "", runner_up_score);
        }
        bool ok = best->Execute(ctx);
        if (ok)
            return best->Name();
    }
    return nullptr;
}
