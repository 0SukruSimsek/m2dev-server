#include "stdafx.h"
#ifdef ENABLE_ANTI_MULTIPLE_FARM
#include "HAntiMultipleFarm.h"
#endif
#include "constants.h"
#include "config.h"
#include "utils.h"
#include "input.h"
#include "desc_client.h"
#include "desc_manager.h"
#include "char.h"
#include "char_manager.h"
#include "cmd.h"
#include "buffer_manager.h"
#include "protocol.h"
#include "pvp.h"
#include "start_position.h"
#include "messenger_manager.h"
#include "guild_manager.h"
#include "party.h"
#include "dungeon.h"
#include "war_map.h"
#include "questmanager.h"
#include "building.h"
#include "wedding.h"
#include "affect.h"
#include "arena.h"
#include "OXEvent.h"
#include "priv_manager.h"
#include "log.h"
#include "horsename_manager.h"
#include "MarkManager.h"
#include "p2p.h"

static void _send_bonus_info(LPCHARACTER ch)
{
	int	item_drop_bonus = 0;
	int gold_drop_bonus = 0;
	int gold10_drop_bonus	= 0;
	int exp_bonus		= 0;

	item_drop_bonus		= CPrivManager::instance().GetPriv(ch, PRIV_ITEM_DROP);
	gold_drop_bonus		= CPrivManager::instance().GetPriv(ch, PRIV_GOLD_DROP);
	gold10_drop_bonus	= CPrivManager::instance().GetPriv(ch, PRIV_GOLD10_DROP);
	exp_bonus			= CPrivManager::instance().GetPriv(ch, PRIV_EXP_PCT);

	if (item_drop_bonus)
	{
		ch->ChatPacket(CHAT_TYPE_NOTICE, 
				LC_TEXT("ì•„ì´í…œ ë“œë¡­ë¥   %d%% ì¶”ê°€ ì´ë²¤íŠ¸ ì¤‘ì…ë‹ˆë‹¤."), item_drop_bonus);
	}
	if (gold_drop_bonus)
	{
		ch->ChatPacket(CHAT_TYPE_NOTICE, 
				LC_TEXT("ê³¨ë“œ ë“œë¡­ë¥  %d%% ì¶”ê°€ ì´ë²¤íŠ¸ ì¤‘ì…ë‹ˆë‹¤."), gold_drop_bonus);
	}
	if (gold10_drop_bonus)
	{
		ch->ChatPacket(CHAT_TYPE_NOTICE, 
				LC_TEXT("ëŒ€ë°•ê³¨ë“œ ë“œë¡­ë¥  %d%% ì¶”ê°€ ì´ë²¤íŠ¸ ì¤‘ì…ë‹ˆë‹¤."), gold10_drop_bonus);
	}
	if (exp_bonus)
	{
		ch->ChatPacket(CHAT_TYPE_NOTICE, 
				LC_TEXT("ê²½í—˜ì¹˜ %d%% ì¶”ê°€ íšë“ ì´ë²¤íŠ¸ ì¤‘ì…ë‹ˆë‹¤."), exp_bonus);
	}
}

static bool FN_is_battle_zone(LPCHARACTER ch)
{
	switch (ch->GetMapIndex())
	{
		case 1:         // ì‹ ìˆ˜ 1ì°¨ ë§ˆì„
		case 2:         // ì‹ ìˆ˜ 2ì°¨ ë§ˆì„
		case 21:        // ì²œì¡° 1ì°¨ ë§ˆì„
		case 23:        // ì²œì¡° 2ì°¨ ë§ˆì„
		case 41:        // ì§„ë…¸ 1ì°¨ ë§ˆì„
		case 43:        // ì§„ë…¸ 2ì°¨ ë§ˆì„
		case 113:       // OX ë§µ
			return false;
	}

	return true;
}

void CInputLogin::LoginByKey(LPDESC d, const char * data)
{
	TPacketCGLogin2 * pinfo = (TPacketCGLogin2 *) data;

	char login[LOGIN_MAX_LEN + 1];
	trim_and_lower(pinfo->login, login, sizeof(login));

	if (g_bNoMoreClient)
	{
		TPacketGCLoginFailure failurePacket;

		failurePacket.header = GC::LOGIN_FAILURE;
		failurePacket.length = sizeof(failurePacket);
		strlcpy(failurePacket.szStatus, "SHUTDOWN", sizeof(failurePacket.szStatus));
		d->Packet(&failurePacket, sizeof(TPacketGCLoginFailure));
		return;
	}

	if (g_iUserLimit > 0)
	{
		int iTotal;
		int * paiEmpireUserCount;
		int iLocal;

		DESC_MANAGER::instance().GetUserCount(iTotal, &paiEmpireUserCount, iLocal);

		if (g_iUserLimit <= iTotal)
		{
			TPacketGCLoginFailure failurePacket;

			failurePacket.header = GC::LOGIN_FAILURE;
			failurePacket.length = sizeof(failurePacket);
			strlcpy(failurePacket.szStatus, "FULL", sizeof(failurePacket.szStatus));

			d->Packet(&failurePacket, sizeof(TPacketGCLoginFailure));
			return;
		}
	}

	sys_log(0, "LOGIN_BY_KEY: %s key %u", login, pinfo->dwLoginKey);

	d->SetLoginKey(pinfo->dwLoginKey);

#ifdef ENABLE_ANTI_MULTIPLE_FARM
	d->SetLoginMacAdress(pinfo->cMAIf);
#endif

	TPacketGDLoginByKey ptod;

	strlcpy(ptod.szLogin, login, sizeof(ptod.szLogin));
	ptod.dwLoginKey = pinfo->dwLoginKey;
	strlcpy(ptod.szIP, d->GetHostName(), sizeof(ptod.szIP));

	db_clientdesc->DBPacket(GD::LOGIN_BY_KEY, d->GetHandle(), &ptod, sizeof(TPacketGDLoginByKey));
}

