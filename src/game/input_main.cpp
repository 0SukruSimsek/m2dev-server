#include "stdafx.h"
#ifdef ENABLE_ANTI_MULTIPLE_FARM
#include "HAntiMultipleFarm.h"
#endif
#include "constants.h"
#include "config.h"
#include "utils.h"
#include "desc_client.h"
#include "desc_manager.h"
#include "buffer_manager.h"
#include "packet_structs.h"
#include "switchbot.h"
#include "protocol.h"
#include "char.h"
#include "bot_chat.h"   // v24 Sorun 2 â€” BotChat_ScanForMentions
#include "char_manager.h"
#include "item.h"
#include "item_manager.h"
#include "cmd.h"
#include "shop.h"
#include "shop_manager.h"
#include "safebox.h"
#include "regen.h"
#include "battle.h"
#include "exchange.h"
#include "questmanager.h"
#include "profiler.h"
#include "messenger_manager.h"
#include "party.h"
#include "p2p.h"
#include "affect.h"
#include "guild.h"
#include "guild_manager.h"
#include "log.h"
#include "banword.h"
#include "empire_text_convert.h"
#include "unique_item.h"
#include "building.h"
#include "locale_service.h"
#include "gm.h"
#include "spam.h"
#include "ani.h"
#include "motion.h"
#include "OXEvent.h"
#include "locale_service.h"
#include "DragonSoul.h"
#include "keyf_train_log.h"  // [KEYF_TRAIN] training log macro
#ifdef ENABLE_NPC_LOCATION_HELPER
#include "npc_location_helper.h"
#endif

extern void SendShout(const char * szText, BYTE bEmpire);
extern int g_nPortalLimitTime;

// Template adapter definitions (declared in input.h, defined here where DESC is complete)
template<void (CInputMain::*fn)(LPCHARACTER, const char*)>
int CInputMain::SimpleHandler(LPDESC d, const char* p)
{
	(this->*fn)(d->GetCharacter(), p);
	return 0;
}

template<void (CInputMain::*fn)(LPCHARACTER, const void*)>
int CInputMain::SimpleHandlerV(LPDESC d, const char* p)
{
	(this->*fn)(d->GetCharacter(), p);
	return 0;
}

static int __deposit_limit()
{
	return (1000*10000); // 1ì²œë§Œ
}

void SendBlockChatInfo(LPCHARACTER ch, int sec)
{
	if (sec <= 0)
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì±„íŒ… ê¸ˆì§€ ìƒíƒœì…ë‹ˆë‹¤."));
		return;
	}

	long hour = sec / 3600;
	sec -= hour * 3600;

	long min = (sec / 60);
	sec -= min * 60;

	char buf[128+1];

	if (hour > 0 && min > 0)
		snprintf(buf, sizeof(buf), LC_TEXT("%d ì‹œê°„ %d ë¶„ %d ì´ˆ ë™ì•ˆ ì±„íŒ…ê¸ˆì§€ ìƒíƒœì…ë‹ˆë‹¤"), hour, min, sec);
	else if (hour > 0 && min == 0)
		snprintf(buf, sizeof(buf), LC_TEXT("%d ì‹œê°„ %d ì´ˆ ë™ì•ˆ ì±„íŒ…ê¸ˆì§€ ìƒíƒœì…ë‹ˆë‹¤"), hour, sec);
	else if (hour == 0 && min > 0)
		snprintf(buf, sizeof(buf), LC_TEXT("%d ë¶„ %d ì´ˆ ë™ì•ˆ ì±„íŒ…ê¸ˆì§€ ìƒíƒœì…ë‹ˆë‹¤"), min, sec);
	else
		snprintf(buf, sizeof(buf), LC_TEXT("%d ì´ˆ ë™ì•ˆ ì±„íŒ…ê¸ˆì§€ ìƒíƒœì…ë‹ˆë‹¤"), sec);

	ch->ChatPacket(CHAT_TYPE_INFO, buf);
}

EVENTINFO(spam_event_info)
{
	char host[MAX_HOST_LENGTH+1];

	spam_event_info()
	{
		::memset( host, 0, MAX_HOST_LENGTH+1 );
	}
};

typedef std::unordered_map<std::string, std::pair<unsigned int, LPEVENT> > spam_score_of_ip_t;
spam_score_of_ip_t spam_score_of_ip;

EVENTFUNC(block_chat_by_ip_event)
{
	spam_event_info* info = dynamic_cast<spam_event_info*>( event->info );

	if ( info == NULL )
	{
		sys_err( "block_chat_by_ip_event> <Factor> Null pointer" );
		return 0;
	}

	const char * host = info->host;

	spam_score_of_ip_t::iterator it = spam_score_of_ip.find(host);

	if (it != spam_score_of_ip.end())
	{
		it->second.first = 0;
		it->second.second = NULL;
	}

	return 0;
}

bool SpamBlockCheck(LPCHARACTER ch, const char* const buf, const size_t buflen)
{
	extern int g_iSpamBlockMaxLevel;

	if (ch->GetLevel() < g_iSpamBlockMaxLevel)
	{
		spam_score_of_ip_t::iterator it = spam_score_of_ip.find(ch->GetDesc()->GetHostName());

		if (it == spam_score_of_ip.end())
		{
			spam_score_of_ip.insert(std::make_pair(ch->GetDesc()->GetHostName(), std::make_pair(0, (LPEVENT) NULL)));
			it = spam_score_of_ip.find(ch->GetDesc()->GetHostName());
		}

		if (it->second.second)
		{
			SendBlockChatInfo(ch, event_time(it->second.second) / passes_per_sec);
			return true;
		}

		unsigned int score;
		const char * word = SpamManager::instance().GetSpamScore(buf, buflen, score);

		it->second.first += score;

		if (word)
			sys_log(0, "SPAM_SCORE: %s text: %s score: %u total: %u word: %s", ch->GetName(), buf, score, it->second.first, word);

		extern unsigned int g_uiSpamBlockScore;
		extern unsigned int g_uiSpamBlockDuration;

		if (it->second.first >= g_uiSpamBlockScore)
		{
			spam_event_info* info = AllocEventInfo<spam_event_info>();
			strlcpy(info->host, ch->GetDesc()->GetHostName(), sizeof(info->host));

			it->second.second = event_create(block_chat_by_ip_event, info, PASSES_PER_SEC(g_uiSpamBlockDuration));
			sys_log(0, "SPAM_IP: %s for %u seconds", info->host, g_uiSpamBlockDuration);

			LogManager::instance().CharLog(ch, 0, "SPAM", word);

			SendBlockChatInfo(ch, event_time(it->second.second) / passes_per_sec);

			return true;
		}
	}

	return false;
}

enum
{
	TEXT_TAG_PLAIN,
	TEXT_TAG_TAG, // ||
	TEXT_TAG_COLOR, // |cffffffff
	TEXT_TAG_HYPERLINK_START, // |H
	TEXT_TAG_HYPERLINK_END, // |h ex) |Hitem:1234:1:1:1|h
	TEXT_TAG_RESTORE_COLOR,
};

int GetTextTag(const char * src, int maxLen, int & tagLen, std::string & extraInfo)
{
	tagLen = 1;

	if (maxLen < 2 || *src != '|')
		return TEXT_TAG_PLAIN;

	const char * cur = ++src;

	if (*cur == '|') // ||ëŠ” |ë¡œ í‘œì‹œí•œë‹¤.
	{
		tagLen = 2;
		return TEXT_TAG_TAG;
	}
	else if (*cur == 'c') // color |cffffffffblahblah|r
	{
		tagLen = 2;
		return TEXT_TAG_COLOR;
	}
	else if (*cur == 'H') // hyperlink |Hitem:10000:0:0:0:0|h[ì´ë¦„]|h
	{
		tagLen = 2;
		return TEXT_TAG_HYPERLINK_START;
	}
	else if (*cur == 'h') // end of hyperlink
	{
		tagLen = 2;
		return TEXT_TAG_HYPERLINK_END;
	}

	return TEXT_TAG_PLAIN;
}

void GetTextTagInfo(const char * src, int src_len, int & hyperlinks, bool & colored)
{
	colored = false;
	hyperlinks = 0;

	int len;
	std::string extraInfo;

	for (int i = 0; i < src_len;)
	{
		int tag = GetTextTag(&src[i], src_len - i, len, extraInfo);

		if (tag == TEXT_TAG_HYPERLINK_START)
			++hyperlinks;

		if (tag == TEXT_TAG_COLOR)
			colored = true;

		i += len;
	}
}

int ProcessTextTag(LPCHARACTER ch, const char * c_pszText, size_t len)
{
	//2012.05.17 ê¹€ìš©ìš±
	//0 : ì •ìƒì ìœ¼ë¡œ ì‚¬ìš©
	//1 : ê¸ˆê°•ê²½ ë¶€ì¡±
	//2 : ê¸ˆê°•ê²½ì´ ìˆìœ¼ë‚˜, ê°œì¸ìƒì ì—ì„œ ì‚¬ìš©ì¤‘
	//3 : êµí™˜ì¤‘
	//4 : ì—ëŸ¬
	int hyperlinks;
	bool colored;
	
	GetTextTagInfo(c_pszText, len, hyperlinks, colored);

	if (colored == true && hyperlinks == 0)
		return 4;

	if (ch->GetExchange())
	{
		if (hyperlinks == 0)
			return 0;
		else
			return 3;
	}

	int nPrismCount = ch->CountSpecifyItem(ITEM_PRISM);

	if (nPrismCount < hyperlinks)
		return 1;


	if (!ch->GetMyShop())
	{
		ch->RemoveSpecifyItem(ITEM_PRISM, hyperlinks);
		return 0;
	} else
	{
		int sellingNumber = ch->GetMyShop()->GetNumberByVnum(ITEM_PRISM);
		if(nPrismCount - sellingNumber < hyperlinks)
		{
			return 2;
		} else
		{
			ch->RemoveSpecifyItem(ITEM_PRISM, hyperlinks);
			return 0;
		}
	}
	
	return 4;
}

int CInputMain::Whisper(LPCHARACTER ch, const char * data, size_t uiBytes)
{
	const TPacketCGWhisper* pinfo = reinterpret_cast<const TPacketCGWhisper*>(data);

	if (uiBytes < pinfo->length)
		return -1;

	int iExtraLen = pinfo->length - sizeof(TPacketCGWhisper);

	if (iExtraLen < 0)
	{
		sys_err("invalid packet length (len %d size %u buffer %u)", iExtraLen, pinfo->length, uiBytes);
		ch->GetDesc()->SetPhase(PHASE_CLOSE);
		return -1;
	}

	if (ch->GetLastPMPulse() < thecore_pulse())
		ch->ClearPMCounter();
		
	if (ch->GetPMCounter() > 3 && ch->GetLastPMPulse() > thecore_pulse())
	{
		ch->GetDesc()->SetPhase(PHASE_CLOSE);
		return -1;
	}

	if (ch->FindAffect(AFFECT_BLOCK_CHAT))
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì±„íŒ… ê¸ˆì§€ ìƒíƒœì…ë‹ˆë‹¤."));
		return (iExtraLen);
	}

	LPCHARACTER pkChr = CHARACTER_MANAGER::instance().FindPC(pinfo->szNameTo);

	if (pkChr == ch)
		return (iExtraLen);
	
	ch->IncreasePMCounter();
	ch->SetLastPMPulse();

	LPDESC pkDesc = NULL;

	BYTE bOpponentEmpire = 0;

	if (test_server)
	{
		if (!pkChr)
			sys_log(0, "Whisper to %s(%s) from %s", "Null", pinfo->szNameTo, ch->GetName());
		else
			sys_log(0, "Whisper to %s(%s) from %s", pkChr->GetName(), pinfo->szNameTo, ch->GetName());
	}
		
	if (ch->IsBlockMode(BLOCK_WHISPER))
	{
		if (ch->GetDesc())
		{
			TPacketGCWhisper pack;
			pack.header = GC::WHISPER;
			pack.bType = WHISPER_TYPE_SENDER_BLOCKED;
			pack.length = sizeof(TPacketGCWhisper);
			strlcpy(pack.szNameFrom, pinfo->szNameTo, sizeof(pack.szNameFrom));
			CHARACTER::SafeSendPacketTo(ch, &pack, sizeof(pack));
		}
		return iExtraLen;
	}

	if (!pkChr)
	{
		CCI * pkCCI = P2P_MANAGER::instance().Find(pinfo->szNameTo);

		if (pkCCI)
		{
			pkDesc = pkCCI->pkDesc;
			pkDesc->SetRelay(pinfo->szNameTo);
			bOpponentEmpire = pkCCI->bEmpire;

			if (test_server)
				sys_log(0, "Whisper to %s from %s (Channel %d Mapindex %d)", "Null", ch->GetName(), pkCCI->bChannel, pkCCI->lMapIndex);
		}
	}
	else
	{
		pkDesc = pkChr->GetDesc();
		bOpponentEmpire = pkChr->GetEmpire();
	}

	if (!pkDesc)
	{
		if (ch->GetDesc())
		{
			TPacketGCWhisper pack;

			pack.header = GC::WHISPER;
			pack.bType = WHISPER_TYPE_NOT_EXIST;
			pack.length = sizeof(TPacketGCWhisper);
			strlcpy(pack.szNameFrom, pinfo->szNameTo, sizeof(pack.szNameFrom));
			CHARACTER::SafeSendPacketTo(ch, &pack, sizeof(TPacketGCWhisper));
			sys_log(0, "WHISPER: no player");
		}
	}
	else
	{
		if (ch->IsBlockMode(BLOCK_WHISPER))
		{
			if (ch->GetDesc())
			{
				TPacketGCWhisper pack;
				pack.header = GC::WHISPER;
				pack.bType = WHISPER_TYPE_SENDER_BLOCKED;
				pack.length = sizeof(TPacketGCWhisper);
				strlcpy(pack.szNameFrom, pinfo->szNameTo, sizeof(pack.szNameFrom));
				CHARACTER::SafeSendPacketTo(ch, &pack, sizeof(pack));
			}
		}
		else if (pkChr && pkChr->IsBlockMode(BLOCK_WHISPER))
		{
			if (ch->GetDesc())
			{
				TPacketGCWhisper pack;
				pack.header = GC::WHISPER;
				pack.bType = WHISPER_TYPE_TARGET_BLOCKED;
				pack.length = sizeof(TPacketGCWhisper);
				strlcpy(pack.szNameFrom, pinfo->szNameTo, sizeof(pack.szNameFrom));
				CHARACTER::SafeSendPacketTo(ch, &pack, sizeof(pack));
			}
		}
		else
		{
			BYTE bType = WHISPER_TYPE_NORMAL;

			char buf[CHAT_MAX_LEN + 1];
			strlcpy(buf, data + sizeof(TPacketCGWhisper), MIN(iExtraLen + 1, sizeof(buf)));
			const size_t buflen = strlen(buf);

			if (true == SpamBlockCheck(ch, buf, buflen))
			{
				if (!pkChr)
				{
					CCI * pkCCI = P2P_MANAGER::instance().Find(pinfo->szNameTo);

					if (pkCCI)
					{
						pkDesc->SetRelay("");
					}
				}
				return iExtraLen;
			}

			if (LC_IsCanada() == false)
			{
				CBanwordManager::instance().ConvertString(buf, buflen);
			}

			if (g_bEmpireWhisper)
				if (!ch->IsEquipUniqueGroup(UNIQUE_GROUP_RING_OF_LANGUAGE))
					if (!(pkChr && pkChr->IsEquipUniqueGroup(UNIQUE_GROUP_RING_OF_LANGUAGE)))
						if (bOpponentEmpire != ch->GetEmpire() && ch->GetEmpire() && bOpponentEmpire // ì„œë¡œ ì œêµ­ì´ ë‹¤ë¥´ë©´ì„œ
								&& ch->GetGMLevel() == GM_PLAYER && gm_get_level(pinfo->szNameTo) == GM_PLAYER) // ë‘˜ë‹¤ ì¼ë°˜ í”Œë ˆì´ì–´ì´ë©´
							// ì´ë¦„ ë°–ì— ëª¨ë¥´ë‹ˆ gm_get_level í•¨ìˆ˜ë¥¼ ì‚¬ìš©
						{
							if (!pkChr)
							{
								// ë‹¤ë¥¸ ì„œë²„ì— ìˆìœ¼ë‹ˆ ì œêµ­ í‘œì‹œë§Œ í•œë‹¤. bTypeì˜ ìƒìœ„ 4ë¹„íŠ¸ë¥¼ Empireë²ˆí˜¸ë¡œ ì‚¬ìš©í•œë‹¤.
								bType = ch->GetEmpire() << 4;
							}
							else
							{
								ConvertEmpireText(ch->GetEmpire(), buf, buflen, 10 + 2 * pkChr->GetSkillPower(SKILL_LANGUAGE1 + ch->GetEmpire() - 1)/*ë³€í™˜í™•ë¥ */);
							}
						}

			int processReturn = ProcessTextTag(ch, buf, buflen);
			if (0!=processReturn)
			{
				if (ch->GetDesc())
				{
					TItemTable * pTable = ITEM_MANAGER::instance().GetTable(ITEM_PRISM);

					if (pTable)
					{
						char buf[128];
						int len;
						if (3==processReturn) //êµí™˜ì¤‘
							len = snprintf(buf, sizeof(buf), LC_TEXT("ë‹¤ë¥¸ ê±°ë˜ì¤‘(ì°½ê³ ,êµí™˜,ìƒì )ì—ëŠ” ê°œì¸ìƒì ì„ ì‚¬ìš©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."), pTable->szLocaleName);
						else
							len = snprintf(buf, sizeof(buf), LC_TEXT("%sì´ í•„ìš”í•©ë‹ˆë‹¤."), pTable->szLocaleName);
						

						if (len < 0 || len >= (int) sizeof(buf))
							len = sizeof(buf) - 1;

						++len;  // \0 ë¬¸ì í¬í•¨

						TPacketGCWhisper pack;

						pack.header = GC::WHISPER;
						pack.bType = WHISPER_TYPE_ERROR;
						pack.length = sizeof(TPacketGCWhisper) + len;
						strlcpy(pack.szNameFrom, pinfo->szNameTo, sizeof(pack.szNameFrom));

						ch->GetDesc()->BufferedPacket(&pack, sizeof(pack));
						CHARACTER::SafeSendPacketTo(ch, buf, len);

						sys_log(0, "WHISPER: not enough %s: char: %s", pTable->szLocaleName, ch->GetName());
					}
				}

				// ë¦´ë˜ì´ ìƒíƒœì¼ ìˆ˜ ìˆìœ¼ë¯€ë¡œ ë¦´ë˜ì´ë¥¼ í’€ì–´ì¤€ë‹¤.
				pkDesc->SetRelay("");
				return (iExtraLen);
			}

			if (ch->IsGM())
				bType = (bType & 0xF0) | WHISPER_TYPE_GM;

			if (buflen > 0)
			{
				TPacketGCWhisper pack;

				pack.header = GC::WHISPER;
				pack.length = sizeof(TPacketGCWhisper) + buflen;
				pack.bType = bType;
				strlcpy(pack.szNameFrom, ch->GetName(), sizeof(pack.szNameFrom));

				// desc->BufferedPacketì„ í•˜ì§€ ì•Šê³  ë²„í¼ì— ì¨ì•¼í•˜ëŠ” ì´ìœ ëŠ” 
				// P2P relayë˜ì–´ íŒ¨í‚·ì´ ìº¡ìŠí™” ë  ìˆ˜ ìˆê¸° ë•Œë¬¸ì´ë‹¤.
				TEMP_BUFFER tmpbuf;

				tmpbuf.write(&pack, sizeof(pack));
				tmpbuf.write(buf, buflen);

				pkDesc->Packet(tmpbuf.read_peek(), tmpbuf.size());

				if (LC_IsEurope() != true)
				{
					sys_log(0, "WHISPER: %s -> %s : %s", ch->GetName(), pinfo->szNameTo, buf);
				}
			}
		}
	}
	if(pkDesc)
		pkDesc->SetRelay("");

	return (iExtraLen);
}

