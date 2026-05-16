// ============================================================================
// bot_logger.cpp — v30 Master CSV log impl
// ============================================================================
#include "stdafx.h"
#include "bot_logger.h"
#include "char.h"
#include "log.h"
#include <cstdio>
#include <ctime>
#include <sys/stat.h>
#include <string>
#ifdef _WIN32
#  include <direct.h>  // _mkdir
#  define bot_mkdir(p) _mkdir(p)
// localtime_r is POSIX; use localtime_s on Windows
static inline void bot_localtime(const time_t* t, struct tm* out)
{
    localtime_s(out, t);
}
#else
#  define bot_mkdir(p) mkdir((p), 0755)
static inline void bot_localtime(const time_t* t, struct tm* out)
{
    localtime_r(t, out);
}
#endif

namespace
{
    const char* LOG_DIR = "../../../logs/bot";
    // Sade format: 2026-05-15 (her gun yeni dosya)
    std::string GetTodayPath()
    {
        time_t t = time(nullptr);
        struct tm tm_buf;
        bot_localtime(&t, &tm_buf);
        char buf[256];
        snprintf(buf, sizeof(buf), "%s/bot-master-%04d-%02d-%02d.csv",
            LOG_DIR, tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday);
        return std::string(buf);
    }

    // CSV escape: virgul ve double quote
    std::string CsvEscape(const char* s)
    {
        if (!s) return "";
        std::string out;
        bool needQuote = false;
        for (const char* p = s; *p; ++p)
        {
            if (*p == ',' || *p == '"' || *p == '\n') { needQuote = true; break; }
        }
        if (!needQuote) return s;
        out = "\"";
        for (const char* p = s; *p; ++p)
        {
            if (*p == '"') out += "\"\"";
            else out += *p;
        }
        out += "\"";
        return out;
    }

    void EnsureDir()
    {
        static bool s_dir_ready = false;
        if (s_dir_ready) return;
        // Recursive mkdir (parent dizinler eksik olabilir)
        bot_mkdir("../../../logs");
        bot_mkdir(LOG_DIR);
        s_dir_ready = true;
    }

    // Master writer
    void WriteCsv(const char* event_type, const char* bot, const char* player,
                  const char* action, DWORD tvid, const char* tname,
                  int dam, int hp_b, int hp_a, long px, long py,
                  DWORD skill, DWORD weapon, const char* extra)
    {
        EnsureDir();
        FILE* fp = fopen(GetTodayPath().c_str(), "a");
        if (!fp) return;

        // Timestamp ISO 8601
        time_t t = time(nullptr);
        struct tm tm_buf;
        bot_localtime(&t, &tm_buf);
        char ts[32];
        strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &tm_buf);

        fprintf(fp, "%s,%s,%s,%s,%s,%u,%s,%d,%d,%d,%ld,%ld,%u,%u,%s\n",
            ts,
            event_type,
            CsvEscape(bot).c_str(),
            CsvEscape(player).c_str(),
            CsvEscape(action).c_str(),
            tvid,
            CsvEscape(tname).c_str(),
            dam, hp_b, hp_a,
            px, py,
            skill, weapon,
            CsvEscape(extra).c_str());
        fclose(fp);
    }
}

void BotLog_Init()
{
    EnsureDir();
    // Header (her gun ilk yazimda yazilir - mevcut dosyada yoksa)
    std::string path = GetTodayPath();
    struct stat st;
    if (stat(path.c_str(), &st) == 0 && st.st_size > 0) return;  // mevcut log var

    FILE* fp = fopen(path.c_str(), "w");
    if (!fp) {
        sys_err("BotLog_Init: cannot create %s", path.c_str());
        return;
    }
    fprintf(fp,
        "timestamp,event_type,bot_name,player_name,action,target_vid,target_name,"
        "damage,hp_before,hp_after,pos_x,pos_y,skill_vnum,weapon_vnum,extra\n");
    fclose(fp);
    sys_log(0, "BotLog_Init: created %s", path.c_str());
}

void BotLog_Combat(LPCHARACTER attacker, LPCHARACTER victim,
                   int damage, int hp_before, int hp_after,
                   const char* dam_type, DWORD skill_vnum, DWORD weapon_vnum)
{
    if (!attacker || !victim) return;
    WriteCsv("COMBAT",
        attacker->GetName(), "",
        dam_type ? dam_type : "MELEE",
        (DWORD)victim->GetVID(), victim->GetName(),
        damage, hp_before, hp_after,
        attacker->GetX(), attacker->GetY(),
        skill_vnum, weapon_vnum,
        "");
}

void BotLog_Move(LPCHARACTER bot, const char* move_type,
                 long from_x, long from_y, long to_x, long to_y)
{
    if (!bot) return;
    char extra[64];
    snprintf(extra, sizeof(extra), "from=%ld,%ld", from_x, from_y);
    WriteCsv("MOVE",
        bot->GetName(), "",
        move_type ? move_type : "?",
        0, "",
        0, 0, 0,
        to_x, to_y,
        0, 0,
        extra);
}

void BotLog_Lifecycle(LPCHARACTER bot, const char* event,
                      const char* killer, int extra_value)
{
    if (!bot) return;
    char extra[64];
    snprintf(extra, sizeof(extra), "killer=%s val=%d",
        killer ? killer : "", extra_value);
    WriteCsv("LIFECYCLE",
        bot->GetName(), "",
        event ? event : "?",
        0, killer ? killer : "",
        0, bot->GetHP(), 0,
        bot->GetX(), bot->GetY(),
        0, 0,
        extra);
}

void BotLog_Inventory(LPCHARACTER bot, const char* event,
                      DWORD item_vnum, int slot, int count)
{
    if (!bot) return;
    char extra[64];
    snprintf(extra, sizeof(extra), "slot=%d count=%d", slot, count);
    WriteCsv("INVENTORY",
        bot->GetName(), "",
        event ? event : "?",
        0, "",
        0, 0, 0,
        bot->GetX(), bot->GetY(),
        0, item_vnum,
        extra);
}

void BotLog_PlayerAction(LPCHARACTER pc, const char* action,
                         LPCHARACTER target, int damage)
{
    if (!pc) return;
    DWORD tvid = target ? (DWORD)target->GetVID() : 0;
    const char* tname = target ? target->GetName() : "";
    const char* victim_type = "";
    if (target) {
        if (target->IsServerSideBot()) victim_type = "victim_type=BOT";
        else if (target->IsPC()) victim_type = "victim_type=PC";
        else if (target->IsMonster()) victim_type = "victim_type=MOB";
        else if (target->IsStone()) victim_type = "victim_type=STONE";
    }
    WriteCsv("PLAYER",
        "", pc->GetName(),
        action ? action : "?",
        tvid, tname,
        damage, target ? target->GetMaxHP() : 0, target ? target->GetHP() : 0,
        pc->GetX(), pc->GetY(),
        0, 0,
        victim_type);
}

void BotLog_AI(LPCHARACTER bot, const char* selected_action, float selected_score,
               const char* runner_up_action, float runner_up_score)
{
    if (!bot) return;
    char extra[128];
    snprintf(extra, sizeof(extra), "score=%.2f runner_up=%s:%.2f",
        selected_score,
        runner_up_action ? runner_up_action : "",
        runner_up_score);
    WriteCsv("AI_DECISION",
        bot->GetName(), "",
        selected_action ? selected_action : "?",
        0, "",
        0, bot->GetHP(), 0,
        bot->GetX(), bot->GetY(),
        0, 0,
        extra);
}