void CInputLogin::ChangeName(LPDESC d, const char * data)
{
	TPacketCGChangeName * p = (TPacketCGChangeName *) data;
	const TAccountTable & c_r = d->GetAccountTable();

	if (!c_r.id)
	{
		sys_err("no account table");
		return;
	}

	if (p->index < 0 || p->index >= PLAYER_PER_ACCOUNT)
	{
		sys_err("index overflow %d, login: %s", p->index, c_r.login);
		return;
	}

	if (!c_r.players[p->index].bChangeName)
		return;

	if (!check_name(p->name))
	{
		TPacketGCCreateFailure pack;
		pack.header = GC::PLAYER_CREATE_FAILURE;
		pack.length = sizeof(pack);
		pack.bType = 0;
		d->Packet(&pack, sizeof(pack));
		return;
	}

	TPacketGDChangeName pdb;

	pdb.pid = c_r.players[p->index].dwID;
	strlcpy(pdb.name, p->name, sizeof(pdb.name));
	db_clientdesc->DBPacket(GD::CHANGE_NAME, d->GetHandle(), &pdb, sizeof(TPacketGDChangeName));
}

void CInputLogin::CharacterSelect(LPDESC d, const char * data)
{
	struct command_player_select * pinfo = (struct command_player_select *) data;
	const TAccountTable & c_r = d->GetAccountTable();

	sys_log(0, "player_select: login: %s index: %d", c_r.login, pinfo->index);

	if (!c_r.id)
	{
		sys_err("no account table");
		return;
	}

	if (pinfo->index < 0 || pinfo->index >= PLAYER_PER_ACCOUNT)
	{
		sys_err("index overflow %d, login: %s", pinfo->index, c_r.login);
		return;
	}

	if (c_r.players[pinfo->index].dwID == 0)
	{
		sys_err("player index(%d) is null. login %s", 
				pinfo->index, c_r.login);
		return;
	}

	if (c_r.players[pinfo->index].bChangeName)
	{
		sys_err("name must be changed idx %d, login %s, name %s", 
				pinfo->index, c_r.login, c_r.players[pinfo->index].szName);
		return;
	}

	TPlayerLoadPacket player_load_packet;

	player_load_packet.account_id	= c_r.id;
	player_load_packet.player_id	= c_r.players[pinfo->index].dwID;
	player_load_packet.account_index	= pinfo->index;

	db_clientdesc->DBPacket(GD::PLAYER_LOAD, d->GetHandle(), &player_load_packet, sizeof(TPlayerLoadPacket));
}

bool NewPlayerTable(TPlayerTable * table,
		const char * name,
		BYTE job,
		BYTE shape,
		BYTE bEmpire,
		BYTE bCon,
		BYTE bInt,
		BYTE bStr,
		BYTE bDex)
{
	if (job >= JOB_MAX_NUM)
		return false;

	memset(table, 0, sizeof(TPlayerTable));

	strlcpy(table->name, name, sizeof(table->name));

	table->level = 1;
	table->job = job;
	table->voice = 0;
	table->part_base = shape;
	
	table->st = JobInitialPoints[job].st;
	table->dx = JobInitialPoints[job].dx;
	table->ht = JobInitialPoints[job].ht;
	table->iq = JobInitialPoints[job].iq;

	table->hp = JobInitialPoints[job].max_hp + table->ht * JobInitialPoints[job].hp_per_ht;
	table->sp = JobInitialPoints[job].max_sp + table->iq * JobInitialPoints[job].sp_per_iq;
	table->stamina = JobInitialPoints[job].max_stamina;

	table->x 	= CREATE_START_X(bEmpire) + number(-300, 300);
	table->y 	= CREATE_START_Y(bEmpire) + number(-300, 300);
	table->z	= 0;
	table->dir	= 0;
	table->playtime = 0;
	table->gold 	= 0;

	table->skill_group = 0;

	if (china_event_server)
	{
		table->level = 35;

		for (int i = 1; i < 35; ++i)
		{
			int iHP = number(JobInitialPoints[job].hp_per_lv_begin, JobInitialPoints[job].hp_per_lv_end);
			int iSP = number(JobInitialPoints[job].sp_per_lv_begin, JobInitialPoints[job].sp_per_lv_end);
			table->sRandomHP += iHP;
			table->sRandomSP += iSP;
			table->stat_point += 3;
		}

		table->hp += table->sRandomHP;
		table->sp += table->sRandomSP;

		table->gold = 1000000;
	}

	return true;
}

