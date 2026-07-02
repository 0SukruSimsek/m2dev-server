// ============================================================================
// bot_visual.cpp — v21 Manual DESC_MANAGER broadcast (chat pattern)
//
// v17/18: SendMovePacket -> PacketView -> m_map_view iter. Server-side bot icin
// m_map_view bos kaliyor (kullanici report: paket logu var ama anim YOK).
//
// v21: bot_chat.cpp pattern — DESC_MANAGER.GetClientSet() iter, map_index filter,
// her client'a direkt Packet() yolla. m_map_view tamamen bypass.
//
// Iki paket gonderiyoruz:
//   1. TPacketGCMove (bFunc=FUNC_ATTACK veya FUNC_SKILL|motion) — PC normal flow
//   2. packet_motion (GC::MOTION + MOTION_NORMAL_ATTACK) — backup
// ============================================================================
#include "stdafx.h"
#include "bot_visual.h"
#include "char.h"
#include "affect.h"
#include "desc.h"
#include "desc_manager.h"
#include "packet_structs.h"
#include "utils.h"
#include "log.h"
#include "motion.h"
#include <algorithm>

// EncodeMovePacket char.cpp:979'da global; burada forward decl
extern void EncodeMovePacket(TPacketGCMove& pack, DWORD dwVID, BYTE bFunc, BYTE bArg,
                             DWORD x, DWORD y, DWORD dwDuration, DWORD dwTime, BYTE bRot);

namespace
{
    // Bot move/motion paketini ayni map'teki tum client'lara yolla
    template<typename TPack>
    struct FBotPacketBroadcast
    {
        const TPack& pack;
        int          map_idx;
        long         center_x;
        long         center_y;
        long         range_sq;

        FBotPacketBroadcast(const TPack& p, int mi, long cx, long cy, long range)
            : pack(p), map_idx(mi), center_x(cx), center_y(cy),
              range_sq((long)range * range) {}

        void operator()(LPDESC d)
        {
            if (!d) return;
            LPCHARACTER ch = d->GetCharacter();
            if (!ch) return;
            if (ch->GetMapIndex() != map_idx) return;
            // Mesafe filtresi (view range ~5000u tipik)
            long dx = ch->GetX() - center_x;
            long dy = ch->GetY() - center_y;
            if ((long long)dx * dx + (long long)dy * dy > range_sq) return;
            d->Packet(&pack, sizeof(TPack));
        }
    };

    template<typename TPack>
    void BroadcastToMapClients(LPCHARACTER bot, const TPack& pack, long range)
    {
        if (!bot) return;
        const auto& clients = DESC_MANAGER::instance().GetClientSet();
        std::for_each(clients.begin(), clients.end(),
            FBotPacketBroadcast<TPack>(pack, bot->GetMapIndex(),
                                        bot->GetX(), bot->GetY(), range));
    }
}

// ============================================================================
// BotEmitAttackVisual — manual broadcast GC_MOVE FUNC_ATTACK + GC_MOTION
// ============================================================================
void BotEmitAttackVisual(LPCHARACTER bot, LPCHARACTER victim)
{
    if (!bot) return;

    DWORD now = get_dword_time();
    if (victim)
        bot->SetRotationToXY(victim->GetX(), victim->GetY());

    // v27 — FUNC_WAIT snap (client interpolation pos hizalama)
    {
        TPacketGCMove wait_pkt;
        EncodeMovePacket(wait_pkt, bot->GetVID(), FUNC_WAIT, 0,
                         bot->GetX(), bot->GetY(), 0, now,
                         (BYTE)((int)bot->GetRotation() / 5));
        BroadcastToMapClients(bot, wait_pkt, 5000);
    }

    // v33 — Per-bot combo index (race-free, Anka2 pattern):
    // FUNC_COMBO + MOTION_COMBO_ATTACK_1+idx -> gercek kilic salla animasyonu.
    BYTE combo_arg = (BYTE)(MOTION_COMBO_ATTACK_1 + bot->GetAndAdvanceBotComboIndex());

    TPacketGCMove mv;
    EncodeMovePacket(mv, bot->GetVID(), FUNC_COMBO, combo_arg,
                     bot->GetX(), bot->GetY(), 0, now,
                     (BYTE)((int)bot->GetRotation() / 5));
    BroadcastToMapClients(bot, mv, 5000);

    // 2) GC::MOTION (mob skill pattern; canonical motion paketi)
    struct packet_motion mo;
    mo.header     = GC::MOTION;
    mo.length     = sizeof(mo);
    mo.vid        = bot->GetVID();
    mo.victim_vid = victim ? victim->GetVID() : 0;
    mo.motion     = MOTION_NORMAL_ATTACK;
    BroadcastToMapClients(bot, mo, 5000);

    sys_log(0, "[BOT_VISUAL_ATTACK] %s rot=%d pos=(%d,%d) tgt=%s (manual broadcast)",
        bot->GetName(), (int)bot->GetRotation(),
        bot->GetX(), bot->GetY(),
        victim ? victim->GetName() : "?");
}

