#include "stdafx.h"
#include "libgame/grid.h"
#include "utils.h"
#include "desc.h"
#include "desc_client.h"
#include "char.h"
#include "item.h"
#include "item_manager.h"
#include "packet_structs.h"
#include "log.h"
#include "db.h"
#include "locale_service.h"
#include "common/length.h"
#include "exchange.h"
#include "DragonSoul.h"

void exchange_packet(LPCHARACTER ch, BYTE sub_header, bool is_me, DWORD arg1, TItemPos arg2, DWORD arg3, void * pvData = NULL);

// êµí™˜ íŒ¨í‚·
void exchange_packet(LPCHARACTER ch, BYTE sub_header, bool is_me, DWORD arg1, TItemPos arg2, DWORD arg3, void * pvData)
{
	if (!ch->GetDesc())
		return;

	struct packet_exchange pack_exchg;

	pack_exchg.header 		= GC::EXCHANGE;
	pack_exchg.length = sizeof(pack_exchg);
	pack_exchg.sub_header 	= sub_header;
	pack_exchg.is_me		= is_me;
	pack_exchg.arg1		= arg1;
	pack_exchg.arg2		= arg2;
	pack_exchg.arg3		= arg3;

	if (sub_header == ExchangeSub::GC::ITEM_ADD && pvData)
	{
		thecore_memcpy(&pack_exchg.alSockets, ((LPITEM) pvData)->GetSockets(), sizeof(pack_exchg.alSockets));
		thecore_memcpy(&pack_exchg.aAttr, ((LPITEM) pvData)->GetAttributes(), sizeof(pack_exchg.aAttr));
	}
	else
	{
		memset(&pack_exchg.alSockets, 0, sizeof(pack_exchg.alSockets));
		memset(&pack_exchg.aAttr, 0, sizeof(pack_exchg.aAttr));
	}

	CHARACTER::SafeSendPacketTo(ch, &pack_exchg, sizeof(pack_exchg));
}

// êµí™˜ì„ ì‹œì‘
bool CHARACTER::ExchangeStart(LPCHARACTER victim)
{
	if (this == victim)	// ìê¸° ìì‹ ê³¼ëŠ” êµí™˜ì„ ëª»í•œë‹¤.
		return false;

	if (IsObserverMode())
	{
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê´€ì „ ìƒíƒœì—ì„œëŠ” êµí™˜ì„ í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		return false;
	}

	if (victim->IsNPC())
		return false;

	//PREVENT_TRADE_WINDOW
	if ( IsOpenSafebox() || GetShopOwner() || GetMyShop() || IsCubeOpen())
	{
		ChatPacket( CHAT_TYPE_INFO, LC_TEXT("ë‹¤ë¥¸ ê±°ë˜ì°½ì´ ì—´ë ¤ìˆì„ê²½ìš° ê±°ë˜ë¥¼ í• ìˆ˜ ì—†ìŠµë‹ˆë‹¤." ) );
		return false;
	}

	if ( victim->IsOpenSafebox() || victim->GetShopOwner() || victim->GetMyShop() || victim->IsCubeOpen() )
	{
		ChatPacket( CHAT_TYPE_INFO, LC_TEXT("ìƒëŒ€ë°©ì´ ë‹¤ë¥¸ ê±°ë˜ì¤‘ì´ë¼ ê±°ë˜ë¥¼ í• ìˆ˜ ì—†ìŠµë‹ˆë‹¤." ) );
		return false;
	}
	//END_PREVENT_TRADE_WINDOW
	int iDist = DISTANCE_APPROX(GetX() - victim->GetX(), GetY() - victim->GetY());

	// ê±°ë¦¬ ì²´í¬
	if (iDist >= EXCHANGE_MAX_DISTANCE)
		return false;

	if (GetExchange())
		return false;

	if (victim->GetExchange())
	{
		exchange_packet(this, ExchangeSub::GC::ALREADY, 0, 0, NPOS, 0);
		return false;
	}

	if (victim->IsBlockMode(BLOCK_EXCHANGE))
	{
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒëŒ€ë°©ì´ êµí™˜ ê±°ë¶€ ìƒíƒœì…ë‹ˆë‹¤."));
		return false;
	}

	SetExchange(M2_NEW CExchange(this));
	victim->SetExchange(M2_NEW CExchange(victim));

	victim->GetExchange()->SetCompany(GetExchange());
	GetExchange()->SetCompany(victim->GetExchange());

	//
	SetExchangeTime();
	victim->SetExchangeTime();

	exchange_packet(victim, ExchangeSub::GC::START, 0, GetVID(), NPOS, 0);
	exchange_packet(this, ExchangeSub::GC::START, 0, victim->GetVID(), NPOS, 0);

	return true;
}