bool RaceToJob(unsigned race, unsigned* ret_job)
{
	*ret_job = 0;

	if (race >= MAIN_RACE_MAX_NUM)
		return false;

	switch (race)
	{
		case MAIN_RACE_WARRIOR_M:
			*ret_job = JOB_WARRIOR;
			break;

		case MAIN_RACE_WARRIOR_W:
			*ret_job = JOB_WARRIOR;
			break;

		case MAIN_RACE_ASSASSIN_M:
			*ret_job = JOB_ASSASSIN;
			break;

		case MAIN_RACE_ASSASSIN_W:
			*ret_job = JOB_ASSASSIN;
			break;

		case MAIN_RACE_SURA_M:
			*ret_job = JOB_SURA;
			break;

		case MAIN_RACE_SURA_W:
			*ret_job = JOB_SURA;
			break;

		case MAIN_RACE_SHAMAN_M:
			*ret_job = JOB_SHAMAN;
			break;

		case MAIN_RACE_SHAMAN_W:
			*ret_job = JOB_SHAMAN;
			break;

		default:
			return false;
			break;
	}
	return true;
}

// ì‹ ê·œ ìºë¦­í„° ì§€ì›
bool NewPlayerTable2(TPlayerTable * table, const char * name, BYTE race, BYTE shape, BYTE bEmpire)
{
	if (race >= MAIN_RACE_MAX_NUM)
	{
		sys_err("NewPlayerTable2.OUT_OF_RACE_RANGE(%d >= max(%d))\n", race, MAIN_RACE_MAX_NUM);
		return false;
	}

	unsigned job;

	if (!RaceToJob(race, &job))
	{	
		sys_err("NewPlayerTable2.RACE_TO_JOB_ERROR(%d)\n", race);
		return false;
	}

	sys_log(0, "NewPlayerTable2(name=%s, race=%d, job=%d)", name, race, job); 

	memset(table, 0, sizeof(TPlayerTable));

	strlcpy(table->name, name, sizeof(table->name));

	table->level		= 1;
	table->job			= race;	// ì§ì—…ëŒ€ì‹  ì¢…ì¡±ì„ ë„£ëŠ”ë‹¤
	table->voice		= 0;
	table->part_base	= shape;

	table->st		= JobInitialPoints[job].st;
	table->dx		= JobInitialPoints[job].dx;
	table->ht		= JobInitialPoints[job].ht;
	table->iq		= JobInitialPoints[job].iq;

	table->hp		= JobInitialPoints[job].max_hp + table->ht * JobInitialPoints[job].hp_per_ht;
	table->sp		= JobInitialPoints[job].max_sp + table->iq * JobInitialPoints[job].sp_per_iq;
	table->stamina	= JobInitialPoints[job].max_stamina;

	table->x		= CREATE_START_X(bEmpire) + number(-300, 300);
	table->y		= CREATE_START_Y(bEmpire) + number(-300, 300);
	table->z		= 0;
	table->dir		= 0;
	table->playtime = 0;
	table->gold 	= 0;

	table->skill_group = 0;

	return true;
}

void CInputLogin::CharacterCreate(LPDESC d, const char * data)
{
	struct command_player_create * pinfo = (struct command_player_create *) data;
	TPlayerCreatePacket player_create_packet;

	sys_log(0, "PlayerCreate: name %s pos %d job %d shape %d",
			pinfo->name, 
			pinfo->index, 
			pinfo->job, 
			pinfo->shape);

	TPacketGCLoginFailure packFailure;
	memset(&packFailure, 0, sizeof(packFailure));
	packFailure.header = GC::PLAYER_CREATE_FAILURE;
	packFailure.length = sizeof(packFailure);

	if (true == g_BlockCharCreation)
	{
		d->Packet(&packFailure, sizeof(packFailure));
		return;
	}

	if (pinfo->index < 0 || pinfo->index >= PLAYER_PER_ACCOUNT)
	{
		sys_err("index overflow %d, login: %s", pinfo->index, d->GetAccountTable().login);
		d->Packet(&packFailure, sizeof(packFailure));
		return;
	}

	// ì‚¬ìš©í•  ìˆ˜ ì—†ëŠ” ì´ë¦„ì´ê±°ë‚˜, ì˜ëª»ëœ í‰ìƒë³µì´ë©´ ìƒì„¤ ì‹¤íŒ¨
	if (!check_name(pinfo->name) || pinfo->shape > 1)
	{
		if (LC_IsCanada() == true)
		{
			TPacketGCCreateFailure pack;
			pack.header = GC::PLAYER_CREATE_FAILURE;
			pack.length = sizeof(pack);
			pack.bType = 1;

			d->Packet(&pack, sizeof(pack));
			return;
		}

		d->Packet(&packFailure, sizeof(packFailure));
		return;
	}

	if (LC_IsEurope() == true)
	{
		const TAccountTable & c_rAccountTable = d->GetAccountTable();

		if (0 == strcmp(c_rAccountTable.login, pinfo->name))
		{
			TPacketGCCreateFailure pack;
			pack.header = GC::PLAYER_CREATE_FAILURE;
			pack.length = sizeof(pack);
			pack.bType = 1;

			d->Packet(&pack, sizeof(pack));
			return;
		}
	}

	memset(&player_create_packet, 0, sizeof(TPlayerCreatePacket));

	if (!NewPlayerTable2(&player_create_packet.player_table, pinfo->name, pinfo->job, pinfo->shape, d->GetEmpire()))
	{
		sys_err("player_prototype error: job %d face %d ", pinfo->job);
		d->Packet(&packFailure, sizeof(packFailure));
		return;
	}

	const TAccountTable & c_rAccountTable = d->GetAccountTable();

	trim_and_lower(c_rAccountTable.login, player_create_packet.login, sizeof(player_create_packet.login));
	strlcpy(player_create_packet.passwd, c_rAccountTable.passwd, sizeof(player_create_packet.passwd));

	player_create_packet.account_id	= c_rAccountTable.id;
	player_create_packet.account_index	= pinfo->index;

	sys_log(0, "PlayerCreate: name %s account_id %d, TPlayerCreatePacketSize(%d), Packet->Gold %d",
			pinfo->name, 
			pinfo->index, 
			sizeof(TPlayerCreatePacket),
			player_create_packet.player_table.gold);

	db_clientdesc->DBPacket(GD::PLAYER_CREATE, d->GetHandle(), &player_create_packet, sizeof(TPlayerCreatePacket));
}