struct RawPacketToCharacterFunc
{
	const void * m_buf;
	int	m_buf_len;

	RawPacketToCharacterFunc(const void * buf, int buf_len) : m_buf(buf), m_buf_len(buf_len)
	{
	}

	void operator () (LPCHARACTER c)
	{
		if (!c->GetDesc())
			return;

		CHARACTER::SafeSendPacketTo(c, m_buf, m_buf_len);
	}
};

struct FEmpireChatPacket
{
	packet_chat& p;
	const char* orig_msg;
	int orig_len;
	char converted_msg[CHAT_MAX_LEN+1];

	BYTE bEmpire;
	int iMapIndex;
	int namelen;

	FEmpireChatPacket(packet_chat& p, const char* chat_msg, int len, BYTE bEmpire, int iMapIndex, int iNameLen)
		: p(p), orig_msg(chat_msg), orig_len(len), bEmpire(bEmpire), iMapIndex(iMapIndex), namelen(iNameLen)
	{
		memset( converted_msg, 0, sizeof(converted_msg) );
	}

	void operator () (LPDESC d)
	{
		if (!d->GetCharacter())
			return;

		if (d->GetCharacter()->GetMapIndex() != iMapIndex)
			return;

		d->BufferedPacket(&p, sizeof(packet_chat));

		if (d->GetEmpire() == bEmpire ||
			bEmpire == 0 ||
			d->GetCharacter()->GetGMLevel() > GM_PLAYER ||
			d->GetCharacter()->IsEquipUniqueGroup(UNIQUE_GROUP_RING_OF_LANGUAGE))
		{
			d->Packet(orig_msg, orig_len);
		}
		else
		{
			// ì‚¬ëŒë§ˆë‹¤ ìŠ¤í‚¬ë ˆë²¨ì´ ë‹¤ë¥´ë‹ˆ ë§¤ë²ˆ í•´ì•¼í•©ë‹ˆë‹¤
			size_t len = strlcpy(converted_msg, orig_msg, sizeof(converted_msg));

			if (len >= sizeof(converted_msg))
				len = sizeof(converted_msg) - 1;

			ConvertEmpireText(bEmpire, converted_msg + namelen, len - namelen, 10 + 2 * d->GetCharacter()->GetSkillPower(SKILL_LANGUAGE1 + bEmpire - 1));
			d->Packet(converted_msg, orig_len);
		}
	}
};

struct FYmirChatPacket
{
	packet_chat& packet;
	const char* m_szChat;
	size_t m_lenChat;
	const char* m_szName;
	
	int m_iMapIndex;
	BYTE m_bEmpire;
	bool m_ring;

	char m_orig_msg[CHAT_MAX_LEN+1];
	int m_len_orig_msg;
	char m_conv_msg[CHAT_MAX_LEN+1];
	int m_len_conv_msg;

	FYmirChatPacket(packet_chat& p, const char* chat, size_t len_chat, const char* name, size_t len_name, int iMapIndex, BYTE empire, bool ring)
		: packet(p),
		m_szChat(chat), m_lenChat(len_chat),
		m_szName(name), 
		m_iMapIndex(iMapIndex), m_bEmpire(empire),
		m_ring(ring)
	{
		m_len_orig_msg = snprintf(m_orig_msg, sizeof(m_orig_msg), "%s : %s", m_szName, m_szChat) + 1; // ë„ ë¬¸ì í¬í•¨

		if (m_len_orig_msg < 0 || m_len_orig_msg >= (int) sizeof(m_orig_msg))
			m_len_orig_msg = sizeof(m_orig_msg) - 1;

		m_len_conv_msg = snprintf(m_conv_msg, sizeof(m_conv_msg), "??? : %s", m_szChat) + 1; // ë„ ë¬¸ì ë¯¸í¬í•¨

		if (m_len_conv_msg < 0 || m_len_conv_msg >= (int) sizeof(m_conv_msg))
			m_len_conv_msg = sizeof(m_conv_msg) - 1;

		ConvertEmpireText(m_bEmpire, m_conv_msg + 6, m_len_conv_msg - 6, 10); // 6ì€ "??? : "ì˜ ê¸¸ì´
	}

	void operator() (LPDESC d)
	{
		if (!d->GetCharacter())
			return;

		if (d->GetCharacter()->GetMapIndex() != m_iMapIndex)
			return;

		if (m_ring ||
			d->GetEmpire() == m_bEmpire ||
			d->GetCharacter()->GetGMLevel() > GM_PLAYER ||
			d->GetCharacter()->IsEquipUniqueGroup(UNIQUE_GROUP_RING_OF_LANGUAGE))
		{
			packet.length = m_len_orig_msg + sizeof(TPacketGCChat);

			d->BufferedPacket(&packet, sizeof(packet_chat));
			d->Packet(m_orig_msg, m_len_orig_msg);
		}
		else
		{
			packet.length = m_len_conv_msg + sizeof(TPacketGCChat);

			d->BufferedPacket(&packet, sizeof(packet_chat));
			d->Packet(m_conv_msg, m_len_conv_msg);
		}
	}
};

int CInputMain::Chat(LPCHARACTER ch, const char * data, size_t uiBytes)
{
	const TPacketCGChat* pinfo = reinterpret_cast<const TPacketCGChat*>(data);

	if (uiBytes < pinfo->length)
		return -1;

	const int iExtraLen = pinfo->length - sizeof(TPacketCGChat);

	if (iExtraLen < 0)
	{
		sys_err("invalid packet length (len %d size %u buffer %u)", iExtraLen, pinfo->length, uiBytes);
		ch->GetDesc()->SetPhase(PHASE_CLOSE);
		return -1;
	}

	char buf[CHAT_MAX_LEN - (CHARACTER_NAME_MAX_LEN + 3) + 1];
	strlcpy(buf, data + sizeof(TPacketCGChat), MIN(iExtraLen + 1, sizeof(buf)));
	const size_t buflen = strlen(buf);

	if (buflen > 1 && *buf == '/')
	{
		interpret_command(ch, buf + 1, buflen - 1);
		return iExtraLen;
	}

	if (ch->IncreaseChatCounter() >= 10)
	{
		if (ch->GetChatCounter() == 10)
		{
			sys_log(0, "CHAT_HACK: %s", ch->GetName());
			ch->GetDesc()->DelayedDisconnect(5);
		}

		return iExtraLen;
	}

	// ì±„íŒ… ê¸ˆì§€ Affect ì²˜ë¦¬
	const CAffect* pAffect = ch->FindAffect(AFFECT_BLOCK_CHAT);

	if (pAffect != NULL)
	{
		SendBlockChatInfo(ch, pAffect->lDuration);
		return iExtraLen;
	}

	if (true == SpamBlockCheck(ch, buf, buflen))
	{
		return iExtraLen;
	}

	// v24 Sorun 2 â€” Bot mention scan (PC chat'inde bot ismi varsa pending set)
	// Yalnizca normal talking chat'te (shout/party degil)
	if (pinfo->type == CHAT_TYPE_TALKING)
		BotChat_ScanForMentions(ch, buf);

	char chatbuf[CHAT_MAX_LEN + 1];
	int len = snprintf(chatbuf, sizeof(chatbuf), "%s : %s", ch->GetName(), buf);

	if (CHAT_TYPE_SHOUT == pinfo->type)
	{
		LogManager::instance().ShoutLog(g_bChannel, ch->GetEmpire(), chatbuf);
	}

	if (LC_IsCanada() == false)
	{
		CBanwordManager::instance().ConvertString(buf, buflen);
	}

	if (len < 0 || len >= (int) sizeof(chatbuf))
		len = sizeof(chatbuf) - 1;

	int processReturn = ProcessTextTag(ch, chatbuf, len);
	if (0!=processReturn)
	{
		const TItemTable* pTable = ITEM_MANAGER::instance().GetTable(ITEM_PRISM);

		if (NULL != pTable)
		{
			if (3==processReturn) //êµí™˜ì¤‘
				ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë‹¤ë¥¸ ê±°ë˜ì¤‘(ì°½ê³ ,êµí™˜,ìƒì )ì—ëŠ” ê°œì¸ìƒì ì„ ì‚¬ìš©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."), pTable->szLocaleName);
			else
				ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("%sì´ í•„ìš”í•©ë‹ˆë‹¤."), pTable->szLocaleName);
						
		}

		return iExtraLen;
	}

	if (pinfo->type == CHAT_TYPE_SHOUT)
	{
		const int SHOUT_LIMIT_LEVEL = g_iUseLocale ? 15 : 3;

		if (ch->GetLevel() < SHOUT_LIMIT_LEVEL)
		{
			ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì™¸ì¹˜ê¸°ëŠ” ë ˆë²¨ %d ì´ìƒë§Œ ì‚¬ìš© ê°€ëŠ¥ í•©ë‹ˆë‹¤."), SHOUT_LIMIT_LEVEL);
			return (iExtraLen);
		}

		if (thecore_heart->pulse - (int) ch->GetLastShoutPulse() < passes_per_sec * 15)
			return (iExtraLen);

		ch->SetLastShoutPulse(thecore_heart->pulse);

		TPacketGGShout p;

		p.header = GG::SHOUT; p.length = sizeof(p);
		p.bEmpire = ch->GetEmpire();
		strlcpy(p.szText, chatbuf, sizeof(p.szText));

		P2P_MANAGER::instance().Send(&p, sizeof(TPacketGGShout));

		SendShout(chatbuf, ch->GetEmpire());

		return (iExtraLen);
	}

	TPacketGCChat pack_chat;

	pack_chat.header = GC::CHAT;
	pack_chat.length = sizeof(TPacketGCChat) + len;
	pack_chat.type = pinfo->type;
	pack_chat.id = ch->GetVID();

	switch (pinfo->type)
	{
		case CHAT_TYPE_TALKING:
			{
				const DESC_MANAGER::DESC_SET & c_ref_set = DESC_MANAGER::instance().GetClientSet();

				if (false)
				{
					std::for_each(c_ref_set.begin(), c_ref_set.end(), 
							FYmirChatPacket(pack_chat,
								buf,
								strlen(buf),
								ch->GetName(),
								strlen(ch->GetName()),
								ch->GetMapIndex(),
								ch->GetEmpire(),
								ch->IsEquipUniqueGroup(UNIQUE_GROUP_RING_OF_LANGUAGE)));
				}
				else
				{
					std::for_each(c_ref_set.begin(), c_ref_set.end(), 
							FEmpireChatPacket(pack_chat,
								chatbuf,
								len, 
								(ch->GetGMLevel() > GM_PLAYER ||
								 ch->IsEquipUniqueGroup(UNIQUE_GROUP_RING_OF_LANGUAGE)) ? 0 : ch->GetEmpire(), 
								ch->GetMapIndex(), strlen(ch->GetName())));
				}
			}
			break;

		case CHAT_TYPE_PARTY:
			{
				if (!ch->GetParty())
					ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("íŒŒí‹° ì¤‘ì´ ì•„ë‹™ë‹ˆë‹¤."));
				else
				{
					TEMP_BUFFER tbuf;
					
					tbuf.write(&pack_chat, sizeof(pack_chat));
					tbuf.write(chatbuf, len);

					RawPacketToCharacterFunc f(tbuf.read_peek(), tbuf.size());
					ch->GetParty()->ForEachOnlineMember(f);
				}
			}
			break;

		case CHAT_TYPE_GUILD:
			{
				if (!ch->GetGuild())
					ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê¸¸ë“œì— ê°€ì…í•˜ì§€ ì•Šì•˜ìŠµë‹ˆë‹¤."));
				else
					ch->GetGuild()->Chat(chatbuf);
			}
			break;

		default:
			sys_err("Unknown chat type %d", pinfo->type);
			break;
	}

	return (iExtraLen);
}

void CInputMain::ItemUse(LPCHARACTER ch, const char * data)
{
	ch->UseItem(((struct command_item_use *) data)->Cell);
}

void CInputMain::ItemToItem(LPCHARACTER ch, const char * pcData)
{
	TPacketCGItemUseToItem * p = (TPacketCGItemUseToItem *) pcData;
	if (ch)
		ch->UseItem(p->Cell, p->TargetCell);
}

void CInputMain::ItemDrop(LPCHARACTER ch, const char * data)
{
	struct command_item_drop * pinfo = (struct command_item_drop *) data;

	if (!ch)
		return;

	// ì—˜í¬ê°€ 0ë³´ë‹¤ í¬ë©´ ì—˜í¬ë¥¼ ë²„ë¦¬ëŠ” ê²ƒ ì´ë‹¤.
	if (pinfo->gold > 0)
		ch->DropGold(pinfo->gold);
	else
		ch->DropItem(pinfo->Cell);
}

void CInputMain::ItemDrop2(LPCHARACTER ch, const char * data)
{
	TPacketCGItemDrop2 * pinfo = (TPacketCGItemDrop2 *) data;

	// ì—˜í¬ê°€ 0ë³´ë‹¤ í¬ë©´ ì—˜í¬ë¥¼ ë²„ë¦¬ëŠ” ê²ƒ ì´ë‹¤.
	
	if (!ch)
		return;
	if (pinfo->gold > 0)
		ch->DropGold(pinfo->gold);
	else
		ch->DropItem(pinfo->Cell, pinfo->count);
}

void CInputMain::ItemMove(LPCHARACTER ch, const char * data)
{
	struct command_item_move * pinfo = (struct command_item_move *) data;

	if (ch)
	{
		// [KEYF_TRAIN] patch start — INV_MOVE event
		KEYF_LOG(ch, "INV_MOVE",
			"from_window=%d from_cell=%d to_window=%d to_cell=%d count=%d",
			pinfo->Cell.window_type, pinfo->Cell.cell,
			pinfo->CellTo.window_type, pinfo->CellTo.cell,
			pinfo->count);
		// [KEYF_TRAIN] patch end
		ch->MoveItem(pinfo->Cell, pinfo->CellTo, pinfo->count);
	}
}

void CInputMain::ItemPickup(LPCHARACTER ch, const char * data)
{
	struct command_item_pickup * pinfo = (struct command_item_pickup*) data;
	if (ch)
	{
		// [KEYF_TRAIN] patch start — ITEM_PICKUP event
		KEYF_LOG(ch, "ITEM_PICKUP", "vid=%u", pinfo->vid);
		// [KEYF_TRAIN] patch end
		ch->PickupItem(pinfo->vid);
	}
}

void CInputMain::QuickslotAdd(LPCHARACTER ch, const char * data)
{
	struct command_quickslot_add * pinfo = (struct command_quickslot_add *) data;
	// [KEYF_TRAIN] patch start — QUICKSLOT_ADD event
	KEYF_LOG(ch, "QUICKSLOT_ADD",
		"pos=%d slot_type=%d slot_num=%d",
		pinfo->pos, pinfo->slot.type, pinfo->slot.pos);
	// [KEYF_TRAIN] patch end
	ch->SetQuickslot(pinfo->pos, pinfo->slot);
}

void CInputMain::QuickslotDelete(LPCHARACTER ch, const char * data)
{
	struct command_quickslot_del * pinfo = (struct command_quickslot_del *) data;
	// [KEYF_TRAIN] patch start — QUICKSLOT_DEL event
	KEYF_LOG(ch, "QUICKSLOT_DEL", "pos=%d", pinfo->pos);
	// [KEYF_TRAIN] patch end
	ch->DelQuickslot(pinfo->pos);
}

void CInputMain::QuickslotSwap(LPCHARACTER ch, const char * data)
{
	struct command_quickslot_swap * pinfo = (struct command_quickslot_swap *) data;
	// [KEYF_TRAIN] patch start — QUICKSLOT_SWAP event
	KEYF_LOG(ch, "QUICKSLOT_SWAP",
		"pos=%d to_pos=%d",
		pinfo->pos, pinfo->change_pos);
	// [KEYF_TRAIN] patch end
	ch->SwapQuickslot(pinfo->pos, pinfo->change_pos);
}

int CInputMain::Messenger(LPCHARACTER ch, const char* c_pData, size_t uiBytes)
{
	TPacketCGMessenger* p = (TPacketCGMessenger*) c_pData;
	
	if (uiBytes < sizeof(TPacketCGMessenger))
		return -1;

	c_pData += sizeof(TPacketCGMessenger);
	uiBytes -= sizeof(TPacketCGMessenger);

	switch (p->subheader)
	{
		case MessengerSub::CG::ADD_BY_VID:
			{
				if (uiBytes < sizeof(TPacketCGMessengerAddByVID))
					return -1;

				TPacketCGMessengerAddByVID * p2 = (TPacketCGMessengerAddByVID *) c_pData;
				LPCHARACTER ch_companion = CHARACTER_MANAGER::instance().Find(p2->vid);

				if (!ch_companion)
					return sizeof(TPacketCGMessengerAddByVID);

				if (ch->IsObserverMode())
					return sizeof(TPacketCGMessengerAddByVID);

				if (ch_companion->IsBlockMode(BLOCK_MESSENGER_INVITE))
				{
					ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒëŒ€ë°©ì´ ë©”ì‹ ì ¸ ì¶”ê°€ ê±°ë¶€ ìƒíƒœì…ë‹ˆë‹¤."));
					return sizeof(TPacketCGMessengerAddByVID);
				}

				LPDESC d = ch_companion->GetDesc();

				if (!d)
					return sizeof(TPacketCGMessengerAddByVID);

				if (ch->GetGMLevel() == GM_PLAYER && ch_companion->GetGMLevel() != GM_PLAYER)
				{
					ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ë©”ì‹ ì ¸> ìš´ì˜ìëŠ” ë©”ì‹ ì ¸ì— ì¶”ê°€í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
					return sizeof(TPacketCGMessengerAddByVID);
				}

				if (ch->GetDesc() == d) // ìì‹ ì€ ì¶”ê°€í•  ìˆ˜ ì—†ë‹¤.
					return sizeof(TPacketCGMessengerAddByVID);

				MessengerManager::instance().RequestToAdd(ch, ch_companion);
				//MessengerManager::instance().AddToList(ch->GetName(), ch_companion->GetName());
			}
			return sizeof(TPacketCGMessengerAddByVID);

		case MessengerSub::CG::ADD_BY_NAME:
			{
				if (uiBytes < CHARACTER_NAME_MAX_LEN)
					return -1;

				char name[CHARACTER_NAME_MAX_LEN + 1];
				strlcpy(name, c_pData, sizeof(name));

				if (MessengerManager::instance().IsInList(ch->GetName(), name))
				{
					ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("[Friends] You are already friends with %s."), name);
					return CHARACTER_NAME_MAX_LEN;
				}

				if (ch->GetGMLevel() == GM_PLAYER && gm_get_level(name) != GM_PLAYER)
				{
					ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ë©”ì‹ ì ¸> ìš´ì˜ìëŠ” ë©”ì‹ ì ¸ì— ì¶”ê°€í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
					return CHARACTER_NAME_MAX_LEN;
				}

				LPCHARACTER tch = CHARACTER_MANAGER::instance().FindPC(name);

				if (!tch)
				{
					const CCI* pkCCI = P2P_MANAGER::instance().Find(name);

					if (!pkCCI)
					{
						ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("%s ë‹˜ì€ ì ‘ì†ë˜ ìˆì§€ ì•ŠìŠµë‹ˆë‹¤."), name);
						return CHARACTER_NAME_MAX_LEN;
					}

					// P2P request
					MessengerManager::instance().P2PRequestToAdd_Stage1(ch, name);
				}
				else
				{
					if (tch == ch) // ìì‹ ì€ ì¶”ê°€í•  ìˆ˜ ì—†ë‹¤.
					{
						ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("[Friends] You cannot add yourself as a friend."));

						return CHARACTER_NAME_MAX_LEN;
					}

					if (tch->IsBlockMode(BLOCK_MESSENGER_INVITE) == true)
					{
						ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒëŒ€ë°©ì´ ë©”ì‹ ì ¸ ì¶”ê°€ ê±°ë¶€ ìƒíƒœì…ë‹ˆë‹¤."));
					}
					else
					{
						// ë©”ì‹ ì €ê°€ ìºë¦­í„°ë‹¨ìœ„ê°€ ë˜ë©´ì„œ ë³€ê²½
						MessengerManager::instance().RequestToAdd(ch, tch);
						//MessengerManager::instance().AddToList(ch->GetName(), tch->GetName());
					}
				}
			}
			return CHARACTER_NAME_MAX_LEN;

		case MessengerSub::CG::REMOVE:
			{
				if (uiBytes < CHARACTER_NAME_MAX_LEN)
					return -1;

				char char_name[CHARACTER_NAME_MAX_LEN + 1];
				strlcpy(char_name, c_pData, sizeof(char_name));

				// MR-3: Remove from messenger Fix
				MessengerManager::instance().RemoveFromList(ch->GetName(), char_name);
        		MessengerManager::instance().RemoveFromList(char_name, ch->GetName(), false);	// friend removed from companion too.
				// MR-3: -- END OF -- Remove from messenger Fix
			}
			return CHARACTER_NAME_MAX_LEN;

		default:
			sys_err("CInputMain::Messenger : Unknown subheader %d : %s", p->subheader, ch->GetName());
			break;
	}

	return 0;
}

