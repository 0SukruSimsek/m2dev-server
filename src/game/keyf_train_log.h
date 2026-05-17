// ============================================================================
// keyf_train_log.h - Keyf (player_id=24) training log macro
// ----------------------------------------------------------------------------
// Bot AI training data collection icin oyuncu eylemlerini ms hassasiyetinde
// loglar. Sadece Keyf icin tetiklenir (PID guard runtime).
//
// Kullanim:
//   KEYF_LOG(ch, "MOVE_START", "from=(%d,%d) to=(%d,%d)", x1, y1, x2, y2);
//
// Output:
//   syslog.log -> [KEYF_TRAIN] EVENT=MOVE_START from=(...) to=(...)
//
// Bu satirlari keyf-collector.sh daemon yakalar, MD dosyasina yazar.
// Keyf-disi karakterler icin: 0 etki (early return).
// ============================================================================

#pragma once

#include "../libthecore/log.h"

// Keyf karakter PID (player.player tablosunda)
#define KEYF_TRAIN_PID 24

// Forward declaration — char.h yerine yalin kullanim icin
class CHARACTER;

// LPCHARACTER ozel cagri: GetPlayerID() inline cagrilir
#define KEYF_LOG(ch, event, fmt, ...) \
    do { \
        if ((ch) && (ch)->GetPlayerID() == KEYF_TRAIN_PID) { \
            sys_log(0, "[KEYF_TRAIN] EVENT=" event " " fmt, ##__VA_ARGS__); \
        } \
    } while(0)

// PID direkt verme varyanti (CHARACTER pointer yoksa)
#define KEYF_LOG_PID(pid, event, fmt, ...) \
    do { \
        if ((pid) == KEYF_TRAIN_PID) { \
            sys_log(0, "[KEYF_TRAIN] EVENT=" event " " fmt, ##__VA_ARGS__); \
        } \
    } while(0)