void CInputLogin::CharacterDelete(LPDESC d, const char * data)
{
	struct command_player_delete * pinfo = (struct command_player_delete *) data;
	const TAccountTable & c_rAccountTable = d->GetAccountTable();

	if (!c_rAccountTable.id)
	{
		sys_err("PlayerDelete: no login data");
		return;
	}

	sys_log(0, "PlayerDelete: login: %s index: %d, social_id %s", c_rAccountTable.login, pinfo->index, pinfo->private_code);

	if (pinfo->index < 0 || pinfo->index >= PLAYER_PER_ACCOUNT)
	{
		sys_err("PlayerDelete: index overflow %d, login: %s", pinfo->index, c_rAccountTable.login);
		return;
	}

	if (!c_rAccountTable.players[pinfo->index].dwID)
	{
		sys_err("PlayerDelete: Wrong Social ID index %d, login: %s", pinfo->index, c_rAccountTable.login);
		TPacketGCBlank pack_wrong;
		pack_wrong.header = GC::PLAYER_DELETE_WRONG_SOCIAL_ID;
		pack_wrong.length = sizeof(pack_wrong);
		d->Packet(&pack_wrong, sizeof(pack_wrong));
		return;
	}

	TPlayerDeletePacket	player_delete_packet;

	trim_and_lower(c_rAccountTable.login, player_delete_packet.login, sizeof(player_delete_packet.login));
	player_delete_packet.player_id	= c_rAccountTable.players[pinfo->index].dwID;
	player_delete_packet.account_index	= pinfo->index;
	strlcpy(player_delete_packet.private_code, pinfo->private_code, sizeof(player_delete_packet.private_code));

	db_clientdesc->DBPacket(GD::PLAYER_DELETE, d->GetHandle(), &player_delete_packet, sizeof(TPlayerDeletePacket));
}