// ============================================================================
// BotEmitSkillVisual — manual broadcast GC_MOVE FUNC_SKILL|motion + GC_MOTION SPECIAL
// motion = skill_vnum - 30 * job (char_skill.cpp:3224 referans)
// ============================================================================
void BotEmitSkillVisual(LPCHARACTER bot, DWORD skill_vnum)
{
    if (!bot) return;
    if (skill_vnum == 0) return;

    BYTE job = bot->GetJob();
    if (job > 3) return;

    int motion = (int)skill_vnum - 30 * (int)job;
    if (motion <= 0 || motion >= 0x7F)
    {
        sys_log(0, "[BOT_VISUAL_SKILL_SKIP] %s vnum=%u job=%u motion_calc=%d",
            bot->GetName(), skill_vnum, job, motion);
        return;
    }

    DWORD now = get_dword_time();

    // v27 arastirici onerisi 3: FUNC_WAIT snap (skill icin de pozisyon hizalama)
    {
        TPacketGCMove wait_pkt;
        EncodeMovePacket(wait_pkt, bot->GetVID(), FUNC_WAIT, 0,
                         bot->GetX(), bot->GetY(), 0, now,
                         (BYTE)((int)bot->GetRotation() / 5));
        BroadcastToMapClients(bot, wait_pkt, 5000);
    }

    // 1) GC::MOVE bFunc=FUNC_SKILL|motion (input_main.cpp:1653 PC pattern)
    BYTE bFunc = (BYTE)(FUNC_SKILL | (motion & 0x7F));
    TPacketGCMove mv;
    EncodeMovePacket(mv, bot->GetVID(), bFunc, (BYTE)motion,
                     bot->GetX(), bot->GetY(), 0, now,
                     (BYTE)((int)bot->GetRotation() / 5));
    BroadcastToMapClients(bot, mv, 5000);

    // 2) GC::MOTION SPECIAL (motion 1..5 -> SPECIAL_1..5)
    int special_offset = (motion - 1) % 6;
    if (special_offset < 5)
    {
        struct packet_motion mo;
        mo.header     = GC::MOTION;
        mo.length     = sizeof(mo);
        mo.vid        = bot->GetVID();
        mo.victim_vid = 0;
        mo.motion     = (uint16_t)(MOTION_SPECIAL_1 + special_offset);
        BroadcastToMapClients(bot, mo, 5000);
    }

    sys_log(0, "[BOT_VISUAL_SKILL] %s vnum=%u job=%u motion=%d bFunc=0x%02X (manual broadcast)",
        bot->GetName(), skill_vnum, job, motion, bFunc);
}

// ============================================================================
// BotEmitAffectAdd — Phase VII A2 ISSUE-006
// Bot'ta GetDesc()==NULL oldugu icin SendAffectAddPacket calismaz.
// Bu fonksiyon TPacketGCAffectAdd'i BroadcastToMapClients ile cevre PC'lere gonderir.
// Boylece buff aura (Hava Kilici, Geomkyung, vb.) yan client'larda render edilir.
// ============================================================================
void BotEmitAffectAdd(LPCHARACTER bot, const CAffect* pAff)
{
    if (!bot || !pAff) return;

    TPacketGCAffectAdd ptoc;
    ptoc.header            = GC::AFFECT_ADD;
    ptoc.length            = sizeof(ptoc);
    ptoc.elem.dwType       = pAff->dwType;
    ptoc.elem.bApplyOn     = pAff->bApplyOn;
    ptoc.elem.lApplyValue  = pAff->lApplyValue;
    ptoc.elem.dwFlag       = pAff->dwFlag;
    ptoc.elem.lDuration    = pAff->lDuration;
    ptoc.elem.lSPCost      = pAff->lSPCost;

    BroadcastToMapClients(bot, ptoc, 5000);

    sys_log(0, "[BOT_VISUAL_AFFECT] %s affect_type=%u apply_on=%u value=%ld flag=%u (broadcast)",
        bot->GetName(), pAff->dwType, (unsigned)pAff->bApplyOn,
        pAff->lApplyValue, pAff->dwFlag);
}
