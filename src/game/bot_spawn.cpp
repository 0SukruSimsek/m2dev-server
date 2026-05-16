// ============================================================================
// Keyf.Online — Server-side AutoBot Spawn v11 (Render + AI re-enable)
//
// v11 yenilik:
//   1. SetBasePart + SetPart(MAIN/HAIR) — bot "yarim yamalak" render fix
//   2. DB query'e part_main, part_base, part_hair eklendi
//   3. SetAutoBot(true) korundu, AI re-enabled (DESC=null safety Damage()'a tasindi)
//   4. char_battle.cpp Damage(): server-side bot DESC=null safety branch (mob saldirisinda paket atla)
//   5. char.cpp BotTeleport(): Save() bot icin atlanir
//
// v10 yenilik: SetCharType(CHAR_TYPE_PC) ZORUNLU — yoksa default CHAR_TYPE_MONSTER
// kalir, IsPC()==false, battle.cpp mob branch'a girer, GetMobAttackRange NULL deref
// crash olur (gdb backtrace 2026-05-11 22:26 channel1_core1 SIGSEGV).
//
// v9 yenilik: SetAutoBot(true) ACIK — PvP-immune; BotTick AI'da DESC=null safety check.
// v5 yenilik: SetPlayerProto + ComputePoints CAGIRMA — DESC=null durumunda crash.
// ============================================================================

#include "stdafx.h"
#include "bot_spawn.h"
#include "char.h"
#include "char_manager.h"
#include "config.h"
#include "log.h"
#include "db.h"
#include "event.h"
#include "../common/length.h"
#include "sectree_manager.h"
#include "item.h"            // v29 — Equipment load
#include "item_manager.h"    // v29 — ITEM_MANAGER
#include "bot_logger.h"      // v30 — Master CSV log
#include "../common/tables.h"
#include "bot_combat_plan.h"     // v14 GetClassSkills helper