void CInputLogin::Entergame(LPDESC d, const char * data)
{
	LPCHARACTER ch;

	if (!(ch = d->GetCharacter()))
	{
		d->SetPhase(PHASE_CLOSE);
		return;
	}

	PIXEL_POSITION pos = ch->GetXYZ();

	if (!SECTREE_MANAGER::instance().GetMovablePosition(ch->GetMapIndex(), pos.x, pos.y, pos))
	{
		PIXEL_POSITION pos2;
		SECTREE_MANAGER::instance().GetRecallPositionByEmpire(ch->GetMapIndex(), ch->GetEmpire(), pos2);

		sys_err("!GetMovablePosition (name %s %dx%d map %d changed to %dx%d)", 
				ch->GetName(),
				pos.x, pos.y,
				ch->GetMapIndex(),
				pos2.x, pos2.y);
		pos = pos2;
	}

	CGuildManager::instance().LoginMember(ch);

#ifdef ENABLE_ANTI_MULTIPLE_FARM
	{
		// Register this player in the anti-farm system
		std::string sMAIf = d->GetLoginMacAdress();
		int8_t i8OldState = static_cast<int8_t>(
			CAntiMultipleFarm::instance().GetPlayerDropState(sMAIf, ch->GetPlayerID()));
		CAntiMultipleFarm::instance().Login(sMAIf, ch->GetPlayerID(), i8OldState);
	}
#endif

	// ìºë¦­í„°ë¥¼ ë§µì— ì¶”ê°€
	ch->Show(ch->GetMapIndex(), pos.x, pos.y, pos.z);

	SECTREE_MANAGER::instance().SendNPCPosition(ch);

	ch->ReviveInvisible(5);

	d->SetPhase(PHASE_GAME);

	if(ch->GetItemAward_cmd())																		//ê²Œì„í˜ì´ì¦ˆ ë“¤ì–´ê°€ë©´
		quest::CQuestManager::instance().ItemInformer(ch->GetPlayerID(),ch->GetItemAward_vnum());	//questmanager í˜¸ì¶œ
	
	sys_log(0, "ENTERGAME: %s %dx%dx%d %s map_index %d", 
			ch->GetName(), ch->GetX(), ch->GetY(), ch->GetZ(), d->GetHostName(), ch->GetMapIndex());

	if (ch->GetHorseLevel() > 0)
	{
		ch->EnterHorse();
	}

	// í”Œë ˆì´ì‹œê°„ ë ˆì½”ë”© ì‹œì‘
	ch->ResetPlayTime();

	// ìë™ ì €ì¥ ì´ë²¤íŠ¸ ì¶”ê°€
	ch->StartSaveEvent();
	ch->StartRecoveryEvent();
	ch->StartCheckSpeedHackEvent();

	CPVPManager::instance().Connect(ch);
	CPVPManager::instance().SendList(d);

	MessengerManager::instance().Login(ch->GetName());

	CPartyManager::instance().SetParty(ch);
	CGuildManager::instance().SendGuildWar(ch);

	building::CManager::instance().SendLandList(d, ch->GetMapIndex());

	marriage::CManager::instance().Login(ch);

	TPacketGCTime p;
	p.header = GC::TIME;
	p.length = sizeof(p);
	p.time = get_global_time();
	d->Packet(&p, sizeof(p));

	TPacketGCChannel p2;
	p2.header = GC::CHANNEL;
	p2.length = sizeof(p2);
	p2.channel = g_bChannel;
	d->Packet(&p2, sizeof(p2));

	// Wave 5+ UI fix: client-side minimap "Metin2, CHX" refresh icin server-command yolla.
	// /cs sonrasi destination channel PlayerLoad'da otomatik tetiklenir.
	ch->ChatPacket(CHAT_TYPE_COMMAND, "setch %d", g_bChannel);

	// Faz 3 (autofarm bot): ai_bot quest flag set ise VEYA karakter ismi "Bot"
	// ile baÅŸlÄ±yorsa SetAutoBot(true). Quest flag DB'den yÃ¼klenmesi quest_manager
	// timing'e baÄŸlÄ±, isim check fail-safe olarak.
	// Pulse event main.cpp'de tum PC'leri her saniye tarar, IsAutoBot ise BotTick().
	{
		bool isBot = false;
		const char* name = ch->GetName();
		if (name && strncmp(name, "Bot", 3) == 0)
		{
			isBot = true;
		}
		else
		{
			auto* pc = quest::CQuestManager::instance().GetPCForce(ch->GetPlayerID());
			if (pc && pc->GetFlag("ai_bot") == 1)
				isBot = true;
		}
		if (isBot)
		{
			ch->SetAutoBot(true);
			sys_log(0, "AutoBot: %s pid=%u marked as bot", ch->GetName(), ch->GetPlayerID());
		}
	}

	_send_bonus_info(ch);
	
	for (int i = 0; i <= PREMIUM_MAX_NUM; ++i)
	{
		int remain = ch->GetPremiumRemainSeconds(i);

		if (remain <= 0)
			continue;

		ch->AddAffect(AFFECT_PREMIUM_START + i, POINT_NONE, 0, 0, remain, 0, true);
		sys_log(0, "PREMIUM: %s type %d %dmin", ch->GetName(), i, remain);
	}

	if (g_bCheckClientVersion)
	{
		int version = atoi(g_stClientVersion.c_str());
		int date = atoi(d->GetClientVersion());

		sys_log(0, "VERSION CHECK %d %d %s %s", version, date, g_stClientVersion.c_str(), d->GetClientVersion());

		if (!d->GetClientVersion())
		{
			d->DelayedDisconnect(10);
		}
		else
		{
			if (version != date)
			{
				ch->ChatPacket(CHAT_TYPE_NOTICE, LC_TEXT("í´ë¼ì´ì–¸íŠ¸ ë²„ì „ì´ í‹€ë ¤ ë¡œê·¸ì•„ì›ƒ ë©ë‹ˆë‹¤. ì •ìƒì ìœ¼ë¡œ íŒ¨ì¹˜ í›„ ì ‘ì†í•˜ì„¸ìš”."));
				d->DelayedDisconnect(10);
				LogManager::instance().HackLog("VERSION_CONFLICT", ch);

				sys_log(0, "VERSION : WRONG VERSION USER : account:%s name:%s hostName:%s server_version:%s client_version:%s",
						d->GetAccountTable().login,
						ch->GetName(),
						d->GetHostName(),
						g_stClientVersion.c_str(),
						d->GetClientVersion());
			}
		}
	}
	else
	{
		sys_log(0, "VERSION : NO CHECK");
	}

	if (ch->IsGM())
		ch->ChatPacket(CHAT_TYPE_COMMAND, "ConsoleEnable");

	if (ch->GetMapIndex() >= 10000)
	{
		if (CWarMapManager::instance().IsWarMap(ch->GetMapIndex()))
			ch->SetWarMap(CWarMapManager::instance().Find(ch->GetMapIndex()));
		else if (marriage::WeddingManager::instance().IsWeddingMap(ch->GetMapIndex()))
			ch->SetWeddingMap(marriage::WeddingManager::instance().Find(ch->GetMapIndex()));
		else {
			// MR-8: Auto-unmount and unequip special ride seals in Nemere's Watchtower
			if (ch->GetMapIndex() >= 352 * 10000 && ch->GetMapIndex() < 353 * 10000)
			{
				ch->RemoveAffect(AFFECT_MOUNT);
				ch->RemoveAffect(AFFECT_MOUNT_BONUS);

				if (ch->IsRiding() || ch->IsHorseRiding())
				{
					ch->StopRiding();
					ch->HorseSummon(false);
				}

				// Check for inventory space
				int emptyCell = ch->GetEmptyInventory(1);
				
				if (emptyCell != -1)
					ch->UnEquipSpecialRideUniqueItem();
			}
			// MR-8: -- END OF -- Auto-unmount and unequip special ride seals in Nemere's Watchtower

			ch->SetDungeon(CDungeonManager::instance().FindByMapIndex(ch->GetMapIndex()));
		}
	}
	else if (CArenaManager::instance().IsArenaMap(ch->GetMapIndex()) == true)
	{
		int memberFlag = CArenaManager::instance().IsMember(ch->GetMapIndex(), ch->GetPlayerID());

		if (memberFlag == MEMBER_OBSERVER)
		{
			ch->SetObserverMode(true);
			ch->SetArenaObserverMode(true);
			if (CArenaManager::instance().RegisterObserverPtr(ch, ch->GetMapIndex(), ch->GetX() / 100, ch->GetY() / 100))
			{
				sys_log(0, "ARENA : Observer add failed");
			}

			if (ch->IsHorseRiding() == true)
			{
				ch->StopRiding();
				ch->HorseSummon(false);
			}
		}
		else if (memberFlag == MEMBER_DUELIST)
		{
			TPacketGCDuelStart duelStart;
			duelStart.header = GC::DUEL_START;
			duelStart.length = sizeof(TPacketGCDuelStart);

			CHARACTER::SafeSendPacketTo(ch, &duelStart, sizeof(TPacketGCDuelStart));

			if (ch->IsHorseRiding() == true)
			{
				ch->StopRiding();
				ch->HorseSummon(false);
			}

			LPPARTY pParty = ch->GetParty();
			if (pParty != NULL)
			{
				if (pParty->GetMemberCount() == 2)
				{
					CPartyManager::instance().DeleteParty(pParty);
				}
				else
				{
					pParty->Quit(ch->GetPlayerID());
				}
			}
		}
		else if (memberFlag == MEMBER_NO)		
		{
			if (ch->GetGMLevel() == GM_PLAYER)
				ch->WarpSet(EMPIRE_START_X(ch->GetEmpire()), EMPIRE_START_Y(ch->GetEmpire()));
		}
		else
		{
			// wtf
		}
	}
	else if (ch->GetMapIndex() == 113)
	{
		// ox ì´ë²¤íŠ¸ ë§µ
		if (COXEventManager::instance().Enter(ch) == false)
		{
			// ox ë§µ ì§„ì… í—ˆê°€ê°€ ë‚˜ì§€ ì•ŠìŒ. í”Œë ˆì´ì–´ë©´ ë§ˆì„ë¡œ ë³´ë‚´ì
			if (ch->GetGMLevel() == GM_PLAYER)
				ch->WarpSet(EMPIRE_START_X(ch->GetEmpire()), EMPIRE_START_Y(ch->GetEmpire()));
		}
	}
	else
	{
		if (CWarMapManager::instance().IsWarMap(ch->GetMapIndex()) ||
				marriage::WeddingManager::instance().IsWeddingMap(ch->GetMapIndex()))
		{
			if (!test_server)
				ch->WarpSet(EMPIRE_START_X(ch->GetEmpire()), EMPIRE_START_Y(ch->GetEmpire()));
		}
	}

	if (ch->GetHorseLevel() > 0)
	{
		DWORD pid = ch->GetPlayerID();

		if (pid != 0 && CHorseNameManager::instance().GetHorseName(pid) == NULL)
			db_clientdesc->DBPacket(GD::REQ_HORSE_NAME, 0, &pid, sizeof(DWORD));
	}

	// ì¤‘ë¦½ë§µì— ë“¤ì–´ê°”ì„ë•Œ ì•ˆë‚´í•˜ê¸°
	if (g_noticeBattleZone)
	{
		if (FN_is_battle_zone(ch))
		{
			ch->ChatPacket(CHAT_TYPE_NOTICE, LC_TEXT("ì´ ë§µì—ì„  ê°•ì œì ì¸ ëŒ€ì „ì´ ìˆì„ìˆ˜ ë„ ìˆìŠµë‹ˆë‹¤."));
			ch->ChatPacket(CHAT_TYPE_NOTICE, LC_TEXT("ì´ ì¡°í•­ì— ë™ì˜í•˜ì§€ ì•Šì„ì‹œ"));
			ch->ChatPacket(CHAT_TYPE_NOTICE, LC_TEXT("ë³¸ì¸ì˜ ì£¼ì„± ë° ë¶€ì„±ìœ¼ë¡œ ëŒì•„ê°€ì‹œê¸° ë°”ëë‹ˆë‹¤."));
		}
	}
}

