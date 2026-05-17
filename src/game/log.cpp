#include "stdafx.h"
#include "constants.h"
#include "config.h"
#include "log.h"

#include "char.h"
#include "desc.h"
#include "item.h"
#include "locale_service.h"

static char	__escape_hint[1024];

LogManager::LogManager() : m_bIsConnect(false)
{
}

LogManager::~LogManager()
{
}

bool LogManager::Connect(const char * host, const int port, const char * user, const char * pwd, const char * db)
{
	if (m_sql.Setup(host, user, pwd, db, g_stLocale.c_str(), false, port))
		m_bIsConnect = true;

	return m_bIsConnect;
}

void LogManager::Query(const char * c_pszFormat, ...)
{
	char szQuery[4096];
	va_list args;

	va_start(args, c_pszFormat);
	vsnprintf(szQuery, sizeof(szQuery), c_pszFormat, args);
	va_end(args);

	if (test_server)
		sys_log(0, "LOG: %s", szQuery);

	m_sql.AsyncQuery(szQuery);
}

bool LogManager::IsConnected()
{
	return m_bIsConnect;
}

void LogManager::ItemLog(DWORD dwPID, DWORD x, DWORD y, DWORD dwItemID, const char * c_pszText, const char * c_pszHint, const char * c_pszIP, DWORD dwVnum)
{
	// M7.4 SQL escape — c_pszText ve c_pszIP escape edilmemisti
	char __escape_text[256], __escape_item_ip[64];
	m_sql.EscapeString(__escape_hint,    sizeof(__escape_hint),    c_pszHint, strlen(c_pszHint));
	m_sql.EscapeString(__escape_text,    sizeof(__escape_text),    c_pszText, strlen(c_pszText));
	m_sql.EscapeString(__escape_item_ip, sizeof(__escape_item_ip), c_pszIP,   strlen(c_pszIP));

	Query("INSERT DELAYED INTO log%s (type, time, who, x, y, what, how, hint, ip, vnum) VALUES('ITEM', NOW(), %u, %u, %u, %u, '%s', '%s', '%s', %u)",
			get_table_postfix(), dwPID, x, y, dwItemID, __escape_text, __escape_hint, __escape_item_ip, dwVnum);
}

void LogManager::ItemLog(LPCHARACTER ch, LPITEM item, const char * c_pszText, const char * c_pszHint)
{
	if (NULL == ch || NULL == item)
	{
		sys_err("character or item nil (ch %p item %p text %s)", get_pointer(ch), get_pointer(item), c_pszText);
		return;
	}

	ItemLog(ch->GetPlayerID(), ch->GetX(), ch->GetY(), item->GetID(),
			NULL == c_pszText ? "" : c_pszText,
		   	c_pszHint, ch->GetDesc() ? ch->GetDesc()->GetHostName() : "",
		   	item->GetOriginalVnum());
}

void LogManager::ItemLog(LPCHARACTER ch, int itemID, int itemVnum, const char * c_pszText, const char * c_pszHint)
{
	ItemLog(ch->GetPlayerID(), ch->GetX(), ch->GetY(), itemID, c_pszText, c_pszHint, ch->GetDesc() ? ch->GetDesc()->GetHostName() : "", itemVnum);
}

void LogManager::CharLog(DWORD dwPID, DWORD x, DWORD y, DWORD dwValue, const char * c_pszText, const char * c_pszHint, const char * c_pszIP)
{
	m_sql.EscapeString(__escape_hint, sizeof(__escape_hint), c_pszHint, strlen(c_pszHint));

	Query("INSERT DELAYED INTO log%s (type, time, who, x, y, what, how, hint, ip) VALUES('CHARACTER', NOW(), %u, %u, %u, %u, '%s', '%s', '%s')",
			get_table_postfix(), dwPID, x, y, dwValue, c_pszText, __escape_hint, c_pszIP);
}

void LogManager::CharLog(LPCHARACTER ch, DWORD dw, const char * c_pszText, const char * c_pszHint)
{
	if (ch)
		CharLog(ch->GetPlayerID(), ch->GetX(), ch->GetY(), dw, c_pszText, c_pszHint, ch->GetDesc() ? ch->GetDesc()->GetHostName() : "");
	else
		CharLog(0, 0, 0, dw, c_pszText, c_pszHint, "");
}

