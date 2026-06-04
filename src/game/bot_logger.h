#pragma once
// bot_logger.h — Master CSV log declarations (v30)
// Generated from bot_logger.cpp function signatures

class CHARACTER;
typedef CHARACTER* LPCHARACTER;

void BotLog_Init();
void BotLog_Combat(LPCHARACTER attacker, LPCHARACTER victim,
                   int damage, int hp_before, int hp_after,
                   const char* dam_type, DWORD skill_vnum, DWORD weapon_vnum);
void BotLog_Move(LPCHARACTER bot, const char* move_type,
                 long from_x, long from_y, long to_x, long to_y);
void BotLog_Lifecycle(LPCHARACTER bot, const char* event,
                      const char* killer, int extra_value);
void BotLog_Inventory(LPCHARACTER bot, const char* event,
                      DWORD item_vnum, int slot, int count);
void BotLog_PlayerAction(LPCHARACTER pc, const char* action,
                         LPCHARACTER target, int damage);
void BotLog_AI(LPCHARACTER bot, const char* selected_action, float selected_score,
               const char* runner_up_action, float runner_up_score);