void CInputLogin::Empire(LPDESC d, const char * c_pData)
{
	const TPacketCGEmpire* p = reinterpret_cast<const TPacketCGEmpire*>(c_pData);

	if (EMPIRE_MAX_NUM <= p->bEmpire)
	{
		d->SetPhase(PHASE_CLOSE);
		return;
	}

	const TAccountTable& r = d->GetAccountTable();

	if (r.bEmpire != 0)
	{
		for (int i = 0; i < PLAYER_PER_ACCOUNT; ++i)
		{
			if (0 != r.players[i].dwID)
			{
				sys_err("EmpireSelectFailed %d", r.players[i].dwID);
				return;
			}
		}
	}

	TEmpireSelectPacket pd;

	pd.dwAccountID = r.id;
	pd.bEmpire = p->bEmpire;

	db_clientdesc->DBPacket(GD::EMPIRE_SELECT, d->GetHandle(), &pd, sizeof(pd));
}

int CInputLogin::GuildSymbolUpload(LPDESC d, const char* c_pData, size_t uiBytes)
{
	if (uiBytes < sizeof(TPacketCGGuildSymbolUpload))
		return -1;

	sys_log(0, "GuildSymbolUpload uiBytes %u", uiBytes);

	TPacketCGGuildSymbolUpload* p = (TPacketCGGuildSymbolUpload*) c_pData;

	if (uiBytes < p->length)
		return -1;

	int iSymbolSize = p->length - sizeof(TPacketCGGuildSymbolUpload);

	if (iSymbolSize <= 0 || iSymbolSize > 64 * 1024)
	{
		// 64k ë³´ë‹¤ í° ê¸¸ë“œ ì‹¬ë³¼ì€ ì˜¬ë¦´ìˆ˜ì—†ë‹¤
		// ì ‘ì†ì„ ëŠê³  ë¬´ì‹œ
		d->SetPhase(PHASE_CLOSE);
		return 0;
	}

	// ë•…ì„ ì†Œìœ í•˜ì§€ ì•Šì€ ê¸¸ë“œì¸ ê²½ìš°.
	if (!test_server)
		if (!building::CManager::instance().FindLandByGuild(p->guild_id))
		{
			d->SetPhase(PHASE_CLOSE);
			return 0;
		}

	sys_log(0, "GuildSymbolUpload Do Upload %02X%02X%02X%02X %d", c_pData[7], c_pData[8], c_pData[9], c_pData[10], sizeof(*p));

	CGuildMarkManager::instance().UploadSymbol(p->guild_id, iSymbolSize, (const BYTE*)(c_pData + sizeof(*p)));
	CGuildMarkManager::instance().SaveSymbol(GUILD_SYMBOL_FILENAME);
	return iSymbolSize;
}

