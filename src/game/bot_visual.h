// ============================================================================
// bot_visual.h — v17 Server-side bot icin atak/skill animasyon broadcast
//
// Sorun (kullanici report): "atak animasyonu yok" / "skill kullanan gormedim"
// Sebep: Server-side bot Attack()/ComputeSkill() damage hesabi yapiyor AMA
// normal PC akisinda CG_ATTACK / CG_MOVE(FUNC_SKILL) ile gelen Move paketi
// yayilmadigi icin uzak istemciler bot'un animasyonunu goremiyor.
//
// Cozum: Mob char_state.cpp:1149 pattern'i — SendMovePacket(FUNC_ATTACK /
// FUNC_SKILL|motion_idx) ile PacketView (sectree viewer broadcast).
//
// motion_idx haritasi (char_skill.cpp:3224 s_anMotion2SkillVnumList referans):
//   motion = vnum - 30 * job
//   warrior(j=0) skill 1..6  -> motion 1..6 (group 1)
//   warrior      skill 16..21 -> motion 16..21 (group 2)
//   assassin(j=1) skill 31..36 -> motion 1..6
//   sura(j=2)    skill 61..66 -> motion 1..6
//   shaman(j=3)  skill 91..96 -> motion 1..6
// ============================================================================
#pragma once

#include "char.h"

// Bot'un normal atak animasyonunu cevre istemcilere yayinla
void BotEmitAttackVisual(LPCHARACTER bot, LPCHARACTER victim);

// Bot'un class-skill animasyonunu cevre istemcilere yayinla
void BotEmitSkillVisual(LPCHARACTER bot, DWORD skill_vnum);

// Bot'a eklenen affect'i cevre istemcilere yayinla (ISSUE-006)
// Bot'ta GetDesc()==NULL oldugu icin SendAffectAddPacket cagrilamaz;
// bu fonksiyon BroadcastToMapClients ile ekrana render ettirer.
struct CAffect;
void BotEmitAffectAdd(LPCHARACTER bot, const CAffect* pAff);