CExchange::CExchange(LPCHARACTER pOwner)
{
	m_pCompany = NULL;

	m_bAccept = false;

	for (int i = 0; i < EXCHANGE_ITEM_MAX_NUM; ++i)
	{
		m_apItems[i] = NULL;
		m_aItemPos[i] = NPOS;
		m_abItemDisplayPos[i] = 0;
	}

	m_lGold = 0;

	m_pOwner = pOwner;
	pOwner->SetExchange(this);

	m_pGrid = M2_NEW CGrid(4,3);
}

CExchange::~CExchange()
{
	M2_DELETE(m_pGrid);
}

bool CExchange::AddItem(TItemPos item_pos, BYTE display_pos)
{
	assert(m_pOwner != NULL && GetCompany());

	if (!item_pos.IsValidItemPosition())
		return false;

	// ì¥ë¹„ëŠ” êµí™˜í•  ìˆ˜ ì—†ìŒ
	if (item_pos.IsEquipPosition())
		return false;

	LPITEM item;

	if (!(item = m_pOwner->GetItem(item_pos)))
		return false;

	if (IS_SET(item->GetAntiFlag(), ITEM_ANTIFLAG_GIVE))
	{
		m_pOwner->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì•„ì´í…œì„ ê±´ë„¤ì¤„ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		return false;
	}

	if (true == item->isLocked())
	{
		return false;
	}

	// ì´ë¯¸ êµí™˜ì°½ì— ì¶”ê°€ëœ ì•„ì´í…œì¸ê°€?
	if (item->IsExchanging())
	{
		sys_log(0, "EXCHANGE under exchanging");
		return false;
	}

	if (!m_pGrid->IsEmpty(display_pos, 1, item->GetSize()))
	{
		sys_log(0, "EXCHANGE not empty item_pos %d %d %d", display_pos, 1, item->GetSize());
		return false;
	}

	Accept(false);
	GetCompany()->Accept(false);

	for (int i = 0; i < EXCHANGE_ITEM_MAX_NUM; ++i)
	{
		if (m_apItems[i])
			continue;

		m_apItems[i]		= item;
		m_aItemPos[i]		= item_pos;
		m_abItemDisplayPos[i]	= display_pos;
		m_pGrid->Put(display_pos, 1, item->GetSize());

		item->SetExchanging(true);

		exchange_packet(m_pOwner, 
				ExchangeSub::GC::ITEM_ADD,
				true,
				item->GetVnum(),
				TItemPos(RESERVED_WINDOW, display_pos),
				item->GetCount(),
				item);

		exchange_packet(GetCompany()->GetOwner(),
				ExchangeSub::GC::ITEM_ADD, 
				false, 
				item->GetVnum(),
				TItemPos(RESERVED_WINDOW, display_pos),
				item->GetCount(),
				item);

		sys_log(0, "EXCHANGE AddItem success %s pos(%d, %d) %d", item->GetName(), item_pos.window_type, item_pos.cell, display_pos);

		return true;
	}

	// ì¶”ê°€í•  ê³µê°„ì´ ì—†ìŒ
	return false;
}