int CInputMain::Shop(LPCHARACTER ch, const char * data, size_t uiBytes)
{
	TPacketCGShop * p = (TPacketCGShop *) data;

	if (uiBytes < sizeof(TPacketCGShop))
		return -1;

	if (test_server)
		sys_log(0, "CInputMain::Shop() ==> SubHeader %d", p->subheader);

	const char * c_pData = data + sizeof(TPacketCGShop);
	uiBytes -= sizeof(TPacketCGShop);

	switch (p->subheader)
	{
		case ShopSub::CG::END:
			sys_log(1, "INPUT: %s SHOP: END", ch->GetName());
			CShopManager::instance().StopShopping(ch);
			return 0;

		case ShopSub::CG::BUY:
			{
				if (uiBytes < sizeof(BYTE) + sizeof(BYTE))
					return -1;

				BYTE bPos = *(c_pData + 1);
				sys_log(1, "INPUT: %s SHOP: BUY %d", ch->GetName(), bPos);
				CShopManager::instance().Buy(ch, bPos);
				return (sizeof(BYTE) + sizeof(BYTE));
			}

		case ShopSub::CG::SELL:
			{
				if (uiBytes < sizeof(BYTE))
					return -1;

				BYTE pos = *c_pData;

				sys_log(0, "INPUT: %s SHOP: SELL", ch->GetName());
				CShopManager::instance().Sell(ch, pos);
				return sizeof(BYTE);
			}

		case ShopSub::CG::SELL2:
			{
				if (uiBytes < sizeof(BYTE) + sizeof(BYTE))
					return -1;

				BYTE pos = *(c_pData++);
				BYTE count = *(c_pData);

				sys_log(0, "INPUT: %s SHOP: SELL2", ch->GetName());
				CShopManager::instance().Sell(ch, pos, count);
				return sizeof(BYTE) + sizeof(BYTE);
			}

		default:
			sys_err("CInputMain::Shop : Unknown subheader %d : %s", p->subheader, ch->GetName());
			break;
	}

	return 0;
}

void CInputMain::OnClick(LPCHARACTER ch, const char * data)
{
	struct command_on_click *	pinfo = (struct command_on_click *) data;
	LPCHARACTER			victim;

	if ((victim = CHARACTER_MANAGER::instance().Find(pinfo->vid)))
		victim->OnClick(ch);
	else if (test_server)
	{
		sys_err("CInputMain::OnClick %s.Click.NOT_EXIST_VID[%d]", ch->GetName(), pinfo->vid);
	}
}

void CInputMain::Exchange(LPCHARACTER ch, const char * data)
{
	struct command_exchange * pinfo = (struct command_exchange *) data;
	LPCHARACTER	to_ch = NULL;

	if (!ch->CanHandleItem())
		return;

	int iPulse = thecore_pulse(); 
	
	if ((to_ch = CHARACTER_MANAGER::instance().Find(pinfo->arg1)))
	{
		if (iPulse - to_ch->GetSafeboxLoadTime() < PASSES_PER_SEC(g_nPortalLimitTime))
		{
			to_ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê±°ë˜ í›„ %dì´ˆ ì´ë‚´ì— ì°½ê³ ë¥¼ ì—´ìˆ˜ ì—†ìŠµë‹ˆë‹¤."), g_nPortalLimitTime);
			return;
		}

		if( true == to_ch->IsDead() )
		{
			return;
		}
	}

	sys_log(0, "CInputMain()::Exchange()  SubHeader %d ", pinfo->sub_header);

	if (iPulse - ch->GetSafeboxLoadTime() < PASSES_PER_SEC(g_nPortalLimitTime))
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê±°ë˜ í›„ %dì´ˆ ì´ë‚´ì— ì°½ê³ ë¥¼ ì—´ìˆ˜ ì—†ìŠµë‹ˆë‹¤."), g_nPortalLimitTime);
		return;
	}


	switch (pinfo->sub_header)
	{
		case ExchangeSub::CG::START:	// arg1 == vid of target character
			if (!ch->GetExchange())
			{
				if ((to_ch = CHARACTER_MANAGER::instance().Find(pinfo->arg1)))
				{
					if (iPulse - ch->GetSafeboxLoadTime() < PASSES_PER_SEC(g_nPortalLimitTime))
					{
						ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì°½ê³ ë¥¼ ì—°í›„ %dì´ˆ ì´ë‚´ì—ëŠ” ê±°ë˜ë¥¼ í• ìˆ˜ ì—†ìŠµë‹ˆë‹¤."), g_nPortalLimitTime);

						if (test_server)
							ch->ChatPacket(CHAT_TYPE_INFO, "[TestOnly][Safebox]Pulse %d LoadTime %d PASS %d", iPulse, ch->GetSafeboxLoadTime(), PASSES_PER_SEC(g_nPortalLimitTime));
						return; 
					}

					if (iPulse - to_ch->GetSafeboxLoadTime() < PASSES_PER_SEC(g_nPortalLimitTime))
					{
						to_ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì°½ê³ ë¥¼ ì—°í›„ %dì´ˆ ì´ë‚´ì—ëŠ” ê±°ë˜ë¥¼ í• ìˆ˜ ì—†ìŠµë‹ˆë‹¤."), g_nPortalLimitTime);


						if (test_server)
							to_ch->ChatPacket(CHAT_TYPE_INFO, "[TestOnly][Safebox]Pulse %d LoadTime %d PASS %d", iPulse, to_ch->GetSafeboxLoadTime(), PASSES_PER_SEC(g_nPortalLimitTime));
						return; 
					}

					if (ch->GetGold() >= GOLD_MAX)
					{	
						ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì•¡ìˆ˜ê°€ 20ì–µ ëƒ¥ì„ ì´ˆê³¼í•˜ì—¬ ê±°ë˜ë¥¼ í• ìˆ˜ê°€ ì—†ìŠµë‹ˆë‹¤.."));

						sys_err("[OVERFLOG_GOLD] START (%u) id %u name %s ", ch->GetGold(), ch->GetPlayerID(), ch->GetName());
						return;
					}

					if (to_ch->IsPC())
					{
						if (quest::CQuestManager::instance().GiveItemToPC(ch->GetPlayerID(), to_ch))
						{
							sys_log(0, "Exchange canceled by quest %s %s", ch->GetName(), to_ch->GetName());
							return;
						}
					}


					if (ch->GetMyShop() || ch->IsOpenSafebox() || ch->GetShopOwner() || ch->IsCubeOpen())
					{
						ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë‹¤ë¥¸ ê±°ë˜ì¤‘ì¼ê²½ìš° ê°œì¸ìƒì ì„ ì—´ìˆ˜ê°€ ì—†ìŠµë‹ˆë‹¤."));
						return;
					}

					ch->ExchangeStart(to_ch);
				}
			}
			break;

		case ExchangeSub::CG::ITEM_ADD:	// arg1 == position of item, arg2 == position in exchange window
			if (ch->GetExchange())
			{
				if (ch->GetExchange()->GetCompany()->GetAcceptStatus() != true)
					ch->GetExchange()->AddItem(pinfo->Pos, pinfo->arg2);
			}
			break;

		case ExchangeSub::CG::ITEM_DEL:	// arg1 == position of item
			if (ch->GetExchange())
			{
				if (ch->GetExchange()->GetCompany()->GetAcceptStatus() != true)
					ch->GetExchange()->RemoveItem(pinfo->arg1);
			}
			break;

		case ExchangeSub::CG::ELK_ADD:	// arg1 == amount of gold
			if (ch->GetExchange())
			{
				const int64_t nTotalGold = static_cast<int64_t>(ch->GetExchange()->GetCompany()->GetOwner()->GetGold()) + static_cast<int64_t>(pinfo->arg1);

				if (GOLD_MAX <= nTotalGold)
				{
					ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒëŒ€ë°©ì˜ ì´ê¸ˆì•¡ì´ 20ì–µ ëƒ¥ì„ ì´ˆê³¼í•˜ì—¬ ê±°ë˜ë¥¼ í• ìˆ˜ê°€ ì—†ìŠµë‹ˆë‹¤.."));

					sys_err("[OVERFLOW_GOLD] ELK_ADD (%u) id %u name %s ",
							ch->GetExchange()->GetCompany()->GetOwner()->GetGold(),
							ch->GetExchange()->GetCompany()->GetOwner()->GetPlayerID(),
						   	ch->GetExchange()->GetCompany()->GetOwner()->GetName());

					return;
				}

				if (ch->GetExchange()->GetCompany()->GetAcceptStatus() != true)
					ch->GetExchange()->AddGold(pinfo->arg1);
			}
			break;

		case ExchangeSub::CG::ACCEPT:	// arg1 == not used
			if (ch->GetExchange())
			{
				sys_log(0, "CInputMain()::Exchange() ==> ACCEPT "); 
				ch->GetExchange()->Accept(true);
			}

			break;

		case ExchangeSub::CG::CANCEL:	// arg1 == not used
			if (ch->GetExchange())
				ch->GetExchange()->Cancel();
			break;
	}
}

void CInputMain::Position(LPCHARACTER ch, const char * data)
{
	struct command_position * pinfo = (struct command_position *) data;

	switch (pinfo->position)
	{
		case POSITION_GENERAL:
			ch->Standup();
			break;

		case POSITION_SITTING_CHAIR:
			ch->Sitdown(0);
			break;

		case POSITION_SITTING_GROUND:
			ch->Sitdown(1);
			break;
	}
}

static const int ComboSequenceBySkillLevel[3][8] = 
{
	// 0   1   2   3   4   5   6   7
	{ 14, 15, 16, 17,  0,  0,  0,  0 },
	{ 14, 15, 16, 18, 20,  0,  0,  0 },
	{ 14, 15, 16, 18, 19, 17,  0,  0 },
};

#define COMBO_HACK_ALLOWABLE_MS	100

// [2013 09 11 CYH]
DWORD ClacValidComboInterval( LPCHARACTER ch, BYTE bArg )
{
	int nInterval = 300;
	float fAdjustNum = 1.5f; // ì¼ë°˜ ìœ ì €ê°€ speed hack ì— ê±¸ë¦¬ëŠ” ê²ƒì„ ë§‰ê¸° ìœ„í•´. 2013.09.10 CYH

	if( !ch )
	{
		sys_err( "ClacValidComboInterval() ch is NULL");
		return nInterval;
	}	

	if( bArg == 13 )
	{
		float normalAttackDuration = CMotionManager::instance().GetNormalAttackDuration(ch->GetRaceNum());
		nInterval = (int) (normalAttackDuration / (((float) ch->GetPoint(POINT_ATT_SPEED) / 100.f) * 900.f) + fAdjustNum );
	}
	else if( bArg == 14 )
	{		
		nInterval = (int)(ani_combo_speed(ch, 1 ) / ((ch->GetPoint(POINT_ATT_SPEED) / 100.f) + fAdjustNum) );
	}
	else if( bArg > 14 && bArg << 22 )
	{
		nInterval = (int)(ani_combo_speed(ch, bArg - 13 ) / ((ch->GetPoint(POINT_ATT_SPEED) / 100.f) + fAdjustNum) );
	}
	else
	{
		sys_err( "ClacValidComboInterval() Invalid bArg(%d) ch(%s)", bArg, ch->GetName() );		
	}	

	return nInterval;
}