void LogManager::LoginLog(bool isLogin, DWORD dwAccountID, DWORD dwPID, BYTE bLevel, BYTE bJob, DWORD dwPlayTime)
{
	Query("INSERT DELAYED INTO loginlog%s (type, time, channel, account_id, pid, level, job, playtime) VALUES (%s, NOW(), %d, %u, %u, %d, %d, %u)",
			get_table_postfix(), isLogin ? "'LOGIN'" : "'LOGOUT'", g_bChannel, dwAccountID, dwPID, bLevel, bJob, dwPlayTime);
}

void LogManager::MoneyLog(BYTE type, DWORD vnum, int gold)
{
	if (type == MONEY_LOG_RESERVED || type >= MONEY_LOG_TYPE_MAX_NUM)
	{
		sys_err("TYPE ERROR: type %d vnum %u gold %d", type, vnum, gold);
		return;
	}

	Query("INSERT DELAYED INTO money_log%s VALUES (NOW(), %d, %d, %d)", get_table_postfix(), type, vnum, gold);
}

void LogManager::HackLog(const char * c_pszHackName, const char * c_pszLogin, const char * c_pszName, const char * c_pszIP)
{
	// M7.2 SQL escape — login/name/ip kullanici girisinden gelebilir
	char __escape_hack[1024], __escape_login[256], __escape_name[256], __escape_ip[64];
	m_sql.EscapeString(__escape_hack,  sizeof(__escape_hack),  c_pszHackName, strlen(c_pszHackName));
	m_sql.EscapeString(__escape_login, sizeof(__escape_login), c_pszLogin,    strlen(c_pszLogin));
	m_sql.EscapeString(__escape_name,  sizeof(__escape_name),  c_pszName,     strlen(c_pszName));
	m_sql.EscapeString(__escape_ip,    sizeof(__escape_ip),    c_pszIP,       strlen(c_pszIP));

	Query("INSERT INTO hack_log (time, login, name, ip, server, why) VALUES(NOW(), '%s', '%s', '%s', '%s', '%s')",
		  __escape_login, __escape_name, __escape_ip, g_stHostname.c_str(), __escape_hack);
}

void LogManager::HackLog(const char * c_pszHackName, LPCHARACTER ch)
{
	if (ch->GetDesc())
	{
		HackLog(c_pszHackName, 
				ch->GetDesc()->GetAccountTable().login,
				ch->GetName(),
				ch->GetDesc()->GetHostName());
	}
}

void LogManager::HackCRCLog(const char * c_pszHackName, const char * c_pszLogin, const char * c_pszName, const char * c_pszIP, DWORD dwCRC)
{
	// M7.2 SQL escape — tum string parametreleri escape edilmemisti
	char __escape_hack[1024], __escape_login[256], __escape_name[256], __escape_ip[64];
	m_sql.EscapeString(__escape_hack,  sizeof(__escape_hack),  c_pszHackName, strlen(c_pszHackName));
	m_sql.EscapeString(__escape_login, sizeof(__escape_login), c_pszLogin,    strlen(c_pszLogin));
	m_sql.EscapeString(__escape_name,  sizeof(__escape_name),  c_pszName,     strlen(c_pszName));
	m_sql.EscapeString(__escape_ip,    sizeof(__escape_ip),    c_pszIP,       strlen(c_pszIP));

	Query("INSERT INTO hack_crc_log (time, login, name, ip, server, why, crc) VALUES(NOW(), '%s', '%s', '%s', '%s', '%s', %u)",
		  __escape_login, __escape_name, __escape_ip, g_stHostname.c_str(), __escape_hack, dwCRC);
}

void LogManager::GoldBarLog(DWORD dwPID, DWORD dwItemID, GOLDBAR_HOW eHow, const char* c_pszHint)
{
	char szHow[32+1];
	
	switch (eHow)
	{
		case PERSONAL_SHOP_BUY:
			snprintf(szHow, sizeof(szHow), "'BUY'");
			break;
			
		case PERSONAL_SHOP_SELL:
			snprintf(szHow, sizeof(szHow), "'SELL'");
			break;
			
		case SHOP_BUY:
			snprintf(szHow, sizeof(szHow), "'SHOP_BUY'");
			break;
			
		case SHOP_SELL:
			snprintf(szHow, sizeof(szHow), "'SHOP_SELL'");
			break;
			
		case EXCHANGE_TAKE:
			snprintf(szHow, sizeof(szHow), "'EXCHANGE_TAKE'");
			break;
			
		case EXCHANGE_GIVE:
			snprintf(szHow, sizeof(szHow), "'EXCHANGE_GIVE'");
			break;

		case QUEST:
			snprintf(szHow, sizeof(szHow), "'QUEST'");
			break;

		default:
			snprintf(szHow, sizeof(szHow), "''");
			break;
	}
	
	Query("INSERT DELAYED INTO goldlog%s (date, time, pid, what, how, hint) VALUES(CURDATE(), CURTIME(), %u, %u, %s, '%s')", 
			get_table_postfix(), dwPID, dwItemID, szHow, c_pszHint);
}