bool CExchange::RemoveItem(BYTE pos)
{
	if (pos >= EXCHANGE_ITEM_MAX_NUM)
		return false;

	if (!m_apItems[pos])
		return false;

	TItemPos PosOfInventory = m_aItemPos[pos];
	m_apItems[pos]->SetExchanging(false);

	m_pGrid->Get(m_abItemDisplayPos[pos], 1, m_apItems[pos]->GetSize());

	exchange_packet(GetOwner(),	ExchangeSub::GC::ITEM_DEL, true, pos, NPOS, 0);
	exchange_packet(GetCompany()->GetOwner(), ExchangeSub::GC::ITEM_DEL, false, pos, PosOfInventory, 0);

	Accept(false);
	GetCompany()->Accept(false);

	m_apItems[pos]	    = NULL;
	m_aItemPos[pos]	    = NPOS;
	m_abItemDisplayPos[pos] = 0;
	return true;
}

bool CExchange::AddGold(long gold)
{
	if (gold <= 0)
		return false;

	if (GetOwner()->GetGold() < gold)
	{
		// ê°€ì§€ê³  ìˆëŠ” ëˆì´ ë¶€ì¡±.
		exchange_packet(GetOwner(), ExchangeSub::GC::LESS_GOLD, 0, 0, NPOS, 0);
		return false;
	}

	if ( LC_IsCanada() == true || LC_IsEurope() == true )
	{
		if ( m_lGold > 0 )
		{
			return false;
		}
	}

	Accept(false);
	GetCompany()->Accept(false);

	m_lGold = gold;

	exchange_packet(GetOwner(), ExchangeSub::GC::GOLD_ADD, true, m_lGold, NPOS, 0);
	exchange_packet(GetCompany()->GetOwner(), ExchangeSub::GC::GOLD_ADD, false, m_lGold, NPOS, 0);
	return true;
}

// ëˆì´ ì¶©ë¶„íˆ ìˆëŠ”ì§€, êµí™˜í•˜ë ¤ëŠ” ì•„ì´í…œì´ ì‹¤ì œë¡œ ìˆëŠ”ì§€ í™•ì¸ í•œë‹¤.
bool CExchange::Check(int * piItemCount)
{
	if (GetOwner()->GetGold() < m_lGold)
		return false;

	int item_count = 0;

	for (int i = 0; i < EXCHANGE_ITEM_MAX_NUM; ++i)
	{
		if (!m_apItems[i])
			continue;

		if (!m_aItemPos[i].IsValidItemPosition())
			return false;

		if (m_apItems[i] != GetOwner()->GetItem(m_aItemPos[i]))
			return false;

		++item_count;
	}

	*piItemCount = item_count;
	return true;
}