bool CheckComboHack(LPCHARACTER ch, BYTE bArg, DWORD dwTime, bool CheckSpeedHack)
{
	//	ì£½ê±°ë‚˜ ê¸°ì ˆ ìƒíƒœì—ì„œëŠ” ê³µê²©í•  ìˆ˜ ì—†ìœ¼ë¯€ë¡œ, skipí•œë‹¤.
	//	ì´ë ‡ê²Œ í•˜ì§€ ë§ê³ , CHRACTER::CanMove()ì— 
	//	if (IsStun() || IsDead()) return false;
	//	ë¥¼ ì¶”ê°€í•˜ëŠ”ê²Œ ë§ë‹¤ê³  ìƒê°í•˜ë‚˜,
	//	ì´ë¯¸ ë‹¤ë¥¸ ë¶€ë¶„ì—ì„œ CanMove()ëŠ” IsStun(), IsDead()ê³¼
	//	ë…ë¦½ì ìœ¼ë¡œ ì²´í¬í•˜ê³  ìˆê¸° ë•Œë¬¸ì— ìˆ˜ì •ì— ì˜í•œ ì˜í–¥ì„
	//	ìµœì†Œí™”í•˜ê¸° ìœ„í•´ ì´ë ‡ê²Œ ë•œë¹µ ì½”ë“œë¥¼ ì¨ë†“ëŠ”ë‹¤.
	if (ch->IsStun() || ch->IsDead())
		return false;
	int ComboInterval = dwTime - ch->GetLastComboTime();
	int HackScalar = 0; // ê¸°ë³¸ ìŠ¤ì¹¼ë¼ ë‹¨ìœ„ 1

	// [2013 09 11 CYH] debugging log
		/*sys_log(0, "COMBO_TEST_LOG: %s arg:%u interval:%d valid:%u atkspd:%u riding:%s",
						ch->GetName(),
						bArg,
						ComboInterval,
						ch->GetValidComboInterval(),
						ch->GetPoint(POINT_ATT_SPEED),
						ch->IsRiding() ? "yes" : "no");*/

#if 0	
	sys_log(0, "COMBO: %s arg:%u seq:%u delta:%d checkspeedhack:%d",
			ch->GetName(), bArg, ch->GetComboSequence(), ComboInterval - ch->GetValidComboInterval(), CheckSpeedHack);
#endif
	// bArg 14 ~ 21ë²ˆ ê¹Œì§€ ì´ 8ì½¤ë³´ ê°€ëŠ¥
	// 1. ì²« ì½¤ë³´(14)ëŠ” ì¼ì • ì‹œê°„ ì´í›„ì— ë°˜ë³µ ê°€ëŠ¥
	// 2. 15 ~ 21ë²ˆì€ ë°˜ë³µ ë¶ˆê°€ëŠ¥
	// 3. ì°¨ë¡€ëŒ€ë¡œ ì¦ê°€í•œë‹¤.
	if (bArg == 14)
	{
		if (CheckSpeedHack && ComboInterval > 0 && ComboInterval < ch->GetValidComboInterval() - COMBO_HACK_ALLOWABLE_MS)
		{
			// FIXME ì²«ë²ˆì§¸ ì½¤ë³´ëŠ” ì´ìƒí•˜ê²Œ ë¹¨ë¦¬ ì˜¬ ìˆ˜ê°€ ìˆì–´ì„œ 300ìœ¼ë¡œ ë‚˜ëˆ” -_-;
			// ë‹¤ìˆ˜ì˜ ëª¬ìŠ¤í„°ì— ì˜í•´ ë‹¤ìš´ë˜ëŠ” ìƒí™©ì—ì„œ ê³µê²©ì„ í•˜ë©´
			// ì²«ë²ˆì§¸ ì½¤ë³´ê°€ ë§¤ìš° ì ì€ ì¸í„°ë²Œë¡œ ë“¤ì–´ì˜¤ëŠ” ìƒí™© ë°œìƒ.
			// ì´ë¡œ ì¸í•´ ì½¤ë³´í•µìœ¼ë¡œ íŠ•ê¸°ëŠ” ê²½ìš°ê°€ ìˆì–´ ë‹¤ìŒ ì½”ë“œ ë¹„ í™œì„±í™”.
			//HackScalar = 1 + (ch->GetValidComboInterval() - ComboInterval) / 300;

			//sys_log(0, "COMBO_HACK: 2 %s arg:%u interval:%d valid:%u atkspd:%u riding:%s",
			//		ch->GetName(),
			//		bArg,
			//		ComboInterval,
			//		ch->GetValidComboInterval(),
			//		ch->GetPoint(POINT_ATT_SPEED),
			//	    ch->IsRiding() ? "yes" : "no");
		}

		ch->SetComboSequence(1);
		// 2013 09 11 CYH edited
		//ch->SetValidComboInterval((int) (ani_combo_speed(ch, 1) / (ch->GetPoint(POINT_ATT_SPEED) / 100.f)));
		ch->SetValidComboInterval( ClacValidComboInterval(ch, bArg) );
		ch->SetLastComboTime(dwTime);
	}
	else if (bArg > 14 && bArg < 22)
	{
		int idx = MIN(2, ch->GetComboIndex());

		if (ch->GetComboSequence() > 5) // í˜„ì¬ 6ì½¤ë³´ ì´ìƒì€ ì—†ë‹¤.
		{
			HackScalar = 1;
			ch->SetValidComboInterval(300);
			sys_log(0, "COMBO_HACK: 5 %s combo_seq:%d", ch->GetName(), ch->GetComboSequence());
		}
		// ìê° ìŒìˆ˜ ì½¤ë³´ ì˜ˆì™¸ì²˜ë¦¬
		else if (bArg == 21 &&
				 idx == 2 &&
				 ch->GetComboSequence() == 5 &&
				 ch->GetJob() == JOB_ASSASSIN &&
				 ch->GetWear(WEAR_WEAPON) &&
				 ch->GetWear(WEAR_WEAPON)->GetSubType() == WEAPON_DAGGER)
			ch->SetValidComboInterval(300);
		else if (ComboSequenceBySkillLevel[idx][ch->GetComboSequence()] != bArg)
		{
			HackScalar = 1;
			ch->SetValidComboInterval(300);

			sys_log(0, "COMBO_HACK: 3 %s arg:%u valid:%u combo_idx:%d combo_seq:%d",
					ch->GetName(),
					bArg,
					ComboSequenceBySkillLevel[idx][ch->GetComboSequence()],
					idx,
					ch->GetComboSequence());
		}
		else
		{
			if (CheckSpeedHack && ComboInterval < ch->GetValidComboInterval() - COMBO_HACK_ALLOWABLE_MS)
			{
				HackScalar = 1 + (ch->GetValidComboInterval() - ComboInterval) / 100;

				sys_log(0, "COMBO_HACK: 2 %s arg:%u interval:%d valid:%u atkspd:%u riding:%s",
						ch->GetName(),
						bArg,
						ComboInterval,
						ch->GetValidComboInterval(),
						ch->GetPoint(POINT_ATT_SPEED),
						ch->IsRiding() ? "yes" : "no");
			}

			// ë§ì„ íƒ”ì„ ë•ŒëŠ” 15ë²ˆ ~ 16ë²ˆì„ ë°˜ë³µí•œë‹¤
			//if (ch->IsHorseRiding())
			if (ch->IsRiding())
				ch->SetComboSequence(ch->GetComboSequence() == 1 ? 2 : 1);
			else
				ch->SetComboSequence(ch->GetComboSequence() + 1);

			// 2013 09 11 CYH edited
			//ch->SetValidComboInterval((int) (ani_combo_speed(ch, bArg - 13) / (ch->GetPoint(POINT_ATT_SPEED) / 100.f)));
			ch->SetValidComboInterval( ClacValidComboInterval(ch, bArg) );
			ch->SetLastComboTime(dwTime);
		}
	}
	else if (bArg == 13) // ê¸°ë³¸ ê³µê²© (ë‘”ê°‘(Polymorph)í–ˆì„ ë•Œ ì˜¨ë‹¤)
	{
		if (CheckSpeedHack && ComboInterval > 0 && ComboInterval < ch->GetValidComboInterval() - COMBO_HACK_ALLOWABLE_MS)
		{
			// ë‹¤ìˆ˜ì˜ ëª¬ìŠ¤í„°ì— ì˜í•´ ë‹¤ìš´ë˜ëŠ” ìƒí™©ì—ì„œ ê³µê²©ì„ í•˜ë©´
			// ì²«ë²ˆì§¸ ì½¤ë³´ê°€ ë§¤ìš° ì ì€ ì¸í„°ë²Œë¡œ ë“¤ì–´ì˜¤ëŠ” ìƒí™© ë°œìƒ.
			// ì´ë¡œ ì¸í•´ ì½¤ë³´í•µìœ¼ë¡œ íŠ•ê¸°ëŠ” ê²½ìš°ê°€ ìˆì–´ ë‹¤ìŒ ì½”ë“œ ë¹„ í™œì„±í™”.
			//HackScalar = 1 + (ch->GetValidComboInterval() - ComboInterval) / 100;

			//sys_log(0, "COMBO_HACK: 6 %s arg:%u interval:%d valid:%u atkspd:%u",
			//		ch->GetName(),
			//		bArg,
			//		ComboInterval,
			//		ch->GetValidComboInterval(),
			//		ch->GetPoint(POINT_ATT_SPEED));
		}

		if (ch->GetRaceNum() >= MAIN_RACE_MAX_NUM)
		{
			// POLYMORPH_BUG_FIX
			
			// DELETEME
			/*
			const CMotion * pkMotion = CMotionManager::instance().GetMotion(ch->GetRaceNum(), MAKE_MOTION_KEY(MOTION_MODE_GENERAL, MOTION_NORMAL_ATTACK));

			if (!pkMotion)
				sys_err("cannot find motion by race %u", ch->GetRaceNum());
			else
			{
				// ì •ìƒì  ê³„ì‚°ì´ë¼ë©´ 1000.fë¥¼ ê³±í•´ì•¼ í•˜ì§€ë§Œ í´ë¼ì´ì–¸íŠ¸ê°€ ì• ë‹ˆë©”ì´ì…˜ ì†ë„ì˜ 90%ì—ì„œ
				// ë‹¤ìŒ ì• ë‹ˆë©”ì´ì…˜ ë¸”ë Œë”©ì„ í—ˆìš©í•˜ë¯€ë¡œ 900.fë¥¼ ê³±í•œë‹¤.
				int k = (int) (pkMotion->GetDuration() / ((float) ch->GetPoint(POINT_ATT_SPEED) / 100.f) * 900.f);
				ch->SetValidComboInterval(k);
				ch->SetLastComboTime(dwTime);
			}
			*/

			// 2013 09 11 CYH edited
			//float normalAttackDuration = CMotionManager::instance().GetNormalAttackDuration(ch->GetRaceNum());
			//int k = (int) (normalAttackDuration / ((float) ch->GetPoint(POINT_ATT_SPEED) / 100.f) * 900.f);			
			//ch->SetValidComboInterval(k);
			ch->SetValidComboInterval( ClacValidComboInterval(ch, bArg) );
			ch->SetLastComboTime(dwTime);
			// END_OF_POLYMORPH_BUG_FIX
		}
		else
		{
			// ë§ì´ ì•ˆë˜ëŠ” ì½¤ë³´ê°€ ì™”ë‹¤ í•´ì»¤ì¼ ê°€ëŠ¥ì„±?
			//if (ch->GetDesc()->DelayedDisconnect(number(2, 9)))
			//{
			//	LogManager::instance().HackLog("Hacker", ch);
			//	sys_log(0, "HACKER: %s arg %u", ch->GetName(), bArg);
			//}

			// ìœ„ ì½”ë“œë¡œ ì¸í•´, í´ë¦¬ëª¨í”„ë¥¼ í‘¸ëŠ” ì¤‘ì— ê³µê²© í•˜ë©´,
			// ê°€ë” í•µìœ¼ë¡œ ì¸ì‹í•˜ëŠ” ê²½ìš°ê°€ ìˆë‹¤.

			// ìì„¸íˆ ë§í˜€ë©´,
			// ì„œë²„ì—ì„œ poly 0ë¥¼ ì²˜ë¦¬í–ˆì§€ë§Œ,
			// í´ë¼ì—ì„œ ê·¸ íŒ¨í‚·ì„ ë°›ê¸° ì „ì—, ëª¹ì„ ê³µê²©. <- ì¦‰, ëª¹ì¸ ìƒíƒœì—ì„œ ê³µê²©.
			//
			// ê·¸ëŸ¬ë©´ í´ë¼ì—ì„œëŠ” ì„œë²„ì— ëª¹ ìƒíƒœë¡œ ê³µê²©í–ˆë‹¤ëŠ” ì»¤ë§¨ë“œë¥¼ ë³´ë‚´ê³  (arg == 13)
			//
			// ì„œë²„ì—ì„œëŠ” raceëŠ” ì¸ê°„ì¸ë° ê³µê²©í˜•íƒœëŠ” ëª¹ì¸ ë†ˆì´ë‹¤! ë¼ê³  í•˜ì—¬ í•µì²´í¬ë¥¼ í–ˆë‹¤.

			// ì‚¬ì‹¤ ê³µê²© íŒ¨í„´ì— ëŒ€í•œ ê²ƒì€ í´ë¼ì´ì–¸íŠ¸ì—ì„œ íŒë‹¨í•´ì„œ ë³´ë‚¼ ê²ƒì´ ì•„ë‹ˆë¼,
			// ì„œë²„ì—ì„œ íŒë‹¨í•´ì•¼ í•  ê²ƒì¸ë°... ì™œ ì´ë ‡ê²Œ í•´ë†¨ì„ê¹Œ...
			// by rtsummit
		}
	}
	else
	{
		// ë§ì´ ì•ˆë˜ëŠ” ì½¤ë³´ê°€ ì™”ë‹¤ í•´ì»¤ì¼ ê°€ëŠ¥ì„±?
		if (ch->GetDesc()->DelayedDisconnect(number(2, 9)))
		{
			LogManager::instance().HackLog("Hacker", ch);
			sys_log(0, "HACKER: %s arg %u", ch->GetName(), bArg);
		}

		HackScalar = 10;
		ch->SetValidComboInterval(300);
	}

	if (HackScalar)
	{
		// ë§ì— íƒ€ê±°ë‚˜ ë‚´ë ¸ì„ ë•Œ 1.5ì´ˆê°„ ê³µê²©ì€ í•µìœ¼ë¡œ ê°„ì£¼í•˜ì§€ ì•Šë˜ ê³µê²©ë ¥ì€ ì—†ê²Œ í•˜ëŠ” ì²˜ë¦¬
		if (get_dword_time() - ch->GetLastMountTime() > 1500)
			ch->IncreaseComboHackCount(1 + HackScalar);

		ch->SkipComboAttackByTime(ch->GetValidComboInterval());
	}

	return HackScalar;
}

void CInputMain::Move(LPCHARACTER ch, const char * data)
{
	if (!ch->CanMove())
		return;

	struct command_move * pinfo = (struct command_move *) data;

	if (pinfo->bFunc >= FUNC_MAX_NUM && !(pinfo->bFunc & 0x80))
	{
		sys_err("invalid move type: %s", ch->GetName());
		return;
	}

	// [KEYF_TRAIN] patch start — MOVE_START event
	KEYF_LOG(ch, "MOVE_START",
		"from=(%d,%d) to=(%d,%d) rot=%d func=%d dwTime=%u",
		ch->GetX(), ch->GetY(), pinfo->lX, pinfo->lY,
		pinfo->bRot * 5, pinfo->bFunc, pinfo->dwTime);
	// [KEYF_TRAIN] patch end

	//enum EMoveFuncType
	//{   
	//	FUNC_WAIT,
	//	FUNC_MOVE,
	//	FUNC_ATTACK,
	//	FUNC_COMBO,
	//	FUNC_MOB_SKILL,
	//	_FUNC_SKILL,
	//	FUNC_MAX_NUM,
	//	FUNC_SKILL = 0x80,
	//};  

	// í…”ë ˆí¬íŠ¸ í•µ ì²´í¬

//	if (!test_server)	//2012.05.15 ê¹€ìš©ìš± : í…Œì„­ì—ì„œ (ë¬´ì ìƒíƒœë¡œ) ë‹¤ìˆ˜ ëª¬ìŠ¤í„° ìƒëŒ€ë¡œ ë‹¤ìš´ë˜ë©´ì„œ ê³µê²©ì‹œ ì½¤ë³´í•µìœ¼ë¡œ ì£½ëŠ” ë¬¸ì œê°€ ìˆì—ˆë‹¤.
	{
		const float fDist = DISTANCE_SQRT((ch->GetX() - pinfo->lX) / 100, (ch->GetY() - pinfo->lY) / 100);

		if (((false == ch->IsRiding() && fDist > 25) || fDist > 40) && OXEVENT_MAP_INDEX != ch->GetMapIndex())
		{
			if( false == LC_IsEurope() )
			{
				const PIXEL_POSITION & warpPos = ch->GetWarpPosition();

				if (warpPos.x == 0 && warpPos.y == 0)
					LogManager::instance().HackLog("Teleport", ch); // ë¶€ì •í™•í•  ìˆ˜ ìˆìŒ
			}

			sys_log(0, "MOVE: %s trying to move too far (dist: %.1fm) Riding(%d)", ch->GetName(), fDist, ch->IsRiding());

			ch->Show(ch->GetMapIndex(), ch->GetX(), ch->GetY(), ch->GetZ());
			ch->Stop();
			return;
		}

		//
		// ìŠ¤í”¼ë“œí•µ(SPEEDHACK) Check
		//
		DWORD dwCurTime = get_dword_time();
		// ì‹œê°„ì„ Syncí•˜ê³  7ì´ˆ í›„ ë¶€í„° ê²€ì‚¬í•œë‹¤. (20090702 ì´ì „ì—” 5ì´ˆì˜€ìŒ)
		bool CheckSpeedHack = (dwCurTime - ch->GetDesc()->GetClientTime() > 7000);

		if (CheckSpeedHack)
		{
			int iDelta = (int) (pinfo->dwTime - ch->GetDesc()->GetClientTime());
			int iServerDelta = (int) (dwCurTime - ch->GetDesc()->GetClientTime());

			iDelta = (int) (dwCurTime - pinfo->dwTime);

			// ì‹œê°„ì´ ëŠ¦ê²Œê°„ë‹¤. ì¼ë‹¨ ë¡œê·¸ë§Œ í•´ë‘”ë‹¤. ì§„ì§œ ì´ëŸ° ì‚¬ëŒë“¤ì´ ë§ì€ì§€ ì²´í¬í•´ì•¼í•¨. TODO
			if (iDelta >= 30000)
			{
				sys_log(0, "SPEEDHACK: slow timer name %s delta %d", ch->GetName(), iDelta);
				// ch->GetDesc()->DelayedDisconnect(3);
			}
			// 1ì´ˆì— 20msec ë¹¨ë¦¬ ê°€ëŠ”ê±° ê¹Œì§€ëŠ” ì´í•´í•œë‹¤.
			else if (iDelta < -(iServerDelta / 50))
			{
				sys_log(0, "SPEEDHACK: DETECTED! %s (delta %d %d)", ch->GetName(), iDelta, iServerDelta);
				// ch->GetDesc()->DelayedDisconnect(3);
			}
		}

		//
		// ì½¤ë³´í•µ ë° ìŠ¤í”¼ë“œí•µ ì²´í¬
		//
		if (pinfo->bFunc == FUNC_COMBO && g_bCheckMultiHack)
		{
			CheckComboHack(ch, pinfo->bArg, pinfo->dwTime, CheckSpeedHack); // ì½¤ë³´ ì²´í¬
		}
	}

	if (pinfo->bFunc == FUNC_MOVE)
	{
		if (ch->GetLimitPoint(POINT_MOV_SPEED) == 0)
			return;

		ch->SetRotation(pinfo->bRot * 5);	// ì¤‘ë³µ ì½”ë“œ
		ch->ResetStopTime();				// ""

		ch->Goto(pinfo->lX, pinfo->lY);
	}
	else
	{
		if (pinfo->bFunc == FUNC_ATTACK || pinfo->bFunc == FUNC_COMBO)
			ch->OnMove(true);
		else if (pinfo->bFunc & FUNC_SKILL)
		{
			const int MASK_SKILL_MOTION = 0x7F;
			unsigned int motion = pinfo->bFunc & MASK_SKILL_MOTION;

			if (!ch->IsUsableSkillMotion(motion))
			{
				const char* name = ch->GetName();
				unsigned int job = ch->GetJob();
				unsigned int group = ch->GetSkillGroup();

				char szBuf[256];
				snprintf(szBuf, sizeof(szBuf), "SKILL_HACK: name=%s, job=%d, group=%d, motion=%d", name, job, group, motion);
				LogManager::instance().HackLog(szBuf, ch->GetDesc()->GetAccountTable().login, ch->GetName(), ch->GetDesc()->GetHostName());
				sys_log(0, "%s", szBuf);

				if (test_server)
				{
					ch->GetDesc()->DelayedDisconnect(number(2, 8));
					ch->ChatPacket(CHAT_TYPE_INFO, szBuf);
				}
				else
				{
					ch->GetDesc()->DelayedDisconnect(number(150, 500));
				}
			}

			ch->OnMove();
		}

		ch->SetRotation(pinfo->bRot * 5);	// ì¤‘ë³µ ì½”ë“œ
		ch->ResetStopTime();				// ""

		ch->Move(pinfo->lX, pinfo->lY);
		ch->Stop();
		ch->StopStaminaConsume();
	}

	TPacketGCMove pack;

	pack.header      = GC::MOVE;
	pack.length = sizeof(pack);
	pack.bFunc        = pinfo->bFunc;
	pack.bArg         = pinfo->bArg;
	pack.bRot         = pinfo->bRot;
	pack.dwVID        = ch->GetVID();
	pack.lX           = pinfo->lX;
	pack.lY           = pinfo->lY;
	pack.dwTime       = get_dword_time();
	pack.dwDuration   = (pinfo->bFunc == FUNC_MOVE) ? ch->GetCurrentMoveDuration() : 0;

	ch->PacketAround(&pack, sizeof(TPacketGCMove), ch);
/*
	if (pinfo->dwTime == 10653691) // ë””ë²„ê±° ë°œê²¬
	{
		if (ch->GetDesc()->DelayedDisconnect(number(15, 30)))
			LogManager::instance().HackLog("Debugger", ch);

	}
	else if (pinfo->dwTime == 10653971) // Softice ë°œê²¬
	{
		if (ch->GetDesc()->DelayedDisconnect(number(15, 30)))
			LogManager::instance().HackLog("Softice", ch);
	}
*/
	/*
	sys_log(0, 
			"MOVE: %s Func:%u Arg:%u Pos:%dx%d Time:%u Dist:%.1f",
			ch->GetName(),
			pinfo->bFunc,
			pinfo->bArg,
			pinfo->lX / 100,
			pinfo->lY / 100,
			pinfo->dwTime,
			fDist);
	*/
}

void CInputMain::Attack(LPCHARACTER ch, const uint16_t header, const char* data)
{
	if (NULL == ch)
		return;

	struct type_identifier
	{
		uint16_t header;
		uint16_t length;
		uint8_t type;
	};

	const struct type_identifier* const type = reinterpret_cast<const struct type_identifier*>(data);

	if (type->type > 0)
	{
		if (false == ch->CanUseSkill(type->type))
		{
			return;
		}

		switch (type->type)
		{
			case SKILL_GEOMPUNG:
			case SKILL_SANGONG:
			case SKILL_YEONSA:
			case SKILL_KWANKYEOK:
			case SKILL_HWAJO:
			case SKILL_GIGUNG:
			case SKILL_PABEOB:
			case SKILL_MARYUNG:
			case SKILL_TUSOK:
			case SKILL_MAHWAN:
			case SKILL_BIPABU:
			case SKILL_NOEJEON:
			case SKILL_CHAIN:
			case SKILL_HORSE_WILDATTACK_RANGE:
				if (CG::SHOOT != type->header)
				{
					if (test_server) 
						ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("Attack :name[%s] Vnum[%d] can't use skill by attack(warning)"), type->type);
					return;
				}
				break;
		}
	}

	switch (header)
	{
		case CG::ATTACK:
			{
				sys_log(0, "ATTACK: Processing attack from %s", ch->GetName());

				if (NULL == ch->GetDesc())
				{
					sys_log(0, "ATTACK: REJECTED - ch->GetDesc() is NULL");
					return;
				}

				const TPacketCGAttack* const packMelee = reinterpret_cast<const TPacketCGAttack*>(data);
				sys_log(0, "ATTACK: dwVID=%u bType=%d CRC1=%d CRC2=%d",
					packMelee->dwVID, packMelee->bType,
					packMelee->bCRCMagicCubeProcPiece, packMelee->bCRCMagicCubeFilePiece);

				ch->GetDesc()->AssembleCRCMagicCube(packMelee->bCRCMagicCubeProcPiece, packMelee->bCRCMagicCubeFilePiece);

				LPCHARACTER	victim = CHARACTER_MANAGER::instance().Find(packMelee->dwVID);

				if (NULL == victim || ch == victim)
				{
					sys_log(0, "ATTACK: REJECTED - victim is NULL or self (victim=%p, ch=%p)", victim, ch);
					return;
				}

				sys_log(0, "ATTACK: victim=%s charType=%d", victim->GetName(), victim->GetCharType());

				switch (victim->GetCharType())
				{
					case CHAR_TYPE_NPC:
						sys_log(0, "ATTACK: REJECTED - victim is CHAR_TYPE_NPC");
						return;
					case CHAR_TYPE_WARP:
						sys_log(0, "ATTACK: REJECTED - victim is CHAR_TYPE_WARP");
						return;
					case CHAR_TYPE_GOTO:
						sys_log(0, "ATTACK: REJECTED - victim is CHAR_TYPE_GOTO");
						return;
				}

				if (packMelee->bType > 0)
				{
					if (false == ch->CheckSkillHitCount(packMelee->bType, victim->GetVID()))
					{
						sys_log(0, "ATTACK: REJECTED - CheckSkillHitCount failed for bType=%d", packMelee->bType);
						return;
					}
				}

				sys_log(0, "ATTACK: Calling ch->Attack(victim=%s, bType=%d)", victim->GetName(), packMelee->bType);
				ch->Attack(victim, packMelee->bType);
			}
			break;

		case CG::SHOOT:
			{
				const TPacketCGShoot* const packShoot = reinterpret_cast<const TPacketCGShoot*>(data);

				ch->Shoot(packShoot->bType);
			}
			break;
	}
}