void LogManager::CubeLog(DWORD dwPID, DWORD x, DWORD y, DWORD item_vnum, DWORD item_uid, int item_count, bool success)
{
	Query("INSERT DELAYED INTO cube%s (pid, time, x, y, item_vnum, item_uid, item_count, success) "
			"VALUES(%u, NOW(), %u, %u, %u, %u, %d, %d)",
			get_table_postfix(), dwPID, x, y, item_vnum, item_uid, item_count, success?1:0);
}

void LogManager::SpeedHackLog(DWORD pid, DWORD x, DWORD y, int hack_count)
{
	Query("INSERT INTO speed_hack%s (pid, time, x, y, hack_count) "
			"VALUES(%u, NOW(), %u, %u, %d)",
			get_table_postfix(), pid, x, y, hack_count);
}

void LogManager::ChangeNameLog(DWORD pid, const char *old_name, const char *new_name, const char *ip)
{
	// M7.3 SQL escape — old_name/new_name/ip kullanici girisinden gelebilir
	char __escape_old_name[256], __escape_new_name[256], __escape_ip[64];
	m_sql.EscapeString(__escape_old_name, sizeof(__escape_old_name), old_name, strlen(old_name));
	m_sql.EscapeString(__escape_new_name, sizeof(__escape_new_name), new_name, strlen(new_name));
	m_sql.EscapeString(__escape_ip,       sizeof(__escape_ip),       ip,       strlen(ip));

	Query("INSERT DELAYED INTO change_name%s (pid, old_name, new_name, time, ip) "
			"VALUES(%u, '%s', '%s', NOW(), '%s') ",
			get_table_postfix(), pid, __escape_old_name, __escape_new_name, __escape_ip);
}

void LogManager::GMCommandLog(DWORD dwPID, const char* szName, const char* szIP, BYTE byChannel, const char* szCommand)
{
	// M7.4 SQL escape — szName ve szIP escape edilmemisti
	char __escape_gm_name[256], __escape_gm_ip[64];
	m_sql.EscapeString(__escape_hint,    sizeof(__escape_hint),    szCommand, strlen(szCommand));
	m_sql.EscapeString(__escape_gm_name, sizeof(__escape_gm_name), szName,    strlen(szName));
	m_sql.EscapeString(__escape_gm_ip,   sizeof(__escape_gm_ip),   szIP,      strlen(szIP));

	Query("INSERT DELAYED INTO command_log%s (userid, server, ip, port, username, command, date ) "
			"VALUES(%u, 999, '%s', %u, '%s', '%s', NOW()) ",
			get_table_postfix(), dwPID, __escape_gm_ip, byChannel, __escape_gm_name, __escape_hint);
}

void LogManager::RefineLog(DWORD pid, const char* item_name, DWORD item_id, int item_refine_level, int is_success, const char* how)
{
	// M7.4 SQL escape — item_name escape vardi ama how (setType) escape edilmemisti
	char __escape_refine_how[128];
	m_sql.EscapeString(__escape_hint,       sizeof(__escape_hint),       item_name, strlen(item_name));
	m_sql.EscapeString(__escape_refine_how, sizeof(__escape_refine_how), how,       strlen(how));

	Query("INSERT INTO refinelog%s (pid, item_name, item_id, step, time, is_success, setType) VALUES(%u, '%s', %u, %d, NOW(), %d, '%s')",
			get_table_postfix(), pid, __escape_hint, item_id, item_refine_level, is_success, __escape_refine_how);
}


void LogManager::ShoutLog(BYTE bChannel, BYTE bEmpire, const char * pszText)
{
	m_sql.EscapeString(__escape_hint, sizeof(__escape_hint), pszText, strlen(pszText));

	Query("INSERT INTO shout_log%s VALUES(NOW(), %d, %d,'%s')", get_table_postfix(), bChannel, bEmpire, __escape_hint);
}