bool CExchange::CheckSpace()
{
	static CGrid s_grid1(5, INVENTORY_MAX_NUM/5 / 2); // inven page 1
	static CGrid s_grid2(5, INVENTORY_MAX_NUM/5 / 2); // inven page 2

	s_grid1.Clear();
	s_grid2.Clear();

	LPCHARACTER	victim = GetCompany()->GetOwner();
	LPITEM item;

	int i;

	for (i = 0; i < INVENTORY_MAX_NUM / 2; ++i)
	{
		if (!(item = victim->GetInventoryItem(i)))
			continue;

		s_grid1.Put(i, 1, item->GetSize());
	}
	for (i = INVENTORY_MAX_NUM / 2; i < INVENTORY_MAX_NUM; ++i)
	{
		if (!(item = victim->GetInventoryItem(i)))
			continue;

		s_grid2.Put(i - INVENTORY_MAX_NUM / 2, 1, item->GetSize());
	}

	// ì•„... ë­”ê°€ ê°œë³‘ì‹  ê°™ì§€ë§Œ... ìš©í˜¼ì„ ì¸ë²¤ì„ ë…¸ë©€ ì¸ë²¤ ë³´ê³  ë”°ë¼ ë§Œë“  ë‚´ ì˜ëª»ì´ë‹¤ ã… ã… 
	static std::vector <WORD> s_vDSGrid(DRAGON_SOUL_INVENTORY_MAX_NUM);
	
	// ì¼ë‹¨ ìš©í˜¼ì„ì„ êµí™˜í•˜ì§€ ì•Šì„ ê°€ëŠ¥ì„±ì´ í¬ë¯€ë¡œ, ìš©í˜¼ì„ ì¸ë²¤ ë³µì‚¬ëŠ” ìš©í˜¼ì„ì´ ìˆì„ ë•Œ í•˜ë„ë¡ í•œë‹¤.
	bool bDSInitialized = false;
	
	for (i = 0; i < EXCHANGE_ITEM_MAX_NUM; ++i)
	{
		if (!(item = m_apItems[i]))
			continue;

		if (item->IsDragonSoul())
		{
			if (!victim->DragonSoul_IsQualified())
			{
				return false;
			}

			if (!bDSInitialized)
			{
				bDSInitialized = true;
				victim->CopyDragonSoulItemGrid(s_vDSGrid);
			}

			bool bExistEmptySpace = false;
			WORD wBasePos = DSManager::instance().GetBasePosition(item);
			if (wBasePos >= DRAGON_SOUL_INVENTORY_MAX_NUM)
				return false;
			
			for (int i = 0; i < DRAGON_SOUL_BOX_SIZE; i++)
			{
				WORD wPos = wBasePos + i;
				// if (0 == s_vDSGrid[wBasePos])
				if (0 == s_vDSGrid[wPos]) // Fix
				{
					bool bEmpty = true;
					for (int j = 1; j < item->GetSize(); j++)
					{
						if (s_vDSGrid[wPos + j * DRAGON_SOUL_BOX_COLUMN_NUM])
						{
							bEmpty = false;
							break;
						}
					}
					if (bEmpty)
					{
						for (int j = 0; j < item->GetSize(); j++)
						{
							s_vDSGrid[wPos + j * DRAGON_SOUL_BOX_COLUMN_NUM] =  wPos + 1;
						}
						bExistEmptySpace = true;
						break;
					}
				}
				if (bExistEmptySpace)
					break;
			}
			if (!bExistEmptySpace)
				return false;
		}
		else
		{
			int iPos = s_grid1.FindBlank(1, item->GetSize());

			if (iPos >= 0)
			{
				s_grid1.Put(iPos, 1, item->GetSize());
			}
			else
			{
				iPos = s_grid2.FindBlank(1, item->GetSize());

				if (iPos >= 0)
				{
					s_grid2.Put(iPos, 1, item->GetSize());
				}
				else
				{
					return false;
				}
			}
		}
	}

	return true;
}

