// cmd_switchbot.cpp -- /switchbot command implementation.
//
// Usage:
//   /switchbot on              -- enable bot mode for self
//   /switchbot off             -- disable bot mode for self
//   /switchbot scenario <name> -- force scenario (FARM/QUEST/DUNGEON/PVP/MARKET/IDLE)
//   /switchbot status          -- print current bot status
//
// Requires GM_LOW_WIZARD or higher (see cmd.cpp table entry).
// NO anti-detection / NO evasion.

#include "stdafx.h"
#include "cmd.h"
#include "char.h"
#include "utils.h"
#include "switchbot.h"

ACMD(do_switchbot)
{
    char arg1[64] = {};
    char arg2[64] = {};
    two_arguments(argument, arg1, sizeof(arg1), arg2, sizeof(arg2));

    if (!*arg1)
    {
        ch->ChatPacket(CHAT_TYPE_INFO,
            "Usage: /switchbot [on|off|scenario <name>|status]");
        return;
    }

    if (0 == strcasecmp(arg1, "on"))
    {
        CSwitchbotManager::instance().Toggle(ch, true);
    }
    else if (0 == strcasecmp(arg1, "off"))
    {
        CSwitchbotManager::instance().Toggle(ch, false);
    }
    else if (0 == strcasecmp(arg1, "scenario"))
    {
        if (!*arg2)
        {
            ch->ChatPacket(CHAT_TYPE_INFO,
                "Usage: /switchbot scenario <FARM|QUEST|DUNGEON|PVP|MARKET|IDLE>");
            return;
        }
        EBotScenario sc = BotScenarioFromName(arg2);
        CSwitchbotManager::instance().ForceScenario(ch, sc);
    }
    else if (0 == strcasecmp(arg1, "status"))
    {
        const std::string s = CSwitchbotManager::instance().StatusString(ch);
        ch->ChatPacket(CHAT_TYPE_INFO, "%s", s.c_str());
    }
    else
    {
        ch->ChatPacket(CHAT_TYPE_INFO,
            "Unknown subcommand. Use: on | off | scenario <name> | status");
    }
}
