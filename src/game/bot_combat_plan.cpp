// ============================================================================
// bot_combat_plan.cpp — v14 Class-Aware Combat Sophistication
// ============================================================================
#include "stdafx.h"
#include "bot_combat_plan.h"
#include "skill.h"
#include "log.h"
#include "../common/length.h"
#include "../common/tables.h"
#include <unordered_map>
#include <cstring>

// ============================================================================
// Class skill matrisi — char_skill.cpp:3568 SkillList[][][6] birebir kopya
// (Origin private static, public mirror buraya)
// ============================================================================
static const DWORD kClassSkills[4][2][6] = {
    // Warrior (job=0)
    { { 1,  2,  3,  4,  5,  6  },  // body group 1
      { 16, 17, 18, 19, 20, 21 }   // mental group 2
    },
    // Assassin (job=1)
    { { 31, 32, 33, 34, 35, 36 },  // body group 1
      { 46, 47, 48, 49, 50, 51 }   // bow group 2
    },
    // Sura (job=2)
    { { 61, 62, 63, 64, 65, 66 },  // body group 1
      { 76, 77, 78, 79, 80, 81 }   // magic group 2
    },
    // Shaman (job=3)
    { { 91, 92, 93, 94, 95, 96  }, // summon group 1
      { 106,107,108,109,110,111 }  // lightning group 2
    }
};

const DWORD* GetClassSkills(BYTE job, BYTE skill_group)
{
    if (job >= 4) job = 0;
    if (skill_group < 1 || skill_group > 2) skill_group = 1;
    return kClassSkills[job][skill_group - 1];
}

const char* SkillGroupName(BYTE job, BYTE skill_group)
{
    static const char* names[4][2] = {
        { "warrior-body", "warrior-mental" },
        { "assassin-body", "assassin-bow" },
        { "sura-body", "sura-magic" },
        { "shaman-summon", "shaman-lightning" }
    };
    if (job >= 4) job = 0;
    if (skill_group < 1 || skill_group > 2) skill_group = 1;
    return names[job][skill_group - 1];
}

// ============================================================================
// SkillMeta cache — lazy-init, immutable per skill_vnum
// ============================================================================
static std::unordered_map<DWORD, SkillMeta> g_skill_meta_cache;

const SkillMeta* GetSkillMetaCached(DWORD vnum)
{
    auto it = g_skill_meta_cache.find(vnum);
    if (it != g_skill_meta_cache.end()) return &it->second;

    SkillMeta meta{};
    meta.vnum = vnum;
    meta.valid = false;

    CSkillProto* pkSk = CSkillManager::instance().Get(vnum);
    if (!pkSk)
    {
        sys_err("GetSkillMetaCached: vnum=%u CSkillProto not found", vnum);
        g_skill_meta_cache[vnum] = meta;
        return &g_skill_meta_cache[vnum];
    }

    meta.valid       = true;
    meta.is_attack   = (pkSk->dwFlag & SKILL_FLAG_ATTACK) != 0;
    meta.is_selfonly = (pkSk->dwFlag & SKILL_FLAG_SELFONLY) != 0;
    meta.is_aoe      = (pkSk->dwFlag & SKILL_FLAG_SPLASH) != 0;
    meta.target_range = pkSk->dwTargetRange;
    meta.type        = (BYTE)pkSk->dwType;

    // Cooldown/SP cost: kSPCostPoly level 1 hesap (en dusuk tahmin, gercek bot lvl 1-14)
    // CanUseSkill zaten runtime'da bot SP'sini check eder; bu sadece pre-filter.
    pkSk->SetPointVar("k", 1.0);
    pkSk->SetSPCostVar("k", 1.0);
    pkSk->SetDurationVar("k", 1.0);

    double cd_sec = pkSk->kCooldownPoly.Eval();
    if (cd_sec <= 0.0 || cd_sec > 600.0) cd_sec = 8.0;  // sanity fallback
    meta.cooldown_ms = (DWORD)(cd_sec * 1000.0);

    double sp = pkSk->kSPCostPoly.Eval();
    if (sp < 0.0 || sp > 10000.0) sp = 50.0;  // fallback
    meta.sp_cost = (int)sp;

    g_skill_meta_cache[vnum] = meta;
    sys_log(0, "SkillMeta cache: vnum=%u type=%d attack=%d selfonly=%d aoe=%d range=%u cd=%ums sp=%d",
        vnum, meta.type, meta.is_attack, meta.is_selfonly, meta.is_aoe,
        meta.target_range, meta.cooldown_ms, meta.sp_cost);
    return &g_skill_meta_cache[vnum];
}