// êµí™˜ ë (ì•„ì´í…œê³¼ ëˆ ë“±ì„ ì‹¤ì œë¡œ ì˜®ê¸´ë‹¤)
bool CExchange::Done()
{
	int		empty_pos, i;
	LPITEM	item;

	LPCHARACTER	victim = GetCompany()->GetOwner();

	for (i = 0; i < EXCHANGE_ITEM_MAX_NUM; ++i)
	{
		if (!(item = m_apItems[i]))
			continue;

		if (item->IsDragonSoul())
			empty_pos = victim->GetEmptyDragonSoulInventory(item);
		else
			empty_pos = victim->GetEmptyInventory(item->GetSize());

		if (empty_pos < 0)
		{
			sys_err("Exchange::Done : Cannot find blank position in inventory %s <-> %s item %s", 
					m_pOwner->GetName(), victim->GetName(), item->GetName());
			continue;
		}

		assert(empty_pos >= 0);

		m_pOwner->SyncQuickslot(QUICKSLOT_TYPE_ITEM, item->GetCell(), 255);

		item->RemoveFromCharacter();
		if (item->IsDragonSoul())
			item->AddToCharacter(victim, TItemPos(DRAGON_SOUL_INVENTORY, empty_pos));
		else
			item->AddToCharacter(victim, TItemPos(INVENTORY, empty_pos));
		ITEM_MANAGER::instance().FlushDelayedSave(item);

		item->SetExchanging(false);
		{
			char exchange_buf[51];

			snprintf(exchange_buf, sizeof(exchange_buf), "%s %u %u", item->GetName(), GetOwner()->GetPlayerID(), item->GetCount());
			LogManager::instance().ItemLog(victim, item, "EXCHANGE_TAKE", exchange_buf);

			snprintf(exchange_buf, sizeof(exchange_buf), "%s %u %u", item->GetName(), victim->GetPlayerID(), item->GetCount());
			LogManager::instance().ItemLog(GetOwner(), item, "EXCHANGE_GIVE", exchange_buf);

			if (item->GetVnum() >= 80003 && item->GetVnum() <= 80007)
			{
				LogManager::instance().GoldBarLog(victim->GetPlayerID(), item->GetID(), EXCHANGE_TAKE, "");
				LogManager::instance().GoldBarLog(GetOwner()->GetPlayerID(), item->GetID(), EXCHANGE_GIVE, "");
			}
		}

		m_apItems[i] = NULL;
	}

	if (m_lGold)
	{
		GetOwner()->PointChange(POINT_GOLD, -m_lGold, true);
		victim->PointChange(POINT_GOLD, m_lGold, true);

		if (m_lGold > 1000)
		{
			char exchange_buf[51];
			snprintf(exchange_buf, sizeof(exchange_buf), "%u %s", GetOwner()->GetPlayerID(), GetOwner()->GetName());
			LogManager::instance().CharLog(victim, m_lGold, "EXCHANGE_GOLD_TAKE", exchange_buf);

			snprintf(exchange_buf, sizeof(exchange_buf), "%u %s", victim->GetPlayerID(), victim->GetName());
			LogManager::instance().CharLog(GetOwner(), m_lGold, "EXCHANGE_GOLD_GIVE", exchange_buf);
		}
	}

	m_pGrid->Clear();
	return true;
}