namespace
{
    LPCHARACTER SpawnAIBot(const char* botName)
    {
        sys_log(0, "SpawnAIBot[%s]: STEP_1 enter", botName);
        if (!botName || !*botName) return nullptr;

        sys_log(0, "SpawnAIBot[%s]: STEP_2 FindPC check", botName);
        if (CHARACTER_MANAGER::instance().FindPC(botName) != nullptr)
        {
            sys_log(0, "SpawnAIBot[%s]: already in game", botName);
            return nullptr;
        }

        sys_log(0, "SpawnAIBot[%s]: STEP_3 DB query", botName);
        char query[640];
        snprintf(query, sizeof(query),
            "SELECT id, name, job, level, st, ht, dx, iq, hp, mp, x, y, z, map_index, "
            "gold, exp, dir, part_main, part_base, part_hair FROM player.player WHERE name='%s' LIMIT 1", botName);
        auto pmsg = DBManager::instance().DirectQuery(query);
        if (!pmsg || pmsg->Get()->uiNumRows == 0)
        {
            sys_err("SpawnAIBot[%s]: STEP_3 FAIL no DB row", botName);
            return nullptr;
        }
        MYSQL_ROW row = mysql_fetch_row(pmsg->Get()->pSQLResult);
        if (!row || !row[0]) return nullptr;

        DWORD pid       = atoi(row[0]);
        const char* nm  = row[1];
        BYTE  job       = atoi(row[2]);
        BYTE  level     = atoi(row[3]);
        int   st        = atoi(row[4]);
        int   ht        = atoi(row[5]);
        int   dx        = atoi(row[6]);
        int   iq        = atoi(row[7]);
        int   hp        = atoi(row[8]);
        int   sp        = atoi(row[9]);
        long  x         = atoi(row[10]);
        long  y         = atoi(row[11]);
        long  z         = row[12] ? atoi(row[12]) : 0;
        long  mapIdx    = atoi(row[13]);
        int   gold      = row[14] ? atoi(row[14]) : 10000;
        int   exp       = row[15] ? atoi(row[15]) : 0;
        BYTE  dir       = row[16] ? atoi(row[16]) : 0;
        // v11 RENDER FIX: bot icin parts read (DB'de part_main=11200 ama part_base/part_hair=0 -> "yarim yamalak")
        WORD  part_main = row[17] ? (WORD)atoi(row[17]) : 0;
        BYTE  part_base = row[18] ? (BYTE)atoi(row[18]) : 0;
        WORD  part_hair = row[19] ? (WORD)atoi(row[19]) : 0;
        BYTE  empire    = 1;  // hardcode (cross-DB sorgu crash)

        sys_log(0, "SpawnAIBot[%s]: STEP_4 DB OK pid=%u lvl=%d job=%d pos=(%ld,%ld) map=%ld",
            botName, pid, (int)level, (int)job, x, y, mapIdx);

        // v16.1 — Spawn pos random offset (kullanici feedback: hepsi tek noktada toplanmis)
        // ±1500u dagilim, pid-bazli deterministik (her bot ayni offset per restart)
        long spawn_off_x = (long)((pid * 0x9E3779B9u) % 3000) - 1500;
        long spawn_off_y = (long)((pid * 0x85EBCA77u) % 3000) - 1500;
        x += spawn_off_x;
        y += spawn_off_y;

        // STEP 5: pos validate
        sys_log(0, "SpawnAIBot[%s]: STEP_5 GetValidLocation (offset +%ld,%+ld)",
            botName, spawn_off_x, spawn_off_y);
        long lMapIndex = mapIdx;
        PIXEL_POSITION pos;
        if (!SECTREE_MANAGER::instance().GetValidLocation(
            mapIdx, x, y, lMapIndex, pos, empire))
        {
            sys_err("SpawnAIBot[%s]: STEP_5 FAIL invalid location", botName);
            return nullptr;
        }
        x = pos.x; y = pos.y;
        sys_log(0, "SpawnAIBot[%s]: STEP_5 OK pos=(%ld,%ld) map=%ld", botName, x, y, lMapIndex);

        // STEP 6: CreateCharacter
        sys_log(0, "SpawnAIBot[%s]: STEP_6 CreateCharacter", botName);
        LPCHARACTER ch = CHARACTER_MANAGER::instance().CreateCharacter(nm, pid);
        if (!ch)
        {
            sys_err("SpawnAIBot[%s]: STEP_6 FAIL", botName);
            return nullptr;
        }
        sys_log(0, "SpawnAIBot[%s]: STEP_6 OK ch=%p VID=%u", botName, ch, (DWORD)ch->GetVID());

        // STEP 7: Manual field set (SetPlayerProto + ComputePoints ATLA — crash)
        sys_log(0, "SpawnAIBot[%s]: STEP_7 manual set fields", botName);
        // SetPlayerID YOK — pid CreateCharacter'da set edildi

        // v10 KRITIK: m_bCharType=CHAR_TYPE_PC (yoksa default CHAR_TYPE_MONSTER, IsPC()=false crash)
        ch->SetCharType(CHAR_TYPE_PC);

        // v12 Real-Player Simulation — persona + reaction time + deterministik chat seed
        BYTE persona = (BYTE)(pid % 5);  // 0..4: aggressive/cautious/explorer/lazy/social
        ch->SetBotPersonality(persona);
        WORD reaction_ms = PersonaBaselineMs(persona);
        ch->SetBotReactionMs(reaction_ms);
        sys_log(0, "SpawnAIBot[%s]: PERSONA=%s reactionMs=%u (pid=%u)",
            botName, PersonaName(persona), (unsigned)reaction_ms, pid);

        // Race + Level (crash riski varsa erken yakala)
        ch->SetRace(job);
        ch->SetLevel(level);
        ch->SetEmpire(empire);
        ch->SetExp(exp);
        ch->SetGold(gold);

        // Stat points (RealPoint = base, Point = current)
        ch->SetRealPoint(POINT_ST, st);
        ch->SetRealPoint(POINT_HT, ht);
        ch->SetRealPoint(POINT_DX, dx);
        ch->SetRealPoint(POINT_IQ, iq);
        ch->SetPoint(POINT_ST, st);
        ch->SetPoint(POINT_HT, ht);
        ch->SetPoint(POINT_DX, dx);
        ch->SetPoint(POINT_IQ, iq);

        // Pos
        ch->SetMapIndex(lMapIndex);
        ch->SetXYZ(x, y, z);
        ch->SetRotation(dir * 5.0f);
        // v28 — Bot home pos (STUCK/respawn'da burayi referans alir — pid%4 bug fix)
        ch->SetBotHome(x, y);

        // HP/SP — v11 fix: GetMaxHP() iMaxHP (instant cache) okuyor, RealPoint degil.
        // ComputePoints atlandigi icin instant cache 0. PointChange(POINT_MAX_HP, 0)
        // RealPoint -> Instant sync yapar (char.cpp:2533 ComputePoints pattern).
        int target_hp = hp > 0 ? hp : 1500;  // level 1 bot icin makul max
        int target_sp = sp > 0 ? sp : 500;
        ch->SetRealPoint(POINT_MAX_HP, target_hp);
        ch->SetRealPoint(POINT_MAX_SP, target_sp);
        ch->PointChange(POINT_MAX_HP, 0);  // RealPoint -> Instant cache (GetMaxHP icin)
        ch->PointChange(POINT_MAX_SP, 0);  // RealPoint -> Instant cache (GetMaxSP icin)
        ch->SetHP(target_hp);
        ch->SetSP(target_sp);
        // Stat point'lerini de instant'a sync (ComputeBattlePoints icin gerekli olabilir)
        ch->PointChange(POINT_ST, 0);
        ch->PointChange(POINT_HT, 0);
        ch->PointChange(POINT_DX, 0);
        ch->PointChange(POINT_IQ, 0);

        // v11 RENDER FIX — body + hair + base part
        // Bot DB'de part_main=11200 (warrior armor) ama part_base=0 part_hair=0 -> "yarim yamalak" render.
        // Job'a gore default base part (body race model):
        //   warrior(0)=0, assassin(1)=1, sura(2)=2, shaman(3)=3
        // Hair default 0 (saçsız). PART_MAIN DB'den.
        BYTE base_part = (part_base != 0) ? part_base : job;  // job 0-3 -> race model
        ch->SetBasePart(base_part);
        ch->SetPart(PART_MAIN, part_main);
        ch->SetPart(PART_HAIR, part_hair);

        // v14.5 SILAH FIX — PART_WEAPON job-aware (+5 refine baseline)
        //   warrior(0)  -> Sword+5         (vnum 15)
        //   assassin(1) -> Dagger+5        (vnum 1005)
        //   sura(2)     -> Glaive+5        (vnum 3005)
        //   shaman(3)   -> Copper Bell+5   (vnum 5005)
        WORD weapon_vnum = 15;  // warrior default
        switch (job)
        {
            case 0: weapon_vnum = 15;   break;  // Sword+5
            case 1: weapon_vnum = 1005; break;  // Dagger+5
            case 2: weapon_vnum = 3005; break;  // Glaive+5
            case 3: weapon_vnum = 5005; break;  // Copper Bell+5
            default: weapon_vnum = 15; break;
        }
        ch->SetPart(PART_WEAPON, weapon_vnum);
        sys_log(0, "SpawnAIBot[%s]: parts set main=%d base=%d hair=%d weapon=%d",
            botName, part_main, base_part, part_hair, weapon_vnum);

        // v14 SKILL ENJEKSIYON — m_pSkillLevels NULL (SetPlayerProto atlandi)
        // Bot icin 6 class skill'ine level 10 ata (orta seviye, sp_cost dusuk)
        ch->EnsureSkillLevels();
        // v14.3 fix: SetSkillGroup artik DESC=null safe (char_skill.cpp:147 guard).
        // CanUseSkill GetSkillGroup()==0 ise false doner -> SkillList check atlanmaz, return false.
        // Bot icin skill_group=1 set zorunlu.
        ch->SetSkillGroup(1);
        const DWORD* class_skills_g1 = GetClassSkills(job, 1);
        const DWORD* class_skills_g2 = GetClassSkills(job, 2);
        for (int i = 0; i < 6; ++i)
        {
            ch->SetSkillLevel(class_skills_g1[i], 10);  // group 1 her skill lvl 10
            ch->SetSkillLevel(class_skills_g2[i], 10);  // group 2 da hazir (bot prefer group 1)
        }
        // Iki temel skill her zaman alinabilir (no class restriction)
        ch->SetSkillLevel(121, 10);  // Combo (warrior)
        ch->SetSkillLevel(122, 10);  // Polymorph (general)
        sys_log(0, "SpawnAIBot[%s]: SKILLS injected (job=%d group=1 vnums=[%u,%u,%u,%u,%u,%u] lvl=10)",
            botName, (int)job,
            class_skills_g1[0], class_skills_g1[1], class_skills_g1[2],
            class_skills_g1[3], class_skills_g1[4], class_skills_g1[5]);

        // v29 — Equipment yukle (CalcMeleeDamage GetWear(WEAR_WEAPON)=NULL bug fix)
        // DB player.item EQUIPMENT slots -> ITEM_MANAGER::CreateItem -> EquipItem
        {
            char itemQ[512];
            snprintf(itemQ, sizeof(itemQ),
                "SELECT id, vnum, count, pos, socket0, socket1, socket2, "
                "attrtype0, attrvalue0, attrtype1, attrvalue1, attrtype2, attrvalue2, "
                "attrtype3, attrvalue3, attrtype4, attrvalue4, attrtype5, attrvalue5, "
                "attrtype6, attrvalue6 "
                "FROM player.item WHERE owner_id=%u AND window='EQUIPMENT'", pid);
            auto itemMsg = DBManager::instance().DirectQuery(itemQ);
            int equipped_count = 0;
            sys_log(0, "SpawnAIBot[%s]: EQUIP query='%s' itemMsg=%p uiNumRows=%lu",
                botName, itemQ, (void*)itemMsg.get(),
                (itemMsg ? itemMsg->Get()->uiNumRows : 0));
            if (itemMsg && itemMsg->Get()->uiNumRows > 0)
            {
                MYSQL_ROW irow;
                while ((irow = mysql_fetch_row(itemMsg->Get()->pSQLResult)) != nullptr)
                {
                    DWORD item_id = atoi(irow[0]);
                    DWORD vnum    = atoi(irow[1]);
                    BYTE  count   = (BYTE)atoi(irow[2]);
                    int   slot    = atoi(irow[3]);
                    LPITEM pitem = ITEM_MANAGER::instance().CreateItem(vnum, count, item_id, false, -1, true);
                    if (!pitem) {
                        sys_err("SpawnAIBot[%s]: equip CreateItem FAIL vnum=%u id=%u", botName, vnum, item_id);
                        continue;
                    }
                    int32_t sockets[ITEM_SOCKET_MAX_NUM] = {0};
                    sockets[0] = (int32_t)atol(irow[4]);
                    sockets[1] = (int32_t)atol(irow[5]);
                    sockets[2] = (int32_t)atol(irow[6]);
                    pitem->SetSockets(sockets);
                    TPlayerItemAttribute attrs[ITEM_ATTRIBUTE_MAX_NUM];
                    for (int a = 0; a < 7; ++a) {
                        attrs[a].bType = (BYTE)atoi(irow[7 + a*2]);
                        attrs[a].sValue = (short)atoi(irow[8 + a*2]);
                    }
                    pitem->SetAttributes(attrs);
                    pitem->SetSkipSave(true);  // bot icin DB save atla
                    // v36 — KRITIK FIX: v35 SetItem(EQUIPMENT,slot) yanlis index'e yaziyor.
                    // SetItem `pItems[slot]` (INVENTORY slot) yazar, GetWear ise
                    // `pItems[INVENTORY_MAX_NUM+slot]` (gercek WEAR) okur — yanli pozitif log.
                    // Dogru yol: EquipTo() — SetWear + ModifyPoints(stat apply) + m_bEquipped zincirini calistirir.
                    if (!ch->EquipItem(pitem, slot)) {
                        // EquipTo dogrudan WEAR slot'a yerlestir, stat'lari uygula
                        if (ch->GetWear(slot) != NULL) {
                            // Slot dolu — once temizle (eski bot session artigi)
                            ch->SetWear(slot, NULL);
                        }
                        if (pitem->EquipTo(ch, slot)) {
                            sys_log(0, "SpawnAIBot[%s]: EquipTo fallback slot=%d vnum=%u OK (wear=%p)",
                                botName, slot, vnum, ch->GetWear(slot));
                            equipped_count++;
                        } else {
                            sys_log(0, "SpawnAIBot[%s]: EquipTo FAIL vnum=%u slot=%d", botName, vnum, slot);
                            M2_DESTROY_ITEM(pitem);
                            continue;
                        }
                    } else {
                        equipped_count++;
                    }
                }
            }
            sys_log(0, "SpawnAIBot[%s]: EQUIPMENT loaded %d items", botName, equipped_count);
        }

        // v33 — EquipItemAttributes (Anka2 pattern): bot ATT_GRADE + DEF_GRADE max bonus
        // Mob saldirilarinda surekli olmesin diye stat boost.
        // PvP-immune zaten AutoBot ile var, ama mob/quest damage hala isliyor.
        {
            int base_lvl = (int)ch->GetLevel();
            // Defense bonus: lvl bazli, AC max-resist simulasyonu
            ch->SetPoint(POINT_DEF_GRADE_BONUS, 50 + base_lvl * 5);
            ch->SetPoint(POINT_ATT_GRADE_BONUS, 30 + base_lvl * 3);
            // Resist max (Anka2 immortality):
            ch->SetPoint(POINT_RESIST_SWORD,   30);
            ch->SetPoint(POINT_RESIST_TWOHAND, 30);
            ch->SetPoint(POINT_RESIST_DAGGER,  30);
            ch->SetPoint(POINT_RESIST_BELL,    30);
            ch->SetPoint(POINT_RESIST_FAN,     30);
            ch->SetPoint(POINT_RESIST_BOW,     30);
            ch->SetPoint(POINT_RESIST_FIRE,    30);
            ch->SetPoint(POINT_RESIST_ELEC,    30);
            ch->SetPoint(POINT_RESIST_MAGIC,   30);
            // HP bonus
            ch->SetPoint(POINT_MAX_HP_PCT, 50);
            sys_log(0, "SpawnAIBot[%s]: EquipItemAttributes def_bonus=%d att_bonus=%d resist=30%% (Anka2 pattern)",
                botName, 50 + base_lvl * 5, 30 + base_lvl * 3);
        }

        // v9: SetAutoBot(true) ACIK — PvP-immune icin gerekli (char_battle.cpp
        // Damage() bot saldirilarda DAMAGE_DODGE doner). v11 BotTick AI re-enabled
        // (DESC=null safety Damage()'a tasindi, BotTick'te guard'lar var).
        ch->SetAutoBot(true);
        sys_log(0, "SpawnAIBot[%s]: STEP_7 OK (AutoBot=true, PvP-immune, AI ON v14)", botName);

        // STEP 8: Show — sectree register
        sys_log(0, "SpawnAIBot[%s]: STEP_8 Show map=%ld pos=(%ld,%ld)",
            botName, lMapIndex, x, y);
        if (!ch->Show(lMapIndex, x, y, z))
        {
            sys_err("SpawnAIBot[%s]: STEP_8 Show FAIL", botName);
            CHARACTER_MANAGER::instance().DestroyCharacter(ch);
            return nullptr;
        }
        sys_log(0, "SpawnAIBot[%s]: STEP_8 OK — bot spawned successfully", botName);
        // v30 — Master log: SPAWN event
        BotLog_Lifecycle(ch, "SPAWN", "", (int)level);

        return ch;
    }
}  // namespace