int CInputMain::SyncPosition(LPCHARACTER ch, const char * c_pcData, size_t uiBytes)
{
	const TPacketCGSyncPosition* pinfo = reinterpret_cast<const TPacketCGSyncPosition*>( c_pcData );

	if (uiBytes < pinfo->length)
		return -1;

	int iExtraLen = pinfo->length - sizeof(TPacketCGSyncPosition);

	if (iExtraLen < 0)
	{
		sys_err("invalid packet length (len %d size %u buffer %u)", iExtraLen, pinfo->length, uiBytes);
		ch->GetDesc()->SetPhase(PHASE_CLOSE);
		return -1;
	}

	if (0 != (iExtraLen % sizeof(TPacketCGSyncPositionElement)))
	{
		sys_err("invalid packet length %d (name: %s)", pinfo->length, ch->GetName());
		return iExtraLen;
	}

	int iCount = iExtraLen / sizeof(TPacketCGSyncPositionElement);

	if (iCount <= 0)
		return iExtraLen;

	static const int nCountLimit = 16;

	if( iCount > nCountLimit )
	{
		//LogManager::instance().HackLog( "SYNC_POSITION_HACK", ch );
		sys_err( "Too many SyncPosition Count(%d) from Name(%s)", iCount, ch->GetName() );
		//ch->GetDesc()->SetPhase(PHASE_CLOSE);
		//return -1;
		iCount = nCountLimit;
	}

	TEMP_BUFFER tbuf;

	TPacketGCSyncPosition * pHeader = (TPacketGCSyncPosition *) tbuf.write_peek(sizeof(TPacketGCSyncPosition));
	tbuf.write_proceed(sizeof(TPacketGCSyncPosition));

	const TPacketCGSyncPositionElement* e = 
		reinterpret_cast<const TPacketCGSyncPositionElement*>(c_pcData + sizeof(TPacketCGSyncPosition));

	timeval tvCurTime;
	gettimeofday(&tvCurTime, NULL);

	for (int i = 0; i < iCount; ++i, ++e)
	{
		LPCHARACTER victim = CHARACTER_MANAGER::instance().Find(e->dwVID);

		if (!victim)
			continue;

		switch (victim->GetCharType())
		{
			case CHAR_TYPE_NPC:
			case CHAR_TYPE_WARP:
			case CHAR_TYPE_GOTO:
				continue;
		}

		// ì†Œìœ ê¶Œ ê²€ì‚¬
		if (!victim->SetSyncOwner(ch))
			continue;

		const float fDistWithSyncOwner = DISTANCE_SQRT( (victim->GetX() - ch->GetX()) / 100, (victim->GetY() - ch->GetY()) / 100 );
		static const float fLimitDistWithSyncOwner = 2500.f + 1000.f;
		// victimê³¼ì˜ ê±°ë¦¬ê°€ 2500 + a ì´ìƒì´ë©´ í•µìœ¼ë¡œ ê°„ì£¼.
		//	ê±°ë¦¬ ì°¸ì¡° : í´ë¼ì´ì–¸íŠ¸ì˜ __GetSkillTargetRange, __GetBowRange í•¨ìˆ˜
		//	2500 : ìŠ¤í‚¬ protoì—ì„œ ê°€ì¥ ì‚¬ê±°ë¦¬ê°€ ê¸´ ìŠ¤í‚¬ì˜ ì‚¬ê±°ë¦¬, ë˜ëŠ” í™œì˜ ì‚¬ê±°ë¦¬
		//	a = POINT_BOW_DISTANCE ê°’... ì¸ë° ì‹¤ì œë¡œ ì‚¬ìš©í•˜ëŠ” ê°’ì¸ì§€ëŠ” ì˜ ëª¨ë¥´ê² ìŒ. ì•„ì´í…œì´ë‚˜ í¬ì…˜, ìŠ¤í‚¬, í€˜ìŠ¤íŠ¸ì—ëŠ” ì—†ëŠ”ë°...
		//		ê·¸ë˜ë„ í˜¹ì‹œë‚˜ í•˜ëŠ” ë§ˆìŒì— ë²„í¼ë¡œ ì‚¬ìš©í•  ê²¸í•´ì„œ 1000.f ë¡œ ë‘ ...
		if (fDistWithSyncOwner > fLimitDistWithSyncOwner)
		{
			// g_iSyncHackLimitCountë²ˆ ê¹Œì§€ëŠ” ë´ì¤Œ.
			if (ch->GetSyncHackCount() < g_iSyncHackLimitCount)
			{
				ch->SetSyncHackCount(ch->GetSyncHackCount() + 1);
				continue;
			}
			else
			{
				LogManager::instance().HackLog( "SYNC_POSITION_HACK", ch );

				sys_err( "Too far SyncPosition DistanceWithSyncOwner(%f)(%s) from Name(%s) CH(%d,%d) VICTIM(%d,%d) SYNC(%d,%d)",
					fDistWithSyncOwner, victim->GetName(), ch->GetName(), ch->GetX(), ch->GetY(), victim->GetX(), victim->GetY(),
					e->lX, e->lY );

				ch->GetDesc()->SetPhase(PHASE_CLOSE);

				return -1;
			}
		}
		
		const float fDist = DISTANCE_SQRT( (victim->GetX() - e->lX) / 100, (victim->GetY() - e->lY) / 100 );
		static const long g_lValidSyncInterval = 50 * 1000; // 100ms -> 50ms 2013 09 11 CYH
		const timeval &tvLastSyncTime = victim->GetLastSyncTime();
		timeval *tvDiff = timediff(&tvCurTime, &tvLastSyncTime);
		
		// SyncPositionì„ ì•…ìš©í•˜ì—¬ íƒ€ìœ ì €ë¥¼ ì´ìƒí•œ ê³³ìœ¼ë¡œ ë³´ë‚´ëŠ” í•µ ë°©ì–´í•˜ê¸° ìœ„í•˜ì—¬,
		// ê°™ì€ ìœ ì €ë¥¼ g_lValidSyncInterval ms ì´ë‚´ì— ë‹¤ì‹œ SyncPositioní•˜ë ¤ê³  í•˜ë©´ í•µìœ¼ë¡œ ê°„ì£¼.
		if (tvDiff->tv_sec == 0 && tvDiff->tv_usec < g_lValidSyncInterval)
		{
			// g_iSyncHackLimitCountë²ˆ ê¹Œì§€ëŠ” ë´ì¤Œ.
			if (ch->GetSyncHackCount() < g_iSyncHackLimitCount)
			{
				ch->SetSyncHackCount(ch->GetSyncHackCount() + 1);
				continue;
			}
			else
			{
				LogManager::instance().HackLog( "SYNC_POSITION_HACK", ch );

				sys_err( "Too often SyncPosition Interval(%ldms)(%s) from Name(%s) VICTIM(%d,%d) SYNC(%d,%d)",
					tvDiff->tv_sec * 1000 + tvDiff->tv_usec / 1000, victim->GetName(), ch->GetName(), victim->GetX(), victim->GetY(),
					e->lX, e->lY );

				ch->GetDesc()->SetPhase(PHASE_CLOSE);

				return -1;
			}
		}
		else if( fDist > 25.0f )
		{
			LogManager::instance().HackLog( "SYNC_POSITION_HACK", ch );

			sys_err( "Too far SyncPosition Distance(%f)(%s) from Name(%s) CH(%d,%d) VICTIM(%d,%d) SYNC(%d,%d)",
				   	fDist, victim->GetName(), ch->GetName(), ch->GetX(), ch->GetY(), victim->GetX(), victim->GetY(),
				  e->lX, e->lY );

			ch->GetDesc()->SetPhase(PHASE_CLOSE);

			return -1;
		}
		else
		{
			victim->SetLastSyncTime(tvCurTime);
			victim->Sync(e->lX, e->lY);
			tbuf.write(e, sizeof(TPacketCGSyncPositionElement));
		}
	}

	if (tbuf.size() != sizeof(TPacketGCSyncPosition))
	{
		pHeader->header = GC::SYNC_POSITION;
		pHeader->length = tbuf.size();

		ch->PacketAround(tbuf.read_peek(), tbuf.size(), ch);
	}

	return iExtraLen;
}

void CInputMain::FlyTarget(LPCHARACTER ch, const char * pcData, uint16_t wHeader)
{
	TPacketCGFlyTargeting * p = (TPacketCGFlyTargeting *) pcData;
	ch->FlyTarget(p->dwTargetVID, p->x, p->y, wHeader);
}

void CInputMain::UseSkill(LPCHARACTER ch, const char * pcData)
{
	TPacketCGUseSkill * p = (TPacketCGUseSkill *) pcData;
	ch->UseSkill(p->dwVnum, CHARACTER_MANAGER::instance().Find(p->dwVID));
}

void CInputMain::ScriptButton(LPCHARACTER ch, const void* c_pData)
{
	TPacketCGScriptButton * p = (TPacketCGScriptButton *) c_pData;
	sys_log(0, "QUEST ScriptButton pid %d idx %u", ch->GetPlayerID(), p->idx);

	quest::PC* pc = quest::CQuestManager::instance().GetPCForce(ch->GetPlayerID());
	if (pc && pc->IsConfirmWait())
	{
		quest::CQuestManager::instance().Confirm(ch->GetPlayerID(), quest::CONFIRM_TIMEOUT);
	}
	else if (p->idx & 0x80000000)
	{
		quest::CQuestManager::Instance().QuestInfo(ch->GetPlayerID(), p->idx & 0x7fffffff);
	}
	else
	{
		quest::CQuestManager::Instance().QuestButton(ch->GetPlayerID(), p->idx);
	}
}

void CInputMain::ScriptAnswer(LPCHARACTER ch, const void* c_pData)
{
	TPacketCGScriptAnswer * p = (TPacketCGScriptAnswer *) c_pData;
	sys_log(0, "QUEST ScriptAnswer pid %d answer %d", ch->GetPlayerID(), p->answer);

	if (p->answer > 250) // ë‹¤ìŒ ë²„íŠ¼ì— ëŒ€í•œ ì‘ë‹µìœ¼ë¡œ ì˜¨ íŒ¨í‚·ì¸ ê²½ìš°
	{
		quest::CQuestManager::Instance().Resume(ch->GetPlayerID());
	}
	else // ì„ íƒ ë²„íŠ¼ì„ ê³¨ë¼ì„œ ì˜¨ íŒ¨í‚·ì¸ ê²½ìš°
	{
		quest::CQuestManager::Instance().Select(ch->GetPlayerID(),  p->answer);
	}
}


// SCRIPT_SELECT_ITEM
void CInputMain::ScriptSelectItem(LPCHARACTER ch, const void* c_pData)
{
	TPacketCGScriptSelectItem* p = (TPacketCGScriptSelectItem*) c_pData;
	sys_log(0, "QUEST ScriptSelectItem pid %d answer %d", ch->GetPlayerID(), p->selection);
	quest::CQuestManager::Instance().SelectItem(ch->GetPlayerID(), p->selection);
}
// END_OF_SCRIPT_SELECT_ITEM

void CInputMain::QuestInputString(LPCHARACTER ch, const void* c_pData)
{
	TPacketCGQuestInputString * p = (TPacketCGQuestInputString*) c_pData;

	char msg[65];
	strlcpy(msg, p->msg, sizeof(msg));
	sys_log(0, "QUEST InputString pid %u msg %s", ch->GetPlayerID(), msg);

	quest::CQuestManager::Instance().Input(ch->GetPlayerID(), msg);
}

void CInputMain::QuestConfirm(LPCHARACTER ch, const void* c_pData)
{
	TPacketCGQuestConfirm* p = (TPacketCGQuestConfirm*) c_pData;
	LPCHARACTER ch_wait = CHARACTER_MANAGER::instance().FindByPID(p->requestPID);
	if (p->answer)
		p->answer = quest::CONFIRM_YES;
	sys_log(0, "QuestConfirm from %s pid %u name %s answer %d", ch->GetName(), p->requestPID, (ch_wait)?ch_wait->GetName():"", p->answer);
	if (ch_wait)
	{
		quest::CQuestManager::Instance().Confirm(ch_wait->GetPlayerID(), (quest::EQuestConfirmType) p->answer, ch->GetPlayerID());
	}
}

void CInputMain::QuestCancel(LPCHARACTER ch)
{
	sys_log(0, "QuestCancel from %s pid %u", ch->GetName(), ch->GetPlayerID());
	quest::CQuestManager::Instance().Cancel(ch->GetPlayerID());
}

void CInputMain::Target(LPCHARACTER ch, const char * pcData)
{
	TPacketCGTarget * p = (TPacketCGTarget *) pcData;

	building::LPOBJECT pkObj = building::CManager::instance().FindObjectByVID(p->dwVID);

	if (pkObj)
	{
		// [KEYF_TRAIN] patch start — TARGET_SELECT (building)
		KEYF_LOG(ch, "TARGET_SELECT", "vid=%u type=BUILDING", p->dwVID);
		// [KEYF_TRAIN] patch end
		TPacketGCTarget pckTarget;
		pckTarget.header = GC::TARGET;
		pckTarget.length = sizeof(pckTarget);
		pckTarget.dwVID = p->dwVID;
		CHARACTER::SafeSendPacketTo(ch, &pckTarget, sizeof(TPacketGCTarget));
	}
	else
	{
		// [KEYF_TRAIN] patch start — TARGET_SELECT (char/mob)
		LPCHARACTER pkTarget = CHARACTER_MANAGER::instance().Find(p->dwVID);
		if (pkTarget)
		{
			KEYF_LOG(ch, "TARGET_SELECT",
				"vid=%u type=%s vnum=%u name=%s hp=%d/%d",
				p->dwVID,
				pkTarget->IsPC() ? "PC" : "MOB",
				pkTarget->GetRaceNum(),
				pkTarget->GetName(),
				pkTarget->GetHP(), pkTarget->GetMaxHP());
		}
		else
		{
			KEYF_LOG(ch, "TARGET_CLEAR", "vid=%u", p->dwVID);
		}
		ch->SetTarget(pkTarget);
		// [KEYF_TRAIN] patch end
	}
}

void CInputMain::Warp(LPCHARACTER ch, const char * pcData)
{
	ch->WarpEnd();
}

void CInputMain::SafeboxCheckin(LPCHARACTER ch, const char * c_pData)
{
	if (quest::CQuestManager::instance().GetPCForce(ch->GetPlayerID())->IsRunning() == true)
		return;

	TPacketCGSafeboxCheckin * p = (TPacketCGSafeboxCheckin *) c_pData;

	if (!ch->CanHandleItem())
		return;

	CSafebox * pkSafebox = ch->GetSafebox();
	LPITEM pkItem = ch->GetItem(p->ItemPos);

	if (!pkSafebox || !pkItem)
		return;
	
	if (pkItem->GetType() == ITEM_BELT && pkItem->IsEquipped()) // Fix
		return;

	if (pkItem->GetCell() >= INVENTORY_MAX_NUM && IS_SET(pkItem->GetFlag(), ITEM_FLAG_IRREMOVABLE))
	{
	    ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ì°½ê³ > ì°½ê³ ë¡œ ì˜®ê¸¸ ìˆ˜ ì—†ëŠ” ì•„ì´í…œ ì…ë‹ˆë‹¤."));
	    return;
	}

	if (!pkSafebox->IsEmpty(p->bSafePos, pkItem->GetSize()))
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ì°½ê³ > ì˜®ê¸¸ ìˆ˜ ì—†ëŠ” ìœ„ì¹˜ì…ë‹ˆë‹¤."));
		return;
	}

	if (pkItem->GetVnum() == UNIQUE_ITEM_SAFEBOX_EXPAND)
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ì°½ê³ > ì´ ì•„ì´í…œì€ ë„£ì„ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		return;
	}

	if( IS_SET(pkItem->GetAntiFlag(), ITEM_ANTIFLAG_SAFEBOX) )
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ì°½ê³ > ì´ ì•„ì´í…œì€ ë„£ì„ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		return;
	}

	if (true == pkItem->isLocked())
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ì°½ê³ > ì´ ì•„ì´í…œì€ ë„£ì„ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		return;
	}

	pkItem->RemoveFromCharacter();
	if (!pkItem->IsDragonSoul())
		ch->SyncQuickslot(QUICKSLOT_TYPE_ITEM, p->ItemPos.cell, 255);
	pkSafebox->Add(p->bSafePos, pkItem);
	
	char szHint[128];
	snprintf(szHint, sizeof(szHint), "%s %u", pkItem->GetName(), pkItem->GetCount());
	LogManager::instance().ItemLog(ch, pkItem, "SAFEBOX PUT", szHint);
}