// êµí™˜ì„ ë™ì˜
bool CExchange::Accept(bool bAccept)
{
	if (m_bAccept == bAccept)
		return true;

	m_bAccept = bAccept;

	// ë‘˜ ë‹¤ ë™ì˜ í–ˆìœ¼ë¯€ë¡œ êµí™˜ ì„±ë¦½
	if (m_bAccept && GetCompany()->m_bAccept)
	{
		int	iItemCount;

		LPCHARACTER victim = GetCompany()->GetOwner();

		//PREVENT_PORTAL_AFTER_EXCHANGE
		GetOwner()->SetExchangeTime();
		victim->SetExchangeTime();		
		//END_PREVENT_PORTAL_AFTER_EXCHANGE

		// exchange_check ì—ì„œëŠ” êµí™˜í•  ì•„ì´í…œë“¤ì´ ì œìë¦¬ì— ìˆë‚˜ í™•ì¸í•˜ê³ ,
		// ì—˜í¬ë„ ì¶©ë¶„íˆ ìˆë‚˜ í™•ì¸í•œë‹¤, ë‘ë²ˆì§¸ ì¸ìë¡œ êµí™˜í•  ì•„ì´í…œ ê°œìˆ˜
		// ë¥¼ ë¦¬í„´í•œë‹¤.
		if (!Check(&iItemCount))
		{
			GetOwner()->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ëˆì´ ë¶€ì¡±í•˜ê±°ë‚˜ ì•„ì´í…œì´ ì œìë¦¬ì— ì—†ìŠµë‹ˆë‹¤."));
			victim->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒëŒ€ë°©ì˜ ëˆì´ ë¶€ì¡±í•˜ê±°ë‚˜ ì•„ì´í…œì´ ì œìë¦¬ì— ì—†ìŠµë‹ˆë‹¤."));
			goto EXCHANGE_END;
		}

		// ë¦¬í„´ ë°›ì€ ì•„ì´í…œ ê°œìˆ˜ë¡œ ìƒëŒ€ë°©ì˜ ì†Œì§€í’ˆì— ë‚¨ì€ ìë¦¬ê°€ ìˆë‚˜ í™•ì¸í•œë‹¤.
		if (!CheckSpace())
		{
			GetOwner()->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒëŒ€ë°©ì˜ ì†Œì§€í’ˆì— ë¹ˆ ê³µê°„ì´ ì—†ìŠµë‹ˆë‹¤."));
			victim->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì†Œì§€í’ˆì— ë¹ˆ ê³µê°„ì´ ì—†ìŠµë‹ˆë‹¤."));
			goto EXCHANGE_END;
		}

		// ìƒëŒ€ë°©ë„ ë§ˆì°¬ê°€ì§€ë¡œ..
		if (!GetCompany()->Check(&iItemCount))
		{
			victim->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ëˆì´ ë¶€ì¡±í•˜ê±°ë‚˜ ì•„ì´í…œì´ ì œìë¦¬ì— ì—†ìŠµë‹ˆë‹¤."));
			GetOwner()->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒëŒ€ë°©ì˜ ëˆì´ ë¶€ì¡±í•˜ê±°ë‚˜ ì•„ì´í…œì´ ì œìë¦¬ì— ì—†ìŠµë‹ˆë‹¤."));
			goto EXCHANGE_END;
		}

		if (!GetCompany()->CheckSpace())
		{
			victim->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒëŒ€ë°©ì˜ ì†Œì§€í’ˆì— ë¹ˆ ê³µê°„ì´ ì—†ìŠµë‹ˆë‹¤."));
			GetOwner()->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì†Œì§€í’ˆì— ë¹ˆ ê³µê°„ì´ ì—†ìŠµë‹ˆë‹¤."));
			goto EXCHANGE_END;
		}

		if (db_clientdesc->GetSocket() == INVALID_SOCKET)
		{
			sys_err("Cannot use exchange feature while DB cache connection is dead.");
			victim->ChatPacket(CHAT_TYPE_INFO, "Unknown error");
			GetOwner()->ChatPacket(CHAT_TYPE_INFO, "Unknown error");
			goto EXCHANGE_END;
		}

		if (Done())
		{
			if (m_lGold) // ëˆì´ ìˆì„ ë–„ë§Œ ì €ì¥
				GetOwner()->Save();

			if (GetCompany()->Done())
			{
				if (GetCompany()->m_lGold) // ëˆì´ ìˆì„ ë•Œë§Œ ì €ì¥
					victim->Save();

				// INTERNATIONAL_VERSION
				GetOwner()->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("%s ë‹˜ê³¼ì˜ êµí™˜ì´ ì„±ì‚¬ ë˜ì—ˆìŠµë‹ˆë‹¤."), victim->GetName());
				victim->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("%s ë‹˜ê³¼ì˜ êµí™˜ì´ ì„±ì‚¬ ë˜ì—ˆìŠµë‹ˆë‹¤."), GetOwner()->GetName());
				// END_OF_INTERNATIONAL_VERSION
			}
		}

EXCHANGE_END:
		Cancel();
		return false;
	}
	else
	{
		// ì•„ë‹ˆë©´ acceptì— ëŒ€í•œ íŒ¨í‚·ì„ ë³´ë‚´ì.
		exchange_packet(GetOwner(), ExchangeSub::GC::ACCEPT, true, m_bAccept, NPOS, 0);
		exchange_packet(GetCompany()->GetOwner(), ExchangeSub::GC::ACCEPT, false, m_bAccept, NPOS, 0);
		return true;
	}
}

// êµí™˜ ì·¨ì†Œ
void CExchange::Cancel()
{
	exchange_packet(GetOwner(), ExchangeSub::GC::END, 0, 0, NPOS, 0);
	GetOwner()->SetExchange(NULL);

	for (int i = 0; i < EXCHANGE_ITEM_MAX_NUM; ++i)
	{
		if (m_apItems[i])
			m_apItems[i]->SetExchanging(false);
	}

	if (GetCompany())
	{
		GetCompany()->SetCompany(NULL);
		GetCompany()->Cancel();
	}

	M2_DELETE(this);
}