void LogManager::LevelLog(LPCHARACTER pChar, unsigned int level, unsigned int playhour)
{
	if (true == LC_IsEurope())
	{
		DWORD aid = 0;

		if (NULL != pChar->GetDesc())
		{
			aid = pChar->GetDesc()->GetAccountTable().id;
		}

		Query("REPLACE INTO levellog%s (name, level, time, account_id, pid, playtime) VALUES('%s', %u, NOW(), %u, %u, %d)",
				get_table_postfix(), pChar->GetName(), level, aid, pChar->GetPlayerID(), playhour);
	}
	else
	{
		Query("REPLACE INTO levellog%s (name, level, time, playtime) VALUES('%s', %u, NOW(), %d)",
				get_table_postfix(), pChar->GetName(), level, playhour);
	}
}

void LogManager::BootLog(const char * c_pszHostName, BYTE bChannel)
{
	Query("INSERT INTO bootlog (time, hostname, channel) VALUES(NOW(), '%s', %d)",
			c_pszHostName, bChannel);
}

void LogManager::FishLog(DWORD dwPID, int prob_idx, int fish_id, int fish_level, DWORD dwMiliseconds, DWORD dwVnum, DWORD dwValue)
{
	Query("INSERT INTO fish_log%s VALUES(NOW(), %u, %d, %u, %d, %u, %u, %u)",
			get_table_postfix(),
			dwPID,
			prob_idx,
			fish_id,
			fish_level,
			dwMiliseconds,
			dwVnum,
			dwValue);
}

void LogManager::QuestRewardLog(const char * c_pszQuestName, DWORD dwPID, DWORD dwLevel, int iValue1, int iValue2)
{
	Query("INSERT INTO quest_reward_log%s VALUES('%s',%u,%u,2,%u,%u,NOW())", 
			get_table_postfix(), 
			c_pszQuestName,
			dwPID,
			dwLevel,
			iValue1, 
			iValue2);
}

void LogManager::DetailLoginLog(bool isLogin, LPCHARACTER ch)
{
	if (NULL == ch->GetDesc())
		return;

	if (true == isLogin)
	{
		Query("INSERT INTO loginlog2(type, is_gm, login_time, channel, account_id, pid, ip, client_version) "
				"VALUES('INVALID', %s, NOW(), %d, %u, %u, inet_aton('%s'), '%s')",
				ch->IsGM() ? "'Y'" : "'N'",
				g_bChannel,
				ch->GetDesc()->GetAccountTable().id,
				ch->GetPlayerID(),
				ch->GetDesc()->GetHostName(),
				ch->GetDesc()->GetClientVersion());
	}
	else
	{
		Query("SET @i = (SELECT MAX(id) FROM loginlog2 WHERE account_id=%u AND pid=%u)",
				ch->GetDesc()->GetAccountTable().id,
				ch->GetPlayerID());

		Query("UPDATE loginlog2 SET type='VALID', logout_time=NOW(), playtime=TIMEDIFF(logout_time,login_time) WHERE id=@i");
	}
}

void LogManager::DragonSlayLog(DWORD dwGuildID, DWORD dwDragonVnum, DWORD dwStartTime, DWORD dwEndTime)
{
	Query( "INSERT INTO dragon_slay_log%s VALUES( %d, %d, FROM_UNIXTIME(%d), FROM_UNIXTIME(%d) )",
			get_table_postfix(),
			dwGuildID, dwDragonVnum, dwStartTime, dwEndTime);
}

// switchbot dev log ─ pattern mirrors FishLog above.
// Inserts into log.log_switchbot_dev; dogrulayici SQL-imports the table first.
void LogManager::SwitchbotLog(DWORD dwPID, const char* name,
	BYTE channel, int map_index,
	const char* scenario, const char* event_type,
	const char* error_detail,
	int last_x, int last_y,
	uint32_t duration_sec,
	uint32_t kills, uint32_t encounters,
	uint32_t stuck_cnt, uint32_t err_cnt)
{
	Query("INSERT INTO log_switchbot_dev "
		"(pid,name,channel,map_index,scenario,event_type,error_detail,"
		"last_x,last_y,duration_sec,"
		"summary_kills,summary_enc,summary_stuck,summary_err,flushed_at) "
		"VALUES(%u,'%s',%d,%d,'%s','%s','%s',%d,%d,%u,%u,%u,%u,%u,NOW())",
		dwPID, name ? name : "",
		(int)channel, map_index,
		scenario     ? scenario     : "",
		event_type   ? event_type   : "",
		error_detail ? error_detail : "",
		last_x, last_y, duration_sec,
		kills, encounters, stuck_cnt, err_cnt);
}

