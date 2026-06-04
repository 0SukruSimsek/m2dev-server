// ============================================================================
// bot_combat_plan.h — v14 Class-Aware Combat Sophistication (Asama C)
//
// 4 PC class icin runtime skill_proto + class-spesifik CombatPlan + KiteAction.
// Skill ID tablosu hardcode degil — char_skill.cpp:3568 SkillList[][][6] referans
// CSkillManager::instance().Get(vnum) runtime'da CSkillProto* doner.
//
// Mimar:
//   - SkillMeta: tek skill icin lightweight cache (lazy-init, immutable)
//   - GetClassSkills(job, group): 6 skill vnum'lu array
//   - CombatPlan: bot-spesifik combat strateji
//   - BuildCombatPlanForBot(bot): job + skill_group + persona uyumlu plan
//
// Kaynak: arastirici c-combat-arastirma plan (bot-ai-c-combat-sophistication.md)
// ============================================================================
#pragma once

#include "char.h"

class CSkillProto;

// ============================================================================
// SkillMeta — runtime skill_proto cache (lazy-init, immutable)
// ============================================================================
struct SkillMeta
{
    DWORD vnum;
    bool  is_attack;          // dwFlag & SKILL_FLAG_ATTACK
    bool  is_selfonly;        // dwFlag & SKILL_FLAG_SELFONLY (buff)
    bool  is_aoe;             // dwFlag & SKILL_FLAG_SPLASH
    DWORD target_range;       // pkSk->dwTargetRange (0 = melee 250-280)
    DWORD cooldown_ms;        // kCooldownPoly default level 5 hesabi
    int   sp_cost;            // kSPCostPoly default level 5 hesabi
    BYTE  type;               // pkSk->dwType (0=all, 1=warrior, 2=assassin, 3=sura, 4=shaman)
    bool  valid;              // CSkillProto bulundu mu
};

// Skill metadata cache (immutable, lazy-init)
const SkillMeta* GetSkillMetaCached(DWORD vnum);

// Job + skill_group (1 or 2) -> 6 skill vnum array (char_skill.cpp:3568 referans)
// Donen pointer'in omru program lifetime.
const DWORD* GetClassSkills(BYTE job, BYTE skill_group);

// Persona/job/skill_group bazli sabit pointer (NULL ise plan disi)
const char* SkillGroupName(BYTE job, BYTE skill_group);

// ============================================================================
// CombatPlan — bot icin combat stratejisi
// ============================================================================
struct CombatPlan
{
    BYTE   job;
    BYTE   skill_group;
    DWORD  preferred_skills[3];   // priority queue, ilk uygun olan kullanilir
    DWORD  buff_skills[2];        // SELFONLY (Jeongwi, Geomkyung warrior)
    int    optimal_range;         // 250 melee, 800 medium, 1500 ranged
    int    kite_threshold_hp;     // %20 warrior, %50 assassin, %60 shaman
    DWORD  next_skill_cd_ms;      // 6000-12000 default
    bool   has_aoe;
    const char* class_name;       // log icin
};

// Bot icin combat plan ata (job + skill_group bazli per-class konfig)
CombatPlan BuildCombatPlanForBot(LPCHARACTER bot);