void CInputLogin::GuildSymbolCRC(LPDESC d, const char* c_pData)
{
	const TPacketCGSymbolCRC & CGPacket = *((TPacketCGSymbolCRC *) c_pData);

	sys_log(0, "GuildSymbolCRC %u %u %u", CGPacket.guild_id, CGPacket.crc, CGPacket.length);

	const CGuildMarkManager::TGuildSymbol * pkGS = CGuildMarkManager::instance().GetGuildSymbol(CGPacket.guild_id);

	if (!pkGS)
		return;

	sys_log(0, "  Server %u %u", pkGS->crc, pkGS->raw.size());

	if (pkGS->raw.size() != CGPacket.length || pkGS->crc != CGPacket.crc)
	{
		TPacketGCGuildSymbolData GCPacket;

		GCPacket.header = GC::SYMBOL_DATA;
		GCPacket.length = sizeof(GCPacket) + pkGS->raw.size();
		GCPacket.guild_id = CGPacket.guild_id;

		d->BufferedPacket(&GCPacket, sizeof(GCPacket));
		d->Packet(&pkGS->raw[0], pkGS->raw.size());

		sys_log(0, "SendGuildSymbolHead %02X%02X%02X%02X Size %d", 
				pkGS->raw[0], pkGS->raw[1], pkGS->raw[2], pkGS->raw[3], pkGS->raw.size());
	}
}

void CInputLogin::GuildMarkUpload(LPDESC d, const char* c_pData)
{
	TPacketCGMarkUpload * p = (TPacketCGMarkUpload *) c_pData;
	CGuildManager& rkGuildMgr = CGuildManager::instance();
	CGuild * pkGuild;

	if (!(pkGuild = rkGuildMgr.FindGuild(p->gid)))
	{
		sys_err("MARK_SERVER: GuildMarkUpload: no guild. gid %u", p->gid);
		return;
	}

	if (pkGuild->GetLevel() < guild_mark_min_level)
	{
		sys_log(0, "MARK_SERVER: GuildMarkUpload: level < %u (%u)", guild_mark_min_level, pkGuild->GetLevel());
		return;
	}

	CGuildMarkManager & rkMarkMgr = CGuildMarkManager::instance();

	sys_log(0, "MARK_SERVER: GuildMarkUpload: gid %u", p->gid);

	bool isEmpty = true;

	for (DWORD iPixel = 0; iPixel < SGuildMark::SIZE; ++iPixel)
		if (*((DWORD *) p->image + iPixel) != 0x00000000)
			isEmpty = false;

	DWORD markID = CGuildMarkManager::INVALID_MARK_ID;

	if (isEmpty)
		rkMarkMgr.DeleteMark(p->gid);
	else
		markID = rkMarkMgr.SaveMark(p->gid, p->image);

	// Broadcast mark update to all game cores via P2P, which will then broadcast to their clients
	if (markID != CGuildMarkManager::INVALID_MARK_ID)
	{
		WORD imgIdx = static_cast<WORD>(markID / CGuildMarkImage::MARK_TOTAL_COUNT);

		// Send P2P packet to all other game cores
		TPacketGGMarkUpdate p2pPacket;
		p2pPacket.header = GG::MARK_UPDATE;
		p2pPacket.length = sizeof(p2pPacket);
		p2pPacket.dwGuildID = p->gid;
		p2pPacket.wImgIdx = imgIdx;
		P2P_MANAGER::instance().Send(&p2pPacket, sizeof(p2pPacket));

		// Also broadcast to clients connected to this core (mark server) using the same logic
		BroadcastGuildMarkUpdate(p->gid, imgIdx);

		sys_log(0, "MARK_SERVER: GuildMarkUpload: Broadcast mark update for guild %u, imgIdx %u via P2P", p->gid, imgIdx);
	}
}

void CInputLogin::GuildMarkIDXList(LPDESC d, const char* c_pData)
{
	CGuildMarkManager & rkMarkMgr = CGuildMarkManager::instance();
	
	DWORD bufSize = sizeof(WORD) * 2 * rkMarkMgr.GetMarkCount();
	char * buf = NULL;

	if (bufSize > 0)
	{
		buf = (char *) malloc(bufSize);
		rkMarkMgr.CopyMarkIdx(buf);
	}

	TPacketGCMarkIDXList p;
	p.header = GC::MARK_IDXLIST;
	p.length = sizeof(p);
	p.bufSize = sizeof(p) + bufSize;
	p.count = rkMarkMgr.GetMarkCount();

	if (buf)
	{
		d->BufferedPacket(&p, sizeof(p));
		d->LargePacket(buf, bufSize);
		free(buf);
	}
	else
		d->Packet(&p, sizeof(p));

	sys_log(0, "MARK_SERVER: GuildMarkIDXList %d bytes sent.", p.bufSize);
}