void CInputMain::SafeboxCheckout(LPCHARACTER ch, const char * c_pData, bool bMall)
{
	TPacketCGSafeboxCheckout * p = (TPacketCGSafeboxCheckout *) c_pData;

	if (!ch->CanHandleItem())
		return;

	CSafebox * pkSafebox;

	if (bMall)
		pkSafebox = ch->GetMall();
	else
		pkSafebox = ch->GetSafebox();

	if (!pkSafebox)
		return;

	LPITEM pkItem = pkSafebox->Get(p->bSafePos);

	if (!pkItem)
		return;
	
	if (!ch->IsEmptyItemGrid(p->ItemPos, pkItem->GetSize()))
		return;

	// ì•„ì´í…œ ëª°ì—ì„œ ì¸ë²¤ìœ¼ë¡œ ì˜®ê¸°ëŠ” ë¶€ë¶„ì—ì„œ ìš©í˜¼ì„ íŠ¹ìˆ˜ ì²˜ë¦¬
	// (ëª°ì—ì„œ ë§Œë“œëŠ” ì•„ì´í…œì€ item_protoì— ì •ì˜ëœëŒ€ë¡œ ì†ì„±ì´ ë¶™ê¸° ë•Œë¬¸ì—,
	//  ìš©í˜¼ì„ì˜ ê²½ìš°, ì´ ì²˜ë¦¬ë¥¼ í•˜ì§€ ì•Šìœ¼ë©´ ì†ì„±ì´ í•˜ë‚˜ë„ ë¶™ì§€ ì•Šê²Œ ëœë‹¤.)
	if (pkItem->IsDragonSoul())
	{
		if (bMall)
		{
			DSManager::instance().DragonSoulItemInitialize(pkItem);
		}

		if (DRAGON_SOUL_INVENTORY != p->ItemPos.window_type)
		{
			ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ì°½ê³ > ì˜®ê¸¸ ìˆ˜ ì—†ëŠ” ìœ„ì¹˜ì…ë‹ˆë‹¤."));
			return;
		}
		
		TItemPos DestPos = p->ItemPos;
		if (!DSManager::instance().IsValidCellForThisItem(pkItem, DestPos))
		{
			int iCell = ch->GetEmptyDragonSoulInventory(pkItem);
			if (iCell < 0)
			{
				ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ì°½ê³ > ì˜®ê¸¸ ìˆ˜ ì—†ëŠ” ìœ„ì¹˜ì…ë‹ˆë‹¤."));
				return ;
			}
			DestPos = TItemPos (DRAGON_SOUL_INVENTORY, iCell);
		}

		pkSafebox->Remove(p->bSafePos);
		pkItem->AddToCharacter(ch, DestPos);
		ITEM_MANAGER::instance().FlushDelayedSave(pkItem);
	}
	else
	{
		if (DRAGON_SOUL_INVENTORY == p->ItemPos.window_type)
		{
			ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ì°½ê³ > ì˜®ê¸¸ ìˆ˜ ì—†ëŠ” ìœ„ì¹˜ì…ë‹ˆë‹¤."));
			return;
		}

		pkSafebox->Remove(p->bSafePos);
		if (bMall)
		{
			if (NULL == pkItem->GetProto())
			{
				sys_err ("pkItem->GetProto() == NULL (id : %d)",pkItem->GetID());
				return ;
			}
			// 100% í™•ë¥ ë¡œ ì†ì„±ì´ ë¶™ì–´ì•¼ í•˜ëŠ”ë° ì•ˆ ë¶™ì–´ìˆë‹¤ë©´ ìƒˆë¡œ ë¶™íŒë‹¤. ...............
			if (100 == pkItem->GetProto()->bAlterToMagicItemPct && 0 == pkItem->GetAttributeCount())
			{
				pkItem->AlterToMagicItem();
			}
		}
		pkItem->AddToCharacter(ch, p->ItemPos);
		ITEM_MANAGER::instance().FlushDelayedSave(pkItem);
	}

	DWORD dwID = pkItem->GetID();
	db_clientdesc->DBPacketHeader(GD::ITEM_FLUSH, 0, sizeof(DWORD));
	db_clientdesc->Packet(&dwID, sizeof(DWORD));

	char szHint[128];
	snprintf(szHint, sizeof(szHint), "%s %u", pkItem->GetName(), pkItem->GetCount());
	if (bMall)
		LogManager::instance().ItemLog(ch, pkItem, "MALL GET", szHint);
	else
		LogManager::instance().ItemLog(ch, pkItem, "SAFEBOX GET", szHint);
}

void CInputMain::SafeboxItemMove(LPCHARACTER ch, const char * data)
{
	struct command_item_move * pinfo = (struct command_item_move *) data;

	if (!ch->CanHandleItem())
		return;

	if (!ch->GetSafebox())
		return;

	ch->GetSafebox()->MoveItem(pinfo->Cell.cell, pinfo->CellTo.cell, pinfo->count);
}

// PARTY_JOIN_BUG_FIX
void CInputMain::PartyInvite(LPCHARACTER ch, const char * c_pData)
{
	if (ch->GetArena())
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ëŒ€ë ¨ì¥ì—ì„œ ì‚¬ìš©í•˜ì‹¤ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		return;
	}

	TPacketCGPartyInvite * p = (TPacketCGPartyInvite*) c_pData;

	LPCHARACTER pInvitee = CHARACTER_MANAGER::instance().Find(p->vid);

	if (!pInvitee || !ch->GetDesc() || !pInvitee->GetDesc())
	{
		sys_err("PARTY Cannot find invited character");
		return;
	}

	ch->PartyInvite(pInvitee);
}

void CInputMain::PartyInviteAnswer(LPCHARACTER ch, const char * c_pData)
{
	if (ch->GetArena())
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ëŒ€ë ¨ì¥ì—ì„œ ì‚¬ìš©í•˜ì‹¤ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		return;
	}

	TPacketCGPartyInviteAnswer * p = (TPacketCGPartyInviteAnswer*) c_pData;

	LPCHARACTER pInviter = CHARACTER_MANAGER::instance().Find(p->leader_vid);

	// pInviter ê°€ ch ì—ê²Œ íŒŒí‹° ìš”ì²­ì„ í–ˆì—ˆë‹¤.

	if (!pInviter)
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<íŒŒí‹°> íŒŒí‹°ìš”ì²­ì„ í•œ ìºë¦­í„°ë¥¼ ì°¾ì„ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
	else if (!p->accept)
		pInviter->PartyInviteDeny(ch->GetPlayerID());
	else
		pInviter->PartyInviteAccept(ch);
}
// END_OF_PARTY_JOIN_BUG_FIX

void CInputMain::PartySetState(LPCHARACTER ch, const char* c_pData)
{
	if (!CPartyManager::instance().IsEnablePCParty())
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<íŒŒí‹°> ì„œë²„ ë¬¸ì œë¡œ íŒŒí‹° ê´€ë ¨ ì²˜ë¦¬ë¥¼ í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		return;
	}

	TPacketCGPartySetState* p = (TPacketCGPartySetState*) c_pData;

	if (!ch->GetParty())
		return;

	if (ch->GetParty()->GetLeaderPID() != ch->GetPlayerID())
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<íŒŒí‹°> ë¦¬ë”ë§Œ ë³€ê²½í•  ìˆ˜ ìˆìŠµë‹ˆë‹¤."));
		return;
	}

	if (!ch->GetParty()->IsMember(p->pid))
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<íŒŒí‹°> ìƒíƒœë¥¼ ë³€ê²½í•˜ë ¤ëŠ” ì‚¬ëŒì´ íŒŒí‹°ì›ì´ ì•„ë‹™ë‹ˆë‹¤."));
		return;
	}

	DWORD pid = p->pid;
	sys_log(0, "PARTY SetRole pid %d to role %d state %s", pid, p->byRole, p->flag ? "on" : "off");

	switch (p->byRole)
	{
		case PARTY_ROLE_NORMAL:
			break;

		case PARTY_ROLE_ATTACKER: 
		case PARTY_ROLE_TANKER: 
		case PARTY_ROLE_BUFFER:
		case PARTY_ROLE_SKILL_MASTER:
		case PARTY_ROLE_HASTE:
		case PARTY_ROLE_DEFENDER:
			if (ch->GetParty()->SetRole(pid, p->byRole, p->flag))
			{
				TPacketPartyStateChange pack;
				pack.dwLeaderPID = ch->GetPlayerID();
				pack.dwPID = p->pid;
				pack.bRole = p->byRole;
				pack.bFlag = p->flag;
				db_clientdesc->DBPacket(GD::PARTY_STATE_CHANGE, 0, &pack, sizeof(pack));
			}
			/* else
			   ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<íŒŒí‹°> ì–´íƒœì»¤ ì„¤ì •ì— ì‹¤íŒ¨í•˜ì˜€ìŠµë‹ˆë‹¤.")); */
			break;

		default:
			sys_err("wrong byRole in PartySetState Packet name %s state %d", ch->GetName(), p->byRole);
			break;
	}
}

void CInputMain::PartyRemove(LPCHARACTER ch, const char* c_pData)
{
	if (ch->GetArena())
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ëŒ€ë ¨ì¥ì—ì„œ ì‚¬ìš©í•˜ì‹¤ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		return;
	}

	if (!CPartyManager::instance().IsEnablePCParty())
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<íŒŒí‹°> ì„œë²„ ë¬¸ì œë¡œ íŒŒí‹° ê´€ë ¨ ì²˜ë¦¬ë¥¼ í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		return;
	}

	// MR-3: Fix party removal chat messages and improved dungeon logic
	TPacketCGPartyRemove* p = (TPacketCGPartyRemove*) c_pData;

	if (!ch->GetParty())
		return;

	LPPARTY pParty = ch->GetParty();

	if (pParty->GetLeaderPID() == ch->GetPlayerID())
	{
		// leader can remove any member
		if (p->pid == ch->GetPlayerID() || pParty->GetMemberCount() == 2)
		{
			if (ch->GetDungeon())
			{
				ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<íŒŒí‹°> ë˜ì „ ì•ˆì—ì„œëŠ” íŒŒí‹°ì—ì„œ ì¶”ë°©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
				return;
			}
			else
			{
				// party disband
				CPartyManager::instance().DeleteParty(pParty);
			}
		}
		else
		{
			if (ch->GetDungeon())
			{
				ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<íŒŒí‹°> ë˜ì ¼ë‚´ì—ì„œëŠ” íŒŒí‹°ì›ì„ ì¶”ë°©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
				return;
			}
			else
			{
				LPCHARACTER B = CHARACTER_MANAGER::instance().FindByPID(p->pid);

				if (B)
				{
					//pParty->SendPartyRemoveOneToAll(B);
					B->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<íŒŒí‹°> íŒŒí‹°ì—ì„œ ì¶”ë°©ë‹¹í•˜ì…¨ìŠµë‹ˆë‹¤."));
					//pParty->Unlink(B);
					//CPartyManager::instance().SetPartyMember(B->GetPlayerID(), NULL);
				}

				pParty->Quit(p->pid);
			}
		}
	}
	else
	{
		// otherwise, only remove itself
		if (p->pid == ch->GetPlayerID())
		{
			if (pParty->GetMemberCount() == 2)
			{
				if (ch->GetDungeon())
				{
					ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<íŒŒí‹°> ë˜ì „ ì•ˆì—ì„œëŠ” íŒŒí‹°ì—ì„œ ì¶”ë°©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
					return;
				}
				else
				{
					// party disband
					CPartyManager::instance().DeleteParty(pParty);
				}
			}
			else
			{
				if (ch->GetDungeon())
				{
					ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<íŒŒí‹°> ë˜ì ¼ë‚´ì—ì„œëŠ” íŒŒí‹°ë¥¼ ë‚˜ê°ˆ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
					return;
				}
				else
				{
					ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<íŒŒí‹°> íŒŒí‹°ì—ì„œ ë‚˜ê°€ì…¨ìŠµë‹ˆë‹¤."));
					//pParty->SendPartyRemoveOneToAll(ch);
					pParty->Quit(ch->GetPlayerID());
					//pParty->SendPartyRemoveAllToOne(ch);
					//CPartyManager::instance().SetPartyMember(ch->GetPlayerID(), NULL);
				}
			}
		}
		else
		{
			ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<íŒŒí‹°> ë‹¤ë¥¸ íŒŒí‹°ì›ì„ íƒˆí‡´ì‹œí‚¬ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		}
	}
	// MR-3: -- END OF -- Fix party removal chat messages and improved dungeon logic
}

void CInputMain::AnswerMakeGuild(LPCHARACTER ch, const char* c_pData)
{
	TPacketCGAnswerMakeGuild* p = (TPacketCGAnswerMakeGuild*) c_pData;

	if (ch->GetGold() < 200000)
		return;

	if (get_global_time() - ch->GetQuestFlag("guild_manage.new_disband_time") <
			CGuildManager::instance().GetDisbandDelay())
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> í•´ì‚°í•œ í›„ %dì¼ ì´ë‚´ì—ëŠ” ê¸¸ë“œë¥¼ ë§Œë“¤ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."), 
				quest::CQuestManager::instance().GetEventFlag("guild_disband_delay"));
		return;
	}

	if (get_global_time() - ch->GetQuestFlag("guild_manage.new_withdraw_time") <
			CGuildManager::instance().GetWithdrawDelay())
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> íƒˆí‡´í•œ í›„ %dì¼ ì´ë‚´ì—ëŠ” ê¸¸ë“œë¥¼ ë§Œë“¤ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."), 
				quest::CQuestManager::instance().GetEventFlag("guild_withdraw_delay"));
		return;
	}

	if (ch->GetGuild())
		return;

	CGuildManager& gm = CGuildManager::instance();

	TGuildCreateParameter cp;
	memset(&cp, 0, sizeof(cp));

	cp.master = ch;
	strlcpy(cp.name, p->guild_name, sizeof(cp.name));

	if (cp.name[0] == 0 || !check_name(cp.name))
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì í•©í•˜ì§€ ì•Šì€ ê¸¸ë“œ ì´ë¦„ ì…ë‹ˆë‹¤."));
		return;
	}

	DWORD dwGuildID = gm.CreateGuild(cp);

	if (dwGuildID)
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> [%s] ê¸¸ë“œê°€ ìƒì„±ë˜ì—ˆìŠµë‹ˆë‹¤."), cp.name);

		int GuildCreateFee;

		if (LC_IsBrazil())
		{
			GuildCreateFee = 500000;
		}
		else
		{
			GuildCreateFee = 200000;
		}

		ch->PointChange(POINT_GOLD, -GuildCreateFee);
		DBManager::instance().SendMoneyLog(MONEY_LOG_GUILD, ch->GetPlayerID(), -GuildCreateFee);

		char Log[128];
		snprintf(Log, sizeof(Log), "GUILD_NAME %s MASTER %s", cp.name, ch->GetName());
		LogManager::instance().CharLog(ch, 0, "MAKE_GUILD", Log);

		if (g_iUseLocale)
			ch->RemoveSpecifyItem(GUILD_CREATE_ITEM_VNUM, 1);
		//ch->SendGuildName(dwGuildID);
	}
	else
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> ê¸¸ë“œ ìƒì„±ì— ì‹¤íŒ¨í•˜ì˜€ìŠµë‹ˆë‹¤."));
}

void CInputMain::PartyUseSkill(LPCHARACTER ch, const char* c_pData)
{
	TPacketCGPartyUseSkill* p = (TPacketCGPartyUseSkill*) c_pData; 
	if (!ch->GetParty())
		return;

	if (ch->GetPlayerID() != ch->GetParty()->GetLeaderPID())
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<íŒŒí‹°> íŒŒí‹° ê¸°ìˆ ì€ íŒŒí‹°ì¥ë§Œ ì‚¬ìš©í•  ìˆ˜ ìˆìŠµë‹ˆë‹¤."));
		return;
	}

	switch (p->bySkillIndex)
	{
		case PARTY_SKILL_HEAL:
			ch->GetParty()->HealParty();
			break;
		case PARTY_SKILL_WARP:
			{
				LPCHARACTER pch = CHARACTER_MANAGER::instance().Find(p->vid);
				if (pch)
					ch->GetParty()->SummonToLeader(pch->GetPlayerID());
				else
					ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<íŒŒí‹°> ì†Œí™˜í•˜ë ¤ëŠ” ëŒ€ìƒì„ ì°¾ì„ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
			}
			break;
	}
}

void CInputMain::PartyParameter(LPCHARACTER ch, const char * c_pData)
{
	TPacketCGPartyParameter * p = (TPacketCGPartyParameter *) c_pData;

	if (ch->GetParty())
		ch->GetParty()->SetParameter(p->bDistributeMode);
}

size_t GetSubPacketSize(uint8_t header)
{
	switch (header)
	{
		case GuildSub::CG::DEPOSIT_MONEY:				return sizeof(int);
		case GuildSub::CG::WITHDRAW_MONEY:				return sizeof(int);
		case GuildSub::CG::ADD_MEMBER:					return sizeof(DWORD);
		case GuildSub::CG::REMOVE_MEMBER:				return sizeof(DWORD);
		case GuildSub::CG::CHANGE_GRADE_NAME:			return 10;
		case GuildSub::CG::CHANGE_GRADE_AUTHORITY:		return sizeof(BYTE) + sizeof(BYTE);
		case GuildSub::CG::OFFER:						return sizeof(DWORD);
		case GuildSub::CG::CHARGE_GSP:					return sizeof(int);
		case GuildSub::CG::POST_COMMENT:				return 1;
		case GuildSub::CG::DELETE_COMMENT:				return sizeof(DWORD);
		case GuildSub::CG::REFRESH_COMMENT:			return 0;
		case GuildSub::CG::CHANGE_MEMBER_GRADE:		return sizeof(DWORD) + sizeof(BYTE);
		case GuildSub::CG::USE_SKILL:					return sizeof(TPacketCGGuildUseSkill);
		case GuildSub::CG::CHANGE_MEMBER_GENERAL:		return sizeof(DWORD) + sizeof(BYTE);
		case GuildSub::CG::GUILD_INVITE_ANSWER:		return sizeof(DWORD) + sizeof(BYTE);
	}

	return 0;
}

// ---------------------------------------------------------------------------
// Guild sub-handler type and dispatch table
// ---------------------------------------------------------------------------
using GuildSubHandler = int (CInputMain::*)(LPCHARACTER, const char*, size_t);

int CInputMain::Guild(LPCHARACTER ch, const char * data, size_t uiBytes)
{
	if (uiBytes < sizeof(TPacketCGGuild))
		return -1;

	const TPacketCGGuild* p = reinterpret_cast<const TPacketCGGuild*>(data);

	uiBytes -= sizeof(TPacketCGGuild);

	const uint8_t SubHeader = p->subheader;
	const size_t SubPacketLen = GetSubPacketSize(SubHeader);

	if (uiBytes < SubPacketLen)
	{
		return -1;
	}

	CGuild* pGuild = ch->GetGuild();

	if (NULL == pGuild)
	{
		if (SubHeader != GuildSub::CG::GUILD_INVITE_ANSWER)
		{
			ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> ê¸¸ë“œì— ì†í•´ìˆì§€ ì•ŠìŠµë‹ˆë‹¤."));
			return SubPacketLen;
		}
	}

	static const std::unordered_map<uint8_t, GuildSubHandler> handlers = {
		{ GuildSub::CG::DEPOSIT_MONEY,			&CInputMain::GuildSub_DepositMoney },
		{ GuildSub::CG::WITHDRAW_MONEY,		&CInputMain::GuildSub_WithdrawMoney },
		{ GuildSub::CG::ADD_MEMBER,			&CInputMain::GuildSub_AddMember },
		{ GuildSub::CG::REMOVE_MEMBER,			&CInputMain::GuildSub_RemoveMember },
		{ GuildSub::CG::CHANGE_GRADE_NAME,		&CInputMain::GuildSub_ChangeGradeName },
		{ GuildSub::CG::CHANGE_GRADE_AUTHORITY,	&CInputMain::GuildSub_ChangeGradeAuthority },
		{ GuildSub::CG::OFFER,				&CInputMain::GuildSub_Offer },
		{ GuildSub::CG::CHARGE_GSP,			&CInputMain::GuildSub_ChargeGSP },
		{ GuildSub::CG::POST_COMMENT,			&CInputMain::GuildSub_PostComment },
		{ GuildSub::CG::DELETE_COMMENT,			&CInputMain::GuildSub_DeleteComment },
		{ GuildSub::CG::REFRESH_COMMENT,		&CInputMain::GuildSub_RefreshComment },
		{ GuildSub::CG::CHANGE_MEMBER_GRADE,		&CInputMain::GuildSub_ChangeMemberGrade },
		{ GuildSub::CG::USE_SKILL,			&CInputMain::GuildSub_UseSkill },
		{ GuildSub::CG::CHANGE_MEMBER_GENERAL,		&CInputMain::GuildSub_ChangeMemberGeneral },
		{ GuildSub::CG::GUILD_INVITE_ANSWER,		&CInputMain::GuildSub_InviteAnswer },
	};

	auto it = handlers.find(SubHeader);
	if (it == handlers.end())
	{
		sys_err("Guild: unknown subheader %d from %s", SubHeader, ch->GetName());
		return 0;
	}

	return (this->*(it->second))(ch, data, uiBytes);
}