// ============================================================================
// BuildCombatPlanForBot — bot job + skill_group bazli per-class konfig
// ============================================================================
CombatPlan BuildCombatPlanForBot(LPCHARACTER bot)
{
    CombatPlan plan{};
    if (!bot) return plan;

    plan.job = bot->GetJob();
    BYTE group = bot->GetSkillGroup();
    if (group == 0) group = 1;  // v14 safety: skill_group=0 -> 1 (R2 azaltma)
    plan.skill_group = group;

    const DWORD* sk = GetClassSkills(plan.job, group);
    plan.class_name = SkillGroupName(plan.job, group);

    switch (plan.job)
    {
        case 0:  // Warrior — tank/melee
            plan.preferred_skills[0] = sk[0];  // Three-Way / Samyeon
            plan.preferred_skills[1] = sk[1];  // Sword Rotation / Palbang
            plan.preferred_skills[2] = sk[4];  // Tanhwan / Geompung
            plan.buff_skills[0]      = sk[2];  // Jeongwi (att speed)
            plan.buff_skills[1]      = sk[3];  // Geomkyung (att grade)
            plan.optimal_range       = 250;
            plan.kite_threshold_hp   = 20;
            plan.has_aoe             = true;
            break;
        case 1:  // Assassin
            if (group == 2)  // Bow tree
            {
                plan.preferred_skills[0] = sk[0];  // Yeonsa (Burning Arrow)
                plan.preferred_skills[1] = sk[1];  // Kwankyeok
                plan.preferred_skills[2] = sk[2];  // Hwajo (AoE arrow)
                plan.optimal_range       = 1500;
                plan.kite_threshold_hp   = 50;
                plan.has_aoe             = true;
            }
            else  // Body tree
            {
                plan.preferred_skills[0] = sk[0];  // Amseop
                plan.preferred_skills[1] = sk[1];  // Gungsin
                plan.preferred_skills[2] = sk[2];  // Charyun (Poison)
                plan.buff_skills[0]      = sk[3];  // Eunhyung (stealth)
                plan.optimal_range       = 280;
                plan.kite_threshold_hp   = 40;
                plan.has_aoe             = false;
            }
            break;
        case 2:  // Sura
            if (group == 2)  // Magic
            {
                plan.preferred_skills[0] = sk[0];  // Maryung
                plan.preferred_skills[1] = sk[1];  // Hwayeompok
                plan.preferred_skills[2] = sk[5];  // Mahwan
                plan.buff_skills[0]      = sk[3];  // Manashield
                plan.optimal_range       = 1500;
                plan.kite_threshold_hp   = 45;
                plan.has_aoe             = true;
            }
            else  // Body
            {
                plan.preferred_skills[0] = sk[0];  // Swaeryeong
                plan.preferred_skills[1] = sk[1];  // Yongkwon
                plan.buff_skills[0]      = sk[2];  // Gwigeom
                plan.buff_skills[1]      = sk[4];  // Jumagap
                plan.optimal_range       = 280;
                plan.kite_threshold_hp   = 30;
                plan.has_aoe             = false;
            }
            break;
        case 3:  // Shaman
            if (group == 2)  // Lightning
            {
                plan.preferred_skills[0] = sk[0];  // Noejeon
                plan.preferred_skills[1] = sk[1];  // Byeurak (Stun)
                plan.preferred_skills[2] = sk[2];  // Chain Lightning
                plan.optimal_range       = 1800;
                plan.kite_threshold_hp   = 60;
                plan.has_aoe             = true;
            }
            else  // Summon (Healer/Support)
            {
                plan.preferred_skills[0] = sk[3];  // Reflect (SELFONLY buff)
                plan.buff_skills[0]      = sk[3];  // Reflect
                plan.buff_skills[1]      = sk[2];  // Paeryong
                plan.optimal_range       = 1500;
                plan.kite_threshold_hp   = 65;
                plan.has_aoe             = false;
            }
            break;
        default:
            // Bilinmeyen job -> warrior fallback
            plan.preferred_skills[0] = 1;
            plan.preferred_skills[1] = 2;
            plan.preferred_skills[2] = 5;
            plan.optimal_range       = 250;
            plan.kite_threshold_hp   = 20;
            plan.has_aoe             = true;
            plan.class_name          = "fallback-warrior";
            break;
    }

    plan.next_skill_cd_ms = 8000;
    return plan;
}