void CInputLogin::GuildMarkCRCList(LPDESC d, const char* c_pData)
{
	TPacketCGMarkCRCList * pCG = (TPacketCGMarkCRCList *) c_pData;

	std::map<BYTE, const SGuildMarkBlock *> mapDiffBlocks;
	CGuildMarkManager::instance().GetDiffBlocks(pCG->imgIdx, pCG->crclist, mapDiffBlocks);

	DWORD blockCount = 0;
	TEMP_BUFFER buf(1024 * 1024); // 1M ë²„í¼

	for (itertype(mapDiffBlocks) it = mapDiffBlocks.begin(); it != mapDiffBlocks.end(); ++it)
	{
		BYTE posBlock = it->first;
		const SGuildMarkBlock & rkBlock = *it->second;

		buf.write(&posBlock, sizeof(BYTE));
		buf.write(&rkBlock.m_sizeCompBuf, sizeof(DWORD));
		buf.write(rkBlock.m_abCompBuf, rkBlock.m_sizeCompBuf);

		++blockCount;
	}

	TPacketGCMarkBlock pGC;

	pGC.header = GC::MARK_BLOCK;
	pGC.length = sizeof(pGC);
	pGC.imgIdx = pCG->imgIdx;
	pGC.bufSize = buf.size() + sizeof(TPacketGCMarkBlock);
	pGC.count = blockCount;

	sys_log(0, "MARK_SERVER: Sending blocks. (imgIdx %u diff %u size %u)", pCG->imgIdx, mapDiffBlocks.size(), pGC.bufSize);

	if (buf.size() > 0)
	{
		d->BufferedPacket(&pGC, sizeof(TPacketGCMarkBlock));
		d->LargePacket(buf.read_peek(), buf.size());
	}
	else
		d->Packet(&pGC, sizeof(TPacketGCMarkBlock));
}


CInputLogin::CInputLogin()
{
	RegisterHandlers();
}

int CInputLogin::HandlePong(LPDESC d, const char*)
{
	Pong(d);
	return 0;
}

int CInputLogin::HandleMarkLogin(LPDESC d, const char*)
{
	extern bool guild_mark_server;
	if (!guild_mark_server)
	{
		sys_err("Guild Mark login requested but i'm not a mark server!");
		d->SetPhase(PHASE_CLOSE);
		return 0;
	}

	sys_log(0, "MARK_SERVER: Login (from LOGIN phase)");
	// Already in LOGIN phase â€” no phase change needed
	return 0;
}

int CInputLogin::HandleGuildSymbolUpload(LPDESC d, const char* c_pData)
{
	return GuildSymbolUpload(d, c_pData, m_iBufferLeft);
}

int CInputLogin::HandleVersion(LPDESC d, const char* c_pData)
{
	Version(d->GetCharacter(), c_pData);
	return 0;
}

void CInputLogin::RegisterHandlers()
{
	m_handlers[CG::PONG]               = &CInputLogin::HandlePong;
	m_handlers[CG::MARK_LOGIN]         = &CInputLogin::HandleMarkLogin;
	m_handlers[CG::LOGIN2]             = &CInputLogin::SimpleHandler<&CInputLogin::LoginByKey>;
	m_handlers[CG::CHARACTER_SELECT]   = &CInputLogin::SimpleHandler<&CInputLogin::CharacterSelect>;
	m_handlers[CG::CHARACTER_CREATE]   = &CInputLogin::SimpleHandler<&CInputLogin::CharacterCreate>;
	m_handlers[CG::CHARACTER_DELETE]   = &CInputLogin::SimpleHandler<&CInputLogin::CharacterDelete>;
	m_handlers[CG::ENTERGAME]          = &CInputLogin::SimpleHandler<&CInputLogin::Entergame>;
	m_handlers[CG::EMPIRE]             = &CInputLogin::SimpleHandler<&CInputLogin::Empire>;
	m_handlers[CG::MARK_CRCLIST]       = &CInputLogin::SimpleHandler<&CInputLogin::GuildMarkCRCList>;
	m_handlers[CG::MARK_IDXLIST]       = &CInputLogin::SimpleHandler<&CInputLogin::GuildMarkIDXList>;
	m_handlers[CG::MARK_UPLOAD]        = &CInputLogin::SimpleHandler<&CInputLogin::GuildMarkUpload>;
	m_handlers[CG::GUILD_SYMBOL_UPLOAD]= &CInputLogin::HandleGuildSymbolUpload;
	m_handlers[CG::SYMBOL_CRC]         = &CInputLogin::SimpleHandler<&CInputLogin::GuildSymbolCRC>;
	m_handlers[CG::CHANGE_NAME]        = &CInputLogin::SimpleHandler<&CInputLogin::ChangeName>;
	m_handlers[CG::CLIENT_VERSION]     = &CInputLogin::HandleVersion;
}

int CInputLogin::Analyze(LPDESC d, uint16_t wHeader, const char * c_pData)
{
	auto it = m_handlers.find(wHeader);
	if (it == m_handlers.end())
	{
		sys_err("CInputLogin::Analyze: unknown header %d (0x%04X) from %s", wHeader, wHeader, d->GetHostName());
		return 0;
	}

	return (this->*(it->second))(d, c_pData);
}