// ---------------------------------------------------------------------------
// Guild sub-handlers
// ---------------------------------------------------------------------------

int CInputMain::GuildSub_DepositMoney(LPCHARACTER ch, const char* data, size_t uiBytes)
{
	const char* c_pData = data + sizeof(TPacketCGGuild);
	const size_t SubPacketLen = GetSubPacketSize(GuildSub::CG::DEPOSIT_MONEY);
	CGuild* pGuild = ch->GetGuild();

	// by mhh : ê¸¸ë“œìê¸ˆì€ ë‹¹ë¶„ê°„ ë„£ì„ ìˆ˜ ì—†ë‹¤.
	return SubPacketLen;

	const int gold = MIN(*reinterpret_cast<const int*>(c_pData), __deposit_limit());

	if (gold < 0)
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> ì˜ëª»ëœ ê¸ˆì•¡ì…ë‹ˆë‹¤."));
		return SubPacketLen;
	}

	if (ch->GetGold() < gold)
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> ê°€ì§€ê³  ìˆëŠ” ëˆì´ ë¶€ì¡±í•©ë‹ˆë‹¤."));
		return SubPacketLen;
	}

	pGuild->RequestDepositMoney(ch, gold);
	return SubPacketLen;
}

int CInputMain::GuildSub_WithdrawMoney(LPCHARACTER ch, const char* data, size_t uiBytes)
{
	const char* c_pData = data + sizeof(TPacketCGGuild);
	const size_t SubPacketLen = GetSubPacketSize(GuildSub::CG::WITHDRAW_MONEY);
	CGuild* pGuild = ch->GetGuild();

	// by mhh : ê¸¸ë“œìê¸ˆì€ ë‹¹ë¶„ê°„ ëº„ ìˆ˜ ì—†ë‹¤.
	return SubPacketLen;

	const int gold = MIN(*reinterpret_cast<const int*>(c_pData), 500000);

	if (gold < 0)
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> ì˜ëª»ëœ ê¸ˆì•¡ì…ë‹ˆë‹¤."));
		return SubPacketLen;
	}

	pGuild->RequestWithdrawMoney(ch, gold);
	return SubPacketLen;
}

int CInputMain::GuildSub_AddMember(LPCHARACTER ch, const char* data, size_t uiBytes)
{
	const char* c_pData = data + sizeof(TPacketCGGuild);
	const size_t SubPacketLen = GetSubPacketSize(GuildSub::CG::ADD_MEMBER);
	CGuild* pGuild = ch->GetGuild();

	const DWORD vid = *reinterpret_cast<const DWORD*>(c_pData);
	LPCHARACTER newmember = CHARACTER_MANAGER::instance().Find(vid);

	// if (!newmember)
	if (!newmember || !newmember->IsPC()) // Fix
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> ê·¸ëŸ¬í•œ ì‚¬ëŒì„ ì°¾ì„ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		return SubPacketLen;
	}

	if (!ch->IsPC())
		return SubPacketLen;

	if (LC_IsCanada() == true)
	{
		if (newmember->GetQuestFlag("change_guild_master.be_other_member") > get_global_time())
		{
			ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> ì•„ì§ ê°€ì…í•  ìˆ˜ ì—†ëŠ” ìºë¦­í„°ì…ë‹ˆë‹¤"));
			return SubPacketLen;
		}
	}

	pGuild->Invite(ch, newmember);
	return SubPacketLen;
}

int CInputMain::GuildSub_RemoveMember(LPCHARACTER ch, const char* data, size_t uiBytes)
{
	const char* c_pData = data + sizeof(TPacketCGGuild);
	const size_t SubPacketLen = GetSubPacketSize(GuildSub::CG::REMOVE_MEMBER);
	CGuild* pGuild = ch->GetGuild();

	if (pGuild->UnderAnyWar() != 0)
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> ê¸¸ë“œì „ ì¤‘ì—ëŠ” ê¸¸ë“œì›ì„ íƒˆí‡´ì‹œí‚¬ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		return SubPacketLen;
	}

	const DWORD pid = *reinterpret_cast<const DWORD*>(c_pData);
	const TGuildMember* m = pGuild->GetMember(ch->GetPlayerID());

	if (NULL == m)
		return -1;

	LPCHARACTER member = CHARACTER_MANAGER::instance().FindByPID(pid);

	if (member)
	{
		if (member->GetGuild() != pGuild)
		{
			ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> ìƒëŒ€ë°©ì´ ê°™ì€ ê¸¸ë“œê°€ ì•„ë‹™ë‹ˆë‹¤."));
			return SubPacketLen;
		}

		if (!pGuild->HasGradeAuth(m->grade, GUILD_AUTH_REMOVE_MEMBER))
		{
			ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> ê¸¸ë“œì›ì„ ê°•ì œ íƒˆí‡´ ì‹œí‚¬ ê¶Œí•œì´ ì—†ìŠµë‹ˆë‹¤."));
			return SubPacketLen;
		}

		member->SetQuestFlag("guild_manage.new_withdraw_time", get_global_time());
		pGuild->RequestRemoveMember(member->GetPlayerID());

		if (LC_IsBrazil() == true)
		{
			DBManager::instance().Query("REPLACE INTO guild_invite_limit VALUES(%d, %d)", pGuild->GetID(), get_global_time());
		}
	}
	else
	{
		if (!pGuild->HasGradeAuth(m->grade, GUILD_AUTH_REMOVE_MEMBER))
		{
			ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> ê¸¸ë“œì›ì„ ê°•ì œ íƒˆí‡´ ì‹œí‚¬ ê¶Œí•œì´ ì—†ìŠµë‹ˆë‹¤."));
			return SubPacketLen;
		}

		if (pGuild->RequestRemoveMember(pid))
			ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> ê¸¸ë“œì›ì„ ê°•ì œ íƒˆí‡´ ì‹œì¼°ìŠµë‹ˆë‹¤."));
		else
			ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> ê·¸ëŸ¬í•œ ì‚¬ëŒì„ ì°¾ì„ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
	}

	return SubPacketLen;
}

int CInputMain::GuildSub_ChangeGradeName(LPCHARACTER ch, const char* data, size_t uiBytes)
{
	const char* c_pData = data + sizeof(TPacketCGGuild);
	const size_t SubPacketLen = GetSubPacketSize(GuildSub::CG::CHANGE_GRADE_NAME);
	CGuild* pGuild = ch->GetGuild();

	char gradename[GUILD_GRADE_NAME_MAX_LEN + 1];
	strlcpy(gradename, c_pData + 1, sizeof(gradename));

	const TGuildMember * m = pGuild->GetMember(ch->GetPlayerID());

	if (NULL == m)
		return -1;

	if (m->grade != GUILD_LEADER_GRADE)
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> ì§ìœ„ ì´ë¦„ì„ ë³€ê²½í•  ê¶Œí•œì´ ì—†ìŠµë‹ˆë‹¤."));
	}
	else if (*c_pData == GUILD_LEADER_GRADE)
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> ê¸¸ë“œì¥ì˜ ì§ìœ„ ì´ë¦„ì€ ë³€ê²½í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
	}
	else if (!check_name(gradename))
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> ì í•©í•˜ì§€ ì•Šì€ ì§ìœ„ ì´ë¦„ ì…ë‹ˆë‹¤."));
	}
	else
	{
		pGuild->ChangeGradeName(*c_pData, gradename);
	}

	return SubPacketLen;
}

int CInputMain::GuildSub_ChangeGradeAuthority(LPCHARACTER ch, const char* data, size_t uiBytes)
{
	const char* c_pData = data + sizeof(TPacketCGGuild);
	const size_t SubPacketLen = GetSubPacketSize(GuildSub::CG::CHANGE_GRADE_AUTHORITY);
	CGuild* pGuild = ch->GetGuild();

	const TGuildMember* m = pGuild->GetMember(ch->GetPlayerID());

	if (NULL == m)
		return -1;

	if (m->grade != GUILD_LEADER_GRADE)
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> ì§ìœ„ ê¶Œí•œì„ ë³€ê²½í•  ê¶Œí•œì´ ì—†ìŠµë‹ˆë‹¤."));
	}
	else if (*c_pData == GUILD_LEADER_GRADE)
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> ê¸¸ë“œì¥ì˜ ê¶Œí•œì€ ë³€ê²½í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
	}
	else
	{
		pGuild->ChangeGradeAuth(*c_pData, *(c_pData + 1));
	}

	return SubPacketLen;
}

int CInputMain::GuildSub_Offer(LPCHARACTER ch, const char* data, size_t uiBytes)
{
	const char* c_pData = data + sizeof(TPacketCGGuild);
	const size_t SubPacketLen = GetSubPacketSize(GuildSub::CG::OFFER);
	CGuild* pGuild = ch->GetGuild();

	DWORD offer = *reinterpret_cast<const DWORD*>(c_pData);

	if (pGuild->GetLevel() >= GUILD_MAX_LEVEL && LC_IsHongKong() == false)
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> ê¸¸ë“œê°€ ì´ë¯¸ ìµœê³  ë ˆë²¨ì…ë‹ˆë‹¤."));
	}
	else
	{
		offer /= 100;
		offer *= 100;

		if (pGuild->OfferExp(ch, offer))
		{
			ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> %uì˜ ê²½í—˜ì¹˜ë¥¼ íˆ¬ìí•˜ì˜€ìŠµë‹ˆë‹¤."), offer);
		}
		else
		{
			ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> ê²½í—˜ì¹˜ íˆ¬ìì— ì‹¤íŒ¨í•˜ì˜€ìŠµë‹ˆë‹¤."));
		}
	}

	return SubPacketLen;
}

int CInputMain::GuildSub_ChargeGSP(LPCHARACTER ch, const char* data, size_t uiBytes)
{
	const char* c_pData = data + sizeof(TPacketCGGuild);
	const size_t SubPacketLen = GetSubPacketSize(GuildSub::CG::CHARGE_GSP);
	CGuild* pGuild = ch->GetGuild();

	const int offer = *reinterpret_cast<const int*>(c_pData);
	const int gold = offer * 100;

	if (offer < 0 || gold < offer || gold < 0 || ch->GetGold() < gold)
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> ëˆì´ ë¶€ì¡±í•©ë‹ˆë‹¤."));
		return SubPacketLen;
	}

	if (!pGuild->ChargeSP(ch, offer))
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> ìš©ì‹ ë ¥ íšŒë³µì— ì‹¤íŒ¨í•˜ì˜€ìŠµë‹ˆë‹¤."));
	}

	return SubPacketLen;
}

int CInputMain::GuildSub_PostComment(LPCHARACTER ch, const char* data, size_t uiBytes)
{
	const char* c_pData = data + sizeof(TPacketCGGuild);
	CGuild* pGuild = ch->GetGuild();

	const size_t length = *c_pData;

	if (length > GUILD_COMMENT_MAX_LEN)
	{
		// ì˜ëª»ëœ ê¸¸ì´.. ëŠì–´ì£¼ì.
		sys_err("POST_COMMENT: %s comment too long (length: %u)", ch->GetName(), length);
		ch->GetDesc()->SetPhase(PHASE_CLOSE);
		return -1;
	}

	if (uiBytes < 1 + length)
		return -1;

	const TGuildMember* m = pGuild->GetMember(ch->GetPlayerID());

	if (NULL == m)
		return -1;

	if (length && !pGuild->HasGradeAuth(m->grade, GUILD_AUTH_NOTICE) && *(c_pData + 1) == '!')
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> ê³µì§€ê¸€ì„ ì‘ì„±í•  ê¶Œí•œì´ ì—†ìŠµë‹ˆë‹¤."));
	}
	else
	{
		std::string str(c_pData + 1, length);
		pGuild->AddComment(ch, str);
	}

	return (1 + length);
}

int CInputMain::GuildSub_DeleteComment(LPCHARACTER ch, const char* data, size_t uiBytes)
{
	const char* c_pData = data + sizeof(TPacketCGGuild);
	const size_t SubPacketLen = GetSubPacketSize(GuildSub::CG::DELETE_COMMENT);
	CGuild* pGuild = ch->GetGuild();

	const DWORD comment_id = *reinterpret_cast<const DWORD*>(c_pData);

	pGuild->DeleteComment(ch, comment_id);
	return SubPacketLen;
}

int CInputMain::GuildSub_RefreshComment(LPCHARACTER ch, const char* data, size_t uiBytes)
{
	const size_t SubPacketLen = GetSubPacketSize(GuildSub::CG::REFRESH_COMMENT);
	CGuild* pGuild = ch->GetGuild();

	pGuild->RefreshComment(ch);
	return SubPacketLen;
}

int CInputMain::GuildSub_ChangeMemberGrade(LPCHARACTER ch, const char* data, size_t uiBytes)
{
	const char* c_pData = data + sizeof(TPacketCGGuild);
	const size_t SubPacketLen = GetSubPacketSize(GuildSub::CG::CHANGE_MEMBER_GRADE);
	CGuild* pGuild = ch->GetGuild();

	const DWORD pid = *reinterpret_cast<const DWORD*>(c_pData);
	const BYTE grade = *(c_pData + sizeof(DWORD));
	const TGuildMember* m = pGuild->GetMember(ch->GetPlayerID());

	if (NULL == m)
		return -1;

	if (m->grade != GUILD_LEADER_GRADE)
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> ì§ìœ„ë¥¼ ë³€ê²½í•  ê¶Œí•œì´ ì—†ìŠµë‹ˆë‹¤."));
	else if (ch->GetPlayerID() == pid)
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> ê¸¸ë“œì¥ì˜ ì§ìœ„ëŠ” ë³€ê²½í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
	else if (grade == 1)
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> ê¸¸ë“œì¥ìœ¼ë¡œ ì§ìœ„ë¥¼ ë³€ê²½í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
	else
		pGuild->ChangeMemberGrade(pid, grade);

	return SubPacketLen;
}

int CInputMain::GuildSub_UseSkill(LPCHARACTER ch, const char* data, size_t uiBytes)
{
	const char* c_pData = data + sizeof(TPacketCGGuild);
	const size_t SubPacketLen = GetSubPacketSize(GuildSub::CG::USE_SKILL);
	CGuild* pGuild = ch->GetGuild();

	const TPacketCGGuildUseSkill* p = reinterpret_cast<const TPacketCGGuildUseSkill*>(c_pData);

	pGuild->UseSkill(p->dwVnum, ch, p->dwPID);
	return SubPacketLen;
}

int CInputMain::GuildSub_ChangeMemberGeneral(LPCHARACTER ch, const char* data, size_t uiBytes)
{
	const char* c_pData = data + sizeof(TPacketCGGuild);
	const size_t SubPacketLen = GetSubPacketSize(GuildSub::CG::CHANGE_MEMBER_GENERAL);
	CGuild* pGuild = ch->GetGuild();

	const DWORD pid = *reinterpret_cast<const DWORD*>(c_pData);
	const BYTE is_general = *(c_pData + sizeof(DWORD));
	const TGuildMember* m = pGuild->GetMember(ch->GetPlayerID());

	if (NULL == m)
		return -1;

	if (m->grade != GUILD_LEADER_GRADE)
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> ì¥êµ°ì„ ì§€ì •í•  ê¶Œí•œì´ ì—†ìŠµë‹ˆë‹¤."));
	}
	else
	{
		if (!pGuild->ChangeMemberGeneral(pid, is_general))
		{
			ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<ê¸¸ë“œ> ë”ì´ìƒ ì¥ìˆ˜ë¥¼ ì§€ì •í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		}
	}

	return SubPacketLen;
}

int CInputMain::GuildSub_InviteAnswer(LPCHARACTER ch, const char* data, size_t uiBytes)
{
	const char* c_pData = data + sizeof(TPacketCGGuild);
	const size_t SubPacketLen = GetSubPacketSize(GuildSub::CG::GUILD_INVITE_ANSWER);

	const DWORD guild_id = *reinterpret_cast<const DWORD*>(c_pData);
	const BYTE accept = *(c_pData + sizeof(DWORD));

	CGuild * g = CGuildManager::instance().FindGuild(guild_id);

	if (g)
	{
		if (accept)
			g->InviteAccept(ch);
		else
			g->InviteDeny(ch->GetPlayerID());
	}

	return SubPacketLen;
}

void CInputMain::Fishing(LPCHARACTER ch, const char* c_pData)
{
	TPacketCGFishing* p = (TPacketCGFishing*)c_pData;
	ch->SetRotation(p->dir * 5);
	ch->fishing();
	return;
}

// switchbot dev framework -- client sends CG::SWITCHBOT_TOGGLE
void CInputMain::SwitchbotToggle(LPCHARACTER ch, const char* c_pData)
{
	if (!ch) return;
	const TPacketCGSwitchbotToggle* p =
		reinterpret_cast<const TPacketCGSwitchbotToggle*>(c_pData);
	CSwitchbotManager::instance().Toggle(ch, p->enable != 0);
}

void CInputMain::ItemGive(LPCHARACTER ch, const char* c_pData)
{
	TPacketCGGiveItem* p = (TPacketCGGiveItem*) c_pData;
	LPCHARACTER to_ch = CHARACTER_MANAGER::instance().Find(p->dwTargetVID);

	if (to_ch)
		ch->GiveItem(to_ch, p->ItemPos);
	else
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì•„ì´í…œì„ ê±´ë„¤ì¤„ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
}

void CInputMain::Hack(LPCHARACTER ch, const char * c_pData)
{
	TPacketCGHack * p = (TPacketCGHack *) c_pData;
	
	char buf[sizeof(p->szBuf)];
	strlcpy(buf, p->szBuf, sizeof(buf));

	sys_err("HACK_DETECT: %s %s", ch->GetName(), buf);

	// í˜„ì¬ í´ë¼ì´ì–¸íŠ¸ì—ì„œ ì´ íŒ¨í‚·ì„ ë³´ë‚´ëŠ” ê²½ìš°ê°€ ì—†ìœ¼ë¯€ë¡œ ë¬´ì¡°ê±´ ëŠë„ë¡ í•œë‹¤
	ch->GetDesc()->SetPhase(PHASE_CLOSE);
}

int CInputMain::MyShop(LPCHARACTER ch, const char * c_pData, size_t uiBytes)
{
	TPacketCGMyShop * p = (TPacketCGMyShop *) c_pData;
	int iExtraLen = p->bCount * sizeof(TShopItemTable);

	if (uiBytes < sizeof(TPacketCGMyShop) + iExtraLen)
		return -1;

	if (ch->GetGold() >= GOLD_MAX)
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì†Œìœ  ëˆì´ 20ì–µëƒ¥ì„ ë„˜ì–´ ê±°ë˜ë¥¼ í•¼ìˆ˜ê°€ ì—†ìŠµë‹ˆë‹¤."));
		sys_log(0, "MyShop ==> OverFlow Gold id %u name %s ", ch->GetPlayerID(), ch->GetName());
		return (iExtraLen);
	}

	if (ch->IsStun() || ch->IsDead())
		return (iExtraLen);

	if (ch->GetExchange() || ch->IsOpenSafebox() || ch->GetShopOwner() || ch->IsCubeOpen())
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë‹¤ë¥¸ ê±°ë˜ì¤‘ì¼ê²½ìš° ê°œì¸ìƒì ì„ ì—´ìˆ˜ê°€ ì—†ìŠµë‹ˆë‹¤."));
		return (iExtraLen);
	}

	sys_log(0, "MyShop count %d", p->bCount);
	ch->OpenMyShop(p->szSign, (TShopItemTable *) (c_pData + sizeof(TPacketCGMyShop)), p->bCount);
	return (iExtraLen);
}