void BotSpawnAll()
{
    if (g_bAuthServer) return;

    // SADECE channel1_1'de spawn et (tek core, duplicate engelle)
    if (g_stHostname != "channel1_1")
    {
        sys_log(0, "BotSpawnAll[v7]: skipped (host=%s)", g_stHostname.c_str());
        return;
    }

    // v30 — Master CSV log init
    BotLog_Init();

    // v14 — Production donus: 4 class dagilim (warrior/assassin/sura/shaman x5 her biri)
    // DEV_MODE icin 1'e dusurulebilir (debug session).
    constexpr int BOT_SPAWN_COUNT = 5;  // v37 Hafta2: 5 bot (risk azaltma, refactor oncesi)

    sys_log(0, "BotSpawnAll[v14]: starting %d server-side bot(s) on %s (4-class + skill_lvl10 + Gaussian+AFK+chat)",
        BOT_SPAWN_COUNT, g_stHostname.c_str());
    int success = 0;
    for (int i = 1; i <= BOT_SPAWN_COUNT; ++i)
    {
        char name[16];
        snprintf(name, sizeof(name), "Bot%02d", i);
        if (SpawnAIBot(name) != nullptr)
            success++;
    }
    sys_log(0, "BotSpawnAll[v12.2]: %d/%d bots spawned successfully (DEV_MODE)",
        success, BOT_SPAWN_COUNT);
}