void CInputMain::Refine(LPCHARACTER ch, const char* c_pData)
{
	const TPacketCGRefine* p = reinterpret_cast<const TPacketCGRefine*>(c_pData);

	if (ch->GetExchange() || ch->IsOpenSafebox() || ch->GetShopOwner() || ch->GetMyShop() || ch->IsCubeOpen())
	{
		ch->ChatPacket(CHAT_TYPE_INFO,  LC_TEXT("ì°½ê³ ,ê±°ë˜ì°½ë“±ì´ ì—´ë¦° ìƒíƒœì—ì„œëŠ” ê°œëŸ‰ì„ í• ìˆ˜ê°€ ì—†ìŠµë‹ˆë‹¤"));
		ch->ClearRefineMode();
		return;
	}

	if (p->type == 255)
	{
		// DoRefine Cancel
		ch->ClearRefineMode();
		return;
	}

	if (p->pos >= INVENTORY_MAX_NUM)
	{
		ch->ClearRefineMode();
		return;
	}

	LPITEM item = ch->GetInventoryItem(p->pos);

	if (!item)
	{
		ch->ClearRefineMode();
		return;
	}

	ch->SetRefineTime();

	if (p->type == REFINE_TYPE_NORMAL)
	{
		sys_log (0, "refine_type_noraml");
		// MR-15: Fix refining by item
		ch->DoRefine(item, false, p->type);
		// MR-15: -- END OF -- Fix refining by item
	}
	else if (p->type == REFINE_TYPE_SCROLL || p->type == REFINE_TYPE_HYUNIRON || p->type == REFINE_TYPE_MUSIN || p->type == REFINE_TYPE_BDRAGON)
	{
		sys_log (0, "refine_type_scroll, ...");
		// MR-15: Fix refining by item
		ch->DoRefineWithScroll(item, p->type);
		// MR-15: -- END OF -- Fix refining by item
	}
	else if (p->type == REFINE_TYPE_MONEY_ONLY)
	{
		const LPITEM item = ch->GetInventoryItem(p->pos);

		if (NULL != item)
		{
			if (500 <= item->GetRefineSet())
			{
				LogManager::instance().HackLog("DEVIL_TOWER_REFINE_HACK", ch);
			}
			else
			{
				if (ch->GetQuestFlag("deviltower_zone.can_refine"))
				{
					// MR-15: Fix refining by item
					ch->DoRefine(item, true, p->type);
					// MR-15: -- END OF -- Fix refining by item
					ch->SetQuestFlag("deviltower_zone.can_refine", 0);
				}
				else
				{
					ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì‚¬ê·€ íƒ€ì›Œ ì™„ë£Œ ë³´ìƒì€ í•œë²ˆê¹Œì§€ ì‚¬ìš©ê°€ëŠ¥í•©ë‹ˆë‹¤."));
				}
			}
		}
	}

	ch->ClearRefineMode();
}

// ---------------------------------------------------------------------------
// Handler map constructors + registration
// ---------------------------------------------------------------------------

CInputMain::CInputMain()
{
	RegisterHandlers();
}

// Custom adapter methods

int CInputMain::HandlePong(LPDESC d, const char*)
{
	Pong(d);
	return 0;
}

int CInputMain::HandleChat(LPDESC d, const char* c_pData)
{
	if (test_server)
	{
		char* pBuf = (char*)c_pData;
		sys_log(0, "%s", pBuf + sizeof(TPacketCGChat));
	}
	return Chat(d->GetCharacter(), c_pData, m_iBufferLeft);
}

int CInputMain::HandleWhisper(LPDESC d, const char* c_pData)
{
	return Whisper(d->GetCharacter(), c_pData, m_iBufferLeft);
}

int CInputMain::HandleMove(LPDESC d, const char* c_pData)
{
	LPCHARACTER ch = d->GetCharacter();
	Move(ch, c_pData);

	if (g_bCheckClientVersion)
	{
		int version = atoi(g_stClientVersion.c_str());
		int date = atoi(d->GetClientVersion());

		if (version != date)
		{
			ch->ChatPacket(CHAT_TYPE_NOTICE, LC_TEXT("í´ë¼ì´ì–¸íŠ¸ ë²„ì „ì´ í‹€ë ¤ ë¡œê·¸ì•„ì›ƒ ë©ë‹ˆë‹¤. ì •ìƒì ìœ¼ë¡œ íŒ¨ì¹˜ í›„ ì ‘ì†í•˜ì„¸ìš”."));
			d->DelayedDisconnect(10);
			LogManager::instance().HackLog("VERSION_CONFLICT", d->GetAccountTable().login, ch->GetName(), d->GetHostName());
		}
	}
	else if (!*d->GetClientVersion())
	{
		sys_err("Version not recieved name %s", ch->GetName());
		d->SetPhase(PHASE_CLOSE);
	}
	return 0;
}

int CInputMain::HandleAttack(LPDESC d, const char* c_pData)
{
	Attack(d->GetCharacter(), CG::ATTACK, c_pData);
	return 0;
}

int CInputMain::HandleShoot(LPDESC d, const char* c_pData)
{
	Attack(d->GetCharacter(), CG::SHOOT, c_pData);
	return 0;
}

int CInputMain::HandleShop(LPDESC d, const char* c_pData)
{
	return Shop(d->GetCharacter(), c_pData, m_iBufferLeft);
}

int CInputMain::HandleMessenger(LPDESC d, const char* c_pData)
{
	return Messenger(d->GetCharacter(), c_pData, m_iBufferLeft);
}

int CInputMain::HandleSyncPosition(LPDESC d, const char* c_pData)
{
	return SyncPosition(d->GetCharacter(), c_pData, m_iBufferLeft);
}

int CInputMain::HandleFlyTargeting(LPDESC d, const char* c_pData)
{
	FlyTarget(d->GetCharacter(), c_pData, CG::FLY_TARGETING);
	return 0;
}

int CInputMain::HandleAddFlyTargeting(LPDESC d, const char* c_pData)
{
	FlyTarget(d->GetCharacter(), c_pData, CG::ADD_FLY_TARGETING);
	return 0;
}

int CInputMain::HandleQuestCancel(LPDESC d, const char*)
{
	QuestCancel(d->GetCharacter());
	return 0;
}

int CInputMain::HandleSafeboxCheckout(LPDESC d, const char* c_pData)
{
	SafeboxCheckout(d->GetCharacter(), c_pData, false);
	return 0;
}

int CInputMain::HandleMallCheckout(LPDESC d, const char* c_pData)
{
	SafeboxCheckout(d->GetCharacter(), c_pData, true);
	return 0;
}

int CInputMain::HandleGuild(LPDESC d, const char* c_pData)
{
	return Guild(d->GetCharacter(), c_pData, m_iBufferLeft);
}

int CInputMain::HandleMyShop(LPDESC d, const char* c_pData)
{
	return MyShop(d->GetCharacter(), c_pData, m_iBufferLeft);
}

int CInputMain::HandleClientVersion(LPDESC d, const char* c_pData)
{
	Version(d->GetCharacter(), c_pData);
	return 0;
}

int CInputMain::HandleDragonSoulRefine(LPDESC d, const char* c_pData)
{
	LPCHARACTER ch = d->GetCharacter();
	TPacketCGDragonSoulRefine* p = reinterpret_cast<TPacketCGDragonSoulRefine*>((void*)c_pData);
	switch (p->bSubType)
	{
		case DragonSoulSub::CLOSE:
			ch->DragonSoul_RefineWindow_Close();
			break;
		case DragonSoulSub::DO_REFINE_GRADE:
			DSManager::instance().DoRefineGrade(ch, p->ItemGrid);
			break;
		case DragonSoulSub::DO_REFINE_STEP:
			DSManager::instance().DoRefineStep(ch, p->ItemGrid);
			break;
		case DragonSoulSub::DO_REFINE_STRENGTH:
			DSManager::instance().DoRefineStrength(ch, p->ItemGrid);
			break;
	}
	return 0;
}

// ---------------------------------------------------------------------------
// Handler registration tables
// ---------------------------------------------------------------------------

void CInputMain::RegisterHandlers()
{
	auto reg = [this](uint16_t h, MainHandler fn, bool blockObs = false) {
		m_handlers[h] = { fn, blockObs };
	};

	// Pong
	reg(CG::PONG,              &CInputMain::HandlePong);

	// Variable-length (custom adapters)
	reg(CG::CHAT,              &CInputMain::HandleChat);
	reg(CG::WHISPER,           &CInputMain::HandleWhisper);
	reg(CG::SHOP,              &CInputMain::HandleShop);
	reg(CG::MESSENGER,         &CInputMain::HandleMessenger);
	reg(CG::SYNC_POSITION,     &CInputMain::HandleSyncPosition);
	reg(CG::GUILD,             &CInputMain::HandleGuild);
	reg(CG::MYSHOP,            &CInputMain::HandleMyShop);

	// Special (custom adapters)
	reg(CG::MOVE,              &CInputMain::HandleMove);
	reg(CG::ATTACK,            &CInputMain::HandleAttack,          true);
	reg(CG::SHOOT,             &CInputMain::HandleShoot,           true);
	reg(CG::FLY_TARGETING,     &CInputMain::HandleFlyTargeting);
	reg(CG::ADD_FLY_TARGETING, &CInputMain::HandleAddFlyTargeting);
	reg(CG::QUEST_CANCEL,      &CInputMain::HandleQuestCancel);
	reg(CG::SAFEBOX_CHECKOUT,  &CInputMain::HandleSafeboxCheckout);
	reg(CG::MALL_CHECKOUT,     &CInputMain::HandleMallCheckout);
	reg(CG::CLIENT_VERSION,    &CInputMain::HandleClientVersion);
	reg(CG::DRAGON_SOUL_REFINE,&CInputMain::HandleDragonSoulRefine);

	// SimpleHandler<fn>(LPCHARACTER, const char*)
	reg(CG::CHARACTER_POSITION,&CInputMain::SimpleHandler<&CInputMain::Position>);
	reg(CG::ITEM_USE,          &CInputMain::SimpleHandler<&CInputMain::ItemUse>,        true);
	reg(CG::ITEM_DROP,         &CInputMain::SimpleHandler<&CInputMain::ItemDrop>,       true);
	reg(CG::ITEM_DROP2,        &CInputMain::SimpleHandler<&CInputMain::ItemDrop2>,      true);
	reg(CG::ITEM_MOVE,         &CInputMain::SimpleHandler<&CInputMain::ItemMove>,       true);
	reg(CG::ITEM_PICKUP,       &CInputMain::SimpleHandler<&CInputMain::ItemPickup>,     true);
	reg(CG::ITEM_USE_TO_ITEM,  &CInputMain::SimpleHandler<&CInputMain::ItemToItem>,     true);
	reg(CG::ITEM_GIVE,         &CInputMain::SimpleHandler<&CInputMain::ItemGive>,       true);
	reg(CG::EXCHANGE,          &CInputMain::SimpleHandler<&CInputMain::Exchange>,        true);
	reg(CG::USE_SKILL,         &CInputMain::SimpleHandler<&CInputMain::UseSkill>,        true);
	reg(CG::QUICKSLOT_ADD,     &CInputMain::SimpleHandler<&CInputMain::QuickslotAdd>);
	reg(CG::QUICKSLOT_DEL,     &CInputMain::SimpleHandler<&CInputMain::QuickslotDelete>);
	reg(CG::QUICKSLOT_SWAP,    &CInputMain::SimpleHandler<&CInputMain::QuickslotSwap>);
	reg(CG::ON_CLICK,          &CInputMain::SimpleHandler<&CInputMain::OnClick>);
	reg(CG::TARGET,            &CInputMain::SimpleHandler<&CInputMain::Target>);
	reg(CG::WARP,              &CInputMain::SimpleHandler<&CInputMain::Warp>);
	reg(CG::SAFEBOX_CHECKIN,   &CInputMain::SimpleHandler<&CInputMain::SafeboxCheckin>);
	reg(CG::SAFEBOX_ITEM_MOVE, &CInputMain::SimpleHandler<&CInputMain::SafeboxItemMove>);
	reg(CG::PARTY_INVITE,      &CInputMain::SimpleHandler<&CInputMain::PartyInvite>);
	reg(CG::PARTY_REMOVE,      &CInputMain::SimpleHandler<&CInputMain::PartyRemove>);
	reg(CG::PARTY_INVITE_ANSWER,&CInputMain::SimpleHandler<&CInputMain::PartyInviteAnswer>);
	reg(CG::PARTY_SET_STATE,   &CInputMain::SimpleHandler<&CInputMain::PartySetState>);
	reg(CG::PARTY_USE_SKILL,   &CInputMain::SimpleHandler<&CInputMain::PartyUseSkill>);
	reg(CG::PARTY_PARAMETER,   &CInputMain::SimpleHandler<&CInputMain::PartyParameter>);
	reg(CG::ANSWER_MAKE_GUILD, &CInputMain::SimpleHandler<&CInputMain::AnswerMakeGuild>);
	reg(CG::FISHING,           &CInputMain::SimpleHandler<&CInputMain::Fishing>);
	reg(CG::HACK,              &CInputMain::SimpleHandler<&CInputMain::Hack>);
	reg(CG::REFINE,            &CInputMain::SimpleHandler<&CInputMain::Refine>);
	reg(CG::SWITCHBOT_TOGGLE,  &CInputMain::SimpleHandler<&CInputMain::SwitchbotToggle>); // switchbot dev

	// SimpleHandlerV<fn>(LPCHARACTER, const void*)
	reg(CG::SCRIPT_ANSWER,      &CInputMain::SimpleHandlerV<&CInputMain::ScriptAnswer>);
	reg(CG::SCRIPT_BUTTON,      &CInputMain::SimpleHandlerV<&CInputMain::ScriptButton>);
	reg(CG::SCRIPT_SELECT_ITEM, &CInputMain::SimpleHandlerV<&CInputMain::ScriptSelectItem>);
	reg(CG::QUEST_INPUT_STRING,  &CInputMain::SimpleHandlerV<&CInputMain::QuestInputString>);
	reg(CG::QUEST_CONFIRM,      &CInputMain::SimpleHandlerV<&CInputMain::QuestConfirm>);

#ifdef ENABLE_ANTI_MULTIPLE_FARM
	reg(CG::ANTI_FARM,          &CInputMain::HandleAntiFarm);
#endif
#ifdef ENABLE_NPC_LOCATION_HELPER
	reg(CG::NPC_LOCATION_HELPER, &CInputMain::HandleNPCLocationHelper);
#endif
}

// ---------------------------------------------------------------------------

int CInputMain::Analyze(LPDESC d, uint16_t wHeader, const char * c_pData)
{
	LPCHARACTER ch = d->GetCharacter();

	if (!ch)
	{
		sys_err("no character on desc");
		d->SetPhase(PHASE_CLOSE);
		return 0;
	}

	auto it = m_handlers.find(wHeader);
	if (it == m_handlers.end())
	{
		sys_err("CInputMain::Analyze: unknown header %d (0x%04X) from %s", wHeader, wHeader, ch->GetName());
		return 0;
	}

	if (it->second.blockInObserverMode && ch->IsObserverMode())
		return 0;

	return (this->*(it->second.handler))(d, c_pData);
}

// ---------------------------------------------------------------------------
// CInputDead
// ---------------------------------------------------------------------------

CInputDead::CInputDead()
{
	RegisterHandlers();
}

void CInputDead::RegisterHandlers()
{
	m_handlers.clear();
	m_handlers[CG::PONG]    = { &CInputDead::HandlePong,    false };
	m_handlers[CG::CHAT]    = { &CInputDead::HandleChat,    false };
	m_handlers[CG::WHISPER] = { &CInputDead::HandleWhisper, false };
	m_handlers[CG::HACK]    = { &CInputDead::SimpleHandler<&CInputDead::Hack>, false };
}

int CInputDead::Analyze(LPDESC d, uint16_t wHeader, const char * c_pData)
{
	if (!d->GetCharacter())
	{
		sys_err("no character on desc");
		return 0;
	}

	auto it = m_handlers.find(wHeader);
	if (it == m_handlers.end())
		return 0;

	return (this->*(it->second.handler))(d, c_pData);
}

// ---------------------------------------------------------------------------
// HAntiMultipleFarm handlers
// ---------------------------------------------------------------------------
#ifdef ENABLE_ANTI_MULTIPLE_FARM
int CInputMain::HandleAntiFarm(LPDESC d, const char* p)
{
	LPCHARACTER ch = d->GetCharacter();
	if (!ch) return 0;
	return RecvAntiFarmUpdateStatus(ch, p, m_iBufferLeft);
}

int CInputMain::RecvAntiFarmUpdateStatus(LPCHARACTER ch, const char* data, size_t uiBytes)
{
	const TSendAntiFarmInfo* p = reinterpret_cast<const TSendAntiFarmInfo*>(data);

	if (uiBytes < sizeof(TSendAntiFarmInfo))
		return -1;

	LPDESC d = nullptr;
	if (!ch || !(d = ch->GetDesc()))
		return -1;

	const char* c_pData = data + sizeof(TSendAntiFarmInfo);
	uiBytes -= sizeof(TSendAntiFarmInfo);

	switch (p->subheader)
	{
	case AF_SH_SEND_STATUS_UPDATE:
	{
		size_t extraLen = (sizeof(DWORD) * MULTIPLE_FARM_MAX_ACCOUNT);
		if (uiBytes < extraLen)
			return -1;

		std::vector<DWORD> v_dwPIDS;
		for (uint8_t i = 0; i < MULTIPLE_FARM_MAX_ACCOUNT; ++i)
			v_dwPIDS.emplace_back(*reinterpret_cast<const DWORD*>(c_pData + (sizeof(DWORD) * i)));

		std::string sMAIf = d->GetLoginMacAdress();
		CAntiMultipleFarm::instance().SendBlockDropStatusChange(sMAIf, v_dwPIDS);

		{
			// Broadcast to other game servers via P2P
			CAntiMultipleFarm::TP2PChangeDropStatus dataPacket(GG::ANTI_FARM);
			dataPacket.length = sizeof(dataPacket);
			strlcpy(dataPacket.cMAIf, sMAIf.c_str(), sizeof(dataPacket.cMAIf));
			for (uint8_t i = 0; i < (uint8_t)v_dwPIDS.size() && i < MULTIPLE_FARM_MAX_ACCOUNT; ++i)
				dataPacket.dwPIDs[i] = v_dwPIDS[i];
			P2P_MANAGER::instance().Send(&dataPacket, sizeof(CAntiMultipleFarm::TP2PChangeDropStatus));
		}

		return (int)extraLen;
	}
	}

	return 0;
}
#endif

// ---------------------------------------------------------------------------
// NPC Location Helper handler
// ---------------------------------------------------------------------------
#ifdef ENABLE_NPC_LOCATION_HELPER
int CInputMain::HandleNPCLocationHelper(LPDESC d, const char* p)
{
	LPCHARACTER ch = d->GetCharacter();
	if (!ch) return 0;

	const TPacketCGNPCLocationHelper* pkt = reinterpret_cast<const TPacketCGNPCLocationHelper*>(p);
	if (!pkt) return 0;

	CNpcLocationHelperManager::instance().HandlePacket(ch, *pkt);
	return 0;
}
#endif // ENABLE_NPC_LOCATION_HELPER

