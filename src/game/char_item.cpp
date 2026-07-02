#include "stdafx.h"

#include <stack>

#include "utils.h"
#include "config.h"
#include "char.h"
#include "char_manager.h"
#include "item_manager.h"
#include "desc.h"
#include "desc_client.h"
#include "desc_manager.h"
#include "packet_structs.h"
#include "affect.h"
#include "skill.h"
#include "start_position.h"
#include "mob_manager.h"
#include "db.h"
#include "log.h"
#include "vector.h"
#include "buffer_manager.h"
#include "questmanager.h"
#include "fishing.h"
#include "party.h"
#include "dungeon.h"
#include "refine.h"
#include "unique_item.h"
#include "war_map.h"
#include "xmas_event.h"
#include "marriage.h"
#include "polymorph.h"
#include "blend_item.h"
#include "castle.h"
#include "BattleArena.h"
#include "arena.h"
#include "threeway_war.h"

#include "safebox.h"
#include "shop.h"

#include "common/VnumHelper.h"
#include "DragonSoul.h"
#include "buff_on_attributes.h"
#include "belt_inventory_helper.h"
#ifdef ENABLE_NPC_LOCATION_HELPER
#include "npc_location_helper.h"
#endif

const int ITEM_BROKEN_METIN_VNUM = 28960;

// CHANGE_ITEM_ATTRIBUTES
const DWORD CHARACTER::msc_dwDefaultChangeItemAttrCycle = 10;
const char CHARACTER::msc_szLastChangeItemAttrFlag[] = "Item.LastChangeItemAttr";
const char CHARACTER::msc_szChangeItemAttrCycleFlag[] = "change_itemattr_cycle";
// END_OF_CHANGE_ITEM_ATTRIBUTES
const BYTE g_aBuffOnAttrPoints[] = { POINT_ENERGY, POINT_COSTUME_ATTR_BONUS };

struct FFindStone
{
	std::map<DWORD, LPCHARACTER> m_mapStone;

	void operator()(LPENTITY pEnt)
	{
		if (pEnt->IsType(ENTITY_CHARACTER) == true)
		{
			LPCHARACTER pChar = (LPCHARACTER)pEnt;

			if (pChar->IsStone() == true)
			{
				m_mapStone[(DWORD)pChar->GetVID()] = pChar;
			}
		}
	}
};


//ê·€í™˜ë¶€, ê·€í™˜ê¸°ì–µë¶€, ê²°í˜¼ë°˜ì§€
static bool IS_SUMMON_ITEM(int vnum)
{
	switch (vnum)
	{
		case 22000:
		case 22010:
		case 22011:
		case 22020:
		case ITEM_MARRIAGE_RING:
			return true;
	}

	return false;
}

static bool IS_MONKEY_DUNGEON(int map_index)
{
	switch (map_index)
	{
		case 5:
		case 25:
		case 45:
		case 108:
		case 109:
			return true;;
	}

	return false;
}

bool IS_SUMMONABLE_ZONE(int map_index)
{
	// ëª½í‚¤ë˜ì „
	if (IS_MONKEY_DUNGEON(map_index))
		return false;
	// ì„±
	if (IS_CASTLE_MAP(map_index))
		return false;

	switch (map_index)
	{
		case 66 : // ì‚¬ê·€íƒ€ì›Œ
		case 71 : // ê±°ë¯¸ ë˜ì „ 2ì¸µ
		case 72 : // ì²œì˜ ë™êµ´
		case 73 : // ì²œì˜ ë™êµ´ 2ì¸µ
		case 193 : // ê±°ë¯¸ ë˜ì „ 2-1ì¸µ
#if 0
		case 184 : // ì²œì˜ ë™êµ´(ì‹ ìˆ˜)
		case 185 : // ì²œì˜ ë™êµ´ 2ì¸µ(ì‹ ìˆ˜)
		case 186 : // ì²œì˜ ë™êµ´(ì²œì¡°)
		case 187 : // ì²œì˜ ë™êµ´ 2ì¸µ(ì²œì¡°)
		case 188 : // ì²œì˜ ë™êµ´(ì§„ë…¸)
		case 189 : // ì²œì˜ ë™êµ´ 2ì¸µ(ì§„ë…¸)
#endif
//		case 206 : // ì•„ê·€ë™êµ´
		case 216 : // ì•„ê·€ë™êµ´
		case 217 : // ê±°ë¯¸ ë˜ì „ 3ì¸µ
		case 208 : // ì²œì˜ ë™êµ´ (ìš©ë°©)
			return false;
	}

	if (CBattleArena::IsBattleArenaMap(map_index)) return false;

	// ëª¨ë“  private ë§µìœ¼ë¡  ì›Œí”„ ë¶ˆê°€ëŠ¥
	if (map_index > 10000) return false;

	return true;
}

bool IS_BOTARYABLE_ZONE(int nMapIndex)
{
	if (LC_IsYMIR() == false && LC_IsKorea() == false) return true;

	switch (nMapIndex)
	{
		case 1 :
		case 3 :
		case 21 :
		case 23 :
		case 41 :
		case 43 :
			return true;
	}
	
	return false;
}

// item socket ì´ í”„ë¡œí† íƒ€ì…ê³¼ ê°™ì€ì§€ ì²´í¬ -- by mhh
static bool FN_check_item_socket(LPITEM item)
{
	for (int i = 0; i < ITEM_SOCKET_MAX_NUM; ++i)
	{
		if (item->GetSocket(i) != item->GetProto()->alSockets[i])
			return false;
	}

	return true;
}

// item socket ë³µì‚¬ -- by mhh
static void FN_copy_item_socket(LPITEM dest, LPITEM src)
{
	for (int i = 0; i < ITEM_SOCKET_MAX_NUM; ++i)
	{
		dest->SetSocket(i, src->GetSocket(i));
	}
}
static bool FN_check_item_sex(LPCHARACTER ch, LPITEM item)
{
	// ë‚¨ì ê¸ˆì§€
	if (IS_SET(item->GetAntiFlag(), ITEM_ANTIFLAG_MALE))
	{
		if (SEX_MALE==GET_SEX(ch))
			return false;
	}
	// ì—¬ìê¸ˆì§€
	if (IS_SET(item->GetAntiFlag(), ITEM_ANTIFLAG_FEMALE)) 
	{
		if (SEX_FEMALE==GET_SEX(ch))
			return false;
	}

	return true;
}


/////////////////////////////////////////////////////////////////////////////
// ITEM HANDLING
/////////////////////////////////////////////////////////////////////////////
bool CHARACTER::CanHandleItem(bool bSkipCheckRefine, bool bSkipObserver)
{
	if (!bSkipObserver)
		if (m_bIsObserver)
			return false;

	if (GetMyShop())
		return false;

	if (!bSkipCheckRefine)
		if (m_bUnderRefine)
			return false;

	if (IsCubeOpen() || NULL != DragonSoul_RefineWindow_GetOpener())
		return false;

	if (IsWarping())
		return false;

	return true;
}

LPITEM CHARACTER::GetInventoryItem(WORD wCell) const
{
	return GetItem(TItemPos(INVENTORY, wCell));
}
LPITEM CHARACTER::GetItem(TItemPos Cell) const
{
	if (!IsValidItemPosition(Cell))
		return NULL;

	WORD wCell = Cell.cell;
	BYTE window_type = Cell.window_type;

	switch (window_type)
	{
		case INVENTORY:
		case EQUIPMENT:
			if (wCell >= INVENTORY_AND_EQUIP_SLOT_MAX)
			{
				sys_err("CHARACTER::GetInventoryItem: invalid item cell %d", wCell);
				return NULL;
			}
			return m_pointsInstant.pItems[wCell];
		case DRAGON_SOUL_INVENTORY:
			if (wCell >= DRAGON_SOUL_INVENTORY_MAX_NUM)
			{
				sys_err("CHARACTER::GetInventoryItem: invalid DS item cell %d", wCell);
				return NULL;
			}
			return m_pointsInstant.pDSItems[wCell];

		default:
			return NULL;
	}
	
	return NULL;
}

void CHARACTER::SetItem(TItemPos Cell, LPITEM pItem)
{
	WORD wCell = Cell.cell;
	BYTE window_type = Cell.window_type;
	if (reinterpret_cast<uintptr_t>(pItem) == 0xff || reinterpret_cast<uintptr_t>(pItem) == 0xffffffff)
	{
		sys_err("!!! FATAL ERROR !!! item == 0xff (char: %s cell: %u)", GetName(), wCell);
		core_dump();
		return;
	}

	if (pItem && pItem->GetOwner())
	{
		assert(!"GetOwner exist");
		return;
	}
	// ê¸°ë³¸ ì¸ë²¤í† ë¦¬
	switch(window_type)
	{
	case INVENTORY:
	case EQUIPMENT:
		{
			if (wCell >= INVENTORY_AND_EQUIP_SLOT_MAX)
			{
				sys_err("CHARACTER::SetItem: invalid item cell %d", wCell);
				return;
			}

			LPITEM pOld = m_pointsInstant.pItems[wCell];

			if (pOld)
			{
				if (wCell < INVENTORY_MAX_NUM)
				{
					for (int i = 0; i < pOld->GetSize(); ++i)
					{
						int p = wCell + (i * 5);

						if (p >= INVENTORY_MAX_NUM)
							continue;

						if (m_pointsInstant.pItems[p] && m_pointsInstant.pItems[p] != pOld)
							continue;

						m_pointsInstant.bItemGrid[p] = 0;
					}
				}
				else
					m_pointsInstant.bItemGrid[wCell] = 0;
			}

			if (pItem)
			{
				if (wCell < INVENTORY_MAX_NUM)
				{
					for (int i = 0; i < pItem->GetSize(); ++i)
					{
						int p = wCell + (i * 5);

						if (p >= INVENTORY_MAX_NUM)
							continue;

						// wCell + 1 ë¡œ í•˜ëŠ” ê²ƒì€ ë¹ˆê³³ì„ ì²´í¬í•  ë•Œ ê°™ì€
						// ì•„ì´í…œì€ ì˜ˆì™¸ì²˜ë¦¬í•˜ê¸° ìœ„í•¨
						m_pointsInstant.bItemGrid[p] = wCell + 1;
					}
				}
				else
					m_pointsInstant.bItemGrid[wCell] = wCell + 1;
			}

			m_pointsInstant.pItems[wCell] = pItem;
		}
		break;
	// ìš©í˜¼ì„ ì¸ë²¤í† ë¦¬
	case DRAGON_SOUL_INVENTORY:
		{
			LPITEM pOld = m_pointsInstant.pDSItems[wCell];

			if (pOld)
			{
				if (wCell < DRAGON_SOUL_INVENTORY_MAX_NUM)
				{
					for (int i = 0; i < pOld->GetSize(); ++i)
					{
						int p = wCell + (i * DRAGON_SOUL_BOX_COLUMN_NUM);

						if (p >= DRAGON_SOUL_INVENTORY_MAX_NUM)
							continue;

						if (m_pointsInstant.pDSItems[p] && m_pointsInstant.pDSItems[p] != pOld)
							continue;

						m_pointsInstant.wDSItemGrid[p] = 0;
					}
				}
				else
					m_pointsInstant.wDSItemGrid[wCell] = 0;
			}

			if (pItem)
			{
				if (wCell >= DRAGON_SOUL_INVENTORY_MAX_NUM)
				{
					sys_err("CHARACTER::SetItem: invalid DS item cell %d", wCell);
					return;
				}

				if (wCell < DRAGON_SOUL_INVENTORY_MAX_NUM)
				{
					for (int i = 0; i < pItem->GetSize(); ++i)
					{
						int p = wCell + (i * DRAGON_SOUL_BOX_COLUMN_NUM);

						if (p >= DRAGON_SOUL_INVENTORY_MAX_NUM)
							continue;

						// wCell + 1 ë¡œ í•˜ëŠ” ê²ƒì€ ë¹ˆê³³ì„ ì²´í¬í•  ë•Œ ê°™ì€
						// ì•„ì´í…œì€ ì˜ˆì™¸ì²˜ë¦¬í•˜ê¸° ìœ„í•¨
						m_pointsInstant.wDSItemGrid[p] = wCell + 1;
					}
				}
				else
					m_pointsInstant.wDSItemGrid[wCell] = wCell + 1;
			}

			m_pointsInstant.pDSItems[wCell] = pItem;
		}
		break;
	default:
		sys_err ("Invalid Inventory type %d", window_type);
		return;
	}

	if (GetDesc())
	{
		// í™•ì¥ ì•„ì´í…œ: ì„œë²„ì—ì„œ ì•„ì´í…œ í”Œë˜ê·¸ ì •ë³´ë¥¼ ë³´ë‚¸ë‹¤
		if (pItem)
		{
			TPacketGCItemSet pack;
			pack.header = GC::ITEM_SET;
			pack.length = sizeof(pack);
			pack.pos = Cell;

			pack.count = pItem->GetCount();
			pack.vnum = pItem->GetVnum();
			pack.flags = pItem->GetFlag();
			pack.anti_flags	= pItem->GetAntiFlag();
			pack.highlight = (Cell.window_type == DRAGON_SOUL_INVENTORY);

			thecore_memcpy(pack.alSockets, pItem->GetSockets(), sizeof(pack.alSockets));
			thecore_memcpy(pack.aAttr, pItem->GetAttributes(), sizeof(pack.aAttr));

			SafeSendPacket(&pack, sizeof(TPacketGCItemSet));
		}
		else
		{
			TPacketGCItemDel pack;
			pack.header = GC::ITEM_DEL;
			pack.length = sizeof(pack);
			pack.pos = Cell;
			SafeSendPacket(&pack, sizeof(TPacketGCItemDel));
		}
	}

	if (pItem)
	{
		pItem->SetCell(this, wCell);
		switch (window_type)
		{
		case INVENTORY:
		case EQUIPMENT:
			if ((wCell < INVENTORY_MAX_NUM) || (BELT_INVENTORY_SLOT_START <= wCell && BELT_INVENTORY_SLOT_END > wCell))
				pItem->SetWindow(INVENTORY);
			else
				pItem->SetWindow(EQUIPMENT);
			break;
		case DRAGON_SOUL_INVENTORY:
			pItem->SetWindow(DRAGON_SOUL_INVENTORY);
			break;
		}
	}
}

LPITEM CHARACTER::GetWear(BYTE bCell) const
{
	// > WEAR_MAX_NUM : ìš©í˜¼ì„ ìŠ¬ë¡¯ë“¤.
	if (bCell >= WEAR_MAX_NUM + (DWORD)DRAGON_SOUL_DECK_MAX_NUM * (DWORD)DS_SLOT_MAX)
	{
		sys_err("CHARACTER::GetWear: invalid wear cell %d", bCell);
		return NULL;
	}

	return m_pointsInstant.pItems[INVENTORY_MAX_NUM + bCell];
}

void CHARACTER::SetWear(BYTE bCell, LPITEM item)
{
	// > WEAR_MAX_NUM : ìš©í˜¼ì„ ìŠ¬ë¡¯ë“¤.
	if (bCell >= WEAR_MAX_NUM + (DWORD)DRAGON_SOUL_DECK_MAX_NUM * (DWORD)DS_SLOT_MAX)
	{
		sys_err("CHARACTER::SetItem: invalid item cell %d", bCell);
		return;
	}

	SetItem(TItemPos (INVENTORY, INVENTORY_MAX_NUM + bCell), item);

	if (!item && bCell == WEAR_WEAPON)
	{
		// ê·€ê²€ ì‚¬ìš© ì‹œ ë²—ëŠ” ê²ƒì´ë¼ë©´ íš¨ê³¼ë¥¼ ì—†ì• ì•¼ í•œë‹¤.
		if (IsAffectFlag(AFF_GWIGUM))
			RemoveAffect(SKILL_GWIGEOM);

		if (IsAffectFlag(AFF_GEOMGYEONG))
			RemoveAffect(SKILL_GEOMKYUNG);
	}
}

void CHARACTER::ClearItem()
{
	int		i;
	LPITEM	item;
    
    // Clear equipment slots first
    for (i = 0; i < WEAR_MAX_NUM + (DWORD)DRAGON_SOUL_DECK_MAX_NUM * (DWORD)DS_SLOT_MAX; ++i)
    {
        if ((item = GetWear(i)))
        {
            item->SetSkipSave(true);
            ITEM_MANAGER::instance().FlushDelayedSave(item);

            item->RemoveFromCharacter();
            M2_DESTROY_ITEM(item);
        }
    }
	
	for (i = 0; i < INVENTORY_AND_EQUIP_SLOT_MAX; ++i)
	{
		if ((item = GetInventoryItem(i)))
		{
			item->SetSkipSave(true);
			ITEM_MANAGER::instance().FlushDelayedSave(item);

			item->RemoveFromCharacter();
			M2_DESTROY_ITEM(item);

			SyncQuickslot(QUICKSLOT_TYPE_ITEM, i, 255);
		}
	}
	for (i = 0; i < DRAGON_SOUL_INVENTORY_MAX_NUM; ++i)
	{
		if ((item = GetItem(TItemPos(DRAGON_SOUL_INVENTORY, i))))
		{
			item->SetSkipSave(true);
			ITEM_MANAGER::instance().FlushDelayedSave(item);

			item->RemoveFromCharacter();
			M2_DESTROY_ITEM(item);
		}
	}
}

bool CHARACTER::IsEmptyItemGrid(TItemPos Cell, BYTE bSize, int iExceptionCell) const
{
	switch (Cell.window_type)
	{
		case INVENTORY:
			{
				BYTE bCell = Cell.cell;

				// bItemCellì€ 0ì´ falseì„ì„ ë‚˜íƒ€ë‚´ê¸° ìœ„í•´ + 1 í•´ì„œ ì²˜ë¦¬í•œë‹¤.
				// ë”°ë¼ì„œ iExceptionCellì— 1ì„ ë”í•´ ë¹„êµí•œë‹¤.
				++iExceptionCell;

				if (Cell.IsBeltInventoryPosition())
				{
					LPITEM beltItem = GetWear(WEAR_BELT);

					if (NULL == beltItem)
						return false;

					if (false == CBeltInventoryHelper::IsAvailableCell(bCell - BELT_INVENTORY_SLOT_START, beltItem->GetValue(0)))
						return false;

					if (m_pointsInstant.bItemGrid[bCell])
					{
						if (m_pointsInstant.bItemGrid[bCell] == iExceptionCell)
							return true;

						return false;
					}

					if (bSize == 1)
						return true;

				}
				else if (bCell >= INVENTORY_MAX_NUM)
					return false;

				if (m_pointsInstant.bItemGrid[bCell])
				{
					if (m_pointsInstant.bItemGrid[bCell] == iExceptionCell)
					{
						if (bSize == 1)
							return true;

						int j = 1;
						BYTE bPage = bCell / (INVENTORY_MAX_NUM / 2);

						do
						{
							BYTE p = bCell + (5 * j);

							if (p >= INVENTORY_MAX_NUM)
								return false;

							if (p / (INVENTORY_MAX_NUM / 2) != bPage)
								return false;

							if (m_pointsInstant.bItemGrid[p])
								if (m_pointsInstant.bItemGrid[p] != iExceptionCell)
									return false;
						}
						while (++j < bSize);

						return true;
					}
					else
						return false;
				}

				// í¬ê¸°ê°€ 1ì´ë©´ í•œì¹¸ì„ ì°¨ì§€í•˜ëŠ” ê²ƒì´ë¯€ë¡œ ê·¸ëƒ¥ ë¦¬í„´
				if (1 == bSize)
					return true;
				else
				{
					int j = 1;
					BYTE bPage = bCell / (INVENTORY_MAX_NUM / 2);

					do
					{
						BYTE p = bCell + (5 * j);

						if (p >= INVENTORY_MAX_NUM)
							return false;

						if (p / (INVENTORY_MAX_NUM / 2) != bPage)
							return false;

						if (m_pointsInstant.bItemGrid[p])
							if (m_pointsInstant.bItemGrid[p] != iExceptionCell)
								return false;
					}
					while (++j < bSize);

					return true;
				}
			}
			break;
		case DRAGON_SOUL_INVENTORY:
			{
				WORD wCell = Cell.cell;
				if (wCell >= DRAGON_SOUL_INVENTORY_MAX_NUM)
					return false;

				// bItemCellì€ 0ì´ falseì„ì„ ë‚˜íƒ€ë‚´ê¸° ìœ„í•´ + 1 í•´ì„œ ì²˜ë¦¬í•œë‹¤.
				// ë”°ë¼ì„œ iExceptionCellì— 1ì„ ë”í•´ ë¹„êµí•œë‹¤.
				iExceptionCell++;

				if (m_pointsInstant.wDSItemGrid[wCell])
				{
					if (m_pointsInstant.wDSItemGrid[wCell] == iExceptionCell)
					{
						if (bSize == 1)
							return true;

						int j = 1;

						do
						{
							WORD p = wCell + (DRAGON_SOUL_BOX_COLUMN_NUM * j);

							if (p >= DRAGON_SOUL_INVENTORY_MAX_NUM)
								return false;

							if (m_pointsInstant.wDSItemGrid[p])
								if (m_pointsInstant.wDSItemGrid[p] != iExceptionCell)
									return false;
						}
						while (++j < bSize);

						return true;
					}
					else
						return false;
				}

				// í¬ê¸°ê°€ 1ì´ë©´ í•œì¹¸ì„ ì°¨ì§€í•˜ëŠ” ê²ƒì´ë¯€ë¡œ ê·¸ëƒ¥ ë¦¬í„´
				if (1 == bSize)
					return true;
				else
				{
					int j = 1;

					do
					{
						WORD p = wCell + (DRAGON_SOUL_BOX_COLUMN_NUM * j);

						if (p >= DRAGON_SOUL_INVENTORY_MAX_NUM)
							return false;

						if (m_pointsInstant.bItemGrid[p])
							if (m_pointsInstant.wDSItemGrid[p] != iExceptionCell)
								return false;
					}
					while (++j < bSize);

					return true;
				}
			}
			break;
		default:
			return false;
	}
}

int CHARACTER::GetEmptyInventory(BYTE size) const
{
	// NOTE: í˜„ì¬ ì´ í•¨ìˆ˜ëŠ” ì•„ì´í…œ ì§€ê¸‰, íšë“ ë“±ì˜ í–‰ìœ„ë¥¼ í•  ë•Œ ì¸ë²¤í† ë¦¬ì˜ ë¹ˆ ì¹¸ì„ ì°¾ê¸° ìœ„í•´ ì‚¬ìš©ë˜ê³  ìˆëŠ”ë°,
	//		ë²¨íŠ¸ ì¸ë²¤í† ë¦¬ëŠ” íŠ¹ìˆ˜ ì¸ë²¤í† ë¦¬ì´ë¯€ë¡œ ê²€ì‚¬í•˜ì§€ ì•Šë„ë¡ í•œë‹¤. (ê¸°ë³¸ ì¸ë²¤í† ë¦¬: INVENTORY_MAX_NUM ê¹Œì§€ë§Œ ê²€ì‚¬)
	for ( int i = 0; i < INVENTORY_MAX_NUM; ++i)
		if (IsEmptyItemGrid(TItemPos (INVENTORY, i), size))
			return i;
	return -1;
}

int CHARACTER::GetEmptyDragonSoulInventory(LPITEM pItem) const
{
	if (NULL == pItem || !pItem->IsDragonSoul())
		return -1;
	if (!DragonSoul_IsQualified())
	{
		return -1;
	}
	BYTE bSize = pItem->GetSize();
	WORD wBaseCell = DSManager::instance().GetBasePosition(pItem);

	if (WORD_MAX == wBaseCell)
		return -1;

	for (int i = 0; i < DRAGON_SOUL_BOX_SIZE; ++i)
		if (IsEmptyItemGrid(TItemPos(DRAGON_SOUL_INVENTORY, i + wBaseCell), bSize))
			return i + wBaseCell;

	return -1;
}

void CHARACTER::CopyDragonSoulItemGrid(std::vector<WORD>& vDragonSoulItemGrid) const
{
	vDragonSoulItemGrid.resize(DRAGON_SOUL_INVENTORY_MAX_NUM);

	std::copy(m_pointsInstant.wDSItemGrid, m_pointsInstant.wDSItemGrid + DRAGON_SOUL_INVENTORY_MAX_NUM, vDragonSoulItemGrid.begin());
}

int CHARACTER::CountEmptyInventory() const
{
	int	count = 0;

	for (int i = 0; i < INVENTORY_MAX_NUM; ++i)
		if (GetInventoryItem(i))
			count += GetInventoryItem(i)->GetSize();

	return (INVENTORY_MAX_NUM - count);
}

void TransformRefineItem(LPITEM pkOldItem, LPITEM pkNewItem)
{
	// ACCESSORY_REFINE
	if (pkOldItem->IsAccessoryForSocket())
	{
		for (int i = 0; i < ITEM_SOCKET_MAX_NUM; ++i)
		{
			pkNewItem->SetSocket(i, pkOldItem->GetSocket(i));
		}
		//pkNewItem->StartAccessorySocketExpireEvent();
	}
	// END_OF_ACCESSORY_REFINE
	else
	{
		// ì—¬ê¸°ì„œ ê¹¨ì§„ì„ì´ ìë™ì ìœ¼ë¡œ ì²­ì†Œ ë¨
		for (int i = 0; i < ITEM_SOCKET_MAX_NUM; ++i)
		{
			if (!pkOldItem->GetSocket(i))
				break;
			else
				pkNewItem->SetSocket(i, 1);
		}

		// ì†Œì¼“ ì„¤ì •
		int slot = 0;

		for (int i = 0; i < ITEM_SOCKET_MAX_NUM; ++i)
		{
			long socket = pkOldItem->GetSocket(i);

			if (socket > 2 && socket != ITEM_BROKEN_METIN_VNUM)
				pkNewItem->SetSocket(slot++, socket);
		}

	}

	// ë§¤ì§ ì•„ì´í…œ ì„¤ì •
	pkOldItem->CopyAttributeTo(pkNewItem);
}

void NotifyRefineSuccess(LPCHARACTER ch, LPITEM item, const char* way, int iType)
{
	if (NULL != ch && item != NULL)
	{
		ch->ChatPacket(CHAT_TYPE_COMMAND, "RefineSuceeded %d", iType);

		LogManager::instance().RefineLog(ch->GetPlayerID(), item->GetName(), item->GetID(), item->GetRefineLevel(), 1, way);
	}
}

void NotifyRefineFail(LPCHARACTER ch, LPITEM item, const char* way, int iType, int success = 0)
{
	if (NULL != ch && NULL != item)
	{
		ch->ChatPacket(CHAT_TYPE_COMMAND, "RefineFailed %d", iType);

		LogManager::instance().RefineLog(ch->GetPlayerID(), item->GetName(), item->GetID(), item->GetRefineLevel(), success, way);
	}
}

void CHARACTER::SetRefineNPC(LPCHARACTER ch)
{
	if ( ch != NULL )
	{
		m_dwRefineNPCVID = ch->GetVID();
	}
	else
	{
		m_dwRefineNPCVID = 0;
	}
}

bool CHARACTER::DoRefine(LPITEM item, bool bMoneyOnly, int iType)
{
	if (!CanHandleItem(true))
	{
		ClearRefineMode();
		return false;
	}
	
	//ê°œëŸ‰ ì‹œê°„ì œí•œ : upgrade_refine_scroll.quest ì—ì„œ ê°œëŸ‰í›„ 5ë¶„ì´ë‚´ì— ì¼ë°˜ ê°œëŸ‰ì„ 
	//ì§„í–‰í• ìˆ˜ ì—†ìŒ
	if (quest::CQuestManager::instance().GetEventFlag("update_refine_time") != 0)
	{
		if (get_global_time() < quest::CQuestManager::instance().GetEventFlag("update_refine_time") + (60 * 5))
		{
			sys_log(0, "can't refine %d %s", GetPlayerID(), GetName());
			return false;
		}
	}

	const TRefineTable * prt = CRefineManager::instance().GetRefineRecipe(item->GetRefineSet());

	if (!prt)
		return false;

	DWORD result_vnum = item->GetRefinedVnum();

	// REFINE_COST
	int cost = ComputeRefineFee(prt->cost);

	int RefineChance = GetQuestFlag("main_quest_lv7.refine_chance");

	if (RefineChance > 0)
	{
		if (!item->CheckItemUseLevel(20) || item->GetType() != ITEM_WEAPON)
		{
			ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë¬´ë£Œ ê°œëŸ‰ ê¸°íšŒëŠ” 20 ì´í•˜ì˜ ë¬´ê¸°ë§Œ ê°€ëŠ¥í•©ë‹ˆë‹¤"));
			return false;
		}

		cost = 0;
		SetQuestFlag("main_quest_lv7.refine_chance", RefineChance - 1);
	}
	// END_OF_REFINE_COST

	if (result_vnum == 0)
	{
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë” ì´ìƒ ê°œëŸ‰í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		return false;
	}

	if (item->GetType() == ITEM_USE && item->GetSubType() == USE_TUNING)
		return false;

	TItemTable * pProto = ITEM_MANAGER::instance().GetTable(item->GetRefinedVnum());

	if (!pProto)
	{
		sys_err("DoRefine NOT GET ITEM PROTO %d", item->GetRefinedVnum());
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ ì•„ì´í…œì€ ê°œëŸ‰í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		return false;
	}

	// Check level limit in korea only
	if (!g_iUseLocale)
	{
		for (int i = 0; i < ITEM_LIMIT_MAX_NUM; ++i)
		{
			long limit = pProto->aLimits[i].lValue;

			switch (pProto->aLimits[i].bType)
			{
				case LIMIT_LEVEL:
					if (GetLevel() < limit)
					{
						ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê°œëŸ‰ëœ í›„ ì•„ì´í…œì˜ ë ˆë²¨ ì œí•œë³´ë‹¤ ë ˆë²¨ì´ ë‚®ìŠµë‹ˆë‹¤."));
						return false;
					}
					break;
			}
		}
	}

	// REFINE_COST
	if (GetGold() < cost)
	{
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê°œëŸ‰ì„ í•˜ê¸° ìœ„í•œ ëˆì´ ë¶€ì¡±í•©ë‹ˆë‹¤."));
		return false;
	}

	if (!bMoneyOnly && !RefineChance)
	{
		for (int i = 0; i < prt->material_count; ++i)
		{
			if (CountSpecifyItem(prt->materials[i].vnum) < prt->materials[i].count)
			{
				if (test_server)
				{
					ChatPacket(CHAT_TYPE_INFO, "Find %d, count %d, require %d", prt->materials[i].vnum, CountSpecifyItem(prt->materials[i].vnum), prt->materials[i].count);
				}
				ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê°œëŸ‰ì„ í•˜ê¸° ìœ„í•œ ì¬ë£Œê°€ ë¶€ì¡±í•©ë‹ˆë‹¤."));
				return false;
			}
		}

		for (int i = 0; i < prt->material_count; ++i)
			RemoveSpecifyItem(prt->materials[i].vnum, prt->materials[i].count);
	}

	int prob = number(1, 100);

	if (IsRefineThroughGuild() || bMoneyOnly)
		prob -= 10;

	// END_OF_REFINE_COST

	if (prob <= prt->prob)
	{
		// ì„±ê³µ! ëª¨ë“  ì•„ì´í…œì´ ì‚¬ë¼ì§€ê³ , ê°™ì€ ì†ì„±ì˜ ë‹¤ë¥¸ ì•„ì´í…œ íšë“
		LPITEM pkNewItem = ITEM_MANAGER::instance().CreateItem(result_vnum, 1, 0, false);

		if (pkNewItem)
		{
			ITEM_MANAGER::CopyAllAttrTo(item, pkNewItem);
			LogManager::instance().ItemLog(this, pkNewItem, "REFINE SUCCESS", pkNewItem->GetName());

			BYTE bCell = item->GetCell();

			// DETAIL_REFINE_LOG
			NotifyRefineSuccess(this, item, IsRefineThroughGuild() ? "GUILD" : "POWER", iType);
			DBManager::instance().SendMoneyLog(MONEY_LOG_REFINE, item->GetVnum(), -cost);
			ITEM_MANAGER::instance().RemoveItem(item, "REMOVE (REFINE SUCCESS)");
			// END_OF_DETAIL_REFINE_LOG

			pkNewItem->AddToCharacter(this, TItemPos(INVENTORY, bCell)); 
			ITEM_MANAGER::instance().FlushDelayedSave(pkNewItem);

			sys_log(0, "Refine Success %d", cost);
			pkNewItem->AttrLog();
			//PointChange(POINT_GOLD, -cost);
			sys_log(0, "PayPee %d", cost);
			PayRefineFee(cost);
			sys_log(0, "PayPee End %d", cost);
		}
		else
		{
			// DETAIL_REFINE_LOG
			// ì•„ì´í…œ ìƒì„±ì— ì‹¤íŒ¨ -> ê°œëŸ‰ ì‹¤íŒ¨ë¡œ ê°„ì£¼
			sys_err("cannot create item %u", result_vnum);
			NotifyRefineFail(this, item, IsRefineThroughGuild() ? "GUILD" : "POWER", iType);
			// END_OF_DETAIL_REFINE_LOG
		}
	}
	else
	{
		// ì‹¤íŒ¨! ëª¨ë“  ì•„ì´í…œì´ ì‚¬ë¼ì§.
		DBManager::instance().SendMoneyLog(MONEY_LOG_REFINE, item->GetVnum(), -cost);
		NotifyRefineFail(this, item, IsRefineThroughGuild() ? "GUILD" : "POWER", iType);
		item->AttrLog();
		ITEM_MANAGER::instance().RemoveItem(item, "REMOVE (REFINE FAIL)");

		//PointChange(POINT_GOLD, -cost);
		PayRefineFee(cost);
	}

	return true;
}

enum enum_RefineScrolls
{
	CHUKBOK_SCROLL = 0,
	HYUNIRON_CHN   = 1, // ì¤‘êµ­ì—ì„œë§Œ ì‚¬ìš©
	YONGSIN_SCROLL = 2,
	MUSIN_SCROLL   = 3,
	YAGONG_SCROLL  = 4,
	MEMO_SCROLL	   = 5,
	BDRAGON_SCROLL	= 6,
};

bool CHARACTER::DoRefineWithScroll(LPITEM item, int iType)
{
	if (!CanHandleItem(true))
	{
		ClearRefineMode();
		return false;
	}

	ClearRefineMode();

	//ê°œëŸ‰ ì‹œê°„ì œí•œ : upgrade_refine_scroll.quest ì—ì„œ ê°œëŸ‰í›„ 5ë¶„ì´ë‚´ì— ì¼ë°˜ ê°œëŸ‰ì„ 
	//ì§„í–‰í• ìˆ˜ ì—†ìŒ
	if (quest::CQuestManager::instance().GetEventFlag("update_refine_time") != 0)
	{
		if (get_global_time() < quest::CQuestManager::instance().GetEventFlag("update_refine_time") + (60 * 5))
		{
			sys_log(0, "can't refine %d %s", GetPlayerID(), GetName());
			return false;
		}
	}

	const TRefineTable * prt = CRefineManager::instance().GetRefineRecipe(item->GetRefineSet());

	if (!prt)
		return false;

	LPITEM pkItemScroll;

	// ê°œëŸ‰ì„œ ì²´í¬
	if (m_iRefineAdditionalCell < 0)
		return false;

	pkItemScroll = GetInventoryItem(m_iRefineAdditionalCell);

	if (!pkItemScroll)
		return false;

	if (!(pkItemScroll->GetType() == ITEM_USE && pkItemScroll->GetSubType() == USE_TUNING))
		return false;

	if (pkItemScroll->GetVnum() == item->GetVnum())
		return false;

	DWORD result_vnum = item->GetRefinedVnum();
	DWORD result_fail_vnum = item->GetRefineFromVnum();

	if (result_vnum == 0)
	{
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë” ì´ìƒ ê°œëŸ‰í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		return false;
	}

	// MUSIN_SCROLL
	if (pkItemScroll->GetValue(0) == MUSIN_SCROLL)
	{
		if (item->GetRefineLevel() >= 4)
		{
			ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ ê°œëŸ‰ì„œë¡œ ë” ì´ìƒ ê°œëŸ‰í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
			return false;
		}
	}
	// END_OF_MUSIC_SCROLL

	else if (pkItemScroll->GetValue(0) == MEMO_SCROLL)
	{
		if (item->GetRefineLevel() != pkItemScroll->GetValue(1))
		{
			ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ ê°œëŸ‰ì„œë¡œ ê°œëŸ‰í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
			return false;
		}
	}
	else if (pkItemScroll->GetValue(0) == BDRAGON_SCROLL)
	{
		if (item->GetType() != ITEM_METIN || item->GetRefineLevel() != 4)
		{
			ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ ì•„ì´í…œìœ¼ë¡œ ê°œëŸ‰í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
			return false;
		}
	}

	TItemTable * pProto = ITEM_MANAGER::instance().GetTable(item->GetRefinedVnum());

	if (!pProto)
	{
		sys_err("DoRefineWithScroll NOT GET ITEM PROTO %d", item->GetRefinedVnum());
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ ì•„ì´í…œì€ ê°œëŸ‰í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		return false;
	}

	// Check level limit in korea only
	if (!g_iUseLocale)
	{
		for (int i = 0; i < ITEM_LIMIT_MAX_NUM; ++i)
		{
			long limit = pProto->aLimits[i].lValue;

			switch (pProto->aLimits[i].bType)
			{
				case LIMIT_LEVEL:
					if (GetLevel() < limit)
					{
						ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê°œëŸ‰ëœ í›„ ì•„ì´í…œì˜ ë ˆë²¨ ì œí•œë³´ë‹¤ ë ˆë²¨ì´ ë‚®ìŠµë‹ˆë‹¤."));
						return false;
					}
					break;
			}
		}
	}

	if (GetGold() < prt->cost)
	{
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê°œëŸ‰ì„ í•˜ê¸° ìœ„í•œ ëˆì´ ë¶€ì¡±í•©ë‹ˆë‹¤."));
		return false;
	}

	for (int i = 0; i < prt->material_count; ++i)
	{
		if (CountSpecifyItem(prt->materials[i].vnum) < prt->materials[i].count)
		{
			if (test_server)
			{
				ChatPacket(CHAT_TYPE_INFO, "Find %d, count %d, require %d", prt->materials[i].vnum, CountSpecifyItem(prt->materials[i].vnum), prt->materials[i].count);
			}
			ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê°œëŸ‰ì„ í•˜ê¸° ìœ„í•œ ì¬ë£Œê°€ ë¶€ì¡±í•©ë‹ˆë‹¤."));
			return false;
		}
	}

	for (int i = 0; i < prt->material_count; ++i)
		RemoveSpecifyItem(prt->materials[i].vnum, prt->materials[i].count);

	int prob = number(1, 100);
	int success_prob = prt->prob;
	bool bDestroyWhenFail = false;

	const char* szRefineType = "SCROLL";

	if (pkItemScroll->GetValue(0) == HYUNIRON_CHN || 
		pkItemScroll->GetValue(0) == YONGSIN_SCROLL || 
		pkItemScroll->GetValue(0) == YAGONG_SCROLL) // í˜„ì² , ìš©ì‹ ì˜ ì¶•ë³µì„œ, ì•¼ê³µì˜ ë¹„ì „ì„œ  ì²˜ë¦¬
	{
		const char hyuniron_prob[9] = { 100, 75, 65, 55, 45, 40, 35, 25, 20 };
		const char hyuniron_prob_euckr[9] = { 100, 75, 65, 55, 45, 40, 35, 30, 25 };

		const char yagong_prob[9] = { 100, 100, 90, 80, 70, 60, 50, 30, 20 };
		const char yagong_prob_euckr[9] = { 100, 100, 90, 80, 70, 60, 50, 40, 30 };

		if (pkItemScroll->GetValue(0) == YONGSIN_SCROLL)
		{
			if (LC_IsYMIR() == true || LC_IsKorea() == true)
				success_prob = hyuniron_prob_euckr[MINMAX(0, item->GetRefineLevel(), 8)];
			else
				success_prob = hyuniron_prob[MINMAX(0, item->GetRefineLevel(), 8)];
		}
		else if (pkItemScroll->GetValue(0) == YAGONG_SCROLL)
		{
			if (LC_IsYMIR() == true || LC_IsKorea() == true)
				success_prob = yagong_prob_euckr[MINMAX(0, item->GetRefineLevel(), 8)];
			else
				success_prob = yagong_prob[MINMAX(0, item->GetRefineLevel(), 8)];
		}
		else
		{
			sys_err("REFINE : Unknown refine scroll item. Value0: %d", pkItemScroll->GetValue(0));
		}

		if (test_server) 
		{
			ChatPacket(CHAT_TYPE_INFO, "[Only Test] Success_Prob %d, RefineLevel %d ", success_prob, item->GetRefineLevel());
		}
		if (pkItemScroll->GetValue(0) == HYUNIRON_CHN) // í˜„ì² ì€ ì•„ì´í…œì´ ë¶€ì„œì ¸ì•¼ í•œë‹¤.
			bDestroyWhenFail = true;

		// DETAIL_REFINE_LOG
		if (pkItemScroll->GetValue(0) == HYUNIRON_CHN)
		{
			szRefineType = "HYUNIRON";
		}
		else if (pkItemScroll->GetValue(0) == YONGSIN_SCROLL)
		{
			szRefineType = "GOD_SCROLL";
		}
		else if (pkItemScroll->GetValue(0) == YAGONG_SCROLL)
		{
			szRefineType = "YAGONG_SCROLL";
		}
		// END_OF_DETAIL_REFINE_LOG
	}

	// DETAIL_REFINE_LOG
	if (pkItemScroll->GetValue(0) == MUSIN_SCROLL) // ë¬´ì‹ ì˜ ì¶•ë³µì„œëŠ” 100% ì„±ê³µ (+4ê¹Œì§€ë§Œ)
	{
		success_prob = 100;

		szRefineType = "MUSIN_SCROLL";
	}
	// END_OF_DETAIL_REFINE_LOG
	else if (pkItemScroll->GetValue(0) == MEMO_SCROLL)
	{
		success_prob = 100;
		szRefineType = "MEMO_SCROLL";
	}
	else if (pkItemScroll->GetValue(0) == BDRAGON_SCROLL)
	{
		success_prob = 80;
		szRefineType = "BDRAGON_SCROLL";
	}

	pkItemScroll->SetCount(pkItemScroll->GetCount() - 1);

	if (prob <= success_prob)
	{
		// ì„±ê³µ! ëª¨ë“  ì•„ì´í…œì´ ì‚¬ë¼ì§€ê³ , ê°™ì€ ì†ì„±ì˜ ë‹¤ë¥¸ ì•„ì´í…œ íšë“
		LPITEM pkNewItem = ITEM_MANAGER::instance().CreateItem(result_vnum, 1, 0, false);

		if (pkNewItem)
		{
			ITEM_MANAGER::CopyAllAttrTo(item, pkNewItem);
			LogManager::instance().ItemLog(this, pkNewItem, "REFINE SUCCESS", pkNewItem->GetName());

			BYTE bCell = item->GetCell();

			// MR-15: Include refine method type in upgrade result
			NotifyRefineSuccess(this, item, szRefineType, iType);
			// MR-15: -- END OF -- Include refine method type in upgrade result

			DBManager::instance().SendMoneyLog(MONEY_LOG_REFINE, item->GetVnum(), -prt->cost);
			ITEM_MANAGER::instance().RemoveItem(item, "REMOVE (REFINE SUCCESS)");

			pkNewItem->AddToCharacter(this, TItemPos(INVENTORY, bCell)); 
			ITEM_MANAGER::instance().FlushDelayedSave(pkNewItem);
			pkNewItem->AttrLog();
			//PointChange(POINT_GOLD, -prt->cost);
			PayRefineFee(prt->cost);
		}
		else
		{
			// ì•„ì´í…œ ìƒì„±ì— ì‹¤íŒ¨ -> ê°œëŸ‰ ì‹¤íŒ¨ë¡œ ê°„ì£¼
			sys_err("cannot create item %u", result_vnum);
			// MR-15: Include refine method type in upgrade result
			NotifyRefineFail(this, item, szRefineType, iType);
			// MR-15: -- END OF -- Include refine method type in upgrade result
		}
	}
	else if (!bDestroyWhenFail && result_fail_vnum)
	{
		// ì‹¤íŒ¨! ëª¨ë“  ì•„ì´í…œì´ ì‚¬ë¼ì§€ê³ , ê°™ì€ ì†ì„±ì˜ ë‚®ì€ ë“±ê¸‰ì˜ ì•„ì´í…œ íšë“
		LPITEM pkNewItem = ITEM_MANAGER::instance().CreateItem(result_fail_vnum, 1, 0, false);

		if (pkNewItem)
		{
			ITEM_MANAGER::CopyAllAttrTo(item, pkNewItem);
			LogManager::instance().ItemLog(this, pkNewItem, "REFINE FAIL", pkNewItem->GetName());

			BYTE bCell = item->GetCell();

			DBManager::instance().SendMoneyLog(MONEY_LOG_REFINE, item->GetVnum(), -prt->cost);
			// MR-15: Include refine method type in upgrade result
			NotifyRefineFail(this, item, szRefineType, iType, -1);
			// MR-15: -- END OF -- Include refine method type in upgrade result
			ITEM_MANAGER::instance().RemoveItem(item, "REMOVE (REFINE FAIL)");

			pkNewItem->AddToCharacter(this, TItemPos(INVENTORY, bCell)); 
			ITEM_MANAGER::instance().FlushDelayedSave(pkNewItem);

			pkNewItem->AttrLog();

			//PointChange(POINT_GOLD, -prt->cost);
			PayRefineFee(prt->cost);
		}
		else
		{
			// ì•„ì´í…œ ìƒì„±ì— ì‹¤íŒ¨ -> ê°œëŸ‰ ì‹¤íŒ¨ë¡œ ê°„ì£¼
			sys_err("cannot create item %u", result_fail_vnum);
			// MR-15: Include refine method type in upgrade result
			NotifyRefineFail(this, item, szRefineType, iType);
			// MR-15: -- END OF -- Include refine method type in upgrade result
		}
	}
	else
	{
		// MR-15: Include refine method type in upgrade result
		NotifyRefineFail(this, item, szRefineType, iType); // ê°œëŸ‰ì‹œ ì•„ì´í…œ ì‚¬ë¼ì§€ì§€ ì•ŠìŒ
		// MR-15: -- END OF -- Include refine method type in upgrade result

		PayRefineFee(prt->cost);
	}

	return true;
}

bool CHARACTER::RefineInformation(BYTE bCell, BYTE bType, int iAdditionalCell)
{
	if (bCell > INVENTORY_MAX_NUM)
		return false;

	LPITEM item = GetInventoryItem(bCell);

	if (!item)
		return false;

	// REFINE_COST
	if (bType == REFINE_TYPE_MONEY_ONLY && !GetQuestFlag("deviltower_zone.can_refine"))
	{
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì‚¬ê·€ íƒ€ì›Œ ì™„ë£Œ ë³´ìƒì€ í•œë²ˆê¹Œì§€ ì‚¬ìš©ê°€ëŠ¥í•©ë‹ˆë‹¤."));
		return false;
	}
	// END_OF_REFINE_COST

	TPacketGCRefineInformation p;

	// MR-15: Fix refining by item
	p.header = GC::REFINE_INFORMATION_NEW;
	// MR-15: -- END OF -- Fix refining by item
	p.length = sizeof(p);
	p.pos = bCell;
	p.src_vnum = item->GetVnum();
	p.result_vnum = item->GetRefinedVnum();
	p.type = bType;

	if (p.result_vnum == 0)
	{
		sys_err("RefineInformation p.result_vnum == 0");
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ ì•„ì´í…œì€ ê°œëŸ‰í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		return false;
	}

	if (item->GetType() == ITEM_USE && item->GetSubType() == USE_TUNING)
	{
		if (bType == 0)
		{
			ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ ì•„ì´í…œì€ ì´ ë°©ì‹ìœ¼ë¡œëŠ” ê°œëŸ‰í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
			return false;
		}
		else
		{
			LPITEM itemScroll = GetInventoryItem(iAdditionalCell);
			if (!itemScroll || item->GetVnum() == itemScroll->GetVnum())
			{
				ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê°™ì€ ê°œëŸ‰ì„œë¥¼ í•©ì¹  ìˆ˜ëŠ” ì—†ìŠµë‹ˆë‹¤."));
				ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì¶•ë³µì˜ ì„œì™€ í˜„ì² ì„ í•©ì¹  ìˆ˜ ìˆìŠµë‹ˆë‹¤."));
				return false;
			}
		}
	}

	CRefineManager & rm = CRefineManager::instance();

	const TRefineTable* prt = rm.GetRefineRecipe(item->GetRefineSet());

	if (!prt)
	{
		sys_err("RefineInformation NOT GET REFINE SET %d", item->GetRefineSet());
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ ì•„ì´í…œì€ ê°œëŸ‰í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		return false;
	}

	// REFINE_COST
	
	//MAIN_QUEST_LV7
	if (GetQuestFlag("main_quest_lv7.refine_chance") > 0)
	{
		// ì¼ë³¸ì€ ì œì™¸
		if (!item->CheckItemUseLevel(20) || item->GetType() != ITEM_WEAPON)
		{
			ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë¬´ë£Œ ê°œëŸ‰ ê¸°íšŒëŠ” 20 ì´í•˜ì˜ ë¬´ê¸°ë§Œ ê°€ëŠ¥í•©ë‹ˆë‹¤"));
			return false;
		}
		p.cost = 0;
	}
	else
		p.cost = ComputeRefineFee(prt->cost);
	
	//END_MAIN_QUEST_LV7
	p.prob = prt->prob;
	if (bType == REFINE_TYPE_MONEY_ONLY)
	{
		p.material_count = 0;
		memset(p.materials, 0, sizeof(p.materials));
	}
	else
	{
		p.material_count = prt->material_count;
		thecore_memcpy(&p.materials, prt->materials, sizeof(prt->materials));
	}
	// END_OF_REFINE_COST

	SafeSendPacket(&p, sizeof(TPacketGCRefineInformation));

	SetRefineMode(iAdditionalCell);
	return true;
}

bool CHARACTER::RefineItem(LPITEM pkItem, LPITEM pkTarget)
{
	if (!CanHandleItem())
		return false;

	if (pkItem->GetSubType() == USE_TUNING)
	{
		// XXX ì„±ëŠ¥, ì†Œì¼“ ê°œëŸ‰ì„œëŠ” ì‚¬ë¼ì¡ŒìŠµë‹ˆë‹¤...
		// XXX ì„±ëŠ¥ê°œëŸ‰ì„œëŠ” ì¶•ë³µì˜ ì„œê°€ ë˜ì—ˆë‹¤!
		// MUSIN_SCROLL
		if (pkItem->GetValue(0) == MUSIN_SCROLL)
			RefineInformation(pkTarget->GetCell(), REFINE_TYPE_MUSIN, pkItem->GetCell());
		// END_OF_MUSIN_SCROLL
		else if (pkItem->GetValue(0) == HYUNIRON_CHN)
			RefineInformation(pkTarget->GetCell(), REFINE_TYPE_HYUNIRON, pkItem->GetCell());
		else if (pkItem->GetValue(0) == BDRAGON_SCROLL)
		{
			if (pkTarget->GetRefineSet() != 702) return false;
			RefineInformation(pkTarget->GetCell(), REFINE_TYPE_BDRAGON, pkItem->GetCell());
		}
		else
		{
			if (pkTarget->GetRefineSet() == 501) return false;
			RefineInformation(pkTarget->GetCell(), REFINE_TYPE_SCROLL, pkItem->GetCell());
		}
	}
	else if (pkItem->GetSubType() == USE_DETACHMENT && IS_SET(pkTarget->GetFlag(), ITEM_FLAG_REFINEABLE))
	{
		LogManager::instance().ItemLog(this, pkTarget, "USE_DETACHMENT", pkTarget->GetName());

		bool bHasMetinStone = false;

		for (int i = 0; i < ITEM_SOCKET_MAX_NUM; i++)
		{
			long socket = pkTarget->GetSocket(i);
			if (socket > 2 && socket != ITEM_BROKEN_METIN_VNUM)
			{
				bHasMetinStone = true;
				break;
			}
		}

		if (bHasMetinStone)
		{
			for (int i = 0; i < ITEM_SOCKET_MAX_NUM; ++i)
			{
				long socket = pkTarget->GetSocket(i);
				if (socket > 2 && socket != ITEM_BROKEN_METIN_VNUM)
				{
					AutoGiveItem(socket);
					//TItemTable* pTable = ITEM_MANAGER::instance().GetTable(pkTarget->GetSocket(i));
					//pkTarget->SetSocket(i, pTable->alValues[2]);
					// ê¹¨ì§„ëŒë¡œ ëŒ€ì²´í•´ì¤€ë‹¤
					pkTarget->SetSocket(i, ITEM_BROKEN_METIN_VNUM);
				}
			}
			pkItem->SetCount(pkItem->GetCount() - 1);
			return true;
		}
		else
		{
			ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë¹¼ë‚¼ ìˆ˜ ìˆëŠ” ë©”í‹´ì„ì´ ì—†ìŠµë‹ˆë‹¤."));
			return false;
		}
	}

	return false;
}

EVENTFUNC(kill_campfire_event)
{
	char_event_info* info = dynamic_cast<char_event_info*>( event->info );

	if ( info == NULL )
	{
		sys_err( "kill_campfire_event> <Factor> Null pointer" );
		return 0;
	}

	LPCHARACTER	ch = info->ch;

	if (ch == NULL) { // <Factor>
		return 0;
	}
	ch->m_pkMiningEvent = NULL;
	M2_DESTROY_CHARACTER(ch);
	return 0;
}

bool CHARACTER::GiveRecallItem(LPITEM item)
{
	int idx = GetMapIndex();
	int iEmpireByMapIndex = -1;

	if (idx < 20)
		iEmpireByMapIndex = 1;
	else if (idx < 40)
		iEmpireByMapIndex = 2;
	else if (idx < 60)
		iEmpireByMapIndex = 3;
	else if (idx < 10000)
		iEmpireByMapIndex = 0;

	switch (idx)
	{
		case 66:
		case 216:
			iEmpireByMapIndex = -1;
			break;
	}

	if (iEmpireByMapIndex && GetEmpire() != iEmpireByMapIndex)
	{
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê¸°ì–µí•´ ë‘˜ ìˆ˜ ì—†ëŠ” ìœ„ì¹˜ ì…ë‹ˆë‹¤."));
		return false;
	}

	int pos;

	if (item->GetCount() == 1)	// If the item is only one, just set it.
	{
		item->SetSocket(0, GetX());
		item->SetSocket(1, GetY());
	}
	else if ((pos = GetEmptyInventory(item->GetSize())) != -1) // Otherwise, find another inventory slot.
	{
		LPITEM item2 = ITEM_MANAGER::instance().CreateItem(item->GetVnum(), 1);

		if (NULL != item2)
		{
			item2->SetSocket(0, GetX());
			item2->SetSocket(1, GetY());
			item2->AddToCharacter(this, TItemPos(INVENTORY, pos));

			item->SetCount(item->GetCount() - 1);
		}
	}
	else
	{
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì†Œì§€í’ˆì— ë¹ˆ ê³µê°„ì´ ì—†ìŠµë‹ˆë‹¤."));
		return false;
	}

	return true;
}

void CHARACTER::ProcessRecallItem(LPITEM item)
{
	int idx;

	if ((idx = SECTREE_MANAGER::instance().GetMapIndex(item->GetSocket(0), item->GetSocket(1))) == 0)
		return;

	int iEmpireByMapIndex = -1;

	if (idx < 20)
		iEmpireByMapIndex = 1;
	else if (idx < 40)
		iEmpireByMapIndex = 2;
	else if (idx < 60)
		iEmpireByMapIndex = 3;
	else if (idx < 10000)
		iEmpireByMapIndex = 0;

	switch (idx)
	{
		case 66:
		case 216:
			iEmpireByMapIndex = -1;
			break;
		// ì•…ë£¡êµ°ë„ ì¼ë•Œ
		case 301:
		case 302:
		case 303:
		case 304:
			if( GetLevel() < 90 )
			{
				ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì•„ì´í…œì˜ ë ˆë²¨ ì œí•œë³´ë‹¤ ë ˆë²¨ì´ ë‚®ìŠµë‹ˆë‹¤."));
				return;
			}
			else
				break;
	}

	if (iEmpireByMapIndex && GetEmpire() != iEmpireByMapIndex)
	{
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê¸°ì–µëœ ìœ„ì¹˜ê°€ íƒ€ì œêµ­ì— ì†í•´ ìˆì–´ì„œ ê·€í™˜í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		item->SetSocket(0, 0);
		item->SetSocket(1, 0);
	}
	else
	{
		sys_log(1, "Recall: %s %d %d -> %d %d", GetName(), GetX(), GetY(), item->GetSocket(0), item->GetSocket(1));
		WarpSet(item->GetSocket(0), item->GetSocket(1));
		item->SetCount(item->GetCount() - 1);
	}
}

void CHARACTER::__OpenPrivateShop()
{
	unsigned bodyPart = GetPart(PART_MAIN);
	switch (bodyPart)
	{
		case 0:
		case 1:
		case 2:
			ChatPacket(CHAT_TYPE_COMMAND, "OpenPrivateShop");
			break;
		default:
			ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê°‘ì˜·ì„ ë²—ì–´ì•¼ ê°œì¸ ìƒì ì„ ì—´ ìˆ˜ ìˆìŠµë‹ˆë‹¤."));
			break;
	}
}

// MYSHOP_PRICE_LIST
void CHARACTER::SendMyShopPriceListCmd(DWORD dwItemVnum, DWORD dwItemPrice)
{
	char szLine[256];
	snprintf(szLine, sizeof(szLine), "MyShopPriceList %u %u", dwItemVnum, dwItemPrice);
	ChatPacket(CHAT_TYPE_COMMAND, szLine);
	sys_log(0, szLine);
}

//
// DB ìºì‹œë¡œ ë¶€í„° ë°›ì€ ë¦¬ìŠ¤íŠ¸ë¥¼ User ì—ê²Œ ì „ì†¡í•˜ê³  ìƒì ì„ ì—´ë¼ëŠ” ì»¤ë§¨ë“œë¥¼ ë³´ë‚¸ë‹¤.
//
void CHARACTER::UseSilkBotaryReal(const TPacketMyshopPricelistHeader* p)
{
	const TItemPriceInfo* pInfo = (const TItemPriceInfo*)(p + 1);

	if (!p->byCount)
		// ê°€ê²© ë¦¬ìŠ¤íŠ¸ê°€ ì—†ë‹¤. dummy ë°ì´í„°ë¥¼ ë„£ì€ ì»¤ë§¨ë“œë¥¼ ë³´ë‚´ì¤€ë‹¤.
		SendMyShopPriceListCmd(1, 0);
	else {
		for (int idx = 0; idx < p->byCount; idx++)
			SendMyShopPriceListCmd(pInfo[ idx ].dwVnum, pInfo[ idx ].dwPrice);
	}

	__OpenPrivateShop();
}

//
// ì´ë²ˆ ì ‘ì† í›„ ì²˜ìŒ ìƒì ì„ Open í•˜ëŠ” ê²½ìš° ë¦¬ìŠ¤íŠ¸ë¥¼ Load í•˜ê¸° ìœ„í•´ DB ìºì‹œì— ê°€ê²©ì •ë³´ ë¦¬ìŠ¤íŠ¸ ìš”ì²­ íŒ¨í‚·ì„ ë³´ë‚¸ë‹¤.
// ì´í›„ë¶€í„°ëŠ” ë°”ë¡œ ìƒì ì„ ì—´ë¼ëŠ” ì‘ë‹µì„ ë³´ë‚¸ë‹¤.
//
void CHARACTER::UseSilkBotary(void)
{
	if (m_bNoOpenedShop) {
		DWORD dwPlayerID = GetPlayerID();
		db_clientdesc->DBPacket(GD::MYSHOP_PRICELIST_REQ, GetDesc()->GetHandle(), &dwPlayerID, sizeof(DWORD));
		m_bNoOpenedShop = false;
	} else {
		__OpenPrivateShop();
	}
}
// END_OF_MYSHOP_PRICE_LIST


int CalculateConsume(LPCHARACTER ch)
{
	static const int WARP_NEED_LIFE_PERCENT	= 30;
	static const int WARP_MIN_LIFE_PERCENT	= 10;
	// CONSUME_LIFE_WHEN_USE_WARP_ITEM
	int consumeLife = 0;
	{
		// CheckNeedLifeForWarp
		const int curLife		= ch->GetHP();
		const int needPercent	= WARP_NEED_LIFE_PERCENT;
		const int needLife = ch->GetMaxHP() * needPercent / 100;
		if (curLife < needLife)
		{
			ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë‚¨ì€ ìƒëª…ë ¥ ì–‘ì´ ëª¨ìë¼ ì‚¬ìš©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
			return -1;
		}

		consumeLife = needLife;


		// CheckMinLifeForWarp: ë…ì— ì˜í•´ì„œ ì£½ìœ¼ë©´ ì•ˆë˜ë¯€ë¡œ ìƒëª…ë ¥ ìµœì†ŒëŸ‰ëŠ” ë‚¨ê²¨ì¤€ë‹¤
		const int minPercent	= WARP_MIN_LIFE_PERCENT;
		const int minLife	= ch->GetMaxHP() * minPercent / 100;
		if (curLife - needLife < minLife)
			consumeLife = curLife - minLife;

		if (consumeLife < 0)
			consumeLife = 0;
	}
	// END_OF_CONSUME_LIFE_WHEN_USE_WARP_ITEM
	return consumeLife;
}

int CalculateConsumeSP(LPCHARACTER lpChar)
{
	static const int NEED_WARP_SP_PERCENT = 30;

	const int curSP = lpChar->GetSP();
	const int needSP = lpChar->GetMaxSP() * NEED_WARP_SP_PERCENT / 100;

	if (curSP < needSP)
	{
		lpChar->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë‚¨ì€ ì •ì‹ ë ¥ ì–‘ì´ ëª¨ìë¼ ì‚¬ìš©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		return -1;
	}

	return needSP;
}

bool CHARACTER::UseItemEx(LPITEM item, TItemPos DestCell)
{
	int iLimitRealtimeStartFirstUseFlagIndex = -1;
	int iLimitTimerBasedOnWearFlagIndex = -1;

	WORD wDestCell = DestCell.cell;
	BYTE bDestInven = DestCell.window_type;
	for (int i = 0; i < ITEM_LIMIT_MAX_NUM; ++i)
	{
		long limitValue = item->GetProto()->aLimits[i].lValue;

		switch (item->GetProto()->aLimits[i].bType)
		{
			case LIMIT_LEVEL:
				if (GetLevel() < limitValue)
				{
					ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì•„ì´í…œì˜ ë ˆë²¨ ì œí•œë³´ë‹¤ ë ˆë²¨ì´ ë‚®ìŠµë‹ˆë‹¤."));
					return false;
				}
				break;

			case LIMIT_REAL_TIME_START_FIRST_USE:
				iLimitRealtimeStartFirstUseFlagIndex = i;
				break;

			case LIMIT_TIMER_BASED_ON_WEAR:
				iLimitTimerBasedOnWearFlagIndex = i;
				break;
		}
	}

	if (test_server)
	{
		sys_log(0, "USE_ITEM %s, Inven %d, Cell %d, ItemType %d, SubType %d", item->GetName(), bDestInven, wDestCell, item->GetType(), item->GetSubType());
	}

	if ( CArenaManager::instance().IsLimitedItem( GetMapIndex(), item->GetVnum() ) == true )
	{
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ëŒ€ë ¨ ì¤‘ì—ëŠ” ì´ìš©í•  ìˆ˜ ì—†ëŠ” ë¬¼í’ˆì…ë‹ˆë‹¤."));
		return false;
	}

	// ì•„ì´í…œ ìµœì´ˆ ì‚¬ìš© ì´í›„ë¶€í„°ëŠ” ì‚¬ìš©í•˜ì§€ ì•Šì•„ë„ ì‹œê°„ì´ ì°¨ê°ë˜ëŠ” ë°©ì‹ ì²˜ë¦¬. 
	if (-1 != iLimitRealtimeStartFirstUseFlagIndex)
	{
		// í•œ ë²ˆì´ë¼ë„ ì‚¬ìš©í•œ ì•„ì´í…œì¸ì§€ ì—¬ë¶€ëŠ” Socket1ì„ ë³´ê³  íŒë‹¨í•œë‹¤. (Socket1ì— ì‚¬ìš©íšŸìˆ˜ ê¸°ë¡)
		if (0 == item->GetSocket(1))
		{
			// ì‚¬ìš©ê°€ëŠ¥ì‹œê°„ì€ Default ê°’ìœ¼ë¡œ Limit Value ê°’ì„ ì‚¬ìš©í•˜ë˜, Socket0ì— ê°’ì´ ìˆìœ¼ë©´ ê·¸ ê°’ì„ ì‚¬ìš©í•˜ë„ë¡ í•œë‹¤. (ë‹¨ìœ„ëŠ” ì´ˆ)
			long duration = (0 != item->GetSocket(0)) ? item->GetSocket(0) : item->GetProto()->aLimits[iLimitRealtimeStartFirstUseFlagIndex].lValue;

			if (0 == duration)
				duration = 60 * 60 * 24 * 7;

			item->SetSocket(0, time(0) + duration);
			item->StartRealTimeExpireEvent();
		}	

		if (false == item->IsEquipped())
			item->SetSocket(1, item->GetSocket(1) + 1);		
	}

	switch (item->GetType())
	{
		case ITEM_HAIR:
			return ItemProcess_Hair(item, wDestCell);

		case ITEM_POLYMORPH:
			return ItemProcess_Polymorph(item);

		case ITEM_QUEST:
			if (GetArena() != NULL || IsObserverMode() == true)
			{
				if (item->GetVnum() == 50051 || item->GetVnum() == 50052 || item->GetVnum() == 50053)
				{
					ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ëŒ€ë ¨ ì¤‘ì—ëŠ” ì´ìš©í•  ìˆ˜ ì—†ëŠ” ë¬¼í’ˆì…ë‹ˆë‹¤."));
					return false;
				}
			}

			if (!IS_SET(item->GetFlag(), ITEM_FLAG_QUEST_USE | ITEM_FLAG_QUEST_USE_MULTIPLE))
			{
				if (item->GetSIGVnum() == 0)
				{
					quest::CQuestManager::instance().UseItem(GetPlayerID(), item, false);
				}
				else
				{
					quest::CQuestManager::instance().SIGUse(GetPlayerID(), item->GetSIGVnum(), item, false);
				}
			}
			break;

		case ITEM_CAMPFIRE:
			{
				float fx, fy;
				GetDeltaByDegree(GetRotation(), 100.0f, &fx, &fy);

				LPSECTREE tree = SECTREE_MANAGER::instance().Get(GetMapIndex(), (long)(GetX()+fx), (long)(GetY()+fy));

				if (!tree)
				{
					ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ëª¨ë‹¥ë¶ˆì„ í”¼ìš¸ ìˆ˜ ì—†ëŠ” ì§€ì ì…ë‹ˆë‹¤."));
					return false;
				}

				if (tree->IsAttr((long)(GetX()+fx), (long)(GetY()+fy), ATTR_WATER))
				{
					ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë¬¼ ì†ì— ëª¨ë‹¥ë¶ˆì„ í”¼ìš¸ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
					return false;
				}

				LPCHARACTER campfire = CHARACTER_MANAGER::instance().SpawnMob(fishing::CAMPFIRE_MOB, GetMapIndex(), (long)(GetX()+fx), (long)(GetY()+fy), 0, false, number(0, 359));

				char_event_info* info = AllocEventInfo<char_event_info>();

				info->ch = campfire;

				campfire->m_pkMiningEvent = event_create(kill_campfire_event, info, PASSES_PER_SEC(40));

				item->SetCount(item->GetCount() - 1);
			}
			break;

		case ITEM_UNIQUE:
			{
				switch (item->GetSubType())
				{
					case USE_ABILITY_UP:
						{
							switch (item->GetValue(0))
							{
								case APPLY_MOV_SPEED:
									AddAffect(AFFECT_UNIQUE_ABILITY, POINT_MOV_SPEED, item->GetValue(2), AFF_MOV_SPEED_POTION, item->GetValue(1), 0, true, true);
									EffectPacket(SE_DXUP_PURPLE);
									break;

								case APPLY_ATT_SPEED:
									AddAffect(AFFECT_UNIQUE_ABILITY, POINT_ATT_SPEED, item->GetValue(2), AFF_ATT_SPEED_POTION, item->GetValue(1), 0, true, true);
									EffectPacket(SE_SPEEDUP_GREEN);
									break;

								case APPLY_STR:
									AddAffect(AFFECT_UNIQUE_ABILITY, POINT_ST, item->GetValue(2), 0, item->GetValue(1), 0, true, true);
									break;

								case APPLY_DEX:
									AddAffect(AFFECT_UNIQUE_ABILITY, POINT_DX, item->GetValue(2), 0, item->GetValue(1), 0, true, true);
									break;

								case APPLY_CON:
									AddAffect(AFFECT_UNIQUE_ABILITY, POINT_HT, item->GetValue(2), 0, item->GetValue(1), 0, true, true);
									break;

								case APPLY_INT:
									AddAffect(AFFECT_UNIQUE_ABILITY, POINT_IQ, item->GetValue(2), 0, item->GetValue(1), 0, true, true);
									break;

								case APPLY_CAST_SPEED:
									AddAffect(AFFECT_UNIQUE_ABILITY, POINT_CASTING_SPEED, item->GetValue(2), 0, item->GetValue(1), 0, true, true);
									break;

								case APPLY_RESIST_MAGIC:
									AddAffect(AFFECT_UNIQUE_ABILITY, POINT_RESIST_MAGIC, item->GetValue(2), 0, item->GetValue(1), 0, true, true);
									break;

								case APPLY_ATT_GRADE_BONUS:
									AddAffect(AFFECT_UNIQUE_ABILITY, POINT_ATT_GRADE_BONUS, 
											item->GetValue(2), 0, item->GetValue(1), 0, true, true);
									break;

								case APPLY_DEF_GRADE_BONUS:
									AddAffect(AFFECT_UNIQUE_ABILITY, POINT_DEF_GRADE_BONUS,
											item->GetValue(2), 0, item->GetValue(1), 0, true, true);
									break;
							}
						}

						if (GetDungeon())
							GetDungeon()->UsePotion(this);

						if (GetWarMap())
							GetWarMap()->UsePotion(this, item);

						item->SetCount(item->GetCount() - 1);
						break;

					default:
						{
							if (item->GetSubType() == USE_SPECIAL)
							{
								sys_log(0, "ITEM_UNIQUE: USE_SPECIAL %u", item->GetVnum());

								switch (item->GetVnum())
								{
									case 71049: // ë¹„ë‹¨ë³´ë”°ë¦¬
										if (LC_IsYMIR() == true || LC_IsKorea() == true)
										{
											if (IS_BOTARYABLE_ZONE(GetMapIndex()) == true)
											{
												UseSilkBotary();
											}
											else
											{
												ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê°œì¸ ìƒì ì„ ì—´ ìˆ˜ ì—†ëŠ” ì§€ì—­ì…ë‹ˆë‹¤"));
											}
										}
										else
										{
											UseSilkBotary();
										}
										break;
								}
							}
							else
							{
								if (!item->IsEquipped())
									EquipItem(item);
								else
									UnequipItem(item);
							}
						}
						break;
				}
			}
			break;

		case ITEM_COSTUME:
		case ITEM_WEAPON:
		case ITEM_ARMOR:
		case ITEM_ROD:
		case ITEM_RING:		// ì‹ ê·œ ë°˜ì§€ ì•„ì´í…œ
		case ITEM_BELT:		// ì‹ ê·œ ë²¨íŠ¸ ì•„ì´í…œ
			// MINING
		case ITEM_PICK:
			// END_OF_MINING
			if (!item->IsEquipped())
				EquipItem(item);
			else
				UnequipItem(item);
			break;
			// ì°©ìš©í•˜ì§€ ì•Šì€ ìš©í˜¼ì„ì€ ì‚¬ìš©í•  ìˆ˜ ì—†ë‹¤.
			// ì •ìƒì ì¸ í´ë¼ë¼ë©´, ìš©í˜¼ì„ì— ê´€í•˜ì—¬ item use íŒ¨í‚·ì„ ë³´ë‚¼ ìˆ˜ ì—†ë‹¤.
			// ìš©í˜¼ì„ ì°©ìš©ì€ item move íŒ¨í‚·ìœ¼ë¡œ í•œë‹¤.
			// ì°©ìš©í•œ ìš©í˜¼ì„ì€ ì¶”ì¶œí•œë‹¤.
		case ITEM_DS:
			{
				if (!item->IsEquipped())
					return false;
				return DSManager::instance().PullOut(this, NPOS, item);
			break;
			}
		case ITEM_SPECIAL_DS:
			if (!item->IsEquipped())
				EquipItem(item);
			else
				UnequipItem(item);
			break;
		
		case ITEM_FISH:
			{
				if (CArenaManager::instance().IsArenaMap(GetMapIndex()) == true)
				{
					ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ëŒ€ë ¨ ì¤‘ì—ëŠ” ì´ìš©í•  ìˆ˜ ì—†ëŠ” ë¬¼í’ˆì…ë‹ˆë‹¤."));
					return false;
				}

				if (item->GetSubType() == FISH_ALIVE)
					fishing::UseFish(this, item);
			}
			break;

		case ITEM_TREASURE_BOX:
			{
				return false;
				//ChatPacket(CHAT_TYPE_TALKING, LC_TEXT("ì—´ì‡ ë¡œ ì ê²¨ ìˆì–´ì„œ ì—´ë¦¬ì§€ ì•ŠëŠ”ê²ƒ ê°™ë‹¤. ì—´ì‡ ë¥¼ êµ¬í•´ë³´ì."));
			}
			break;

		case ITEM_TREASURE_KEY:
			{
				LPITEM item2;

				if (!GetItem(DestCell) || !(item2 = GetItem(DestCell)))
					return false;

				if (item2->IsExchanging())
					return false;

				if (item2->GetType() != ITEM_TREASURE_BOX)
				{
					ChatPacket(CHAT_TYPE_TALKING, LC_TEXT("ì—´ì‡ ë¡œ ì—¬ëŠ” ë¬¼ê±´ì´ ì•„ë‹Œê²ƒ ê°™ë‹¤."));
					return false;
				}

				if (item->GetValue(0) == item2->GetValue(0))
				{
					//ChatPacket(CHAT_TYPE_TALKING, LC_TEXT("ì—´ì‡ ëŠ” ë§ìœ¼ë‚˜ ì•„ì´í…œ ì£¼ëŠ” ë¶€ë¶„ êµ¬í˜„ì´ ì•ˆë˜ì—ˆìŠµë‹ˆë‹¤."));
					DWORD dwBoxVnum = item2->GetVnum();
					std::vector <DWORD> dwVnums;
					std::vector <DWORD> dwCounts;
					std::vector <LPITEM> item_gets;
					int count = 0;

					if (GiveItemFromSpecialItemGroup(dwBoxVnum, dwVnums, dwCounts, item_gets, count))
					{
						ITEM_MANAGER::instance().RemoveItem(item);
						ITEM_MANAGER::instance().RemoveItem(item2);
						
						for (int i = 0; i < count; i++){
							switch (dwVnums[i])
							{
								case CSpecialItemGroup::GOLD:
									ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ëˆ %d ëƒ¥ì„ íšë“í–ˆìŠµë‹ˆë‹¤."), dwCounts[i]);
									break;
								case CSpecialItemGroup::EXP:
									ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒìì—ì„œ ë¶€í„° ì‹ ë¹„í•œ ë¹›ì´ ë‚˜ì˜µë‹ˆë‹¤."));
									ChatPacket(CHAT_TYPE_INFO, LC_TEXT("%dì˜ ê²½í—˜ì¹˜ë¥¼ íšë“í–ˆìŠµë‹ˆë‹¤."), dwCounts[i]);
									break;
								case CSpecialItemGroup::MOB:
									ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒìì—ì„œ ëª¬ìŠ¤í„°ê°€ ë‚˜íƒ€ë‚¬ìŠµë‹ˆë‹¤!"));
									break;
								case CSpecialItemGroup::SLOW:
									ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒìì—ì„œ ë‚˜ì˜¨ ë¹¨ê°„ ì—°ê¸°ë¥¼ ë“¤ì´ë§ˆì‹œì ì›€ì§ì´ëŠ” ì†ë„ê°€ ëŠë ¤ì¡ŒìŠµë‹ˆë‹¤!"));
									break;
								case CSpecialItemGroup::DRAIN_HP:
									ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒìê°€ ê°‘ìê¸° í­ë°œí•˜ì˜€ìŠµë‹ˆë‹¤! ìƒëª…ë ¥ì´ ê°ì†Œí–ˆìŠµë‹ˆë‹¤."));
									break;
								case CSpecialItemGroup::POISON:
									ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒìì—ì„œ ë‚˜ì˜¨ ë…¹ìƒ‰ ì—°ê¸°ë¥¼ ë“¤ì´ë§ˆì‹œì ë…ì´ ì˜¨ëª¸ìœ¼ë¡œ í¼ì§‘ë‹ˆë‹¤!"));
									break;
								case CSpecialItemGroup::MOB_GROUP:
									ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒìì—ì„œ ëª¬ìŠ¤í„°ê°€ ë‚˜íƒ€ë‚¬ìŠµë‹ˆë‹¤!"));
									break;
								default:
									if (item_gets[i])
									{
										if (dwCounts[i] > 1)
											ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒìì—ì„œ %s ê°€ %d ê°œ ë‚˜ì™”ìŠµë‹ˆë‹¤."), item_gets[i]->GetName(), dwCounts[i]);
										else
											ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒìì—ì„œ %s ê°€ ë‚˜ì™”ìŠµë‹ˆë‹¤."), item_gets[i]->GetName());

									}
							}
						}
					}
					else
					{
						ChatPacket(CHAT_TYPE_TALKING, LC_TEXT("ì—´ì‡ ê°€ ë§ì§€ ì•ŠëŠ” ê²ƒ ê°™ë‹¤."));
						return false;
					}
				}
				else
				{
					ChatPacket(CHAT_TYPE_TALKING, LC_TEXT("ì—´ì‡ ê°€ ë§ì§€ ì•ŠëŠ” ê²ƒ ê°™ë‹¤."));
					return false;
				}
			}
			break;

		case ITEM_GIFTBOX:
			{
				DWORD dwBoxVnum = item->GetVnum();
				std::vector <DWORD> dwVnums;
				std::vector <DWORD> dwCounts;
				std::vector <LPITEM> item_gets;
				int count = 0;

				if (dwBoxVnum == 50033 && LC_IsYMIR()) // ì•Œìˆ˜ì—†ëŠ” ìƒì
				{
					if (GetLevel() < 15)
					{
						ChatPacket(CHAT_TYPE_INFO, LC_TEXT("15ë ˆë²¨ ì´í•˜ì—ì„œëŠ” ì‚¬ìš©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
						return false;
					}
				}

				if( (dwBoxVnum > 51500 && dwBoxVnum < 52000) || (dwBoxVnum >= 50255 && dwBoxVnum <= 50260) )	// ìš©í˜¼ì›ì„ë“¤
				{
					if( !(this->DragonSoul_IsQualified()) )
					{
						ChatPacket(CHAT_TYPE_INFO,LC_TEXT("ë¨¼ì € ìš©í˜¼ì„ í€˜ìŠ¤íŠ¸ë¥¼ ì™„ë£Œí•˜ì…”ì•¼ í•©ë‹ˆë‹¤."));
						return false;
					}
				}

				if (GiveItemFromSpecialItemGroup(dwBoxVnum, dwVnums, dwCounts, item_gets, count))
				{
					item->SetCount(item->GetCount()-1);

					for (int i = 0; i < count; i++){
						switch (dwVnums[i])
						{
						case CSpecialItemGroup::GOLD:
							ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ëˆ %d ëƒ¥ì„ íšë“í–ˆìŠµë‹ˆë‹¤."), dwCounts[i]);
							break;
						case CSpecialItemGroup::EXP:
							ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒìì—ì„œ ë¶€í„° ì‹ ë¹„í•œ ë¹›ì´ ë‚˜ì˜µë‹ˆë‹¤."));
							ChatPacket(CHAT_TYPE_INFO, LC_TEXT("%dì˜ ê²½í—˜ì¹˜ë¥¼ íšë“í–ˆìŠµë‹ˆë‹¤."), dwCounts[i]);
							break;
						case CSpecialItemGroup::MOB:
							ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒìì—ì„œ ëª¬ìŠ¤í„°ê°€ ë‚˜íƒ€ë‚¬ìŠµë‹ˆë‹¤!"));
							break;
						case CSpecialItemGroup::SLOW:
							ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒìì—ì„œ ë‚˜ì˜¨ ë¹¨ê°„ ì—°ê¸°ë¥¼ ë“¤ì´ë§ˆì‹œì ì›€ì§ì´ëŠ” ì†ë„ê°€ ëŠë ¤ì¡ŒìŠµë‹ˆë‹¤!"));
							break;
						case CSpecialItemGroup::DRAIN_HP:
							ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒìê°€ ê°‘ìê¸° í­ë°œí•˜ì˜€ìŠµë‹ˆë‹¤! ìƒëª…ë ¥ì´ ê°ì†Œí–ˆìŠµë‹ˆë‹¤."));
							break;
						case CSpecialItemGroup::POISON:
							ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒìì—ì„œ ë‚˜ì˜¨ ë…¹ìƒ‰ ì—°ê¸°ë¥¼ ë“¤ì´ë§ˆì‹œì ë…ì´ ì˜¨ëª¸ìœ¼ë¡œ í¼ì§‘ë‹ˆë‹¤!"));
							break;
						case CSpecialItemGroup::MOB_GROUP:
							ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒìì—ì„œ ëª¬ìŠ¤í„°ê°€ ë‚˜íƒ€ë‚¬ìŠµë‹ˆë‹¤!"));
							break;
						default:
							if (item_gets[i])
							{
								if (dwCounts[i] > 1)
									ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒìì—ì„œ %s ê°€ %d ê°œ ë‚˜ì™”ìŠµë‹ˆë‹¤."), item_gets[i]->GetName(), dwCounts[i]);
								else
									ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒìì—ì„œ %s ê°€ ë‚˜ì™”ìŠµë‹ˆë‹¤."), item_gets[i]->GetName());
							}
						}
					}
				}
				else
				{
					ChatPacket(CHAT_TYPE_TALKING, LC_TEXT("ì•„ë¬´ê²ƒë„ ì–»ì„ ìˆ˜ ì—†ì—ˆìŠµë‹ˆë‹¤."));
					return false;
				}
			}
			break;

		case ITEM_SKILLFORGET:
			{
				if (!item->GetSocket(0))
				{
					ITEM_MANAGER::instance().RemoveItem(item);
					return false;
				}

				DWORD dwVnum = item->GetSocket(0);

				if (SkillLevelDown(dwVnum))
				{
					ITEM_MANAGER::instance().RemoveItem(item);
					ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìŠ¤í‚¬ ë ˆë²¨ì„ ë‚´ë¦¬ëŠ”ë° ì„±ê³µí•˜ì˜€ìŠµë‹ˆë‹¤."));
				}
				else
					ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìŠ¤í‚¬ ë ˆë²¨ì„ ë‚´ë¦´ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
			}
			break;

		case ITEM_SKILLBOOK:
			{
				if (IsPolymorphed())
				{
					ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë³€ì‹ ì¤‘ì—ëŠ” ì±…ì„ ì½ì„ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
					return false;
				}

				DWORD dwVnum = 0;

				if (item->GetVnum() == 50300)
				{
					dwVnum = item->GetSocket(0);
				}
				else
				{
					// ìƒˆë¡œìš´ ìˆ˜ë ¨ì„œëŠ” value 0 ì— ìŠ¤í‚¬ ë²ˆí˜¸ê°€ ìˆìœ¼ë¯€ë¡œ ê·¸ê²ƒì„ ì‚¬ìš©.
					dwVnum = item->GetValue(0);
				}

				if (0 == dwVnum)
				{
					ITEM_MANAGER::instance().RemoveItem(item);

					return false;
				}

				if (true == LearnSkillByBook(dwVnum))
				{
					ITEM_MANAGER::instance().RemoveItem(item);

					int iReadDelay = number(SKILLBOOK_DELAY_MIN, SKILLBOOK_DELAY_MAX);

					SetSkillNextReadTime(dwVnum, get_global_time() + iReadDelay);
				}
			}
			break;

		case ITEM_USE:
			{
#ifdef ENABLE_NPC_LOCATION_HELPER
				if (item->GetVnum() == 79667) // NPC_LOCATION_HELPER_ACTIVATOR
				{
					if (!CNpcLocationHelperManager::instance().IsActive(this))
						item->SetCount(item->GetCount() - 1);
					CNpcLocationHelperManager::instance().Activate(this);
					return true;
				}
#endif // ENABLE_NPC_LOCATION_HELPER
				if (item->GetVnum() > 50800 && item->GetVnum() <= 50820)
				{
					if (test_server)
						sys_log (0, "ADD addtional effect : vnum(%d) subtype(%d)", item->GetOriginalVnum(), item->GetSubType());

					int affect_type = AFFECT_EXP_BONUS_EURO_FREE;
					int apply_type = aApplyInfo[item->GetValue(0)].bPointType;
					int apply_value = item->GetValue(2);
					int apply_duration = item->GetValue(1);

					switch (item->GetSubType())
					{
						case USE_ABILITY_UP:
							if (FindAffect(affect_type, apply_type))
							{
								ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ë¯¸ íš¨ê³¼ê°€ ê±¸ë ¤ ìˆìŠµë‹ˆë‹¤."));
								return false;
							}

							{
								switch (item->GetValue(0))
								{
									case APPLY_MOV_SPEED:
										AddAffect(affect_type, apply_type, apply_value, AFF_MOV_SPEED_POTION, apply_duration, 0, true, true);
										EffectPacket(SE_DXUP_PURPLE);
										break;

									case APPLY_ATT_SPEED:
										AddAffect(affect_type, apply_type, apply_value, AFF_ATT_SPEED_POTION, apply_duration, 0, true, true);
										EffectPacket(SE_SPEEDUP_GREEN);
										break;

									case APPLY_STR:
									case APPLY_DEX:
									case APPLY_CON:
									case APPLY_INT:
									case APPLY_CAST_SPEED:
									case APPLY_RESIST_MAGIC:
									case APPLY_ATT_GRADE_BONUS:
									case APPLY_DEF_GRADE_BONUS:
										AddAffect(affect_type, apply_type, apply_value, 0, apply_duration, 0, true, true);
										break;
								}
							}

							if (GetDungeon())
								GetDungeon()->UsePotion(this);

							if (GetWarMap())
								GetWarMap()->UsePotion(this, item);

							item->SetCount(item->GetCount() - 1);
							break;

					case USE_AFFECT :
						{
							if (FindAffect(AFFECT_EXP_BONUS_EURO_FREE, aApplyInfo[item->GetValue(1)].bPointType))
							{
								ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ë¯¸ íš¨ê³¼ê°€ ê±¸ë ¤ ìˆìŠµë‹ˆë‹¤."));
							}
							else
							{
								AddAffect(AFFECT_EXP_BONUS_EURO_FREE, aApplyInfo[item->GetValue(1)].bPointType, item->GetValue(2), 0, item->GetValue(3), 0, false, true);
								item->SetCount(item->GetCount() - 1);
							}
						}
						break;

					case USE_POTION_NODELAY:
						{
							if (CArenaManager::instance().IsArenaMap(GetMapIndex()) == true)
							{
								if (quest::CQuestManager::instance().GetEventFlag("arena_potion_limit") > 0)
								{
									ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ëŒ€ë ¨ì¥ì—ì„œ ì‚¬ìš©í•˜ì‹¤ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
									return false;
								}

								switch (item->GetVnum())
								{
									case 70020 :
									case 71018 :
									case 71019 :
									case 71020 :
										if (quest::CQuestManager::instance().GetEventFlag("arena_potion_limit_count") < 10000)
										{
											if (m_nPotionLimit <= 0)
											{
												ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì‚¬ìš© ì œí•œëŸ‰ì„ ì´ˆê³¼í•˜ì˜€ìŠµë‹ˆë‹¤."));
												return false;
											}
										}
										break;

									default :
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ëŒ€ë ¨ì¥ì—ì„œ ì‚¬ìš©í•˜ì‹¤ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
										return false;
										break;
								}
							}

							bool used = false;

							if (item->GetValue(0) != 0) // HP ì ˆëŒ€ê°’ íšŒë³µ
							{
								if (GetHP() < GetMaxHP())
								{
									PointChange(POINT_HP, item->GetValue(0) * (100 + GetPoint(POINT_POTION_BONUS)) / 100);
									EffectPacket(SE_HPUP_RED);
									used = TRUE;
								}
							}

							if (item->GetValue(1) != 0)	// SP ì ˆëŒ€ê°’ íšŒë³µ
							{
								if (GetSP() < GetMaxSP())
								{
									PointChange(POINT_SP, item->GetValue(1) * (100 + GetPoint(POINT_POTION_BONUS)) / 100);
									EffectPacket(SE_SPUP_BLUE);
									used = TRUE;
								}
							}

							if (item->GetValue(3) != 0) // HP % íšŒë³µ
							{
								if (GetHP() < GetMaxHP())
								{
									PointChange(POINT_HP, item->GetValue(3) * GetMaxHP() / 100);
									EffectPacket(SE_HPUP_RED);
									used = TRUE;
								}
							}

							if (item->GetValue(4) != 0) // SP % íšŒë³µ
							{
								if (GetSP() < GetMaxSP())
								{
									PointChange(POINT_SP, item->GetValue(4) * GetMaxSP() / 100);
									EffectPacket(SE_SPUP_BLUE);
									used = TRUE;
								}
							}

							if (used)
							{
								if (item->GetVnum() == 50085 || item->GetVnum() == 50086)
								{
									if (test_server)
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì›”ë³‘ ë˜ëŠ” ì¢…ì ë¥¼ ì‚¬ìš©í•˜ì˜€ìŠµë‹ˆë‹¤"));
									SetUseSeedOrMoonBottleTime();
								}
								if (GetDungeon())
									GetDungeon()->UsePotion(this);

								if (GetWarMap())
									GetWarMap()->UsePotion(this, item);

								m_nPotionLimit--;

								//RESTRICT_USE_SEED_OR_MOONBOTTLE
								item->SetCount(item->GetCount() - 1);
								//END_RESTRICT_USE_SEED_OR_MOONBOTTLE
							}
						}
						break;
					}

					return true;
				}


				if (item->GetVnum() >= 27863 && item->GetVnum() <= 27883)
				{
					if (CArenaManager::instance().IsArenaMap(GetMapIndex()) == true)
					{
						ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ëŒ€ë ¨ ì¤‘ì—ëŠ” ì´ìš©í•  ìˆ˜ ì—†ëŠ” ë¬¼í’ˆì…ë‹ˆë‹¤."));
						return false;
					}
				}

				if (test_server)
				{
					 sys_log (0, "USE_ITEM %s Type %d SubType %d vnum %d", item->GetName(), item->GetType(), item->GetSubType(), item->GetOriginalVnum());
				}

				switch (item->GetSubType())
				{
					case USE_TIME_CHARGE_PER:
						{
							LPITEM pDestItem = GetItem(DestCell);
							if (NULL == pDestItem)
							{
								return false;
							}
							// ìš°ì„  ìš©í˜¼ì„ì— ê´€í•´ì„œë§Œ í•˜ë„ë¡ í•œë‹¤.
							if (pDestItem->IsDragonSoul())
							{
								int ret;
								char buf[128];
								if (item->GetVnum() == DRAGON_HEART_VNUM)
								{
									ret = pDestItem->GiveMoreTime_Per((float)item->GetSocket(ITEM_SOCKET_CHARGING_AMOUNT_IDX));
								}
								else
								{
									ret = pDestItem->GiveMoreTime_Per((float)item->GetValue(ITEM_VALUE_CHARGING_AMOUNT_IDX));
								}
								if (ret > 0)
								{
									if (item->GetVnum() == DRAGON_HEART_VNUM)
									{
										sprintf(buf, "Inc %ds by item{VN:%d SOC%d:%d}", ret, item->GetVnum(), ITEM_SOCKET_CHARGING_AMOUNT_IDX, item->GetSocket(ITEM_SOCKET_CHARGING_AMOUNT_IDX));
									}
									else
									{
										sprintf(buf, "Inc %ds by item{VN:%d VAL%d:%ld}", ret, item->GetVnum(), ITEM_VALUE_CHARGING_AMOUNT_IDX, item->GetValue(ITEM_VALUE_CHARGING_AMOUNT_IDX));
									}

									ChatPacket(CHAT_TYPE_INFO, LC_TEXT("%dì´ˆ ë§Œí¼ ì¶©ì „ë˜ì—ˆìŠµë‹ˆë‹¤."), ret);
									item->SetCount(item->GetCount() - 1);
									LogManager::instance().ItemLog(this, item, "DS_CHARGING_SUCCESS", buf);
									return true;
								}
								else
								{
									if (item->GetVnum() == DRAGON_HEART_VNUM)
									{
										sprintf(buf, "No change by item{VN:%d SOC%d:%d}", item->GetVnum(), ITEM_SOCKET_CHARGING_AMOUNT_IDX, item->GetSocket(ITEM_SOCKET_CHARGING_AMOUNT_IDX));
									}
									else
									{
										sprintf(buf, "No change by item{VN:%d VAL%d:%ld}", item->GetVnum(), ITEM_VALUE_CHARGING_AMOUNT_IDX, item->GetValue(ITEM_VALUE_CHARGING_AMOUNT_IDX));
									}

									ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì¶©ì „í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
									LogManager::instance().ItemLog(this, item, "DS_CHARGING_FAILED", buf);
									return false;
								}
							}
							else
								return false;
						}
						break;
					case USE_TIME_CHARGE_FIX:
						{
							LPITEM pDestItem = GetItem(DestCell);
							if (NULL == pDestItem)
							{
								return false;
							}
							// ìš°ì„  ìš©í˜¼ì„ì— ê´€í•´ì„œë§Œ í•˜ë„ë¡ í•œë‹¤.
							if (pDestItem->IsDragonSoul())
							{
								int ret = pDestItem->GiveMoreTime_Fix(item->GetValue(ITEM_VALUE_CHARGING_AMOUNT_IDX));
								char buf[128];
								if (ret)
								{
									ChatPacket(CHAT_TYPE_INFO, LC_TEXT("%dì´ˆ ë§Œí¼ ì¶©ì „ë˜ì—ˆìŠµë‹ˆë‹¤."), ret);
									sprintf(buf, "Increase %ds by item{VN:%d VAL%d:%ld}", ret, item->GetVnum(), ITEM_VALUE_CHARGING_AMOUNT_IDX, item->GetValue(ITEM_VALUE_CHARGING_AMOUNT_IDX));
									LogManager::instance().ItemLog(this, item, "DS_CHARGING_SUCCESS", buf);
									item->SetCount(item->GetCount() - 1);
									return true;
								}
								else
								{
									ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì¶©ì „í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
									sprintf(buf, "No change by item{VN:%d VAL%d:%ld}", item->GetVnum(), ITEM_VALUE_CHARGING_AMOUNT_IDX, item->GetValue(ITEM_VALUE_CHARGING_AMOUNT_IDX));
									LogManager::instance().ItemLog(this, item, "DS_CHARGING_FAILED", buf);
									return false;
								}
							}
							else
								return false;
						}
						break;
					case USE_SPECIAL:
						
						switch (item->GetVnum())
						{
							//í¬ë¦¬ìŠ¤ë§ˆìŠ¤ ë€ì£¼
							case ITEM_NOG_POCKET:
								{
									/*
									ë€ì£¼ëŠ¥ë ¥ì¹˜ : item_proto value ì˜ë¯¸
										ì´ë™ì†ë„  value 1
										ê³µê²©ë ¥	  value 2
										ê²½í—˜ì¹˜    value 3
										ì§€ì†ì‹œê°„  value 0 (ë‹¨ìœ„ ì´ˆ)

									*/
									if (FindAffect(AFFECT_NOG_ABILITY))
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ë¯¸ íš¨ê³¼ê°€ ê±¸ë ¤ ìˆìŠµë‹ˆë‹¤."));
										return false;
									}
									long time = item->GetValue(0);
									long moveSpeedPer	= item->GetValue(1);
									long attPer	= item->GetValue(2);
									long expPer			= item->GetValue(3);
									AddAffect(AFFECT_NOG_ABILITY, POINT_MOV_SPEED, moveSpeedPer, AFF_MOV_SPEED_POTION, time, 0, true, true);
									EffectPacket(SE_DXUP_PURPLE);
									AddAffect(AFFECT_NOG_ABILITY, POINT_MALL_ATTBONUS, attPer, AFF_NONE, time, 0, true, true);
									AddAffect(AFFECT_NOG_ABILITY, POINT_MALL_EXPBONUS, expPer, AFF_NONE, time, 0, true, true);
									item->SetCount(item->GetCount() - 1);
								}
								break;
								
							//ë¼ë§ˆë‹¨ìš© ì‚¬íƒ•
							case ITEM_RAMADAN_CANDY:
								{
									/*
									ì‚¬íƒ•ëŠ¥ë ¥ì¹˜ : item_proto value ì˜ë¯¸
										ì´ë™ì†ë„  value 1
										ê³µê²©ë ¥	  value 2
										ê²½í—˜ì¹˜    value 3
										ì§€ì†ì‹œê°„  value 0 (ë‹¨ìœ„ ì´ˆ)

									*/
									long time = item->GetValue(0);
									long moveSpeedPer	= item->GetValue(1);
									long attPer	= item->GetValue(2);
									long expPer			= item->GetValue(3);
									AddAffect(AFFECT_RAMADAN_ABILITY, POINT_MOV_SPEED, moveSpeedPer, AFF_MOV_SPEED_POTION, time, 0, true, true);
									AddAffect(AFFECT_RAMADAN_ABILITY, POINT_MALL_ATTBONUS, attPer, AFF_NONE, time, 0, true, true);
									AddAffect(AFFECT_RAMADAN_ABILITY, POINT_MALL_EXPBONUS, expPer, AFF_NONE, time, 0, true, true);
									item->SetCount(item->GetCount() - 1);
								}
								break;
							case ITEM_MARRIAGE_RING:
								{
									marriage::TMarriage* pMarriage = marriage::CManager::instance().Get(GetPlayerID());
									if (pMarriage)
									{
										if (pMarriage->ch1 != NULL)
										{
											if (CArenaManager::instance().IsArenaMap(pMarriage->ch1->GetMapIndex()) == true)
											{
												ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ëŒ€ë ¨ ì¤‘ì—ëŠ” ì´ìš©í•  ìˆ˜ ì—†ëŠ” ë¬¼í’ˆì…ë‹ˆë‹¤."));
												break;
											}
										}

										if (pMarriage->ch2 != NULL)
										{
											if (CArenaManager::instance().IsArenaMap(pMarriage->ch2->GetMapIndex()) == true)
											{
												ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ëŒ€ë ¨ ì¤‘ì—ëŠ” ì´ìš©í•  ìˆ˜ ì—†ëŠ” ë¬¼í’ˆì…ë‹ˆë‹¤."));
												break;
											}
										}

										int consumeSP = CalculateConsumeSP(this);

										if (consumeSP < 0)
											return false;

										PointChange(POINT_SP, -consumeSP, false);

										WarpToPID(pMarriage->GetOther(GetPlayerID()));
									}
									else
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê²°í˜¼ ìƒíƒœê°€ ì•„ë‹ˆë©´ ê²°í˜¼ë°˜ì§€ë¥¼ ì‚¬ìš©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
								}
								break;

								//ê¸°ì¡´ ìš©ê¸°ì˜ ë§í† 
							case UNIQUE_ITEM_CAPE_OF_COURAGE:
								//ë¼ë§ˆë‹¨ ë³´ìƒìš© ìš©ê¸°ì˜ ë§í† 
							case 70057:
							case REWARD_BOX_UNIQUE_ITEM_CAPE_OF_COURAGE:
								AggregateMonster();
								item->SetCount(item->GetCount()-1);
								break;

							case UNIQUE_ITEM_WHITE_FLAG:
								ForgetMyAttacker();
								item->SetCount(item->GetCount()-1);
								break;

							case UNIQUE_ITEM_TREASURE_BOX:
								break;

							case 30093:
							case 30094:
							case 30095:
							case 30096:
								// ë³µì£¼ë¨¸ë‹ˆ
								{
									const int MAX_BAG_INFO = 26;
									static struct LuckyBagInfo
									{
										DWORD count;
										int prob;
										DWORD vnum;
									} b1[MAX_BAG_INFO] =
									{
										{ 1000,	302,	1 },
										{ 10,	150,	27002 },
										{ 10,	75,	27003 },
										{ 10,	100,	27005 },
										{ 10,	50,	27006 },
										{ 10,	80,	27001 },
										{ 10,	50,	27002 },
										{ 10,	80,	27004 },
										{ 10,	50,	27005 },
										{ 1,	10,	50300 },
										{ 1,	6,	92 },
										{ 1,	2,	132 },
										{ 1,	6,	1052 },
										{ 1,	2,	1092 },
										{ 1,	6,	2082 },
										{ 1,	2,	2122 },
										{ 1,	6,	3082 },
										{ 1,	2,	3122 },
										{ 1,	6,	5052 },
										{ 1,	2,	5082 },
										{ 1,	6,	7082 },
										{ 1,	2,	7122 },
										{ 1,	1,	11282 },
										{ 1,	1,	11482 },
										{ 1,	1,	11682 },
										{ 1,	1,	11882 },
									};

									struct LuckyBagInfo b2[MAX_BAG_INFO] =
									{
										{ 1000,	302,	1 },
										{ 10,	150,	27002 },
										{ 10,	75,	27002 },
										{ 10,	100,	27005 },
										{ 10,	50,	27005 },
										{ 10,	80,	27001 },
										{ 10,	50,	27002 },
										{ 10,	80,	27004 },
										{ 10,	50,	27005 },
										{ 1,	10,	50300 },
										{ 1,	6,	92 },
										{ 1,	2,	132 },
										{ 1,	6,	1052 },
										{ 1,	2,	1092 },
										{ 1,	6,	2082 },
										{ 1,	2,	2122 },
										{ 1,	6,	3082 },
										{ 1,	2,	3122 },
										{ 1,	6,	5052 },
										{ 1,	2,	5082 },
										{ 1,	6,	7082 },
										{ 1,	2,	7122 },
										{ 1,	1,	11282 },
										{ 1,	1,	11482 },
										{ 1,	1,	11682 },
										{ 1,	1,	11882 },
									};
	
									LuckyBagInfo * bi = NULL;
									if (LC_IsHongKong())
										bi = b2;
									else
										bi = b1;

									int pct = number(1, 1000);

									int i;
									for (i=0;i<MAX_BAG_INFO;i++)
									{
										if (pct <= bi[i].prob)
											break;
										pct -= bi[i].prob;
									}
									if (i>=MAX_BAG_INFO)
										return false;

									if (bi[i].vnum == 50300)
									{
										// ìŠ¤í‚¬ìˆ˜ë ¨ì„œëŠ” íŠ¹ìˆ˜í•˜ê²Œ ì¤€ë‹¤.
										GiveRandomSkillBook();
									}
									else if (bi[i].vnum == 1)
									{
										PointChange(POINT_GOLD, 1000, true);
									}
									else
									{
										AutoGiveItem(bi[i].vnum, bi[i].count);
									}
									ITEM_MANAGER::instance().RemoveItem(item);
								}
								break;

							case 50004: // ì´ë²¤íŠ¸ìš© ê°ì§€ê¸°
								{
									if (item->GetSocket(0))
									{
										item->SetSocket(0, item->GetSocket(0) + 1);
									}
									else
									{
										// ì²˜ìŒ ì‚¬ìš©ì‹œ
										int iMapIndex = GetMapIndex();

										PIXEL_POSITION pos;

										if (SECTREE_MANAGER::instance().GetRandomLocation(iMapIndex, pos, 700))
										{
											item->SetSocket(0, 1);
											item->SetSocket(1, pos.x);
											item->SetSocket(2, pos.y);
										}
										else
										{
											ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ ê³³ì—ì„  ì´ë²¤íŠ¸ìš© ê°ì§€ê¸°ê°€ ë™ì‘í•˜ì§€ ì•ŠëŠ”ê²ƒ ê°™ìŠµë‹ˆë‹¤."));
											return false;
										}
									}

									int dist = 0;
									float distance = (DISTANCE_SQRT(GetX()-item->GetSocket(1), GetY()-item->GetSocket(2)));

									if (distance < 1000.0f)
									{
										// ë°œê²¬!
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ë²¤íŠ¸ìš© ê°ì§€ê¸°ê°€ ì‹ ë¹„ë¡œìš´ ë¹›ì„ ë‚´ë©° ì‚¬ë¼ì§‘ë‹ˆë‹¤."));

										// ì‚¬ìš©íšŸìˆ˜ì— ë”°ë¼ ì£¼ëŠ” ì•„ì´í…œì„ ë‹¤ë¥´ê²Œ í•œë‹¤.
										struct TEventStoneInfo
										{
											DWORD dwVnum;
											int count;
											int prob;
										};
										const int EVENT_STONE_MAX_INFO = 15;
										TEventStoneInfo info_10[EVENT_STONE_MAX_INFO] =
										{ 
											{ 27001, 10,  8 },
											{ 27004, 10,  6 },
											{ 27002, 10, 12 },
											{ 27005, 10, 12 },
											{ 27100,  1,  9 },
											{ 27103,  1,  9 },
											{ 27101,  1, 10 },
											{ 27104,  1, 10 },
											{ 27999,  1, 12 },

											{ 25040,  1,  4 },

											{ 27410,  1,  0 },
											{ 27600,  1,  0 },
											{ 25100,  1,  0 },

											{ 50001,  1,  0 },
											{ 50003,  1,  1 },
										};
										TEventStoneInfo info_7[EVENT_STONE_MAX_INFO] =
										{ 
											{ 27001, 10,  1 },
											{ 27004, 10,  1 },
											{ 27004, 10,  9 },
											{ 27005, 10,  9 },
											{ 27100,  1,  5 },
											{ 27103,  1,  5 },
											{ 27101,  1, 10 },
											{ 27104,  1, 10 },
											{ 27999,  1, 14 },

											{ 25040,  1,  5 },

											{ 27410,  1,  5 },
											{ 27600,  1,  5 },
											{ 25100,  1,  5 },

											{ 50001,  1,  0 },
											{ 50003,  1,  5 },

										};
										TEventStoneInfo info_4[EVENT_STONE_MAX_INFO] =
										{ 
											{ 27001, 10,  0 },
											{ 27004, 10,  0 },
											{ 27002, 10,  0 },
											{ 27005, 10,  0 },
											{ 27100,  1,  0 },
											{ 27103,  1,  0 },
											{ 27101,  1,  0 },
											{ 27104,  1,  0 },
											{ 27999,  1, 25 },

											{ 25040,  1,  0 },

											{ 27410,  1,  0 },
											{ 27600,  1,  0 },
											{ 25100,  1, 15 },

											{ 50001,  1, 10 },
											{ 50003,  1, 50 },

										};

										{
											TEventStoneInfo* info;
											if (item->GetSocket(0) <= 4)
												info = info_4;
											else if (item->GetSocket(0) <= 7)
												info = info_7;
											else
												info = info_10;

											int prob = number(1, 100);

											for (int i = 0; i < EVENT_STONE_MAX_INFO; ++i)
											{
												if (!info[i].prob)
													continue;

												if (prob <= info[i].prob)
												{
													if (info[i].dwVnum == 50001)
													{
														DWORD * pdw = M2_NEW DWORD[2];

														pdw[0] = info[i].dwVnum;
														pdw[1] = info[i].count;

														// ì¶”ì²¨ì„œëŠ” ì†Œì¼“ì„ ì„¤ì •í•œë‹¤
														DBManager::instance().ReturnQuery(QID_LOTTO, GetPlayerID(), pdw,
																"INSERT INTO lotto_list VALUES(0, 'server%s', %u, NOW())", 
																get_table_postfix(), GetPlayerID());
													}
													else
														AutoGiveItem(info[i].dwVnum, info[i].count);

													break;
												}
												prob -= info[i].prob;
											}
										}

										char chatbuf[CHAT_MAX_LEN + 1];
										int len = snprintf(chatbuf, sizeof(chatbuf), "StoneDetect %u 0 0", (DWORD)GetVID());

										if (len < 0 || len >= (int) sizeof(chatbuf))
											len = sizeof(chatbuf) - 1;

										++len;  // \0 ë¬¸ìê¹Œì§€ ë³´ë‚´ê¸°

										TPacketGCChat pack_chat;
										pack_chat.header	= GC::CHAT;
										pack_chat.length		= sizeof(TPacketGCChat) + len;
										pack_chat.type		= CHAT_TYPE_COMMAND;
										pack_chat.id		= 0;
										pack_chat.bEmpire	= GetDesc()->GetEmpire();
										//pack_chat.id	= vid;

										TEMP_BUFFER buf;
										buf.write(&pack_chat, sizeof(TPacketGCChat));
										buf.write(chatbuf, len);

										PacketAround(buf.read_peek(), buf.size());

										ITEM_MANAGER::instance().RemoveItem(item, "REMOVE (DETECT_EVENT_STONE) 1");
										return true;
									}
									else if (distance < 20000)
										dist = 1;
									else if (distance < 70000)
										dist = 2;
									else
										dist = 3;

									// ë§ì´ ì‚¬ìš©í–ˆìœ¼ë©´ ì‚¬ë¼ì§„ë‹¤.
									const int STONE_DETECT_MAX_TRY = 10;
									if (item->GetSocket(0) >= STONE_DETECT_MAX_TRY)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ë²¤íŠ¸ìš© ê°ì§€ê¸°ê°€ í”ì ë„ ì—†ì´ ì‚¬ë¼ì§‘ë‹ˆë‹¤."));
										ITEM_MANAGER::instance().RemoveItem(item, "REMOVE (DETECT_EVENT_STONE) 0");
										AutoGiveItem(27002);
										return true;
									}

									if (dist)
									{
										char chatbuf[CHAT_MAX_LEN + 1];
										int len = snprintf(chatbuf, sizeof(chatbuf),
												"StoneDetect %u %d %d",
											   	(DWORD)GetVID(), dist, (int)GetDegreeFromPositionXY(GetX(), item->GetSocket(2), item->GetSocket(1), GetY()));

										if (len < 0 || len >= (int) sizeof(chatbuf))
											len = sizeof(chatbuf) - 1;

										++len;  // \0 ë¬¸ìê¹Œì§€ ë³´ë‚´ê¸°

										TPacketGCChat pack_chat;
										pack_chat.header	= GC::CHAT;
										pack_chat.length		= sizeof(TPacketGCChat) + len;
										pack_chat.type		= CHAT_TYPE_COMMAND;
										pack_chat.id		= 0;
										pack_chat.bEmpire	= GetDesc()->GetEmpire();
										//pack_chat.id		= vid;

										TEMP_BUFFER buf;
										buf.write(&pack_chat, sizeof(TPacketGCChat));
										buf.write(chatbuf, len);

										PacketAround(buf.read_peek(), buf.size());
									}

								}
								break;

							case 27989: // ì˜ì„ê°ì§€ê¸°
							case 76006: // ì„ ë¬¼ìš© ì˜ì„ê°ì§€ê¸°
								{
									LPSECTREE_MAP pMap = SECTREE_MANAGER::instance().GetMap(GetMapIndex());

									if (pMap != NULL)
									{
										item->SetSocket(0, item->GetSocket(0) + 1);

										FFindStone f;

										// <Factor> SECTREE::for_each -> SECTREE::for_each_entity
										pMap->for_each(f);

										if (f.m_mapStone.size() > 0)
										{
											std::map<DWORD, LPCHARACTER>::iterator stone = f.m_mapStone.begin();

											DWORD max = UINT_MAX;
											LPCHARACTER pTarget = stone->second;

											while (stone != f.m_mapStone.end())
											{
												DWORD dist = (DWORD)DISTANCE_SQRT(GetX()-stone->second->GetX(), GetY()-stone->second->GetY());

												if (dist != 0 && max > dist)
												{
													max = dist;
													pTarget = stone->second;
												}
												stone++;
											}

											if (pTarget != NULL)
											{
												int val = 3;

												if (max < 10000) val = 2;
												else if (max < 70000) val = 1;

												ChatPacket(CHAT_TYPE_COMMAND, "StoneDetect %u %d %d", (DWORD)GetVID(), val,
														(int)GetDegreeFromPositionXY(GetX(), pTarget->GetY(), pTarget->GetX(), GetY()));
											}
											else
											{
												ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê°ì§€ê¸°ë¥¼ ì‘ìš©í•˜ì˜€ìœ¼ë‚˜ ê°ì§€ë˜ëŠ” ì˜ì„ì´ ì—†ìŠµë‹ˆë‹¤."));
											}
										}
										else
										{
											ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê°ì§€ê¸°ë¥¼ ì‘ìš©í•˜ì˜€ìœ¼ë‚˜ ê°ì§€ë˜ëŠ” ì˜ì„ì´ ì—†ìŠµë‹ˆë‹¤."));
										}

										if (item->GetSocket(0) >= 6)
										{
											ChatPacket(CHAT_TYPE_COMMAND, "StoneDetect %u 0 0", (DWORD)GetVID());
											ITEM_MANAGER::instance().RemoveItem(item);
										}
									}
									break;
								}
								break;

							case 27996: // ë…ë³‘
								item->SetCount(item->GetCount() - 1);
								/*if (GetSkillLevel(SKILL_CREATE_POISON))
								  AddAffect(AFFECT_ATT_GRADE, POINT_ATT_GRADE, 3, AFF_DRINK_POISON, 15*60, 0, true);
								  else
								  {
								// ë…ë‹¤ë£¨ê¸°ê°€ ì—†ìœ¼ë©´ 50% ì¦‰ì‚¬ 50% ê³µê²©ë ¥ +2
								if (number(0, 1))
								{
								if (GetHP() > 100)
								PointChange(POINT_HP, -(GetHP() - 1));
								else
								Dead();
								}
								else
								AddAffect(AFFECT_ATT_GRADE, POINT_ATT_GRADE, 2, AFF_DRINK_POISON, 15*60, 0, true);
								}*/
								break;

							case 27987: // ì¡°ê°œ
								// 50  ëŒì¡°ê° 47990
								// 30  ê½
								// 10  ë°±ì§„ì£¼ 47992
								// 7   ì²­ì§„ì£¼ 47993
								// 3   í”¼ì§„ì£¼ 47994
								{
									item->SetCount(item->GetCount() - 1);

									int r = number(1, 100);

									if (r <= 50)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì¡°ê°œì—ì„œ ëŒì¡°ê°ì´ ë‚˜ì™”ìŠµë‹ˆë‹¤."));
										AutoGiveItem(27990);
									}
									else
									{
										const int prob_table_euckr[] =
										{
											80, 90, 97
										};

										const int prob_table_gb2312[] =
										{
											95, 97, 99
										};

										const int * prob_table = !g_iUseLocale ? prob_table_euckr : prob_table_gb2312;

										if (r <= prob_table[0])
										{
											ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì¡°ê°œê°€ í”ì ë„ ì—†ì´ ì‚¬ë¼ì§‘ë‹ˆë‹¤."));
										}
										else if (r <= prob_table[1])
										{
											ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì¡°ê°œì—ì„œ ë°±ì§„ì£¼ê°€ ë‚˜ì™”ìŠµë‹ˆë‹¤."));
											AutoGiveItem(27992);
										}
										else if (r <= prob_table[2])
										{
											ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì¡°ê°œì—ì„œ ì²­ì§„ì£¼ê°€ ë‚˜ì™”ìŠµë‹ˆë‹¤."));
											AutoGiveItem(27993);
										}
										else
										{
											ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì¡°ê°œì—ì„œ í”¼ì§„ì£¼ê°€ ë‚˜ì™”ìŠµë‹ˆë‹¤."));
											AutoGiveItem(27994);
										}
									}
								}
								break;

							case 71013: // ì¶•ì œìš©í­ì£½
								CreateFly(number(FLY_FIREWORK1, FLY_FIREWORK6), this);
								item->SetCount(item->GetCount() - 1);
								break;

							case 50100: // í­ì£½
							case 50101:
							case 50102:
							case 50103:
							case 50104:
							case 50105:
							case 50106:
								CreateFly(item->GetVnum() - 50100 + FLY_FIREWORK1, this);
								item->SetCount(item->GetCount() - 1);
								break;

							case 50200: // ë³´ë”°ë¦¬
								if (LC_IsYMIR() == true || LC_IsKorea() == true)
								{
									if (IS_BOTARYABLE_ZONE(GetMapIndex()) == true)
									{
										__OpenPrivateShop();
									}
									else
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê°œì¸ ìƒì ì„ ì—´ ìˆ˜ ì—†ëŠ” ì§€ì—­ì…ë‹ˆë‹¤"));
									}
								}
								else
								{
									__OpenPrivateShop();
								}
								break;

							case fishing::FISH_MIND_PILL_VNUM:
								AddAffect(AFFECT_FISH_MIND_PILL, POINT_NONE, 0, AFF_FISH_MIND, 20*60, 0, true);
								item->SetCount(item->GetCount() - 1);
								break;

							case 50301: // í†µì†”ë ¥ ìˆ˜ë ¨ì„œ
							case 50302:
							case 50303:
								{
									if (IsPolymorphed() == true)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë‘”ê°‘ ì¤‘ì—ëŠ” ëŠ¥ë ¥ì„ ì˜¬ë¦´ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
										return false;
									}

									int lv = GetSkillLevel(SKILL_LEADERSHIP);

									if (lv < item->GetValue(0))
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ ì±…ì€ ë„ˆë¬´ ì–´ë ¤ì›Œ ì´í•´í•˜ê¸°ê°€ í˜ë“­ë‹ˆë‹¤."));
										return false;
									}

									if (lv >= item->GetValue(1))
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ ì±…ì€ ì•„ë¬´ë¦¬ ë´ë„ ë„ì›€ì´ ë  ê²ƒ ê°™ì§€ ì•ŠìŠµë‹ˆë‹¤."));
										return false;
									}

									if (LearnSkillByBook(SKILL_LEADERSHIP))
									{
										ITEM_MANAGER::instance().RemoveItem(item);

										int iReadDelay = number(SKILLBOOK_DELAY_MIN, SKILLBOOK_DELAY_MAX);

										SetSkillNextReadTime(SKILL_LEADERSHIP, get_global_time() + iReadDelay);
									}
								}
								break;

							case 50304: // ì—°ê³„ê¸° ìˆ˜ë ¨ì„œ
							case 50305:
							case 50306:
								{
									if (IsPolymorphed())
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë³€ì‹ ì¤‘ì—ëŠ” ì±…ì„ ì½ì„ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
										return false;
										
									}
									if (GetSkillLevel(SKILL_COMBO) == 0 && GetLevel() < 30)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë ˆë²¨ 30ì´ ë˜ê¸° ì „ì—ëŠ” ìŠµë“í•  ìˆ˜ ìˆì„ ê²ƒ ê°™ì§€ ì•ŠìŠµë‹ˆë‹¤."));
										return false;
									}

									if (GetSkillLevel(SKILL_COMBO) == 1 && GetLevel() < 50)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë ˆë²¨ 50ì´ ë˜ê¸° ì „ì—ëŠ” ìŠµë“í•  ìˆ˜ ìˆì„ ê²ƒ ê°™ì§€ ì•ŠìŠµë‹ˆë‹¤."));
										return false;
									}

									if (GetSkillLevel(SKILL_COMBO) >= 2)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì—°ê³„ê¸°ëŠ” ë”ì´ìƒ ìˆ˜ë ¨í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
										return false;
									}

									int iPct = item->GetValue(0);

									if (LearnSkillByBook(SKILL_COMBO, iPct))
									{
										ITEM_MANAGER::instance().RemoveItem(item);

										int iReadDelay = number(SKILLBOOK_DELAY_MIN, SKILLBOOK_DELAY_MAX);

										SetSkillNextReadTime(SKILL_COMBO, get_global_time() + iReadDelay);
									}
								}
								break;
							case 50311: // ì–¸ì–´ ìˆ˜ë ¨ì„œ
							case 50312:
							case 50313:
								{
									if (IsPolymorphed())
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë³€ì‹ ì¤‘ì—ëŠ” ì±…ì„ ì½ì„ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
										return false;
										
									}
									DWORD dwSkillVnum = item->GetValue(0);
									int iPct = MINMAX(0, item->GetValue(1), 100);
									if (GetSkillLevel(dwSkillVnum)>=20 || dwSkillVnum-SKILL_LANGUAGE1+1 == GetEmpire())
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ë¯¸ ì™„ë²½í•˜ê²Œ ì•Œì•„ë“¤ì„ ìˆ˜ ìˆëŠ” ì–¸ì–´ì´ë‹¤."));
										return false;
									}

									if (LearnSkillByBook(dwSkillVnum, iPct))
									{
										ITEM_MANAGER::instance().RemoveItem(item);

										int iReadDelay = number(SKILLBOOK_DELAY_MIN, SKILLBOOK_DELAY_MAX);

										SetSkillNextReadTime(dwSkillVnum, get_global_time() + iReadDelay);
									}
								}
								break;

							case 50061 : // ì¼ë³¸ ë§ ì†Œí™˜ ìŠ¤í‚¬ ìˆ˜ë ¨ì„œ
								{
									if (IsPolymorphed())
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë³€ì‹ ì¤‘ì—ëŠ” ì±…ì„ ì½ì„ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
										return false;
										
									}
									DWORD dwSkillVnum = item->GetValue(0);
									int iPct = MINMAX(0, item->GetValue(1), 100);

									if (GetSkillLevel(dwSkillVnum) >= 10)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë” ì´ìƒ ìˆ˜ë ¨í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
										return false;
									}

									if (LearnSkillByBook(dwSkillVnum, iPct))
									{
										ITEM_MANAGER::instance().RemoveItem(item);

										int iReadDelay = number(SKILLBOOK_DELAY_MIN, SKILLBOOK_DELAY_MAX);

										SetSkillNextReadTime(dwSkillVnum, get_global_time() + iReadDelay);
									}
								}
								break;

							case 50314: case 50315: case 50316: // ë³€ì‹  ìˆ˜ë ¨ì„œ
							case 50323: case 50324: // ì¦í˜ˆ ìˆ˜ë ¨ì„œ
							case 50325: case 50326: // ì² í†µ ìˆ˜ë ¨ì„œ
								{
									if (IsPolymorphed() == true)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë‘”ê°‘ ì¤‘ì—ëŠ” ëŠ¥ë ¥ì„ ì˜¬ë¦´ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
										return false;
									}
									
									int iSkillLevelLowLimit = item->GetValue(0);
									int iSkillLevelHighLimit = item->GetValue(1);
									int iPct = MINMAX(0, item->GetValue(2), 100);
									int iLevelLimit = item->GetValue(3);
									DWORD dwSkillVnum = 0;
									
									switch (item->GetVnum())
									{
										case 50314: case 50315: case 50316:
											dwSkillVnum = SKILL_POLYMORPH;
											break;

										case 50323: case 50324:
											dwSkillVnum = SKILL_ADD_HP;
											break;

										case 50325: case 50326:
											dwSkillVnum = SKILL_RESIST_PENETRATE;
											break;

										default:
											return false;
									}

									if (0 == dwSkillVnum)
										return false;

									if (GetLevel() < iLevelLimit)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ ì±…ì„ ì½ìœ¼ë ¤ë©´ ë ˆë²¨ì„ ë” ì˜¬ë ¤ì•¼ í•©ë‹ˆë‹¤."));
										return false;
									}

									if (GetSkillLevel(dwSkillVnum) >= 40)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë” ì´ìƒ ìˆ˜ë ¨í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
										return false;
									}

									if (GetSkillLevel(dwSkillVnum) < iSkillLevelLowLimit)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ ì±…ì€ ë„ˆë¬´ ì–´ë ¤ì›Œ ì´í•´í•˜ê¸°ê°€ í˜ë“­ë‹ˆë‹¤."));
										return false;
									}

									if (GetSkillLevel(dwSkillVnum) >= iSkillLevelHighLimit)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ ì±…ìœ¼ë¡œëŠ” ë” ì´ìƒ ìˆ˜ë ¨í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
										return false;
									}

									if (LearnSkillByBook(dwSkillVnum, iPct))
									{
										ITEM_MANAGER::instance().RemoveItem(item);

										int iReadDelay = number(SKILLBOOK_DELAY_MIN, SKILLBOOK_DELAY_MAX);

										SetSkillNextReadTime(dwSkillVnum, get_global_time() + iReadDelay);
									}
								}
								break;

							case 50902:
							case 50903:
							case 50904:
								{
									if (IsPolymorphed())
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë³€ì‹ ì¤‘ì—ëŠ” ì±…ì„ ì½ì„ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
										return false;
										
									}
									DWORD dwSkillVnum = SKILL_CREATE;
									int iPct = MINMAX(0, item->GetValue(1), 100);

									if (GetSkillLevel(dwSkillVnum)>=40)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë” ì´ìƒ ìˆ˜ë ¨í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
										return false;
									}

									if (LearnSkillByBook(dwSkillVnum, iPct))
									{
										ITEM_MANAGER::instance().RemoveItem(item);

										int iReadDelay = number(SKILLBOOK_DELAY_MIN, SKILLBOOK_DELAY_MAX);

										SetSkillNextReadTime(dwSkillVnum, get_global_time() + iReadDelay);

										if (test_server) 
										{
											ChatPacket(CHAT_TYPE_INFO, "[TEST_SERVER] Success to learn skill ");
										}
									}
									else
									{
										if (test_server) 
										{
											ChatPacket(CHAT_TYPE_INFO, "[TEST_SERVER] Failed to learn skill ");
										}
									}
								}
								break;

								// MINING
							case ITEM_MINING_SKILL_TRAIN_BOOK:
								{
									if (IsPolymorphed())
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë³€ì‹ ì¤‘ì—ëŠ” ì±…ì„ ì½ì„ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
										return false;
										
									}
									DWORD dwSkillVnum = SKILL_MINING;
									int iPct = MINMAX(0, item->GetValue(1), 100);

									if (GetSkillLevel(dwSkillVnum)>=40)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë” ì´ìƒ ìˆ˜ë ¨í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
										return false;
									}

									if (LearnSkillByBook(dwSkillVnum, iPct))
									{
										ITEM_MANAGER::instance().RemoveItem(item);

										int iReadDelay = number(SKILLBOOK_DELAY_MIN, SKILLBOOK_DELAY_MAX);

										SetSkillNextReadTime(dwSkillVnum, get_global_time() + iReadDelay);
									}
								}
								break;
								// END_OF_MINING

							case ITEM_HORSE_SKILL_TRAIN_BOOK:
								{
									if (IsPolymorphed())
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë³€ì‹ ì¤‘ì—ëŠ” ì±…ì„ ì½ì„ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
										return false;
										
									}
									DWORD dwSkillVnum = SKILL_HORSE;
									int iPct = MINMAX(0, item->GetValue(1), 100);

									if (GetLevel() < 50)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì•„ì§ ìŠ¹ë§ˆ ìŠ¤í‚¬ì„ ìˆ˜ë ¨í•  ìˆ˜ ìˆëŠ” ë ˆë²¨ì´ ì•„ë‹™ë‹ˆë‹¤."));
										return false;
									}

									if (!test_server && get_global_time() < GetSkillNextReadTime(dwSkillVnum))
									{
										if (FindAffect(AFFECT_SKILL_NO_BOOK_DELAY))
										{
											// ì£¼ì•ˆìˆ ì„œ ì‚¬ìš©ì¤‘ì—ëŠ” ì‹œê°„ ì œí•œ ë¬´ì‹œ
											RemoveAffect(AFFECT_SKILL_NO_BOOK_DELAY);
											ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì£¼ì•ˆìˆ ì„œë¥¼ í†µí•´ ì£¼í™”ì…ë§ˆì—ì„œ ë¹ ì ¸ë‚˜ì™”ìŠµë‹ˆë‹¤."));
										}
										else
										{
											SkillLearnWaitMoreTimeMessage(GetSkillNextReadTime(dwSkillVnum) - get_global_time());
											return false;
										}
									}

									if (GetPoint(POINT_HORSE_SKILL) >= 20 || 
											GetSkillLevel(SKILL_HORSE_WILDATTACK) + GetSkillLevel(SKILL_HORSE_CHARGE) + GetSkillLevel(SKILL_HORSE_ESCAPE) >= 60 ||
											GetSkillLevel(SKILL_HORSE_WILDATTACK_RANGE) + GetSkillLevel(SKILL_HORSE_CHARGE) + GetSkillLevel(SKILL_HORSE_ESCAPE) >= 60)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë” ì´ìƒ ìŠ¹ë§ˆ ìˆ˜ë ¨ì„œë¥¼ ì½ì„ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
										return false;
									}

									if (number(1, 100) <= iPct)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìŠ¹ë§ˆ ìˆ˜ë ¨ì„œë¥¼ ì½ì–´ ìŠ¹ë§ˆ ìŠ¤í‚¬ í¬ì¸íŠ¸ë¥¼ ì–»ì—ˆìŠµë‹ˆë‹¤."));
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì–»ì€ í¬ì¸íŠ¸ë¡œëŠ” ìŠ¹ë§ˆ ìŠ¤í‚¬ì˜ ë ˆë²¨ì„ ì˜¬ë¦´ ìˆ˜ ìˆìŠµë‹ˆë‹¤."));
										PointChange(POINT_HORSE_SKILL, 1);

										int iReadDelay = number(SKILLBOOK_DELAY_MIN, SKILLBOOK_DELAY_MAX);

										if (!test_server)
											SetSkillNextReadTime(dwSkillVnum, get_global_time() + iReadDelay);
									}
									else
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìŠ¹ë§ˆ ìˆ˜ë ¨ì„œ ì´í•´ì— ì‹¤íŒ¨í•˜ì˜€ìŠµë‹ˆë‹¤."));
									}

									ITEM_MANAGER::instance().RemoveItem(item);
								}
								break;

							case 70102: // ì„ ë‘
							case 70103: // ì„ ë‘
								{
									if (GetAlignment() >= 0)
										return false;

									int delta = MIN(-GetAlignment(), item->GetValue(0));

									sys_log(0, "%s ALIGNMENT ITEM %d", GetName(), delta);

									UpdateAlignment(delta);
									item->SetCount(item->GetCount() - 1);

									if (delta / 10 > 0)
									{
										ChatPacket(CHAT_TYPE_TALKING, LC_TEXT("ë§ˆìŒì´ ë§‘ì•„ì§€ëŠ”êµ°. ê°€ìŠ´ì„ ì§“ëˆ„ë¥´ë˜ ë¬´ì–¸ê°€ê°€ ì¢€ ê°€ë²¼ì›Œì§„ ëŠë‚Œì´ì•¼."));
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì„ ì•…ì¹˜ê°€ %d ì¦ê°€í•˜ì˜€ìŠµë‹ˆë‹¤."), delta/10);
									}
								}
								break;

							case 71107: // ì²œë„ë³µìˆ­ì•„
								{
									int val = item->GetValue(0);
									int interval = item->GetValue(1);
									quest::PC* pPC = quest::CQuestManager::instance().GetPC(GetPlayerID());
									int last_use_time = pPC->GetFlag("mythical_peach.last_use_time");

									if (get_global_time() - last_use_time < interval * 60 * 60)
									{
										if (test_server == false)
										{
											ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì•„ì§ ì‚¬ìš©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
											return false;
										}
										else
										{
											ChatPacket(CHAT_TYPE_INFO, LC_TEXT("í…ŒìŠ¤íŠ¸ ì„œë²„ ì‹œê°„ì œí•œ í†µê³¼"));
										}
									}
									
									if (GetAlignment() == 200000)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì„ ì•…ì¹˜ë¥¼ ë” ì´ìƒ ì˜¬ë¦´ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
										return false;
									}
									
									if (200000 - GetAlignment() < val * 10)
									{
										val = (200000 - GetAlignment()) / 10;
									}
									
									int old_alignment = GetAlignment() / 10;

									UpdateAlignment(val*10);
									
									item->SetCount(item->GetCount()-1);
									pPC->SetFlag("mythical_peach.last_use_time", get_global_time());

									ChatPacket(CHAT_TYPE_TALKING, LC_TEXT("ë§ˆìŒì´ ë§‘ì•„ì§€ëŠ”êµ°. ê°€ìŠ´ì„ ì§“ëˆ„ë¥´ë˜ ë¬´ì–¸ê°€ê°€ ì¢€ ê°€ë²¼ì›Œì§„ ëŠë‚Œì´ì•¼."));
									ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì„ ì•…ì¹˜ê°€ %d ì¦ê°€í•˜ì˜€ìŠµë‹ˆë‹¤."), val);

									char buf[256 + 1];
									snprintf(buf, sizeof(buf), "%d %d", old_alignment, GetAlignment() / 10);
									LogManager::instance().CharLog(this, val, "MYTHICAL_PEACH", buf);
								}
								break;

							case 71109: // íƒˆì„ì„œ
							case 72719:
								{
									LPITEM item2;

									if (!IsValidItemPosition(DestCell) || !(item2 = GetItem(DestCell)))
										return false;

									if (item2->IsExchanging() == true)
										return false;

									if (item2->GetSocketCount() == 0)
										return false;

									switch( item2->GetType() )
									{
										case ITEM_WEAPON:
											break;
										case ITEM_ARMOR:
											switch (item2->GetSubType())
											{
											case ARMOR_EAR:
											case ARMOR_WRIST:
											case ARMOR_NECK:
												ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë¹¼ë‚¼ ì˜ì„ì´ ì—†ìŠµë‹ˆë‹¤"));
												return false;
											}
											break;

										default:
											return false;
									}

									std::stack<long> socket;

									for (int i = 0; i < ITEM_SOCKET_MAX_NUM; ++i)
										socket.push(item2->GetSocket(i));

									int idx = ITEM_SOCKET_MAX_NUM - 1;

									while (socket.size() > 0)
									{
										if (socket.top() > 2 && socket.top() != ITEM_BROKEN_METIN_VNUM)
											break;

										idx--;
										socket.pop();
									}

									if (socket.size() == 0)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë¹¼ë‚¼ ì˜ì„ì´ ì—†ìŠµë‹ˆë‹¤"));
										return false;
									}

									LPITEM pItemReward = AutoGiveItem(socket.top());

									if (pItemReward != NULL)
									{
										item2->SetSocket(idx, 1);

										char buf[256+1];
										snprintf(buf, sizeof(buf), "%s(%u) %s(%u)", 
												item2->GetName(), item2->GetID(), pItemReward->GetName(), pItemReward->GetID());
										LogManager::instance().ItemLog(this, item, "USE_DETACHMENT_ONE", buf);

										item->SetCount(item->GetCount() - 1);
									}
								}
								break;

							case 70201:   // íƒˆìƒ‰ì œ
							case 70202:   // ì—¼ìƒ‰ì•½(í°ìƒ‰)
							case 70203:   // ì—¼ìƒ‰ì•½(ê¸ˆìƒ‰)
							case 70204:   // ì—¼ìƒ‰ì•½(ë¹¨ê°„ìƒ‰)
							case 70205:   // ì—¼ìƒ‰ì•½(ê°ˆìƒ‰)
							case 70206:   // ì—¼ìƒ‰ì•½(ê²€ì€ìƒ‰)
								{
									// NEW_HAIR_STYLE_ADD
									if (GetPart(PART_HAIR) >= 1001)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("í˜„ì¬ í—¤ì–´ìŠ¤íƒ€ì¼ì—ì„œëŠ” ì—¼ìƒ‰ê³¼ íƒˆìƒ‰ì´ ë¶ˆê°€ëŠ¥í•©ë‹ˆë‹¤."));
									}
									// END_NEW_HAIR_STYLE_ADD
									else
									{
										quest::CQuestManager& q = quest::CQuestManager::instance();
										quest::PC* pPC = q.GetPC(GetPlayerID());

										if (pPC)
										{
											int last_dye_level = pPC->GetFlag("dyeing_hair.last_dye_level");

											if (last_dye_level == 0 ||
													last_dye_level+3 <= GetLevel() ||
													item->GetVnum() == 70201)
											{
												SetPart(PART_HAIR, item->GetVnum() - 70201);

												if (item->GetVnum() == 70201)
													pPC->SetFlag("dyeing_hair.last_dye_level", 0);
												else
													pPC->SetFlag("dyeing_hair.last_dye_level", GetLevel());

												item->SetCount(item->GetCount() - 1);
												UpdatePacket();
											}
											else
											{
												ChatPacket(CHAT_TYPE_INFO, LC_TEXT("%d ë ˆë²¨ì´ ë˜ì–´ì•¼ ë‹¤ì‹œ ì—¼ìƒ‰í•˜ì‹¤ ìˆ˜ ìˆìŠµë‹ˆë‹¤."), last_dye_level+3);
											}
										}
									}
								}
								break;

							case ITEM_NEW_YEAR_GREETING_VNUM:
								{
									DWORD dwBoxVnum = ITEM_NEW_YEAR_GREETING_VNUM;
									std::vector <DWORD> dwVnums;
									std::vector <DWORD> dwCounts;
									std::vector <LPITEM> item_gets;
									int count = 0;

									if (GiveItemFromSpecialItemGroup(dwBoxVnum, dwVnums, dwCounts, item_gets, count))
									{
										for (int i = 0; i < count; i++)
										{
											if (dwVnums[i] == CSpecialItemGroup::GOLD)
												ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ëˆ %d ëƒ¥ì„ íšë“í–ˆìŠµë‹ˆë‹¤."), dwCounts[i]);
										}

										item->SetCount(item->GetCount() - 1);
									}
								}
								break;

							case ITEM_VALENTINE_ROSE:
							case ITEM_VALENTINE_CHOCOLATE:
								{
									DWORD dwBoxVnum = item->GetVnum();
									std::vector <DWORD> dwVnums;
									std::vector <DWORD> dwCounts;
									std::vector <LPITEM> item_gets;
									int count = 0;


									if (item->GetVnum() == ITEM_VALENTINE_ROSE && SEX_MALE==GET_SEX(this) ||
										item->GetVnum() == ITEM_VALENTINE_CHOCOLATE && SEX_FEMALE==GET_SEX(this))
									{
										// ì„±ë³„ì´ ë§ì§€ì•Šì•„ ì“¸ ìˆ˜ ì—†ë‹¤.
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì„±ë³„ì´ ë§ì§€ì•Šì•„ ì´ ì•„ì´í…œì„ ì—´ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
										return false;
									}


									if (GiveItemFromSpecialItemGroup(dwBoxVnum, dwVnums, dwCounts, item_gets, count))
										item->SetCount(item->GetCount()-1);
								}
								break;

							case ITEM_WHITEDAY_CANDY:
							case ITEM_WHITEDAY_ROSE:
								{
									DWORD dwBoxVnum = item->GetVnum();
									std::vector <DWORD> dwVnums;
									std::vector <DWORD> dwCounts;
									std::vector <LPITEM> item_gets;
									int count = 0;


									if (item->GetVnum() == ITEM_WHITEDAY_CANDY && SEX_MALE==GET_SEX(this) ||
										item->GetVnum() == ITEM_WHITEDAY_ROSE && SEX_FEMALE==GET_SEX(this))
									{
										// ì„±ë³„ì´ ë§ì§€ì•Šì•„ ì“¸ ìˆ˜ ì—†ë‹¤.
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì„±ë³„ì´ ë§ì§€ì•Šì•„ ì´ ì•„ì´í…œì„ ì—´ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
										return false;
									}


									if (GiveItemFromSpecialItemGroup(dwBoxVnum, dwVnums, dwCounts, item_gets, count))
										item->SetCount(item->GetCount()-1);
								}
								break;

							case 50011: // ì›”ê´‘ë³´í•©
								{
									DWORD dwBoxVnum = 50011;
									std::vector <DWORD> dwVnums;
									std::vector <DWORD> dwCounts;
									std::vector <LPITEM> item_gets;
									int count = 0;

									if (GiveItemFromSpecialItemGroup(dwBoxVnum, dwVnums, dwCounts, item_gets, count))
									{
										for (int i = 0; i < count; i++)
										{
											char buf[50 + 1];
											snprintf(buf, sizeof(buf), "%u %u", dwVnums[i], dwCounts[i]);
											LogManager::instance().ItemLog(this, item, "MOONLIGHT_GET", buf);

											//ITEM_MANAGER::instance().RemoveItem(item);
											item->SetCount(item->GetCount() - 1);

											switch (dwVnums[i])
											{
											case CSpecialItemGroup::GOLD:
												ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ëˆ %d ëƒ¥ì„ íšë“í–ˆìŠµë‹ˆë‹¤."), dwCounts[i]);
												break;

											case CSpecialItemGroup::EXP:
												ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒìì—ì„œ ë¶€í„° ì‹ ë¹„í•œ ë¹›ì´ ë‚˜ì˜µë‹ˆë‹¤."));
												ChatPacket(CHAT_TYPE_INFO, LC_TEXT("%dì˜ ê²½í—˜ì¹˜ë¥¼ íšë“í–ˆìŠµë‹ˆë‹¤."), dwCounts[i]);
												break;

											case CSpecialItemGroup::MOB:
												ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒìì—ì„œ ëª¬ìŠ¤í„°ê°€ ë‚˜íƒ€ë‚¬ìŠµë‹ˆë‹¤!"));
												break;

											case CSpecialItemGroup::SLOW:
												ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒìì—ì„œ ë‚˜ì˜¨ ë¹¨ê°„ ì—°ê¸°ë¥¼ ë“¤ì´ë§ˆì‹œì ì›€ì§ì´ëŠ” ì†ë„ê°€ ëŠë ¤ì¡ŒìŠµë‹ˆë‹¤!"));
												break;

											case CSpecialItemGroup::DRAIN_HP:
												ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒìê°€ ê°‘ìê¸° í­ë°œí•˜ì˜€ìŠµë‹ˆë‹¤! ìƒëª…ë ¥ì´ ê°ì†Œí–ˆìŠµë‹ˆë‹¤."));
												break;

											case CSpecialItemGroup::POISON:
												ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒìì—ì„œ ë‚˜ì˜¨ ë…¹ìƒ‰ ì—°ê¸°ë¥¼ ë“¤ì´ë§ˆì‹œì ë…ì´ ì˜¨ëª¸ìœ¼ë¡œ í¼ì§‘ë‹ˆë‹¤!"));
												break;

											case CSpecialItemGroup::MOB_GROUP:
												ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒìì—ì„œ ëª¬ìŠ¤í„°ê°€ ë‚˜íƒ€ë‚¬ìŠµë‹ˆë‹¤!"));
												break;

											default:
												if (item_gets[i])
												{
													if (dwCounts[i] > 1)
														ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒìì—ì„œ %s ê°€ %d ê°œ ë‚˜ì™”ìŠµë‹ˆë‹¤."), item_gets[i]->GetName(), dwCounts[i]);
													else
														ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ìƒìì—ì„œ %s ê°€ ë‚˜ì™”ìŠµë‹ˆë‹¤."), item_gets[i]->GetName());
												}
												break;
											}
										}
									}
									else
									{
										ChatPacket(CHAT_TYPE_TALKING, LC_TEXT("ì•„ë¬´ê²ƒë„ ì–»ì„ ìˆ˜ ì—†ì—ˆìŠµë‹ˆë‹¤."));
										return false;
									}
								}
								break;

							case ITEM_GIVE_STAT_RESET_COUNT_VNUM:
								{
									//PointChange(POINT_GOLD, -iCost);
									PointChange(POINT_STAT_RESET_COUNT, 1);
									item->SetCount(item->GetCount()-1);
								}
								break;

							case 50107:
								{
									EffectPacket(SE_CHINA_FIREWORK);
									// ìŠ¤í„´ ê³µê²©ì„ ì˜¬ë ¤ì¤€ë‹¤
									AddAffect(AFFECT_CHINA_FIREWORK, POINT_STUN_PCT, 30, AFF_CHINA_FIREWORK, 5*60, 0, true);
									item->SetCount(item->GetCount()-1);
								}
								break;

							case 50108:
								{
									if (CArenaManager::instance().IsArenaMap(GetMapIndex()) == true)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ëŒ€ë ¨ ì¤‘ì—ëŠ” ì´ìš©í•  ìˆ˜ ì—†ëŠ” ë¬¼í’ˆì…ë‹ˆë‹¤."));
										return false;
									}

									EffectPacket(SE_SPIN_TOP);
									// ìŠ¤í„´ ê³µê²©ì„ ì˜¬ë ¤ì¤€ë‹¤
									AddAffect(AFFECT_CHINA_FIREWORK, POINT_STUN_PCT, 30, AFF_CHINA_FIREWORK, 5*60, 0, true);
									item->SetCount(item->GetCount()-1);
								}
								break;

							case ITEM_WONSO_BEAN_VNUM:
								PointChange(POINT_HP, GetMaxHP() - GetHP());
								item->SetCount(item->GetCount()-1);
								break;

							case ITEM_WONSO_SUGAR_VNUM:
								PointChange(POINT_SP, GetMaxSP() - GetSP());
								item->SetCount(item->GetCount()-1);
								break;

							case ITEM_WONSO_FRUIT_VNUM:
								PointChange(POINT_STAMINA, GetMaxStamina()-GetStamina());
								item->SetCount(item->GetCount()-1);
								break;

							case ITEM_ELK_VNUM: // ëˆê¾¸ëŸ¬ë¯¸
								{
									int iGold = item->GetSocket(0);
									ITEM_MANAGER::instance().RemoveItem(item);
									ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ëˆ %d ëƒ¥ì„ íšë“í–ˆìŠµë‹ˆë‹¤."), iGold);
									PointChange(POINT_GOLD, iGold);
								}
								break;

							case 70021:
								break;

							case 27995:
								{
								}
								break;

							case 71092 : // ë³€ì‹  í•´ì²´ë¶€ ì„ì‹œ
								{
									if (m_pkChrTarget != NULL)
									{
										if (m_pkChrTarget->IsPolymorphed())
										{
											m_pkChrTarget->SetPolymorph(0);
											m_pkChrTarget->RemoveAffect(AFFECT_POLYMORPH);
										}
									}
									else
									{
										if (IsPolymorphed())
										{
											SetPolymorph(0);
											RemoveAffect(AFFECT_POLYMORPH);
										}
									}
								}
								break;

							case 71051 : // ì§„ì¬ê°€
								{
									// ìœ ëŸ½, ì‹±ê°€í´, ë² íŠ¸ë‚¨ ì§„ì¬ê°€ ì‚¬ìš©ê¸ˆì§€
									// if (LC_IsEurope() || LC_IsSingapore() || LC_IsVietnam())
										// return false;

									LPITEM item2;

									if (!IsValidItemPosition(DestCell) || !(item2 = GetInventoryItem(wDestCell)))
										return false;

									if (item2->IsExchanging() == true)
										return false;
									
									if (item2->IsEquipped() == true) // Fix
										return false;
									
									if (item2->GetType() == ITEM_COSTUME) // Fix
										return false;

									if (item2->GetAttributeSetIndex() == -1)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì†ì„±ì„ ë³€ê²½í•  ìˆ˜ ì—†ëŠ” ì•„ì´í…œì…ë‹ˆë‹¤."));
										return false;
									}

									if (item2->AddRareAttribute() == true)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì„±ê³µì ìœ¼ë¡œ ì†ì„±ì´ ì¶”ê°€ ë˜ì—ˆìŠµë‹ˆë‹¤"));

										int iAddedIdx = item2->GetRareAttrCount() + 4;
										char buf[21];
										snprintf(buf, sizeof(buf), "%u", item2->GetID());

										LogManager::instance().ItemLog(
												GetPlayerID(),
												item2->GetAttributeType(iAddedIdx),
												item2->GetAttributeValue(iAddedIdx),
												item->GetID(),
												"ADD_RARE_ATTR",
												buf,
												GetDesc()->GetHostName(),
												item->GetOriginalVnum());

										item->SetCount(item->GetCount() - 1);
									}
									else
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë” ì´ìƒ ì´ ì•„ì´í…œìœ¼ë¡œ ì†ì„±ì„ ì¶”ê°€í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤"));
									}
								}
								break;

							case 71052 : // ì§„ì¬ê²½
								{
									// ìœ ëŸ½, ì‹±ê°€í´, ë² íŠ¸ë‚¨ ì§„ì¬ê°€ ì‚¬ìš©ê¸ˆì§€
									// if (LC_IsEurope() || LC_IsSingapore() || LC_IsVietnam())
										// return false;

									LPITEM item2;

									if (!IsValidItemPosition(DestCell) || !(item2 = GetItem(DestCell)))
										return false;

									if (item2->IsExchanging() == true)
										return false;
									
									if (item2->IsEquipped() == true) // Fix
										return false;
									
									if (item2->GetType() == ITEM_COSTUME) // Fix
										return false;

									if (item2->GetAttributeSetIndex() == -1)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì†ì„±ì„ ë³€ê²½í•  ìˆ˜ ì—†ëŠ” ì•„ì´í…œì…ë‹ˆë‹¤."));
										return false;
									}

									if (item2->ChangeRareAttribute() == true)
									{
										char buf[21];
										snprintf(buf, sizeof(buf), "%u", item2->GetID());
										LogManager::instance().ItemLog(this, item, "CHANGE_RARE_ATTR", buf);

										item->SetCount(item->GetCount() - 1);
									}
									else
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë³€ê²½ ì‹œí‚¬ ì†ì„±ì´ ì—†ìŠµë‹ˆë‹¤"));
									}
								}
								break;

							case ITEM_AUTO_HP_RECOVERY_S:
							case ITEM_AUTO_HP_RECOVERY_M:
							case ITEM_AUTO_HP_RECOVERY_L:
							case ITEM_AUTO_HP_RECOVERY_X:
							case ITEM_AUTO_SP_RECOVERY_S:
							case ITEM_AUTO_SP_RECOVERY_M:
							case ITEM_AUTO_SP_RECOVERY_L:
							case ITEM_AUTO_SP_RECOVERY_X:
							// ë¬´ì‹œë¬´ì‹œí•˜ì§€ë§Œ ì´ì „ì— í•˜ë˜ ê±¸ ê³ ì¹˜ê¸°ëŠ” ë¬´ì„­ê³ ...
							// ê·¸ë˜ì„œ ê·¸ëƒ¥ í•˜ë“œ ì½”ë”©. ì„ ë¬¼ ìƒììš© ìë™ë¬¼ì•½ ì•„ì´í…œë“¤.
							case REWARD_BOX_ITEM_AUTO_SP_RECOVERY_XS: 
							case REWARD_BOX_ITEM_AUTO_SP_RECOVERY_S: 
							case REWARD_BOX_ITEM_AUTO_HP_RECOVERY_XS: 
							case REWARD_BOX_ITEM_AUTO_HP_RECOVERY_S:
							case FUCKING_BRAZIL_ITEM_AUTO_SP_RECOVERY_S:
							case FUCKING_BRAZIL_ITEM_AUTO_HP_RECOVERY_S:
								{
									if (CArenaManager::instance().IsArenaMap(GetMapIndex()) == true)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ëŒ€ë ¨ì¥ì—ì„œ ì‚¬ìš©í•˜ì‹¤ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
										return false;
									}

									EAffectTypes type = AFFECT_NONE;
									bool isSpecialPotion = false;

									switch (item->GetVnum())
									{
										case ITEM_AUTO_HP_RECOVERY_X:
											isSpecialPotion = true;

										case ITEM_AUTO_HP_RECOVERY_S:
										case ITEM_AUTO_HP_RECOVERY_M:
										case ITEM_AUTO_HP_RECOVERY_L:
										case REWARD_BOX_ITEM_AUTO_HP_RECOVERY_XS:
										case REWARD_BOX_ITEM_AUTO_HP_RECOVERY_S:
										case FUCKING_BRAZIL_ITEM_AUTO_HP_RECOVERY_S:
											type = AFFECT_AUTO_HP_RECOVERY;
											break;

										case ITEM_AUTO_SP_RECOVERY_X:
											isSpecialPotion = true;

										case ITEM_AUTO_SP_RECOVERY_S:
										case ITEM_AUTO_SP_RECOVERY_M:
										case ITEM_AUTO_SP_RECOVERY_L:
										case REWARD_BOX_ITEM_AUTO_SP_RECOVERY_XS:
										case REWARD_BOX_ITEM_AUTO_SP_RECOVERY_S:
										case FUCKING_BRAZIL_ITEM_AUTO_SP_RECOVERY_S:
											type = AFFECT_AUTO_SP_RECOVERY;
											break;
									}

									if (AFFECT_NONE == type)
										break;

									if (item->GetCount() > 1)
									{
										int pos = GetEmptyInventory(item->GetSize());

										if (-1 == pos)
										{
											ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì†Œì§€í’ˆì— ë¹ˆ ê³µê°„ì´ ì—†ìŠµë‹ˆë‹¤."));
											break;
										}

										item->SetCount( item->GetCount() - 1 );

										LPITEM item2 = ITEM_MANAGER::instance().CreateItem( item->GetVnum(), 1 );
										item2->AddToCharacter(this, TItemPos(INVENTORY, pos));

										if (item->GetSocket(1) != 0)
										{
											item2->SetSocket(1, item->GetSocket(1));
										}

										item = item2;
									}

									CAffect* pAffect = FindAffect( type );

									if (NULL == pAffect)
									{
										EPointTypes bonus = POINT_NONE;

										if (true == isSpecialPotion)
										{
											if (type == AFFECT_AUTO_HP_RECOVERY)
											{
												bonus = POINT_MAX_HP_PCT;
											}
											else if (type == AFFECT_AUTO_SP_RECOVERY)
											{
												bonus = POINT_MAX_SP_PCT;
											}
										}

										AddAffect( type, bonus, 4, item->GetID(), INFINITE_AFFECT_DURATION, 0, true, false);

										// MR-3: Active autopotion unlock
										//item->Lock(true);
										// MR-3: -- END OF -- Active autopotion unlock
										item->SetSocket(0, true);

										AutoRecoveryItemProcess( type );
									}
									else
									{
										if (item->GetID() == pAffect->dwFlag)
										{
											RemoveAffect( pAffect );

											item->Lock(false);
											item->SetSocket(0, false);
										}
										else
										{
											LPITEM old = FindItemByID( pAffect->dwFlag );

											if (NULL != old)
											{
												old->Lock(false);
												old->SetSocket(0, false);
											}

											RemoveAffect( pAffect );

											EPointTypes bonus = POINT_NONE;

											if (true == isSpecialPotion)
											{
												if (type == AFFECT_AUTO_HP_RECOVERY)
												{
													bonus = POINT_MAX_HP_PCT;
												}
												else if (type == AFFECT_AUTO_SP_RECOVERY)
												{
													bonus = POINT_MAX_SP_PCT;
												}
											}

											AddAffect( type, bonus, 4, item->GetID(), INFINITE_AFFECT_DURATION, 0, true, false);

											// MR-3: Active autopotion unlock
											//item->Lock(true);
											// MR-3: -- END OF -- Active autopotion unlock
											item->SetSocket(0, true);

											AutoRecoveryItemProcess( type );
										}
									}
								}
								break;
						}
						break;

					case USE_CLEAR:
						{
							RemoveBadAffect();
							item->SetCount(item->GetCount() - 1);
						}
						break;

					case USE_INVISIBILITY:
						{
							if (item->GetVnum() == 70026)
							{
								quest::CQuestManager& q = quest::CQuestManager::instance();
								quest::PC* pPC = q.GetPC(GetPlayerID());

								if (pPC != NULL)
								{
									int last_use_time = pPC->GetFlag("mirror_of_disapper.last_use_time");

									if (get_global_time() - last_use_time < 10*60)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì•„ì§ ì‚¬ìš©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
										return false;
									}

									pPC->SetFlag("mirror_of_disapper.last_use_time", get_global_time());
								}
							}

							AddAffect(AFFECT_INVISIBILITY, POINT_NONE, 0, AFF_INVISIBILITY, 300, 0, true);
							item->SetCount(item->GetCount() - 1);
						}
						break;

					case USE_POTION_NODELAY:
						{
							if (CArenaManager::instance().IsArenaMap(GetMapIndex()) == true)
							{
								if (quest::CQuestManager::instance().GetEventFlag("arena_potion_limit") > 0)
								{
									ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ëŒ€ë ¨ì¥ì—ì„œ ì‚¬ìš©í•˜ì‹¤ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
									return false;
								}

								switch (item->GetVnum())
								{
									case 70020 :
									case 71018 :
									case 71019 :
									case 71020 :
										if (quest::CQuestManager::instance().GetEventFlag("arena_potion_limit_count") < 10000)
										{
											if (m_nPotionLimit <= 0)
											{
												ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì‚¬ìš© ì œí•œëŸ‰ì„ ì´ˆê³¼í•˜ì˜€ìŠµë‹ˆë‹¤."));
												return false;
											}
										}
										break;

									default :
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ëŒ€ë ¨ì¥ì—ì„œ ì‚¬ìš©í•˜ì‹¤ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
										return false;
								}
							}

							bool used = false;

							if (item->GetValue(0) != 0) // HP ì ˆëŒ€ê°’ íšŒë³µ
							{
								if (GetHP() < GetMaxHP())
								{
									PointChange(POINT_HP, item->GetValue(0) * (100 + GetPoint(POINT_POTION_BONUS)) / 100);
									EffectPacket(SE_HPUP_RED);
									used = TRUE;
								}
							}

							if (item->GetValue(1) != 0)	// SP ì ˆëŒ€ê°’ íšŒë³µ
							{
								if (GetSP() < GetMaxSP())
								{
									PointChange(POINT_SP, item->GetValue(1) * (100 + GetPoint(POINT_POTION_BONUS)) / 100);
									EffectPacket(SE_SPUP_BLUE);
									used = TRUE;
								}
							}

							if (item->GetValue(3) != 0) // HP % íšŒë³µ
							{
								if (GetHP() < GetMaxHP())
								{
									PointChange(POINT_HP, item->GetValue(3) * GetMaxHP() / 100);
									EffectPacket(SE_HPUP_RED);
									used = TRUE;
								}
							}

							if (item->GetValue(4) != 0) // SP % íšŒë³µ
							{
								if (GetSP() < GetMaxSP())
								{
									PointChange(POINT_SP, item->GetValue(4) * GetMaxSP() / 100);
									EffectPacket(SE_SPUP_BLUE);
									used = TRUE;
								}
							}

							if (used)
							{
								if (item->GetVnum() == 50085 || item->GetVnum() == 50086)
								{
									if (test_server)
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì›”ë³‘ ë˜ëŠ” ì¢…ì ë¥¼ ì‚¬ìš©í•˜ì˜€ìŠµë‹ˆë‹¤"));
									SetUseSeedOrMoonBottleTime();
								}
								if (GetDungeon())
									GetDungeon()->UsePotion(this);

								if (GetWarMap())
									GetWarMap()->UsePotion(this, item);

								m_nPotionLimit--;

								//RESTRICT_USE_SEED_OR_MOONBOTTLE
								item->SetCount(item->GetCount() - 1);
								//END_RESTRICT_USE_SEED_OR_MOONBOTTLE
							}
						}
						break;

					case USE_POTION:
						if (CArenaManager::instance().IsArenaMap(GetMapIndex()) == true)
						{
							if (quest::CQuestManager::instance().GetEventFlag("arena_potion_limit") > 0)
							{
								ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ëŒ€ë ¨ì¥ì—ì„œ ì‚¬ìš©í•˜ì‹¤ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
								return false;
							}
						
							switch (item->GetVnum())
							{
								case 27001 :
								case 27002 :
								case 27003 :
								case 27004 :
								case 27005 :
								case 27006 :
									if (quest::CQuestManager::instance().GetEventFlag("arena_potion_limit_count") < 10000)
									{
										if (m_nPotionLimit <= 0)
										{
											ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì‚¬ìš© ì œí•œëŸ‰ì„ ì´ˆê³¼í•˜ì˜€ìŠµë‹ˆë‹¤."));
											return false;
										}
									}
									break;

								default :
									ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ëŒ€ë ¨ì¥ì—ì„œ ì‚¬ìš©í•˜ì‹¤ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
									return false;
							}
						}
						
						if (item->GetValue(1) != 0)
						{
							if (GetPoint(POINT_SP_RECOVERY) + GetSP() >= GetMaxSP())
							{
								return false;
							}

							PointChange(POINT_SP_RECOVERY, item->GetValue(1) * MIN(200, (100 + GetPoint(POINT_POTION_BONUS))) / 100);
							StartAffectEvent();
							EffectPacket(SE_SPUP_BLUE);
						}

						if (item->GetValue(0) != 0)
						{
							if (GetPoint(POINT_HP_RECOVERY) + GetHP() >= GetMaxHP())
							{
								return false;
							}

							PointChange(POINT_HP_RECOVERY, item->GetValue(0) * MIN(200, (100 + GetPoint(POINT_POTION_BONUS))) / 100);
							StartAffectEvent();
							EffectPacket(SE_HPUP_RED);
						}

						if (GetDungeon())
							GetDungeon()->UsePotion(this);

						if (GetWarMap())
							GetWarMap()->UsePotion(this, item);

						item->SetCount(item->GetCount() - 1);
						m_nPotionLimit--;
						break;

					case USE_POTION_CONTINUE:
						{
							if (item->GetValue(0) != 0)
							{
								AddAffect(AFFECT_HP_RECOVER_CONTINUE, POINT_HP_RECOVER_CONTINUE, item->GetValue(0), 0, item->GetValue(2), 0, true);
							}
							else if (item->GetValue(1) != 0)
							{
								AddAffect(AFFECT_SP_RECOVER_CONTINUE, POINT_SP_RECOVER_CONTINUE, item->GetValue(1), 0, item->GetValue(2), 0, true);
							}
							else
								return false;
						}

						if (GetDungeon())
							GetDungeon()->UsePotion(this);

						if (GetWarMap())
							GetWarMap()->UsePotion(this, item);

						item->SetCount(item->GetCount() - 1);
						break;

					case USE_ABILITY_UP:
						{
							switch (item->GetValue(0))
							{
								case APPLY_MOV_SPEED:
									AddAffect(AFFECT_MOV_SPEED, POINT_MOV_SPEED, item->GetValue(2), AFF_MOV_SPEED_POTION, item->GetValue(1), 0, true);
									EffectPacket(SE_DXUP_PURPLE);
									break;

								case APPLY_ATT_SPEED:
									AddAffect(AFFECT_ATT_SPEED, POINT_ATT_SPEED, item->GetValue(2), AFF_ATT_SPEED_POTION, item->GetValue(1), 0, true);
									EffectPacket(SE_SPEEDUP_GREEN);
									break;

								case APPLY_STR:
									AddAffect(AFFECT_STR, POINT_ST, item->GetValue(2), 0, item->GetValue(1), 0, true);
									break;

								case APPLY_DEX:
									AddAffect(AFFECT_DEX, POINT_DX, item->GetValue(2), 0, item->GetValue(1), 0, true);
									break;

								case APPLY_CON:
									AddAffect(AFFECT_CON, POINT_HT, item->GetValue(2), 0, item->GetValue(1), 0, true);
									break;

								case APPLY_INT:
									AddAffect(AFFECT_INT, POINT_IQ, item->GetValue(2), 0, item->GetValue(1), 0, true);
									break;

								case APPLY_CAST_SPEED:
									AddAffect(AFFECT_CAST_SPEED, POINT_CASTING_SPEED, item->GetValue(2), 0, item->GetValue(1), 0, true);
									break;

								case APPLY_ATT_GRADE_BONUS:
									AddAffect(AFFECT_ATT_GRADE, POINT_ATT_GRADE_BONUS, 
											item->GetValue(2), 0, item->GetValue(1), 0, true);
									break;

								case APPLY_DEF_GRADE_BONUS:
									AddAffect(AFFECT_DEF_GRADE, POINT_DEF_GRADE_BONUS,
											item->GetValue(2), 0, item->GetValue(1), 0, true);
									break;
							}
						}

						if (GetDungeon())
							GetDungeon()->UsePotion(this);

						if (GetWarMap())
							GetWarMap()->UsePotion(this, item);

						item->SetCount(item->GetCount() - 1);
						break;

					case USE_TALISMAN:
						{
							const int TOWN_PORTAL	= 1;
							const int MEMORY_PORTAL = 2;


							// gm_guild_build, oxevent ë§µì—ì„œ ê·€í™˜ë¶€ ê·€í™˜ê¸°ì–µë¶€ ë¥¼ ì‚¬ìš©ëª»í•˜ê²Œ ë§‰ìŒ
							if (GetMapIndex() == 200 || GetMapIndex() == 113)
							{
								ChatPacket(CHAT_TYPE_INFO, LC_TEXT("í˜„ì¬ ìœ„ì¹˜ì—ì„œ ì‚¬ìš©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
								return false;
							}

							if (CArenaManager::instance().IsArenaMap(GetMapIndex()) == true)
							{
								ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ëŒ€ë ¨ ì¤‘ì—ëŠ” ì´ìš©í•  ìˆ˜ ì—†ëŠ” ë¬¼í’ˆì…ë‹ˆë‹¤."));
								return false;
							}

							if (m_pkWarpEvent)
							{
								ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ë™í•  ì¤€ë¹„ê°€ ë˜ì–´ìˆìŒìœ¼ë¡œ ê·€í™˜ë¶€ë¥¼ ì‚¬ìš©í• ìˆ˜ ì—†ìŠµë‹ˆë‹¤"));
								return false;
							}

							// CONSUME_LIFE_WHEN_USE_WARP_ITEM
							int consumeLife = CalculateConsume(this);

							if (consumeLife < 0)
								return false;
							// END_OF_CONSUME_LIFE_WHEN_USE_WARP_ITEM

							if (item->GetValue(0) == TOWN_PORTAL) // ê·€í™˜ë¶€
							{
								if (item->GetSocket(0) == 0)
								{
									if (!GetDungeon())
										if (!GiveRecallItem(item))
											return false;

									PIXEL_POSITION posWarp;

									if (SECTREE_MANAGER::instance().GetRecallPositionByEmpire(GetMapIndex(), GetEmpire(), posWarp))
									{
										// CONSUME_LIFE_WHEN_USE_WARP_ITEM
										PointChange(POINT_HP, -consumeLife, false);
										// END_OF_CONSUME_LIFE_WHEN_USE_WARP_ITEM

										WarpSet(posWarp.x, posWarp.y);
									}
									else
									{
										sys_err("CHARACTER::UseItem : cannot find spawn position (name %s, %d x %d)", GetName(), GetX(), GetY());
									}
								}
								else
								{
									if (test_server)
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì›ë˜ ìœ„ì¹˜ë¡œ ë³µê·€"));	

									ProcessRecallItem(item);
								}
							}
							else if (item->GetValue(0) == MEMORY_PORTAL) // ê·€í™˜ê¸°ì–µë¶€
							{
								if (item->GetSocket(0) == 0)
								{
									if (GetDungeon())
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë˜ì „ ì•ˆì—ì„œëŠ” %s%s ì‚¬ìš©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."),
												item->GetName(),
												g_iUseLocale ? "" : (under_han(item->GetName()) ? LC_TEXT("ì„") : LC_TEXT("ë¥¼")));
										return false;
									}

									if (!GiveRecallItem(item))
										return false;
								}
								else
								{
									// CONSUME_LIFE_WHEN_USE_WARP_ITEM
									PointChange(POINT_HP, -consumeLife, false);
									// END_OF_CONSUME_LIFE_WHEN_USE_WARP_ITEM

									ProcessRecallItem(item);
								}
							}
						}
						break;

					case USE_TUNING:
					case USE_DETACHMENT:
						{
							LPITEM item2;

							if (!IsValidItemPosition(DestCell) || !(item2 = GetItem(DestCell)))
								return false;

							if (item2->IsExchanging())
								return false;
	
							if (item2->IsEquipped()) // Fix
								return false;
	
							if (item2->GetVnum() >= 28330 && item2->GetVnum() <= 28343) // ì˜ì„+3
							{
								ChatPacket(CHAT_TYPE_INFO, LC_TEXT("+3 ì˜ì„ì€ ì´ ì•„ì´í…œìœ¼ë¡œ ê°œëŸ‰í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤"));
								return false;
							}
							
							if (item2->GetVnum() >= 28430 && item2->GetVnum() <= 28443)  // ì˜ì„+4
							{
								if (item->GetVnum() == 71056) // ì²­ë£¡ì˜ìˆ¨ê²°
								{
									RefineItem(item, item2);
								}
								else
								{
									ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì˜ì„ì€ ì´ ì•„ì´í…œìœ¼ë¡œ ê°œëŸ‰í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤"));
								}
							}
							else
							{
								RefineItem(item, item2);
							}
						}
						break;

						//  ACCESSORY_REFINE & ADD/CHANGE_ATTRIBUTES
					case USE_PUT_INTO_BELT_SOCKET:
					case USE_PUT_INTO_RING_SOCKET:
					case USE_PUT_INTO_ACCESSORY_SOCKET:
					case USE_ADD_ACCESSORY_SOCKET:
					case USE_CLEAN_SOCKET:
					case USE_CHANGE_ATTRIBUTE:
					case USE_CHANGE_ATTRIBUTE2 :
					case USE_ADD_ATTRIBUTE:
					case USE_ADD_ATTRIBUTE2:
						{
							LPITEM item2;
							if (!IsValidItemPosition(DestCell) || !(item2 = GetItem(DestCell)))
								return false;

							if (item2->IsEquipped())
							{
								BuffOnAttr_RemoveBuffsFromItem(item2);
							}

							// [NOTE] ì½”ìŠ¤íŠ¬ ì•„ì´í…œì—ëŠ” ì•„ì´í…œ ìµœì´ˆ ìƒì„±ì‹œ ëœë¤ ì†ì„±ì„ ë¶€ì—¬í•˜ë˜, ì¬ê²½ì¬ê°€ ë“±ë“±ì€ ë§‰ì•„ë‹¬ë¼ëŠ” ìš”ì²­ì´ ìˆì—ˆìŒ.
							// ì›ë˜ ANTI_CHANGE_ATTRIBUTE ê°™ì€ ì•„ì´í…œ Flagë¥¼ ì¶”ê°€í•˜ì—¬ ê¸°íš ë ˆë²¨ì—ì„œ ìœ ì—°í•˜ê²Œ ì»¨íŠ¸ë¡¤ í•  ìˆ˜ ìˆë„ë¡ í•  ì˜ˆì •ì´ì—ˆìœ¼ë‚˜
							// ê·¸ë”´ê±° í•„ìš”ì—†ìœ¼ë‹ˆ ë‹¥ì¹˜ê³  ë¹¨ë¦¬ í•´ë‹¬ë˜ì„œ ê·¸ëƒ¥ ì—¬ê¸°ì„œ ë§‰ìŒ... -_-
							if (ITEM_COSTUME == item2->GetType())
							{
								ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì†ì„±ì„ ë³€ê²½í•  ìˆ˜ ì—†ëŠ” ì•„ì´í…œì…ë‹ˆë‹¤."));
								return false;
							}

							if (item2->IsExchanging())
								return false;
							
							if (item2->IsEquipped()) // Fix
								return false;

							switch (item->GetSubType())
							{
								case USE_CLEAN_SOCKET:
									{
										int i;
										for (i = 0; i < ITEM_SOCKET_MAX_NUM; ++i)
										{
											if (item2->GetSocket(i) == ITEM_BROKEN_METIN_VNUM)
												break;
										}

										if (i == ITEM_SOCKET_MAX_NUM)
										{
											ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì²­ì†Œí•  ì„ì´ ë°•í˜€ìˆì§€ ì•ŠìŠµë‹ˆë‹¤."));
											return false;
										}

										int j = 0;

										for (i = 0; i < ITEM_SOCKET_MAX_NUM; ++i)
										{
											if (item2->GetSocket(i) != ITEM_BROKEN_METIN_VNUM && item2->GetSocket(i) != 0)
												item2->SetSocket(j++, item2->GetSocket(i));
										}

										for (; j < ITEM_SOCKET_MAX_NUM; ++j)
										{
											if (item2->GetSocket(j) > 0)
												item2->SetSocket(j, 1);
										}

										{
											char buf[21];
											snprintf(buf, sizeof(buf), "%u", item2->GetID());
											LogManager::instance().ItemLog(this, item, "CLEAN_SOCKET", buf);
										}

										item->SetCount(item->GetCount() - 1);

									}
									break;

								case USE_CHANGE_ATTRIBUTE :
									if (item2->GetAttributeSetIndex() == -1)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì†ì„±ì„ ë³€ê²½í•  ìˆ˜ ì—†ëŠ” ì•„ì´í…œì…ë‹ˆë‹¤."));
										return false;
									}

									if (item2->GetAttributeCount() == 0)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë³€ê²½í•  ì†ì„±ì´ ì—†ìŠµë‹ˆë‹¤."));
										return false;
									}

									if (GM_PLAYER == GetGMLevel() && false == test_server)
									{
										//
										// Event Flag ë¥¼ í†µí•´ ì´ì „ì— ì•„ì´í…œ ì†ì„± ë³€ê²½ì„ í•œ ì‹œê°„ìœ¼ë¡œ ë¶€í„° ì¶©ë¶„í•œ ì‹œê°„ì´ í˜ë €ëŠ”ì§€ ê²€ì‚¬í•˜ê³ 
										// ì‹œê°„ì´ ì¶©ë¶„íˆ í˜ë €ë‹¤ë©´ í˜„ì¬ ì†ì„±ë³€ê²½ì— ëŒ€í•œ ì‹œê°„ì„ ì„¤ì •í•´ ì¤€ë‹¤.
										//

										DWORD dwChangeItemAttrCycle = quest::CQuestManager::instance().GetEventFlag(msc_szChangeItemAttrCycleFlag);
										if (dwChangeItemAttrCycle < msc_dwDefaultChangeItemAttrCycle)
											dwChangeItemAttrCycle = msc_dwDefaultChangeItemAttrCycle;

										quest::PC* pPC = quest::CQuestManager::instance().GetPC(GetPlayerID());

										if (pPC)
										{
											DWORD dwNowMin = get_global_time() / 60;

											// DWORD dwLastChangeItemAttrMin = pPC->GetFlag(msc_szLastChangeItemAttrFlag);

											// if (dwLastChangeItemAttrMin + dwChangeItemAttrCycle > dwNowMin)
											// {
												// ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì†ì„±ì„ ë°”ê¾¼ì§€ %dë¶„ ì´ë‚´ì—ëŠ” ë‹¤ì‹œ ë³€ê²½í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤.(%d ë¶„ ë‚¨ìŒ)"),
														// dwChangeItemAttrCycle, dwChangeItemAttrCycle - (dwNowMin - dwLastChangeItemAttrMin));
												// return false;
											// }

											pPC->SetFlag(msc_szLastChangeItemAttrFlag, dwNowMin);
										}
									}

									if (item->GetSubType() == USE_CHANGE_ATTRIBUTE2)
									{
										int aiChangeProb[ITEM_ATTRIBUTE_MAX_LEVEL] = 
										{
											0, 0, 30, 40, 3
										};

										item2->ChangeAttribute(aiChangeProb);
									}
									else if (item->GetVnum() == 76014)
									{
										int aiChangeProb[ITEM_ATTRIBUTE_MAX_LEVEL] = 
										{
											0, 10, 50, 39, 1
										};

										item2->ChangeAttribute(aiChangeProb);
									}

									else
									{
										// ì—°ì¬ê²½ íŠ¹ìˆ˜ì²˜ë¦¬
										// ì ˆëŒ€ë¡œ ì—°ì¬ê°€ ì¶”ê°€ ì•ˆë ê±°ë¼ í•˜ì—¬ í•˜ë“œ ì½”ë”©í•¨.
										if (item->GetVnum() == 71151 || item->GetVnum() == 76023)
										{
											if ((item2->GetType() == ITEM_WEAPON)
												|| (item2->GetType() == ITEM_ARMOR && item2->GetSubType() == ARMOR_BODY))
											{
												bool bCanUse = true;
												for (int i = 0; i < ITEM_LIMIT_MAX_NUM; ++i)
												{
													if (item2->GetLimitType(i) == LIMIT_LEVEL && item2->GetLimitValue(i) > 40)
													{
														bCanUse = false;
														break;
													}
												}
												if (false == bCanUse)
												{
													ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì ìš© ë ˆë²¨ë³´ë‹¤ ë†’ì•„ ì‚¬ìš©ì´ ë¶ˆê°€ëŠ¥í•©ë‹ˆë‹¤."));
													break;
												}
											}
											else
											{
												ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë¬´ê¸°ì™€ ê°‘ì˜·ì—ë§Œ ì‚¬ìš© ê°€ëŠ¥í•©ë‹ˆë‹¤."));
												break;
											}
										}
										item2->ChangeAttribute();
									}

									ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì†ì„±ì„ ë³€ê²½í•˜ì˜€ìŠµë‹ˆë‹¤."));
									{
										char buf[21];
										snprintf(buf, sizeof(buf), "%u", item2->GetID());
										LogManager::instance().ItemLog(this, item, "CHANGE_ATTRIBUTE", buf);
									}

									item->SetCount(item->GetCount() - 1);
									break;

								case USE_ADD_ATTRIBUTE :
									if (item2->GetAttributeSetIndex() == -1)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì†ì„±ì„ ë³€ê²½í•  ìˆ˜ ì—†ëŠ” ì•„ì´í…œì…ë‹ˆë‹¤."));
										return false;
									}

									if (item2->GetAttributeCount() < 4)
									{
										// ì—°ì¬ê°€ íŠ¹ìˆ˜ì²˜ë¦¬
										// ì ˆëŒ€ë¡œ ì—°ì¬ê°€ ì¶”ê°€ ì•ˆë ê±°ë¼ í•˜ì—¬ í•˜ë“œ ì½”ë”©í•¨.
										if (item->GetVnum() == 71152 || item->GetVnum() == 76024)
										{
											if ((item2->GetType() == ITEM_WEAPON)
												|| (item2->GetType() == ITEM_ARMOR && item2->GetSubType() == ARMOR_BODY))
											{
												bool bCanUse = true;
												for (int i = 0; i < ITEM_LIMIT_MAX_NUM; ++i)
												{
													if (item2->GetLimitType(i) == LIMIT_LEVEL && item2->GetLimitValue(i) > 40)
													{
														bCanUse = false;
														break;
													}
												}
												if (false == bCanUse)
												{
													ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì ìš© ë ˆë²¨ë³´ë‹¤ ë†’ì•„ ì‚¬ìš©ì´ ë¶ˆê°€ëŠ¥í•©ë‹ˆë‹¤."));
													break;
												}
											}
											else
											{
												ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë¬´ê¸°ì™€ ê°‘ì˜·ì—ë§Œ ì‚¬ìš© ê°€ëŠ¥í•©ë‹ˆë‹¤."));
												break;
											}
										}
										char buf[21];
										snprintf(buf, sizeof(buf), "%u", item2->GetID());

										if (number(1, 100) <= aiItemAttributeAddPercent[item2->GetAttributeCount()])
										{
											item2->AddAttribute();
											ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì†ì„± ì¶”ê°€ì— ì„±ê³µí•˜ì˜€ìŠµë‹ˆë‹¤."));

											int iAddedIdx = item2->GetAttributeCount() - 1;
											LogManager::instance().ItemLog(
													GetPlayerID(), 
													item2->GetAttributeType(iAddedIdx),
													item2->GetAttributeValue(iAddedIdx),
													item->GetID(), 
													"ADD_ATTRIBUTE_SUCCESS",
													buf,
													GetDesc()->GetHostName(),
													item->GetOriginalVnum());
										}
										else
										{
											ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì†ì„± ì¶”ê°€ì— ì‹¤íŒ¨í•˜ì˜€ìŠµë‹ˆë‹¤."));
											LogManager::instance().ItemLog(this, item, "ADD_ATTRIBUTE_FAIL", buf);
										}

										item->SetCount(item->GetCount() - 1);
									}
									else
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë”ì´ìƒ ì´ ì•„ì´í…œì„ ì´ìš©í•˜ì—¬ ì†ì„±ì„ ì¶”ê°€í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
									}
									break;

								case USE_ADD_ATTRIBUTE2 :
									// ì¶•ë³µì˜ êµ¬ìŠ¬ 
									// ì¬ê°€ë¹„ì„œë¥¼ í†µí•´ ì†ì„±ì„ 4ê°œ ì¶”ê°€ ì‹œí‚¨ ì•„ì´í…œì— ëŒ€í•´ì„œ í•˜ë‚˜ì˜ ì†ì„±ì„ ë” ë¶™ì—¬ì¤€ë‹¤.
									if (item2->GetAttributeSetIndex() == -1)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì†ì„±ì„ ë³€ê²½í•  ìˆ˜ ì—†ëŠ” ì•„ì´í…œì…ë‹ˆë‹¤."));
										return false;
									}

									// ì†ì„±ì´ ì´ë¯¸ 4ê°œ ì¶”ê°€ ë˜ì—ˆì„ ë•Œë§Œ ì†ì„±ì„ ì¶”ê°€ ê°€ëŠ¥í•˜ë‹¤.
									if (item2->GetAttributeCount() == 4)
									{
										char buf[21];
										snprintf(buf, sizeof(buf), "%u", item2->GetID());

										if (number(1, 100) <= aiItemAttributeAddPercent[item2->GetAttributeCount()])
										{
											item2->AddAttribute();
											ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì†ì„± ì¶”ê°€ì— ì„±ê³µí•˜ì˜€ìŠµë‹ˆë‹¤."));

											int iAddedIdx = item2->GetAttributeCount() - 1;
											LogManager::instance().ItemLog(
													GetPlayerID(),
													item2->GetAttributeType(iAddedIdx),
													item2->GetAttributeValue(iAddedIdx),
													item->GetID(),
													"ADD_ATTRIBUTE2_SUCCESS",
													buf,
													GetDesc()->GetHostName(),
													item->GetOriginalVnum());
										}
										else
										{
											ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì†ì„± ì¶”ê°€ì— ì‹¤íŒ¨í•˜ì˜€ìŠµë‹ˆë‹¤."));
											LogManager::instance().ItemLog(this, item, "ADD_ATTRIBUTE2_FAIL", buf);
										}

										item->SetCount(item->GetCount() - 1);
									}
									else if (item2->GetAttributeCount() == 5)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë” ì´ìƒ ì´ ì•„ì´í…œì„ ì´ìš©í•˜ì—¬ ì†ì„±ì„ ì¶”ê°€í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
									}
									else if (item2->GetAttributeCount() < 4)
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë¨¼ì € ì¬ê°€ë¹„ì„œë¥¼ ì´ìš©í•˜ì—¬ ì†ì„±ì„ ì¶”ê°€ì‹œì¼œ ì£¼ì„¸ìš”."));
									}
									else
									{
										// wtf ?!
										sys_err("ADD_ATTRIBUTE2 : Item has wrong AttributeCount(%d)", item2->GetAttributeCount());
									}
									break;

								case USE_ADD_ACCESSORY_SOCKET:
									{
										char buf[21];
										snprintf(buf, sizeof(buf), "%u", item2->GetID());

										if (item2->IsAccessoryForSocket())
										{
											if (item2->GetAccessorySocketMaxGrade() < ITEM_ACCESSORY_SOCKET_MAX_NUM)
											{
												if (number(1, 100) <= 50)
												{
													item2->SetAccessorySocketMaxGrade(item2->GetAccessorySocketMaxGrade() + 1);
													ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì†Œì¼“ì´ ì„±ê³µì ìœ¼ë¡œ ì¶”ê°€ë˜ì—ˆìŠµë‹ˆë‹¤."));
													LogManager::instance().ItemLog(this, item, "ADD_SOCKET_SUCCESS", buf);
												}
												else
												{
													ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì†Œì¼“ ì¶”ê°€ì— ì‹¤íŒ¨í•˜ì˜€ìŠµë‹ˆë‹¤."));
													LogManager::instance().ItemLog(this, item, "ADD_SOCKET_FAIL", buf);
												}

												item->SetCount(item->GetCount() - 1);
											}
											else
											{
												ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ ì•¡ì„¸ì„œë¦¬ì—ëŠ” ë”ì´ìƒ ì†Œì¼“ì„ ì¶”ê°€í•  ê³µê°„ì´ ì—†ìŠµë‹ˆë‹¤."));
											}
										}
										else
										{
											ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ ì•„ì´í…œìœ¼ë¡œ ì†Œì¼“ì„ ì¶”ê°€í•  ìˆ˜ ì—†ëŠ” ì•„ì´í…œì…ë‹ˆë‹¤."));
										}
									}
									break;

								case USE_PUT_INTO_BELT_SOCKET:
								case USE_PUT_INTO_ACCESSORY_SOCKET:
									if (item2->IsAccessoryForSocket() && item->CanPutInto(item2))
									{
										char buf[21];
										snprintf(buf, sizeof(buf), "%u", item2->GetID());

										if (item2->GetAccessorySocketGrade() < item2->GetAccessorySocketMaxGrade())
										{
											if (number(1, 100) <= aiAccessorySocketPutPct[item2->GetAccessorySocketGrade()])
											{
												item2->SetAccessorySocketGrade(item2->GetAccessorySocketGrade() + 1);
												ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì¥ì°©ì— ì„±ê³µí•˜ì˜€ìŠµë‹ˆë‹¤."));
												LogManager::instance().ItemLog(this, item, "PUT_SOCKET_SUCCESS", buf);
											}
											else
											{
												ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì¥ì°©ì— ì‹¤íŒ¨í•˜ì˜€ìŠµë‹ˆë‹¤."));
												LogManager::instance().ItemLog(this, item, "PUT_SOCKET_FAIL", buf);
											}

											item->SetCount(item->GetCount() - 1);
										}
										else
										{
											if (item2->GetAccessorySocketMaxGrade() == 0)
												ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë¨¼ì € ë‹¤ì´ì•„ëª¬ë“œë¡œ ì•…ì„¸ì„œë¦¬ì— ì†Œì¼“ì„ ì¶”ê°€í•´ì•¼í•©ë‹ˆë‹¤."));
											else if (item2->GetAccessorySocketMaxGrade() < ITEM_ACCESSORY_SOCKET_MAX_NUM)
											{
												ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ ì•¡ì„¸ì„œë¦¬ì—ëŠ” ë”ì´ìƒ ì¥ì°©í•  ì†Œì¼“ì´ ì—†ìŠµë‹ˆë‹¤."));
												ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë‹¤ì´ì•„ëª¬ë“œë¡œ ì†Œì¼“ì„ ì¶”ê°€í•´ì•¼í•©ë‹ˆë‹¤."));
											}
											else
												ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ ì•¡ì„¸ì„œë¦¬ì—ëŠ” ë”ì´ìƒ ë³´ì„ì„ ì¥ì°©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
										}
									}
									else
									{
										ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ ì•„ì´í…œì„ ì¥ì°©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
									}
									break;
							}
							if (item2->IsEquipped())
							{
								BuffOnAttr_AddBuffsFromItem(item2);
							}
						}
						break;
						//  END_OF_ACCESSORY_REFINE & END_OF_ADD_ATTRIBUTES & END_OF_CHANGE_ATTRIBUTES

					case USE_BAIT:
						{

							if (m_pkFishingEvent)
							{
								ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë‚šì‹œ ì¤‘ì— ë¯¸ë¼ë¥¼ ê°ˆì•„ë¼ìš¸ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
								return false;
							}

							LPITEM weapon = GetWear(WEAR_WEAPON);

							if (!weapon || weapon->GetType() != ITEM_ROD)
								return false;

							if (weapon->GetSocket(2))
							{
								ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ë¯¸ ê½‚í˜€ìˆë˜ ë¯¸ë¼ë¥¼ ë¹¼ê³  %së¥¼ ë¼ì›ë‹ˆë‹¤."), item->GetName());
							}
							else
							{
								ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë‚šì‹œëŒ€ì— %së¥¼ ë¯¸ë¼ë¡œ ë¼ì›ë‹ˆë‹¤."), item->GetName());
							}

							weapon->SetSocket(2, item->GetValue(0));
							item->SetCount(item->GetCount() - 1);
						}
						break;

					case USE_MOVE:
					case USE_TREASURE_BOX:
					case USE_MONEYBAG:
						break;

#ifdef ENABLE_TITLE_SYSTEM
					case USE_TITLE:
					{
						int iDuration = item->GetValue(1);
						if (iDuration <= 0)
							iDuration = item->GetValue(2);

						const DWORD dwVnum = item->GetVnum();
						const int iBundle0[5] = { 1000, 1001, 1002, 1003, 1004 };
						const int iBundle1[5] = { 1005, 2000, 2001, 2002, 2003 };
						const int iBundle2[5] = { 3000, 3001, 3002, 3003, 3004 };

						const int* pBundle = NULL;
						int iBundleCount = 0;
						switch (dwVnum)
						{
							case 57000: pBundle = iBundle0; iBundleCount = 5; break;
							case 57001: pBundle = iBundle1; iBundleCount = 5; break;
							case 57002: pBundle = iBundle2; iBundleCount = 5; break;
						}

						if (pBundle && iBundleCount > 0)
						{
							int iUnlockedCount = 0;
							for (int i = 0; i < iBundleCount; ++i)
							{
								const int iTitleID = pBundle[i];
								char szOwned[64];
								char szExpire[64];
								snprintf(szOwned, sizeof(szOwned), "title_system.owned.%d", iTitleID);
								snprintf(szExpire, sizeof(szExpire), "title_system.expire.%d", iTitleID);

								if (GetQuestFlag(szOwned) > 0)
									continue;

								SetQuestFlag(szOwned, 1);
								if (iDuration > 0)
									SetQuestFlag(szExpire, get_global_time() + iDuration);
								else
									SetQuestFlag(szExpire, 0);
								ChatPacket(CHAT_TYPE_COMMAND, "TitleSyncAdd %d", iTitleID);
								++iUnlockedCount;
							}

							if (iUnlockedCount <= 0)
							{
								ChatPacket(CHAT_TYPE_INFO, "You already own all titles in this package.");
								break;
							}

							item->SetCount(item->GetCount() - 1);
							ChatPacket(CHAT_TYPE_INFO, "Title package unlocked: %d new titles.", iUnlockedCount);
							break;
						}

						// Tek unvan sertifikasi
						int iTitleID = item->GetValue(0);
						if (iTitleID <= 0)
						{
							ChatPacket(CHAT_TYPE_INFO, "Invalid title certificate.");
							break;
						}

						char szOwned[64];
						char szExpire[64];
						snprintf(szOwned, sizeof(szOwned), "title_system.owned.%d", iTitleID);
						snprintf(szExpire, sizeof(szExpire), "title_system.expire.%d", iTitleID);

						if (GetQuestFlag(szOwned) > 0)
						{
							ChatPacket(CHAT_TYPE_INFO, "You already own this title.");
							break;
						}

						// FIX-2: active-unvan guard — do_title equip ile tutarli davranis
						const int iActiveTitle = GetQuestFlag("title_system.active");
						if (iActiveTitle > 0 && iActiveTitle != iTitleID)
						{
							ChatPacket(CHAT_TYPE_INFO, "Remove your current title first.");
							break;
						}

						SetQuestFlag(szOwned, 1);
						if (iDuration > 0)
							SetQuestFlag(szExpire, get_global_time() + iDuration);
						else
							SetQuestFlag(szExpire, 0);

						SetQuestFlag("title_system.active", iTitleID);

						// FIX-1b: sertifika kullaniminda client UI aninda guncelle
						ChatPacket(CHAT_TYPE_COMMAND, "TitleSyncAdd %d", iTitleID);
						ChatPacket(CHAT_TYPE_COMMAND, "TitleSyncActive %d", iTitleID);

						ChatPacket(CHAT_TYPE_INFO, "Title unlocked and equipped (ID: %d)", iTitleID);
						item->SetCount(item->GetCount() - 1);
					}
					break;
#endif // ENABLE_TITLE_SYSTEM

					// MR-12: Overwrite lower value affects
					case USE_AFFECT :
						{
							const DWORD affectType = item->GetValue(0);
							const BYTE applyType = aApplyInfo[item->GetValue(1)].bPointType;
							const long applyValue = item->GetValue(2);
							const long applyDuration = item->GetValue(3);

							CAffect* existing = FindAffect(affectType, applyType);

							if (existing)
							{
								if (applyValue <= existing->lApplyValue)
								{
									ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ë¯¸ íš¨ê³¼ê°€ ê±¸ë ¤ ìˆìŠµë‹ˆë‹¤."));
									break;
								}

								RemoveAffect(existing);
							}

							AddAffect(affectType, applyType, applyValue, 0, applyDuration, 0, false);
							item->SetCount(item->GetCount() - 1);
						}
						break;
					// MR-12: -- END OF -- Overwrite lower value affects

					case USE_CREATE_STONE:
						AutoGiveItem(number(28000, 28013));
						item->SetCount(item->GetCount() - 1);
						break;

					// ë¬¼ì•½ ì œì¡° ìŠ¤í‚¬ìš© ë ˆì‹œí”¼ ì²˜ë¦¬	
					case USE_RECIPE :
						{
							LPITEM pSource1 = FindSpecifyItem(item->GetValue(1));
							DWORD dwSourceCount1 = item->GetValue(2);

							LPITEM pSource2 = FindSpecifyItem(item->GetValue(3));
							DWORD dwSourceCount2 = item->GetValue(4);

							if (dwSourceCount1 != 0)
							{
								if (pSource1 == NULL)
								{
									ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë¬¼ì•½ ì¡°í•©ì„ ìœ„í•œ ì¬ë£Œê°€ ë¶€ì¡±í•©ë‹ˆë‹¤."));
									return false;
								}
							}

							if (dwSourceCount2 != 0)
							{
								if (pSource2 == NULL)
								{
									ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë¬¼ì•½ ì¡°í•©ì„ ìœ„í•œ ì¬ë£Œê°€ ë¶€ì¡±í•©ë‹ˆë‹¤."));
									return false;
								}
							}

							if (pSource1 != NULL)
							{
								if (pSource1->GetCount() < dwSourceCount1)
								{
									ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì¬ë£Œ(%s)ê°€ ë¶€ì¡±í•©ë‹ˆë‹¤."), pSource1->GetName());
									return false;
								}

								pSource1->SetCount(pSource1->GetCount() - dwSourceCount1);
							}

							if (pSource2 != NULL)
							{
								if (pSource2->GetCount() < dwSourceCount2)
								{
									ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì¬ë£Œ(%s)ê°€ ë¶€ì¡±í•©ë‹ˆë‹¤."), pSource2->GetName());
									return false;
								}

								pSource2->SetCount(pSource2->GetCount() - dwSourceCount2);
							}

							LPITEM pBottle = FindSpecifyItem(50901);

							if (!pBottle || pBottle->GetCount() < 1)
							{
								ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë¹ˆ ë³‘ì´ ëª¨ìë¦…ë‹ˆë‹¤."));
								return false;
							}

							pBottle->SetCount(pBottle->GetCount() - 1);

							if (number(1, 100) > item->GetValue(5))
							{
								ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë¬¼ì•½ ì œì¡°ì— ì‹¤íŒ¨í–ˆìŠµë‹ˆë‹¤."));
								return false;
							}

							AutoGiveItem(item->GetValue(0));
						}
						break;
#ifdef ENABLE_NPC_LOCATION_HELPER
				case USE_MAP:
					{
						if (item->GetVnum() >= 79800 && item->GetVnum() <= 79822)
						{
							if (CNpcLocationHelperManager::instance().UnlockMapByItem(this, item->GetVnum(), item->GetValue(0)))
								item->SetCount(item->GetCount() - 1);
							return true;
						}
					}
					break;
#endif // ENABLE_NPC_LOCATION_HELPER
				}
			}
			break;

		case ITEM_METIN:
			{
				LPITEM item2;

				if (!IsValidItemPosition(DestCell) || !(item2 = GetItem(DestCell)))
					return false;

				if (item2->IsExchanging())
					return false;
				
				if (item2->IsEquipped()) //Fix
					return false;

				if (item2->GetType() == ITEM_PICK) return false;
				if (item2->GetType() == ITEM_ROD) return false;

				int i;

				for (i = 0; i < ITEM_SOCKET_MAX_NUM; ++i)
				{
					DWORD dwVnum;   

					if ((dwVnum = item2->GetSocket(i)) <= 2)
						continue;

					TItemTable * p = ITEM_MANAGER::instance().GetTable(dwVnum);

					if (!p)
						continue;

					if (item->GetValue(5) == p->alValues[5])
					{
						ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê°™ì€ ì¢…ë¥˜ì˜ ë©”í‹´ì„ì€ ì—¬ëŸ¬ê°œ ë¶€ì°©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
						return false;
					}
				}

				if (item2->GetType() == ITEM_ARMOR)
				{
					if (!IS_SET(item->GetWearFlag(), WEARABLE_BODY) || !IS_SET(item2->GetWearFlag(), WEARABLE_BODY))
					{
						ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ ë©”í‹´ì„ì€ ì¥ë¹„ì— ë¶€ì°©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
						return false;
					}
				}
				else if (item2->GetType() == ITEM_WEAPON)
				{
					if (!IS_SET(item->GetWearFlag(), WEARABLE_WEAPON))
					{
						ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ ë©”í‹´ì„ì€ ë¬´ê¸°ì— ë¶€ì°©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
						return false;
					}
				}
				else
				{
					ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë¶€ì°©í•  ìˆ˜ ìˆëŠ” ìŠ¬ë¡¯ì´ ì—†ìŠµë‹ˆë‹¤."));
					return false;
				}

				for (i = 0; i < ITEM_SOCKET_MAX_NUM; ++i)
					if (item2->GetSocket(i) >= 1 && item2->GetSocket(i) <= 2 && item2->GetSocket(i) >= item->GetValue(2))
					{
						// ì„ í™•ë¥ 
						if (number(1, 100) <= 30)
						{
							ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë©”í‹´ì„ ë¶€ì°©ì— ì„±ê³µí•˜ì˜€ìŠµë‹ˆë‹¤."));
							item2->SetSocket(i, item->GetVnum());
						}
						else
						{
							ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë©”í‹´ì„ ë¶€ì°©ì— ì‹¤íŒ¨í•˜ì˜€ìŠµë‹ˆë‹¤."));
							item2->SetSocket(i, ITEM_BROKEN_METIN_VNUM);
						}

						LogManager::instance().ItemLog(this, item2, "SOCKET", item->GetName());
						ITEM_MANAGER::instance().RemoveItem(item, "REMOVE (METIN)");
						break;
					}

				if (i == ITEM_SOCKET_MAX_NUM)
					ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë¶€ì°©í•  ìˆ˜ ìˆëŠ” ìŠ¬ë¡¯ì´ ì—†ìŠµë‹ˆë‹¤."));
			}
			break;

		case ITEM_AUTOUSE:
		case ITEM_MATERIAL:
		case ITEM_SPECIAL:
		case ITEM_TOOL:
		case ITEM_LOTTERY:
			break;

		case ITEM_TOTEM:
			{
				if (!item->IsEquipped())
					EquipItem(item);
			}
			break;

		case ITEM_BLEND:
			// ìƒˆë¡œìš´ ì•½ì´ˆë“¤
			sys_log(0,"ITEM_BLEND!!");
			if (Blend_Item_find(item->GetVnum()))
			{
				int		affect_type		= AFFECT_BLEND;
				if (item->GetSocket(0) >= _countof(aApplyInfo))
				{
					sys_err ("INVALID BLEND ITEM(id : %d, vnum : %d). APPLY TYPE IS %d.", item->GetID(), item->GetVnum(), item->GetSocket(0));
					return false;
				}
				int		apply_type		= aApplyInfo[item->GetSocket(0)].bPointType;
				int		apply_value		= item->GetSocket(1);
				int		apply_duration	= item->GetSocket(2);
				
				if (FindAffect(affect_type, apply_type))
				{
					ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ë¯¸ íš¨ê³¼ê°€ ê±¸ë ¤ ìˆìŠµë‹ˆë‹¤."));
				}
				else
				{
					if (FindAffect(AFFECT_EXP_BONUS_EURO_FREE, POINT_RESIST_MAGIC))
					{
						ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ë¯¸ íš¨ê³¼ê°€ ê±¸ë ¤ ìˆìŠµë‹ˆë‹¤."));
					}
					else
					{
						AddAffect(affect_type, apply_type, apply_value, 0, apply_duration, 0, false);
						item->SetCount(item->GetCount() - 1);
					}
				}
			}
			break;
		case ITEM_EXTRACT:
			{
				LPITEM pDestItem = GetItem(DestCell);
				if (NULL == pDestItem)
				{
					return false;
				}
				switch (item->GetSubType())
				{
				case EXTRACT_DRAGON_SOUL:
					if (pDestItem->IsDragonSoul())
					{
						return DSManager::instance().PullOut(this, NPOS, pDestItem, item);
					}
					return false;
				case EXTRACT_DRAGON_HEART:
					if (pDestItem->IsDragonSoul())
					{
						return DSManager::instance().ExtractDragonHeart(this, pDestItem, item);
					}
					return false;
				default:
					return false;
				}
			}
			break;

		case ITEM_NONE:
			sys_err("Item type NONE %s", item->GetName());
			break;

		default:
			sys_log(0, "UseItemEx: Unknown type %s %d", item->GetName(), item->GetType());
			return false;
	}

	return true;
}

int g_nPortalLimitTime = 10;

bool CHARACTER::UseItem(TItemPos Cell, TItemPos DestCell)
{
	WORD wCell = Cell.cell;
	BYTE window_type = Cell.window_type;
	WORD wDestCell = DestCell.cell;
	BYTE bDestInven = DestCell.window_type;
	LPITEM item;

	if (!CanHandleItem())
		return false;

	if (!IsValidItemPosition(Cell) || !(item = GetItem(Cell)))
			return false;

	sys_log(0, "%s: USE_ITEM %s (inven %d, cell: %d)", GetName(), item->GetName(), window_type, wCell);

	if (item->IsExchanging())
		return false;

	if (!item->CanUsedBy(this))
	{
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("êµ°ì§ì´ ë§ì§€ì•Šì•„ ì´ ì•„ì´í…œì„ ì‚¬ìš©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		return false;
	}

	if (IsStun())
		return false;

	if (false == FN_check_item_sex(this, item))
	{
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì„±ë³„ì´ ë§ì§€ì•Šì•„ ì´ ì•„ì´í…œì„ ì‚¬ìš©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		return false;
	}

	//PREVENT_TRADE_WINDOW
	if (IS_SUMMON_ITEM(item->GetVnum()))
	{
		if (false == IS_SUMMONABLE_ZONE(GetMapIndex()))
		{
			ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì‚¬ìš©í• ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
			return false;
		}

		// ê²½í˜¼ë°˜ì§€ ì‚¬ìš©ì§€ ìƒëŒ€ë°©ì´ SUMMONABLE_ZONEì— ìˆëŠ”ê°€ëŠ” WarpToPC()ì—ì„œ ì²´í¬
		
		//ì‚¼ê±°ë¦¬ ê´€ë ¤ ë§µì—ì„œëŠ” ê·€í™˜ë¶€ë¥¼ ë§‰ì•„ë²„ë¦°ë‹¤.
		if (CThreeWayWar::instance().IsThreeWayWarMapIndex(GetMapIndex()))
		{
			ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì‚¼ê±°ë¦¬ ì „íˆ¬ ì°¸ê°€ì¤‘ì—ëŠ” ê·€í™˜ë¶€,ê·€í™˜ê¸°ì–µë¶€ë¥¼ ì‚¬ìš©í• ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
			return false;
		}
		int iPulse = thecore_pulse();

		//ì°½ê³  ì—°í›„ ì²´í¬
		if (iPulse - GetSafeboxLoadTime() < PASSES_PER_SEC(g_nPortalLimitTime))
		{
			ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì°½ê³ ë¥¼ ì—°í›„ %dì´ˆ ì´ë‚´ì—ëŠ” ê·€í™˜ë¶€,ê·€í™˜ê¸°ì–µë¶€ë¥¼ ì‚¬ìš©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."), g_nPortalLimitTime);

			if (test_server)
				ChatPacket(CHAT_TYPE_INFO, "[TestOnly]Pulse %d LoadTime %d PASS %d", iPulse, GetSafeboxLoadTime(), PASSES_PER_SEC(g_nPortalLimitTime));
			return false; 
		}

		//ê±°ë˜ê´€ë ¨ ì°½ ì²´í¬
		if (GetExchange() || GetMyShop() || GetShopOwner() || IsOpenSafebox() || IsCubeOpen())
		{
			ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê±°ë˜ì°½,ì°½ê³  ë“±ì„ ì—° ìƒíƒœì—ì„œëŠ” ê·€í™˜ë¶€,ê·€í™˜ê¸°ì–µë¶€ ë¥¼ ì‚¬ìš©í• ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
			return false;
		}

		//PREVENT_REFINE_HACK
		//ê°œëŸ‰í›„ ì‹œê°„ì²´í¬ 
		{
			if (iPulse - GetRefineTime() < PASSES_PER_SEC(g_nPortalLimitTime))
			{
				ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì•„ì´í…œ ê°œëŸ‰í›„ %dì´ˆ ì´ë‚´ì—ëŠ” ê·€í™˜ë¶€,ê·€í™˜ê¸°ì–µë¶€ë¥¼ ì‚¬ìš©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."), g_nPortalLimitTime);
				return false;
			}
		}
		//END_PREVENT_REFINE_HACK
		

		//PREVENT_ITEM_COPY
		{
			if (iPulse - GetMyShopTime() < PASSES_PER_SEC(g_nPortalLimitTime))
			{
				ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê°œì¸ìƒì  ì‚¬ìš©í›„ %dì´ˆ ì´ë‚´ì—ëŠ” ê·€í™˜ë¶€,ê·€í™˜ê¸°ì–µë¶€ë¥¼ ì‚¬ìš©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."), g_nPortalLimitTime);
				return false;
			}
			
		}
		//END_PREVENT_ITEM_COPY
		

		//ê·€í™˜ë¶€ ê±°ë¦¬ì²´í¬
		if (item->GetVnum() != 70302)
		{
			PIXEL_POSITION posWarp;

			int x = 0;
			int y = 0;

			double nDist = 0;
			const double nDistant = 5000.0;
			//ê·€í™˜ê¸°ì–µë¶€ 
			if (item->GetVnum() == 22010)
			{
				x = item->GetSocket(0) - GetX();
				y = item->GetSocket(1) - GetY();
			}
			//ê·€í™˜ë¶€
			else if (item->GetVnum() == 22000) 
			{
				SECTREE_MANAGER::instance().GetRecallPositionByEmpire(GetMapIndex(), GetEmpire(), posWarp);

				if (item->GetSocket(0) == 0)
				{
					x = posWarp.x - GetX();
					y = posWarp.y - GetY();
				}
				else
				{
					x = item->GetSocket(0) - GetX();
					y = item->GetSocket(1) - GetY();
				}
			}

			nDist = sqrt(pow((float)x,2) + pow((float)y,2));

			if (nDistant > nDist)
			{
				ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ë™ ë˜ì–´ì§ˆ ìœ„ì¹˜ì™€ ë„ˆë¬´ ê°€ê¹Œì›Œ ê·€í™˜ë¶€ë¥¼ ì‚¬ìš©í• ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));

				if (test_server)
					ChatPacket(CHAT_TYPE_INFO, "PossibleDistant %f nNowDist %f", nDistant,nDist);

				return false;
			}
		}

		//PREVENT_PORTAL_AFTER_EXCHANGE
		//êµí™˜ í›„ ì‹œê°„ì²´í¬
		if (iPulse - GetExchangeTime()  < PASSES_PER_SEC(g_nPortalLimitTime))
		{
			ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê±°ë˜ í›„ %dì´ˆ ì´ë‚´ì—ëŠ” ê·€í™˜ë¶€,ê·€í™˜ê¸°ì–µë¶€ë“±ì„ ì‚¬ìš©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."), g_nPortalLimitTime);
			return false;
		}
		//END_PREVENT_PORTAL_AFTER_EXCHANGE

	}

	//ë³´ë”°ë¦¬ ë¹„ë‹¨ ì‚¬ìš©ì‹œ ê±°ë˜ì°½ ì œí•œ ì²´í¬ 
	if (item->GetVnum() == 50200 | item->GetVnum() == 71049)
	{
		if (GetExchange() || GetMyShop() || GetShopOwner() || IsOpenSafebox() || IsCubeOpen())
		{
			ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê±°ë˜ì°½,ì°½ê³  ë“±ì„ ì—° ìƒíƒœì—ì„œëŠ” ë³´ë”°ë¦¬,ë¹„ë‹¨ë³´ë”°ë¦¬ë¥¼ ì‚¬ìš©í• ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
			return false;
		}

	}
	//END_PREVENT_TRADE_WINDOW

	if (IS_SET(item->GetFlag(), ITEM_FLAG_LOG)) // ì‚¬ìš© ë¡œê·¸ë¥¼ ë‚¨ê¸°ëŠ” ì•„ì´í…œ ì²˜ë¦¬
	{
		DWORD vid = item->GetVID();
		DWORD oldCount = item->GetCount();
		DWORD vnum = item->GetVnum();

		char hint[ITEM_NAME_MAX_LEN + 32 + 1];
		int len = snprintf(hint, sizeof(hint) - 32, "%s", item->GetName());

		if (len < 0 || len >= (int) sizeof(hint) - 32)
			len = (sizeof(hint) - 32) - 1;

		bool ret = UseItemEx(item, DestCell);

		if (NULL == ITEM_MANAGER::instance().FindByVID(vid)) // UseItemExì—ì„œ ì•„ì´í…œì´ ì‚­ì œ ë˜ì—ˆë‹¤. ì‚­ì œ ë¡œê·¸ë¥¼ ë‚¨ê¹€
		{
			LogManager::instance().ItemLog(this, vid, vnum, "REMOVE", hint);
		}
		else if (oldCount != item->GetCount())
		{
			snprintf(hint + len, sizeof(hint) - len, " %u", oldCount - 1);
			LogManager::instance().ItemLog(this, vid, vnum, "USE_ITEM", hint);
		}
		return (ret);
	}
	else
		return UseItemEx(item, DestCell);
}

bool CHARACTER::DropItem(TItemPos Cell, BYTE bCount)
{
	LPITEM item = NULL; 

	if (!CanHandleItem())
	{
		if (NULL != DragonSoul_RefineWindow_GetOpener())
			ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê°•í™”ì°½ì„ ì—° ìƒíƒœì—ì„œëŠ” ì•„ì´í…œì„ ì˜®ê¸¸ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		return false;
	}

	if (IsDead())
		return false;

	if (!IsValidItemPosition(Cell) || !(item = GetItem(Cell)))
		return false;

	if (item->IsExchanging())
		return false;

	if (true == item->isLocked())
		return false;

	if (quest::CQuestManager::instance().GetPCForce(GetPlayerID())->IsRunning() == true)
		return false;

	if (IS_SET(item->GetAntiFlag(), ITEM_ANTIFLAG_DROP | ITEM_ANTIFLAG_GIVE))
	{
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë²„ë¦´ ìˆ˜ ì—†ëŠ” ì•„ì´í…œì…ë‹ˆë‹¤."));
		return false;
	}

	if (bCount == 0 || bCount > item->GetCount())
		bCount = item->GetCount();

	SyncQuickslot(QUICKSLOT_TYPE_ITEM, Cell.cell, 255);	// Quickslot ì—ì„œ ì§€ì›€

	LPITEM pkItemToDrop;
	
	// MR-3: Auto-deactivate auto potions before dropping
	switch (item->GetVnum())
	{
		case ITEM_AUTO_HP_RECOVERY_S:
		case ITEM_AUTO_HP_RECOVERY_M:
		case ITEM_AUTO_HP_RECOVERY_L:
		case ITEM_AUTO_HP_RECOVERY_X:
		case ITEM_AUTO_SP_RECOVERY_S:
		case ITEM_AUTO_SP_RECOVERY_M:
		case ITEM_AUTO_SP_RECOVERY_L:
		case ITEM_AUTO_SP_RECOVERY_X:
			if (item->GetSocket(0) == 1)
				item->SetSocket(0, 0);
			break;
		default:
			break;
	}
	// MR-3: -- END OF -- Auto-deactivate auto potions before dropping

	if (bCount == item->GetCount())
	{
		item->RemoveFromCharacter();
		pkItemToDrop = item;
	}
	else
	{
		if (bCount == 0)
		{
			if (test_server)
				sys_log(0, "[DROP_ITEM] drop item count == 0");
			return false;
		}
	
		// check non-split items for china
		//if (LC_IsNewCIBN())
		//	if (item->GetVnum() == 71095 || item->GetVnum() == 71050 || item->GetVnum() == 70038)
		//		return false;

		item->SetCount(item->GetCount() - bCount);
		ITEM_MANAGER::instance().FlushDelayedSave(item);

		pkItemToDrop = ITEM_MANAGER::instance().CreateItem(item->GetVnum(), bCount);

		// copy item socket -- by mhh
		FN_copy_item_socket(pkItemToDrop, item);

		char szBuf[51 + 1];
		snprintf(szBuf, sizeof(szBuf), "%u %u", pkItemToDrop->GetID(), pkItemToDrop->GetCount());
		LogManager::instance().ItemLog(this, item, "ITEM_SPLIT", szBuf);
	}

	PIXEL_POSITION pxPos = GetXYZ();
	
	// Clear the variable, it looks the player does not dropped any item in the past second.
	if (thecore_pulse() > LastDropTime + passes_per_sec)
		CountDrops = 0;
		
	// It looks the player dropped min. 4 items in the past 1 second
	if (thecore_pulse() < LastDropTime + passes_per_sec && CountDrops >= 4)
	{
		// Set it to 0
		CountDrops = 0;
		sys_err("%s[%d] has been disconnected because of drophack using", GetName(), GetPlayerID());
			// Disconnect the player
		GetDesc()->SetPhase(PHASE_CLOSE);
		return false;
	}

	if (pkItemToDrop->AddToGround(GetMapIndex(), pxPos))
	{
		// í•œêµ­ì—ëŠ” ì•„ì´í…œì„ ë²„ë¦¬ê³  ë³µêµ¬í•´ë‹¬ë¼ëŠ” ì§„ìƒìœ ì €ë“¤ì´ ë§ì•„ì„œ
		// ì•„ì´í…œì„ ë°”ë‹¥ì— ë²„ë¦´ ì‹œ ì†ì„±ë¡œê·¸ë¥¼ ë‚¨ê¸´ë‹¤.
		if (LC_IsYMIR())
			item->AttrLog();

		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë–¨ì–´ì§„ ì•„ì´í…œì€ 3ë¶„ í›„ ì‚¬ë¼ì§‘ë‹ˆë‹¤."));
		pkItemToDrop->StartDestroyEvent();

		ITEM_MANAGER::instance().FlushDelayedSave(pkItemToDrop);
		
		char szHint[32 + 1];
		snprintf(szHint, sizeof(szHint), "%s %u %u", pkItemToDrop->GetName(), pkItemToDrop->GetCount(), pkItemToDrop->GetOriginalVnum());
		LogManager::instance().ItemLog(this, pkItemToDrop, "DROP", szHint);
		
		LastDropTime = thecore_pulse();
		CountDrops++;
		
		//Motion(MOTION_PICKUP);
	}

	return true;
}

bool CHARACTER::DropGold(int gold)
{
	if (gold <= 0 || gold > GetGold())
		return false;

	if (!CanHandleItem())
		return false;

	if (0 != g_GoldDropTimeLimitValue)
	{
		if (get_dword_time() < m_dwLastGoldDropTime+g_GoldDropTimeLimitValue)
		{
			ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì•„ì§ ê³¨ë“œë¥¼ ë²„ë¦´ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
			return false;
		}
	}

	m_dwLastGoldDropTime = get_dword_time();

	LPITEM item = ITEM_MANAGER::instance().CreateItem(1, gold);

	if (item)
	{
		PIXEL_POSITION pos = GetXYZ();

		if (item->AddToGround(GetMapIndex(), pos))
		{
			//Motion(MOTION_PICKUP);
			PointChange(POINT_GOLD, -gold, true);

			// ë¸Œë¼ì§ˆì— ëˆì´ ì—†ì–´ì§„ë‹¤ëŠ” ë²„ê·¸ê°€ ìˆëŠ”ë°,
			// ê°€ëŠ¥í•œ ì‹œë‚˜ë¦¬ì˜¤ ì¤‘ì— í•˜ë‚˜ëŠ”,
			// ë©”í¬ë¡œë‚˜, í•µì„ ì¨ì„œ 1000ì› ì´í•˜ì˜ ëˆì„ ê³„ì† ë²„ë ¤ ê³¨ë“œë¥¼ 0ìœ¼ë¡œ ë§Œë“¤ê³ , 
			// ëˆì´ ì—†ì–´ì¡Œë‹¤ê³  ë³µêµ¬ ì‹ ì²­í•˜ëŠ” ê²ƒì¼ ìˆ˜ë„ ìˆë‹¤.
			// ë”°ë¼ì„œ ê·¸ëŸ° ê²½ìš°ë¥¼ ì¡ê¸° ìœ„í•´ ë‚®ì€ ìˆ˜ì¹˜ì˜ ê³¨ë“œì— ëŒ€í•´ì„œë„ ë¡œê·¸ë¥¼ ë‚¨ê¹€.
			if (LC_IsBrazil() == true)
			{
				if (gold >= 213)
					LogManager::instance().CharLog(this, gold, "DROP_GOLD", "");
			}
			else
			{
				if (gold > 1000) // ì²œì› ì´ìƒë§Œ ê¸°ë¡í•œë‹¤.
					LogManager::instance().CharLog(this, gold, "DROP_GOLD", "");
			}

			if (false == LC_IsBrazil())
			{
				item->StartDestroyEvent(150);
				ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë–¨ì–´ì§„ ì•„ì´í…œì€ %dë¶„ í›„ ì‚¬ë¼ì§‘ë‹ˆë‹¤."), 150/60);
			}
			else
			{
				item->StartDestroyEvent(60);
				ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë–¨ì–´ì§„ ì•„ì´í…œì€ %dë¶„ í›„ ì‚¬ë¼ì§‘ë‹ˆë‹¤."), 1);
			}
		}

		Save();
		return true;
	}

	return false;
}

bool CHARACTER::MoveItem(TItemPos Cell, TItemPos DestCell, BYTE count)
{
	if (Cell.IsSamePosition(DestCell)) // ItemMove dupe exploit fix
		return false;

	LPITEM item = NULL;

	if (!IsValidItemPosition(Cell))
		return false;

	if (!(item = GetItem(Cell)))
		return false;

	if (item->IsExchanging())
		return false;

	if (item->GetCount() < count)
		return false;

	if (INVENTORY == Cell.window_type && Cell.cell >= INVENTORY_MAX_NUM && IS_SET(item->GetFlag(), ITEM_FLAG_IRREMOVABLE))
		return false;

	if (true == item->isLocked())
		return false;

	if (!IsValidItemPosition(DestCell))
	{
		return false;
	}

	if (!CanHandleItem())
	{
		if (NULL != DragonSoul_RefineWindow_GetOpener())
			ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê°•í™”ì°½ì„ ì—° ìƒíƒœì—ì„œëŠ” ì•„ì´í…œì„ ì˜®ê¸¸ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		return false;
	}

	// Only specific types of items can be placed in the belt inventory.
	if (DestCell.IsBeltInventoryPosition() && false == CBeltInventoryHelper::CanMoveIntoBeltInventory(item))
	{
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ ì•„ì´í…œì€ ë²¨íŠ¸ ì¸ë²¤í† ë¦¬ë¡œ ì˜®ê¸¸ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));			
		return false;
	}

	// When moving an item that is already equipped to another location, check if 'unequipping' is currently allowed before proceeding.
	if (Cell.IsEquipPosition() && !CanUnequipNow(item))
		return false;

	// If the destination cell is an equipment position, equip the item.
	if (DestCell.IsEquipPosition())
	{
		if (GetItem(DestCell))	// For equipment, checking only this position is sufficient.
		{
			ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ë¯¸ ì¥ë¹„ë¥¼ ì°©ìš©í•˜ê³  ìˆìŠµë‹ˆë‹¤."));
			
			return false;
		}

		EquipItem(item, DestCell.cell - INVENTORY_MAX_NUM);
	}
	else
	{
		if (item->IsDragonSoul())
		{
			if (item->IsEquipped())
			{
				return DSManager::instance().PullOut(this, DestCell, item);
			}
			else
			{
				if (DestCell.window_type != DRAGON_SOUL_INVENTORY)
				{
					return false;
				}

				if (!DSManager::instance().IsValidCellForThisItem(item, DestCell))
					return false;
			}
		}
		else if (DRAGON_SOUL_INVENTORY == DestCell.window_type)
		{
			// Items that are not Dragon Soul cannot be placed in the Dragon Soul inventory.
			return false;
		}

		LPITEM item2;

		if ((item2 = GetItem(DestCell)) && item != item2 && item2->IsStackable() &&
				!IS_SET(item2->GetAntiFlag(), ITEM_ANTIFLAG_STACK) &&
				item2->GetVnum() == item->GetVnum()) // í•©ì¹  ìˆ˜ ìˆëŠ” ì•„ì´í…œì˜ ê²½ìš°
		{
			for (int i = 0; i < ITEM_SOCKET_MAX_NUM; ++i)
				if (item2->GetSocket(i) != item->GetSocket(i))
					return false;

			if (count == 0)
				count = item->GetCount();

			sys_log(0, "%s: ITEM_STACK %s (window: %d, cell : %d) -> (window:%d, cell %d) count %d", GetName(), item->GetName(), Cell.window_type, Cell.cell, 
				DestCell.window_type, DestCell.cell, count);

			count = MIN(200 - item2->GetCount(), count);

			item->SetCount(item->GetCount() - count);
			item2->SetCount(item2->GetCount() + count);
			return true;
		}

		if (!IsEmptyItemGrid(DestCell, item->GetSize(), Cell.cell))
			return false;

		if (count == 0 || count >= item->GetCount() || !item->IsStackable() || IS_SET(item->GetAntiFlag(), ITEM_ANTIFLAG_STACK))
		{
			sys_log(0, "%s: ITEM_MOVE %s (window: %d, cell : %d) -> (window:%d, cell %d) count %d", GetName(), item->GetName(), Cell.window_type, Cell.cell, 
				DestCell.window_type, DestCell.cell, count);

			// MR-3: Auto-deactivate auto potions before moving to safebox or myshop
			if (item && DestCell.window_type == SAFEBOX)
			{
				switch (item->GetVnum())
				{
					case ITEM_AUTO_HP_RECOVERY_S:
					case ITEM_AUTO_HP_RECOVERY_M:
					case ITEM_AUTO_HP_RECOVERY_L:
					case ITEM_AUTO_HP_RECOVERY_X:
					case ITEM_AUTO_SP_RECOVERY_S:
					case ITEM_AUTO_SP_RECOVERY_M:
					case ITEM_AUTO_SP_RECOVERY_L:
					case ITEM_AUTO_SP_RECOVERY_X:
						if (item->GetSocket(0) == 1)
							item->SetSocket(0, 0);
						break;
					default:
						break;
				}
			}
			// MR-3: -- END OF -- Auto-deactivate auto potions before moving to safebox or myshop
			
			item->RemoveFromCharacter();
			SetItem(DestCell, item);

			if (INVENTORY == Cell.window_type && INVENTORY == DestCell.window_type)
				SyncQuickslot(QUICKSLOT_TYPE_ITEM, Cell.cell, DestCell.cell);
		}
		else if (count < item->GetCount())
		{
			//check non-split items 
			//if (LC_IsNewCIBN())
			//{
			//	if (item->GetVnum() == 71095 || item->GetVnum() == 71050 || item->GetVnum() == 70038)
			//	{
			//		return false;
			//	}
			//}

			sys_log(0, "%s: ITEM_SPLIT %s (window: %d, cell : %d) -> (window:%d, cell %d) count %d", GetName(), item->GetName(), Cell.window_type, Cell.cell, 
				DestCell.window_type, DestCell.cell, count);

			item->SetCount(item->GetCount() - count);
			LPITEM item2 = ITEM_MANAGER::instance().CreateItem(item->GetVnum(), count);

			// copy socket -- by mhh
			FN_copy_item_socket(item2, item);

			item2->AddToCharacter(this, DestCell);

			char szBuf[51+1];
			snprintf(szBuf, sizeof(szBuf), "%u %u %u %u ", item2->GetID(), item2->GetCount(), item->GetCount(), item->GetCount() + item2->GetCount());
			LogManager::instance().ItemLog(this, item, "ITEM_SPLIT", szBuf);
		}
	}

	return true;
}

namespace NPartyPickupDistribute
{
	struct FFindOwnership
	{
		LPITEM item;
		LPCHARACTER owner;

		FFindOwnership(LPITEM item) 
			: item(item), owner(NULL)
		{
		}

		void operator () (LPCHARACTER ch)
		{
			if (item->IsOwnership(ch))
				owner = ch;
		}
	};

	struct FCountNearMember
	{
		int		total;
		int		x, y;

		FCountNearMember(LPCHARACTER center )
			: total(0), x(center->GetX()), y(center->GetY())
		{
		}

		void operator () (LPCHARACTER ch)
		{
			if (DISTANCE_APPROX(ch->GetX() - x, ch->GetY() - y) <= PARTY_DEFAULT_RANGE)
				total += 1;
		}
	};

	struct FMoneyDistributor
	{
		int		total;
		LPCHARACTER	c;
		int		x, y;
		int		iMoney;

		FMoneyDistributor(LPCHARACTER center, int iMoney) 
			: total(0), c(center), x(center->GetX()), y(center->GetY()), iMoney(iMoney) 
		{
		}

		void operator ()(LPCHARACTER ch)
		{
			if (ch!=c)
				if (DISTANCE_APPROX(ch->GetX() - x, ch->GetY() - y) <= PARTY_DEFAULT_RANGE)
				{
					ch->PointChange(POINT_GOLD, iMoney, true);

					if (iMoney > 1000) // ì²œì› ì´ìƒë§Œ ê¸°ë¡í•œë‹¤.
						LogManager::instance().CharLog(ch, iMoney, "GET_GOLD", "");
				}
		}
	};
}

void CHARACTER::GiveGold(int iAmount)
{
	if (iAmount <= 0)
		return;

	sys_log(0, "GIVE_GOLD: %s %d", GetName(), iAmount);

	if (GetParty())
	{
		LPPARTY pParty = GetParty();

		// íŒŒí‹°ê°€ ìˆëŠ” ê²½ìš° ë‚˜ëˆ„ì–´ ê°€ì§„ë‹¤.
		DWORD dwTotal = iAmount;
		DWORD dwMyAmount = dwTotal;

		NPartyPickupDistribute::FCountNearMember funcCountNearMember(this);
		pParty->ForEachOnlineMember(funcCountNearMember);

		if (funcCountNearMember.total > 1)
		{
			DWORD dwShare = dwTotal / funcCountNearMember.total;
			dwMyAmount -= dwShare * (funcCountNearMember.total - 1);

			NPartyPickupDistribute::FMoneyDistributor funcMoneyDist(this, dwShare);

			pParty->ForEachOnlineMember(funcMoneyDist);
		}

		PointChange(POINT_GOLD, dwMyAmount, true);

		if (dwMyAmount > 1000) // ì²œì› ì´ìƒë§Œ ê¸°ë¡í•œë‹¤.
			LogManager::instance().CharLog(this, dwMyAmount, "GET_GOLD", "");
	}
	else
	{
		PointChange(POINT_GOLD, iAmount, true);

		// ë¸Œë¼ì§ˆì— ëˆì´ ì—†ì–´ì§„ë‹¤ëŠ” ë²„ê·¸ê°€ ìˆëŠ”ë°,
		// ê°€ëŠ¥í•œ ì‹œë‚˜ë¦¬ì˜¤ ì¤‘ì— í•˜ë‚˜ëŠ”,
		// ë©”í¬ë¡œë‚˜, í•µì„ ì¨ì„œ 1000ì› ì´í•˜ì˜ ëˆì„ ê³„ì† ë²„ë ¤ ê³¨ë“œë¥¼ 0ìœ¼ë¡œ ë§Œë“¤ê³ , 
		// ëˆì´ ì—†ì–´ì¡Œë‹¤ê³  ë³µêµ¬ ì‹ ì²­í•˜ëŠ” ê²ƒì¼ ìˆ˜ë„ ìˆë‹¤.
		// ë”°ë¼ì„œ ê·¸ëŸ° ê²½ìš°ë¥¼ ì¡ê¸° ìœ„í•´ ë‚®ì€ ìˆ˜ì¹˜ì˜ ê³¨ë“œì— ëŒ€í•´ì„œë„ ë¡œê·¸ë¥¼ ë‚¨ê¹€.
		if (LC_IsBrazil() == true)
		{
			if (iAmount >= 213)
				LogManager::instance().CharLog(this, iAmount, "GET_GOLD", "");
		}
		else
		{
			if (iAmount > 1000) // ì²œì› ì´ìƒë§Œ ê¸°ë¡í•œë‹¤.
				LogManager::instance().CharLog(this, iAmount, "GET_GOLD", "");
		}
	}
}

bool CHARACTER::PickupItem(DWORD dwVID)
{
	LPITEM item = ITEM_MANAGER::instance().FindByVID(dwVID);

	if (IsObserverMode())
		return false;

	if (!item || !item->GetSectree())
		return false;

	if (item->DistanceValid(this))
	{
		if (item->IsOwnership(this))
		{
			// ë§Œì•½ ì£¼ìœ¼ë ¤ í•˜ëŠ” ì•„ì´í…œì´ ì—˜í¬ë¼ë©´
			if (item->GetType() == ITEM_ELK)
			{
				GiveGold(item->GetCount());
				item->RemoveFromGround();

				M2_DESTROY_ITEM(item);

				Save();
			}
			// í‰ë²”í•œ ì•„ì´í…œì´ë¼ë©´
			else
			{
				if (item->IsStackable() && !IS_SET(item->GetAntiFlag(), ITEM_ANTIFLAG_STACK))
				{
					BYTE bCount = item->GetCount();

					for (int i = 0; i < INVENTORY_MAX_NUM; ++i)
					{
						LPITEM item2 = GetInventoryItem(i);

						if (!item2)
							continue;

						if (item2->GetVnum() == item->GetVnum())
						{
							int j;

							for (j = 0; j < ITEM_SOCKET_MAX_NUM; ++j)
								if (item2->GetSocket(j) != item->GetSocket(j))
									break;

							if (j != ITEM_SOCKET_MAX_NUM)
								continue;

							BYTE bCount2 = MIN(200 - item2->GetCount(), bCount);
							bCount -= bCount2;

							item2->SetCount(item2->GetCount() + bCount2);

							if (bCount == 0)
							{
								ItemGetPacket(item2->GetVnum(), item2->GetCount());
								M2_DESTROY_ITEM(item);
								if (item2->GetType() == ITEM_QUEST)
									quest::CQuestManager::instance().PickupItem (GetPlayerID(), item2);
								return true;
							}
						}
					}

					item->SetCount(bCount);
				}

				int iEmptyCell;
				if (item->IsDragonSoul())
				{
					if ((iEmptyCell = GetEmptyDragonSoulInventory(item)) == -1)
					{
						sys_log(0, "No empty ds inventory pid %u size %ud itemid %u", GetPlayerID(), item->GetSize(), item->GetID());
						ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì†Œì§€í•˜ê³  ìˆëŠ” ì•„ì´í…œì´ ë„ˆë¬´ ë§ìŠµë‹ˆë‹¤."));
						return false;
					}
				}
				else
				{
					if ((iEmptyCell = GetEmptyInventory(item->GetSize())) == -1)
					{
						sys_log(0, "No empty inventory pid %u size %ud itemid %u", GetPlayerID(), item->GetSize(), item->GetID());
						ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì†Œì§€í•˜ê³  ìˆëŠ” ì•„ì´í…œì´ ë„ˆë¬´ ë§ìŠµë‹ˆë‹¤."));
						return false;
					}
				}

				item->RemoveFromGround();
				
				if (item->IsDragonSoul())
					item->AddToCharacter(this, TItemPos(DRAGON_SOUL_INVENTORY, iEmptyCell));
				else
					item->AddToCharacter(this, TItemPos(INVENTORY, iEmptyCell));

				char szHint[32+1];
				snprintf(szHint, sizeof(szHint), "%s %u %u", item->GetName(), item->GetCount(), item->GetOriginalVnum());
				LogManager::instance().ItemLog(this, item, "GET", szHint);
				ItemGetPacket(item->GetVnum(), item->GetCount());

				if (item->GetType() == ITEM_QUEST)
					quest::CQuestManager::instance().PickupItem (GetPlayerID(), item);
			}

			//Motion(MOTION_PICKUP);
			return true;
		}
		else if (!IS_SET(item->GetAntiFlag(), ITEM_ANTIFLAG_GIVE | ITEM_ANTIFLAG_DROP) && GetParty())
		{
			// ë‹¤ë¥¸ íŒŒí‹°ì› ì†Œìœ ê¶Œ ì•„ì´í…œì„ ì£¼ìœ¼ë ¤ê³  í•œë‹¤ë©´
			NPartyPickupDistribute::FFindOwnership funcFindOwnership(item);

			GetParty()->ForEachOnlineMember(funcFindOwnership);

			LPCHARACTER owner = funcFindOwnership.owner;

			int iEmptyCell;

			if (item->IsDragonSoul())
			{
				if (!(owner && (iEmptyCell = owner->GetEmptyDragonSoulInventory(item)) != -1))
				{
					owner = this;

					if ((iEmptyCell = GetEmptyDragonSoulInventory(item)) == -1)
					{
						owner->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì†Œì§€í•˜ê³  ìˆëŠ” ì•„ì´í…œì´ ë„ˆë¬´ ë§ìŠµë‹ˆë‹¤."));
						return false;
					}
				}
			}
			else
			{
				if (!(owner && (iEmptyCell = owner->GetEmptyInventory(item->GetSize())) != -1))
				{
					owner = this;

					if ((iEmptyCell = GetEmptyInventory(item->GetSize())) == -1)
					{
						owner->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì†Œì§€í•˜ê³  ìˆëŠ” ì•„ì´í…œì´ ë„ˆë¬´ ë§ìŠµë‹ˆë‹¤."));
						return false;
					}
				}
			}

			item->RemoveFromGround();

			if (item->IsDragonSoul())
				item->AddToCharacter(owner, TItemPos(DRAGON_SOUL_INVENTORY, iEmptyCell));
			else
				item->AddToCharacter(owner, TItemPos(INVENTORY, iEmptyCell));

			char szHint[32+1];
			snprintf(szHint, sizeof(szHint), "%s %u %u", item->GetName(), item->GetCount(), item->GetOriginalVnum());
			LogManager::instance().ItemLog(owner, item, "GET", szHint);

			if (owner == this)
				ItemGetPacket(item->GetVnum(), item->GetCount());
			else
			{
				owner->ItemGetPacket(item->GetVnum(), item->GetCount(), GetName());
				ItemGetPacket(item->GetVnum(), item->GetCount(), owner->GetName(), true);
			}

			if (item->GetType() == ITEM_QUEST)
				quest::CQuestManager::instance().PickupItem (owner->GetPlayerID(), item);

			return true;
		}
	}

	return false;
}

bool CHARACTER::SwapItem(BYTE bCell, BYTE bDestCell)
{
	if (!CanHandleItem())
		return false;

	TItemPos srcCell(INVENTORY, bCell), destCell(INVENTORY, bDestCell);

	// ì˜¬ë°”ë¥¸ Cell ì¸ì§€ ê²€ì‚¬
	// ìš©í˜¼ì„ì€ Swapí•  ìˆ˜ ì—†ìœ¼ë¯€ë¡œ, ì—¬ê¸°ì„œ ê±¸ë¦¼.
	//if (bCell >= INVENTORY_MAX_NUM + WEAR_MAX_NUM || bDestCell >= INVENTORY_MAX_NUM + WEAR_MAX_NUM)
	if (srcCell.IsDragonSoulEquipPosition() || destCell.IsDragonSoulEquipPosition())
		return false;

	// ê°™ì€ CELL ì¸ì§€ ê²€ì‚¬
	if (bCell == bDestCell)
		return false;

	// ë‘˜ ë‹¤ ì¥ë¹„ì°½ ìœ„ì¹˜ë©´ Swap í•  ìˆ˜ ì—†ë‹¤.
	if (srcCell.IsEquipPosition() && destCell.IsEquipPosition())
		return false;

	LPITEM item1, item2;

	// item2ê°€ ì¥ë¹„ì°½ì— ìˆëŠ” ê²ƒì´ ë˜ë„ë¡.
	if (srcCell.IsEquipPosition())
	{
		item1 = GetInventoryItem(bDestCell);
		item2 = GetInventoryItem(bCell);
	}
	else
	{
		item1 = GetInventoryItem(bCell);
		item2 = GetInventoryItem(bDestCell);
	}

	if (!item1 || !item2)
		return false;
	
	if (item1 == item2)
	{
	    sys_log(0, "[WARNING][WARNING][HACK USER!] : %s %d %d", m_stName.c_str(), bCell, bDestCell);
	    return false;
	}

	// item2ê°€ bCellìœ„ì¹˜ì— ë“¤ì–´ê°ˆ ìˆ˜ ìˆëŠ”ì§€ í™•ì¸í•œë‹¤.
	if (!IsEmptyItemGrid(TItemPos (INVENTORY, item1->GetCell()), item2->GetSize(), item1->GetCell()))
		return false;

	// ë°”ê¿€ ì•„ì´í…œì´ ì¥ë¹„ì°½ì— ìˆìœ¼ë©´
	if (TItemPos(EQUIPMENT, item2->GetCell()).IsEquipPosition())
	{
		BYTE bEquipCell = item2->GetCell() - INVENTORY_MAX_NUM;
		BYTE bInvenCell = item1->GetCell();

		// Proceed only if the currently equipped item can be unequipped,
		// and the item intended to be equipped is eligible for equipping.
		bool canUnequip = CanUnequipNow(item2, TItemPos(EQUIPMENT, item2->GetCell()), TItemPos(INVENTORY, item1->GetCell()));
		bool canEquip = CanEquipNow(item1);
		if (false == canUnequip || false == canEquip)
			return false;

		if (bEquipCell != item1->FindEquipCell(this)) // ê°™ì€ ìœ„ì¹˜ì¼ë•Œë§Œ í—ˆìš©
			return false;

		item2->RemoveFromCharacter();

		if (item1->EquipTo(this, bEquipCell))
			item2->AddToCharacter(this, TItemPos(INVENTORY, bInvenCell));
		else
			sys_err("SwapItem cannot equip %s! item1 %s", item2->GetName(), item1->GetName());
	}
	else
	{
		BYTE bCell1 = item1->GetCell();
		BYTE bCell2 = item2->GetCell();
		
		item1->RemoveFromCharacter();
		item2->RemoveFromCharacter();

		item1->AddToCharacter(this, TItemPos(INVENTORY, bCell2));
		item2->AddToCharacter(this, TItemPos(INVENTORY, bCell1));
	}

	return true;
}

bool CHARACTER::UnequipItem(LPITEM item)
{
	int pos;

	if (false == CanUnequipNow(item))
		return false;

	if (item->IsDragonSoul())
		pos = GetEmptyDragonSoulInventory(item);
	else
		pos = GetEmptyInventory(item->GetSize());

	// HARD CODING
	if (item->GetVnum() == UNIQUE_ITEM_HIDE_ALIGNMENT_TITLE)
		ShowAlignment(true);

	item->RemoveFromCharacter();
	if (item->IsDragonSoul())
	{
		item->AddToCharacter(this, TItemPos(DRAGON_SOUL_INVENTORY, pos));
	}
	else
		item->AddToCharacter(this, TItemPos(INVENTORY, pos));

	CheckMaximumPoints();

	return true;
}

//
// @version	05/07/05 Bang2ni - Skill ì‚¬ìš©í›„ 1.5 ì´ˆ ì´ë‚´ì— ì¥ë¹„ ì°©ìš© ê¸ˆì§€
//
bool CHARACTER::EquipItem(LPITEM item, int iCandidateCell)
{
	if (item->IsExchanging())
		return false;

	if (false == item->IsEquipable())
		return false;

	if (false == CanEquipNow(item))
		return false;

	int iWearCell = item->FindEquipCell(this, iCandidateCell);

	if (iWearCell < 0)
		return false;

	// ë¬´ì–¸ê°€ë¥¼ íƒ„ ìƒíƒœì—ì„œ í„±ì‹œë„ ì…ê¸° ê¸ˆì§€
	if (iWearCell == WEAR_BODY && IsRiding() && (item->GetVnum() >= 11901 && item->GetVnum() <= 11904))
	{
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë§ì„ íƒ„ ìƒíƒœì—ì„œ ì˜ˆë³µì„ ì…ì„ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		return false;
	}

	if (iWearCell != WEAR_ARROW && IsPolymorphed())
	{
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë‘”ê°‘ ì¤‘ì—ëŠ” ì°©ìš©ì¤‘ì¸ ì¥ë¹„ë¥¼ ë³€ê²½í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		return false;
	}

	if (FN_check_item_sex(this, item) == false)
	{
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì„±ë³„ì´ ë§ì§€ì•Šì•„ ì´ ì•„ì´í…œì„ ì‚¬ìš©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		return false;
	}

	//ì‹ ê·œ íƒˆê²ƒ ì‚¬ìš©ì‹œ ê¸°ì¡´ ë§ ì‚¬ìš©ì—¬ë¶€ ì²´í¬
	if(item->IsRideItem() && IsRiding())
	{
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ë¯¸ íƒˆê²ƒì„ ì´ìš©ì¤‘ì…ë‹ˆë‹¤."));
		return false;
	}

	// í™”ì‚´ ì´ì™¸ì—ëŠ” ë§ˆì§€ë§‰ ê³µê²© ì‹œê°„ ë˜ëŠ” ìŠ¤í‚¬ ì‚¬ìš© 1.5 í›„ì— ì¥ë¹„ êµì²´ê°€ ê°€ëŠ¥
	DWORD dwCurTime = get_dword_time();

	if (iWearCell != WEAR_ARROW 
		&& (dwCurTime - GetLastAttackTime() <= 1500 || dwCurTime - m_dwLastSkillTime <= 1500))
	{
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê°€ë§Œíˆ ìˆì„ ë•Œë§Œ ì°©ìš©í•  ìˆ˜ ìˆìŠµë‹ˆë‹¤."));
		return false;
	}

	// ìš©í˜¼ì„ íŠ¹ìˆ˜ ì²˜ë¦¬
	if (item->IsDragonSoul())
	{
		// ê°™ì€ íƒ€ì…ì˜ ìš©í˜¼ì„ì´ ì´ë¯¸ ë“¤ì–´ê°€ ìˆë‹¤ë©´ ì°©ìš©í•  ìˆ˜ ì—†ë‹¤.
		// ìš©í˜¼ì„ì€ swapì„ ì§€ì›í•˜ë©´ ì•ˆë¨.
		if(GetInventoryItem(INVENTORY_MAX_NUM + iWearCell))
		{
			ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ë¯¸ ê°™ì€ ì¢…ë¥˜ì˜ ìš©í˜¼ì„ì„ ì°©ìš©í•˜ê³  ìˆìŠµë‹ˆë‹¤."));
			return false;
		}
		
		if (!item->EquipTo(this, iWearCell))
		{
			return false;
		}
	}
	// ìš©í˜¼ì„ì´ ì•„ë‹˜.
	else
	{
		// ì°©ìš©í•  ê³³ì— ì•„ì´í…œì´ ìˆë‹¤ë©´,
		if (GetWear(iWearCell) && !IS_SET(GetWear(iWearCell)->GetFlag(), ITEM_FLAG_IRREMOVABLE))
		{
			// ì´ ì•„ì´í…œì€ í•œë²ˆ ë°•íˆë©´ ë³€ê²½ ë¶ˆê°€. swap ì—­ì‹œ ì™„ì „ ë¶ˆê°€
			if (item->GetWearFlag() == WEARABLE_ABILITY) 
				return false;

			if (false == SwapItem(item->GetCell(), INVENTORY_MAX_NUM + iWearCell))
			{
				return false;
			}
		}
		else
		{
			BYTE bOldCell = item->GetCell();

			if (item->EquipTo(this, iWearCell))
			{
				SyncQuickslot(QUICKSLOT_TYPE_ITEM, bOldCell, iWearCell);
			}
		}
	}

	if (true == item->IsEquipped())
	{
		// ì•„ì´í…œ ìµœì´ˆ ì‚¬ìš© ì´í›„ë¶€í„°ëŠ” ì‚¬ìš©í•˜ì§€ ì•Šì•„ë„ ì‹œê°„ì´ ì°¨ê°ë˜ëŠ” ë°©ì‹ ì²˜ë¦¬. 
		if (-1 != item->GetProto()->cLimitRealTimeFirstUseIndex)
		{
			// í•œ ë²ˆì´ë¼ë„ ì‚¬ìš©í•œ ì•„ì´í…œì¸ì§€ ì—¬ë¶€ëŠ” Socket1ì„ ë³´ê³  íŒë‹¨í•œë‹¤. (Socket1ì— ì‚¬ìš©íšŸìˆ˜ ê¸°ë¡)
			if (0 == item->GetSocket(1))
			{
				// ì‚¬ìš©ê°€ëŠ¥ì‹œê°„ì€ Default ê°’ìœ¼ë¡œ Limit Value ê°’ì„ ì‚¬ìš©í•˜ë˜, Socket0ì— ê°’ì´ ìˆìœ¼ë©´ ê·¸ ê°’ì„ ì‚¬ìš©í•˜ë„ë¡ í•œë‹¤. (ë‹¨ìœ„ëŠ” ì´ˆ)
				long duration = (0 != item->GetSocket(0)) ? item->GetSocket(0) : item->GetProto()->aLimits[item->GetProto()->cLimitRealTimeFirstUseIndex].lValue;

				if (0 == duration)
					duration = 60 * 60 * 24 * 7;

				item->SetSocket(0, time(0) + duration);
				item->StartRealTimeExpireEvent();
			}

			item->SetSocket(1, item->GetSocket(1) + 1);
		}

		if (item->GetVnum() == UNIQUE_ITEM_HIDE_ALIGNMENT_TITLE)
			ShowAlignment(false);

		const DWORD& dwVnum = item->GetVnum();

		// ë¼ë§ˆë‹¨ ì´ë²¤íŠ¸ ì´ˆìŠ¹ë‹¬ì˜ ë°˜ì§€(71135) ì°©ìš©ì‹œ ì´í™íŠ¸ ë°œë™
		if (true == CItemVnumHelper::IsRamadanMoonRing(dwVnum))
		{
			this->EffectPacket(SE_EQUIP_RAMADAN_RING);
		}
		// í• ë¡œìœˆ ì‚¬íƒ•(71136) ì°©ìš©ì‹œ ì´í™íŠ¸ ë°œë™
		else if (true == CItemVnumHelper::IsHalloweenCandy(dwVnum))
		{
			this->EffectPacket(SE_EQUIP_HALLOWEEN_CANDY);
		}
		// í–‰ë³µì˜ ë°˜ì§€(71143) ì°©ìš©ì‹œ ì´í™íŠ¸ ë°œë™
		else if (true == CItemVnumHelper::IsHappinessRing(dwVnum))
		{
			this->EffectPacket(SE_EQUIP_HAPPINESS_RING);
		}
		// ì‚¬ë‘ì˜ íŒ¬ë˜íŠ¸(71145) ì°©ìš©ì‹œ ì´í™íŠ¸ ë°œë™
		else if (true == CItemVnumHelper::IsLovePendant(dwVnum))
		{
			this->EffectPacket(SE_EQUIP_LOVE_PENDANT);
		}
		// ITEM_UNIQUEì˜ ê²½ìš°, SpecialItemGroupì— ì •ì˜ë˜ì–´ ìˆê³ , (item->GetSIGVnum() != NULL)
		// 
		else if (ITEM_UNIQUE == item->GetType() && 0 != item->GetSIGVnum())
		{
			const CSpecialItemGroup* pGroup = ITEM_MANAGER::instance().GetSpecialItemGroup(item->GetSIGVnum());
			if (NULL != pGroup)
			{
				const CSpecialAttrGroup* pAttrGroup = ITEM_MANAGER::instance().GetSpecialAttrGroup(pGroup->GetAttrVnum(item->GetVnum()));
				if (NULL != pAttrGroup)
				{
					const std::string& std = pAttrGroup->m_stEffectFileName;
					SpecificEffectPacket(std.c_str());
				}
			}
		}

		if (UNIQUE_SPECIAL_RIDE == item->GetSubType() && IS_SET(item->GetFlag(), ITEM_FLAG_QUEST_USE))
		{
			quest::CQuestManager::instance().UseItem(GetPlayerID(), item, false);
		}
	}

	return true;
}

void CHARACTER::BuffOnAttr_AddBuffsFromItem(LPITEM pItem)
{
	for (int i = 0; i < sizeof(g_aBuffOnAttrPoints)/sizeof(g_aBuffOnAttrPoints[0]); i++)
	{
		TMapBuffOnAttrs::iterator it = m_map_buff_on_attrs.find(g_aBuffOnAttrPoints[i]);
		if (it != m_map_buff_on_attrs.end())
		{
			it->second->AddBuffFromItem(pItem);
		}
	}
}

void CHARACTER::BuffOnAttr_RemoveBuffsFromItem(LPITEM pItem)
{
	for (int i = 0; i < sizeof(g_aBuffOnAttrPoints)/sizeof(g_aBuffOnAttrPoints[0]); i++)
	{
		TMapBuffOnAttrs::iterator it = m_map_buff_on_attrs.find(g_aBuffOnAttrPoints[i]);
		if (it != m_map_buff_on_attrs.end())
		{
			it->second->RemoveBuffFromItem(pItem);
		}
	}
}

void CHARACTER::BuffOnAttr_ClearAll()
{
	for (TMapBuffOnAttrs::iterator it = m_map_buff_on_attrs.begin(); it != m_map_buff_on_attrs.end(); it++)
	{
		CBuffOnAttributes* pBuff = it->second;
		if (pBuff)
		{
			pBuff->Initialize();
		}
	}
}

void CHARACTER::BuffOnAttr_ValueChange(BYTE bType, BYTE bOldValue, BYTE bNewValue)
{
	TMapBuffOnAttrs::iterator it = m_map_buff_on_attrs.find(bType);

	if (0 == bNewValue)
	{
		if (m_map_buff_on_attrs.end() == it)
			return;
		else
			it->second->Off();
	}
	else if(0 == bOldValue)
	{
		CBuffOnAttributes* pBuff;
		if (m_map_buff_on_attrs.end() == it)
		{
			switch (bType)
			{
			case POINT_ENERGY:
				{
					static BYTE abSlot[] = { WEAR_BODY, WEAR_HEAD, WEAR_FOOTS, WEAR_WRIST, WEAR_WEAPON, WEAR_NECK, WEAR_EAR, WEAR_SHIELD };
					static std::vector <BYTE> vec_slots (abSlot, abSlot + _countof(abSlot));
					pBuff = M2_NEW CBuffOnAttributes(this, bType, &vec_slots);
				}
				break;
			case POINT_COSTUME_ATTR_BONUS:
				{
					static BYTE abSlot[] = { WEAR_COSTUME_BODY, WEAR_COSTUME_HAIR };
					static std::vector <BYTE> vec_slots (abSlot, abSlot + _countof(abSlot));
					pBuff = M2_NEW CBuffOnAttributes(this, bType, &vec_slots);
				}
				break;
			default:
				break;
			}
			m_map_buff_on_attrs.insert(TMapBuffOnAttrs::value_type(bType, pBuff));

		}
		else
			pBuff = it->second;
			
		pBuff->On(bNewValue);
	}
	else
	{
		if (m_map_buff_on_attrs.end() == it)
			return;
		else
			it->second->ChangeBuffValue(bNewValue);
	}
}


LPITEM CHARACTER::FindSpecifyItem(DWORD vnum) const
{
	for (int i = 0; i < INVENTORY_MAX_NUM; ++i)
		if (GetInventoryItem(i) && GetInventoryItem(i)->GetVnum() == vnum)
			return GetInventoryItem(i);

	return NULL;
}

LPITEM CHARACTER::FindItemByID(DWORD id) const
{
	for (int i=0 ; i < INVENTORY_MAX_NUM ; ++i)
	{
		if (NULL != GetInventoryItem(i) && GetInventoryItem(i)->GetID() == id)
			return GetInventoryItem(i);
	}

	for (int i=BELT_INVENTORY_SLOT_START; i < BELT_INVENTORY_SLOT_END ; ++i)
	{
		if (NULL != GetInventoryItem(i) && GetInventoryItem(i)->GetID() == id)
			return GetInventoryItem(i);
	}

	return NULL;
}

int CHARACTER::CountSpecifyItem(DWORD vnum) const
{
	int	count = 0;

	for (int i = 0; i < INVENTORY_MAX_NUM; ++i) {
		LPITEM item = GetInventoryItem(i);
		if (!item) continue;

		if (item->GetVnum() != vnum)
			continue;

		if (m_pkMyShop && m_pkMyShop->IsSellingItem(item->GetID()))
			continue;

		count += item->GetCount();
	}

	return count;
}

bool CHARACTER::RemoveSpecifyItem(DWORD vnum, DWORD count)
{
	if (0 == count)
		return false;

	for (UINT i = 0; i < INVENTORY_MAX_NUM; ++i) {
		LPITEM item = GetInventoryItem(i);
		if (!item) continue;

		if (item->GetVnum() != vnum)
			continue;

		//ê°œì¸ ìƒì ì— ë“±ë¡ëœ ë¬¼ê±´ì´ë©´ ë„˜ì–´ê°„ë‹¤. (ê°œì¸ ìƒì ì—ì„œ íŒë§¤ë ë•Œ ì´ ë¶€ë¶„ìœ¼ë¡œ ë“¤ì–´ì˜¬ ê²½ìš° ë¬¸ì œ!)
		if (m_pkMyShop && m_pkMyShop->IsSellingItem(item->GetID()))
			continue;

		if (vnum >= 80003 && vnum <= 80007)
			LogManager::instance().GoldBarLog(GetPlayerID(), item->GetID(), QUEST, "RemoveSpecifyItem");

		DWORD item_count = std::min(count, item->GetCount());
		item->SetCount(item->GetCount() - item_count);
		count -= item_count;

		if (0 == count) return true;
	}

	if (0 == count) return true;

	// ì˜ˆì™¸ì²˜ë¦¬ê°€ ì•½í•˜ë‹¤.
	sys_log(0, "CHARACTER::RemoveSpecifyItem cannot remove enough item vnum %u, still remain %d", vnum, count);
	return false;
}

int CHARACTER::CountSpecifyTypeItem(BYTE type) const
{
	int	count = 0;

	for (int i = 0; i < INVENTORY_MAX_NUM; ++i)
	{
		LPITEM pItem = GetInventoryItem(i);
		if (pItem != NULL && pItem->GetType() == type)
		{
			count += pItem->GetCount();
		}
	}

	return count;
}

void CHARACTER::RemoveSpecifyTypeItem(BYTE type, DWORD count)
{
	if (0 == count)
		return;

	for (UINT i = 0; i < INVENTORY_MAX_NUM; ++i)
	{
		if (NULL == GetInventoryItem(i))
			continue;

		if (GetInventoryItem(i)->GetType() != type)
			continue;

		//ê°œì¸ ìƒì ì— ë“±ë¡ëœ ë¬¼ê±´ì´ë©´ ë„˜ì–´ê°„ë‹¤. (ê°œì¸ ìƒì ì—ì„œ íŒë§¤ë ë•Œ ì´ ë¶€ë¶„ìœ¼ë¡œ ë“¤ì–´ì˜¬ ê²½ìš° ë¬¸ì œ!)
		if(m_pkMyShop)
		{
			bool isItemSelling = m_pkMyShop->IsSellingItem(GetInventoryItem(i)->GetID());
			if (isItemSelling)
				continue;
		}

		if (count >= GetInventoryItem(i)->GetCount())
		{
			count -= GetInventoryItem(i)->GetCount();
			GetInventoryItem(i)->SetCount(0);

			if (0 == count)
				return;
		}
		else
		{
			GetInventoryItem(i)->SetCount(GetInventoryItem(i)->GetCount() - count);
			return;
		}
	}
}

void CHARACTER::AutoGiveItem(LPITEM item, bool longOwnerShip)
{
	if (NULL == item)
	{
		sys_err ("NULL point.");
		return;
	}
	if (item->GetOwner())
	{
		sys_err ("item %d 's owner exists!",item->GetID());
		return;
	}
	
	int cell;
	if (item->IsDragonSoul())
	{
		cell = GetEmptyDragonSoulInventory(item);
	}
	else
	{
		cell = GetEmptyInventory (item->GetSize());
	}

	if (cell != -1)
	{
		if (item->IsDragonSoul())
			item->AddToCharacter(this, TItemPos(DRAGON_SOUL_INVENTORY, cell));
		else
			item->AddToCharacter(this, TItemPos(INVENTORY, cell));

		LogManager::instance().ItemLog(this, item, "SYSTEM", item->GetName());

		if (item->GetType() == ITEM_USE && item->GetSubType() == USE_POTION)
		{
			TQuickslot * pSlot;

			if (GetQuickslot(0, &pSlot) && pSlot->type == QUICKSLOT_TYPE_NONE)
			{
				TQuickslot slot;
				slot.type = QUICKSLOT_TYPE_ITEM;
				slot.pos = cell;
				SetQuickslot(0, slot);
			}
		}
	}
	else
	{
		item->AddToGround (GetMapIndex(), GetXYZ());
		item->StartDestroyEvent();

		if (longOwnerShip)
			item->SetOwnership (this, 300);
		else
			item->SetOwnership (this, 60);
		LogManager::instance().ItemLog(this, item, "SYSTEM_DROP", item->GetName());
	}
}

LPITEM CHARACTER::AutoGiveItem(DWORD dwItemVnum, BYTE bCount, int iRarePct, bool bMsg)
{
	TItemTable * p = ITEM_MANAGER::instance().GetTable(dwItemVnum);

	if (!p)
		return NULL;

	DBManager::instance().SendMoneyLog(MONEY_LOG_DROP, dwItemVnum, bCount);

	if (p->dwFlags & ITEM_FLAG_STACKABLE && p->bType != ITEM_BLEND) 
	{
		for (int i = 0; i < INVENTORY_MAX_NUM; ++i)
		{
			LPITEM item = GetInventoryItem(i);

			if (!item)
				continue;

			if (item->GetVnum() == dwItemVnum && FN_check_item_socket(item))
			{
				if (IS_SET(p->dwFlags, ITEM_FLAG_MAKECOUNT))
				{
					if (bCount < p->alValues[1])
						bCount = p->alValues[1];
				}

				BYTE bCount2 = MIN(200 - item->GetCount(), bCount);
				bCount -= bCount2;

				item->SetCount(item->GetCount() + bCount2);

				if (bCount == 0)
				{
					if (bMsg)
						ItemGetPacket(item->GetVnum(), item->GetCount());

					return item;
				}
			}
		}
	}

	LPITEM item = ITEM_MANAGER::instance().CreateItem(dwItemVnum, bCount, 0, true);

	if (!item)
	{
		sys_err("cannot create item by vnum %u (name: %s)", dwItemVnum, GetName());
		return NULL;
	}

	if (item->GetType() == ITEM_BLEND)
	{
		for (int i=0; i < INVENTORY_MAX_NUM; i++)
		{
			LPITEM inv_item = GetInventoryItem(i);

			if (inv_item == NULL) continue;

			if (inv_item->GetType() == ITEM_BLEND)
			{
				if (inv_item->GetVnum() == item->GetVnum())
				{
					if (inv_item->GetSocket(0) == item->GetSocket(0) &&
							inv_item->GetSocket(1) == item->GetSocket(1) &&
							inv_item->GetSocket(2) == item->GetSocket(2) &&
							inv_item->GetCount() < ITEM_MAX_COUNT)
					{
						inv_item->SetCount(inv_item->GetCount() + item->GetCount());
						return inv_item;
					}
				}
			}
		}
	}

	int iEmptyCell;
	if (item->IsDragonSoul())
	{
		iEmptyCell = GetEmptyDragonSoulInventory(item);
	}
	else
		iEmptyCell = GetEmptyInventory(item->GetSize());

	if (iEmptyCell != -1)
	{
		if (bMsg)
			ItemGetPacket(item->GetVnum(), item->GetCount());

		if (item->IsDragonSoul())
			item->AddToCharacter(this, TItemPos(DRAGON_SOUL_INVENTORY, iEmptyCell));
		else
			item->AddToCharacter(this, TItemPos(INVENTORY, iEmptyCell));
		LogManager::instance().ItemLog(this, item, "SYSTEM", item->GetName());

		if (item->GetType() == ITEM_USE && item->GetSubType() == USE_POTION)
		{
			TQuickslot * pSlot;

			if (GetQuickslot(0, &pSlot) && pSlot->type == QUICKSLOT_TYPE_NONE)
			{
				TQuickslot slot;
				slot.type = QUICKSLOT_TYPE_ITEM;
				slot.pos = iEmptyCell;
				SetQuickslot(0, slot);
			}
		}
	}
	else
	{
		item->AddToGround(GetMapIndex(), GetXYZ());
		item->StartDestroyEvent();
		// ì•ˆí‹° ë“œë flagê°€ ê±¸ë ¤ìˆëŠ” ì•„ì´í…œì˜ ê²½ìš°, 
		// ì¸ë²¤ì— ë¹ˆ ê³µê°„ì´ ì—†ì–´ì„œ ì–´ì©” ìˆ˜ ì—†ì´ ë–¨ì–´íŠ¸ë¦¬ê²Œ ë˜ë©´,
		// ownershipì„ ì•„ì´í…œì´ ì‚¬ë¼ì§ˆ ë•Œê¹Œì§€(300ì´ˆ) ìœ ì§€í•œë‹¤.
		if (IS_SET(item->GetAntiFlag(), ITEM_ANTIFLAG_DROP))
			item->SetOwnership(this, 300);
		else
			item->SetOwnership(this, 60);
		LogManager::instance().ItemLog(this, item, "SYSTEM_DROP", item->GetName());
	}

	sys_log(0, 
		"7: %d %d", dwItemVnum, bCount);
	return item;
}

bool CHARACTER::GiveItem(LPCHARACTER victim, TItemPos Cell)
{
	if (!CanHandleItem())
		return false;

	LPITEM item = GetItem(Cell);

	// MR-3: Auto-deactivate auto potions before trading/transferring
	if (item) {
		switch (item->GetVnum()) {
			case ITEM_AUTO_HP_RECOVERY_S:
			case ITEM_AUTO_HP_RECOVERY_M:
			case ITEM_AUTO_HP_RECOVERY_L:
			case ITEM_AUTO_HP_RECOVERY_X:
			case ITEM_AUTO_SP_RECOVERY_S:
			case ITEM_AUTO_SP_RECOVERY_M:
			case ITEM_AUTO_SP_RECOVERY_L:
			case ITEM_AUTO_SP_RECOVERY_X:
				if (item->GetSocket(0) == 1)
					item->SetSocket(0, 0);
				break;
			default:
				break;
		}
	}
	// MR-3: -- END OF -- Auto-deactivate auto potions before trading/transferring

	if (item && !item->IsExchanging())
	{
		if (victim->CanReceiveItem(this, item))
		{
			victim->ReceiveItem(this, item);
			return true;
		}
	}

	return false;
}

bool CHARACTER::CanReceiveItem(LPCHARACTER from, LPITEM item) const
{
	if (IsPC())
		return false;

	// TOO_LONG_DISTANCE_EXCHANGE_BUG_FIX
	if (DISTANCE_APPROX(GetX() - from->GetX(), GetY() - from->GetY()) > 2000)
		return false;
	// END_OF_TOO_LONG_DISTANCE_EXCHANGE_BUG_FIX

	switch (GetRaceNum())
	{
		case fishing::CAMPFIRE_MOB:
			if (item->GetType() == ITEM_FISH && 
					(item->GetSubType() == FISH_ALIVE || item->GetSubType() == FISH_DEAD))
				return true;
			break;

		case fishing::FISHER_MOB:
			if (item->GetType() == ITEM_ROD)
				return true;
			break;

			// BUILDING_NPC
		case BLACKSMITH_WEAPON_MOB:
		case DEVILTOWER_BLACKSMITH_WEAPON_MOB:
			if (item->GetType() == ITEM_WEAPON && 
					item->GetRefinedVnum())
				return true;
			else
				return false;
			break;

		case BLACKSMITH_ARMOR_MOB:
		case DEVILTOWER_BLACKSMITH_ARMOR_MOB:
			if (item->GetType() == ITEM_ARMOR && 
					(item->GetSubType() == ARMOR_BODY || item->GetSubType() == ARMOR_SHIELD || item->GetSubType() == ARMOR_HEAD) &&
					item->GetRefinedVnum())
				return true;
			else
				return false;
			break;

		case BLACKSMITH_ACCESSORY_MOB:
		case DEVILTOWER_BLACKSMITH_ACCESSORY_MOB:
			if (item->GetType() == ITEM_ARMOR &&
					!(item->GetSubType() == ARMOR_BODY || item->GetSubType() == ARMOR_SHIELD || item->GetSubType() == ARMOR_HEAD) &&
					item->GetRefinedVnum())
				return true;
			else
				return false;
			break;
			// END_OF_BUILDING_NPC

		case BLACKSMITH_MOB:
			if (item->GetRefinedVnum() && item->GetRefineSet() < 500)
			{
				return true;
			}
			else
			{
				return false;
			}

		case BLACKSMITH2_MOB:
			if (item->GetRefineSet() >= 500)
			{
				return true;
			}
			else
			{
				return false;
			}

		case ALCHEMIST_MOB:
			if (item->GetRefinedVnum())
				return true;
			break;

		case 20101:
		case 20102:
		case 20103:
			// ì´ˆê¸‰ ë§
			if (item->GetVnum() == ITEM_REVIVE_HORSE_1)
			{
				if (!IsDead())
				{
					from->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì£½ì§€ ì•Šì€ ë§ì—ê²Œ ì„ ì´ˆë¥¼ ë¨¹ì¼ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
					return false;
				}
				return true;
			}
			else if (item->GetVnum() == ITEM_HORSE_FOOD_1)
			{
				if (IsDead())
				{
					from->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì£½ì€ ë§ì—ê²Œ ì‚¬ë£Œë¥¼ ë¨¹ì¼ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
					return false;
				}
				return true;
			}
			else if (item->GetVnum() == ITEM_HORSE_FOOD_2 || item->GetVnum() == ITEM_HORSE_FOOD_3)
			{
				return false;
			}
			break;
		case 20104:
		case 20105:
		case 20106:
			// ì¤‘ê¸‰ ë§
			if (item->GetVnum() == ITEM_REVIVE_HORSE_2)
			{
				if (!IsDead())
				{
					from->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì£½ì§€ ì•Šì€ ë§ì—ê²Œ ì„ ì´ˆë¥¼ ë¨¹ì¼ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
					return false;
				}
				return true;
			}
			else if (item->GetVnum() == ITEM_HORSE_FOOD_2)
			{
				if (IsDead())
				{
					from->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì£½ì€ ë§ì—ê²Œ ì‚¬ë£Œë¥¼ ë¨¹ì¼ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
					return false;
				}
				return true;
			}
			else if (item->GetVnum() == ITEM_HORSE_FOOD_1 || item->GetVnum() == ITEM_HORSE_FOOD_3)
			{
				return false;
			}
			break;
		case 20107:
		case 20108:
		case 20109:
			// ê³ ê¸‰ ë§
			if (item->GetVnum() == ITEM_REVIVE_HORSE_3)
			{
				if (!IsDead())
				{
					from->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì£½ì§€ ì•Šì€ ë§ì—ê²Œ ì„ ì´ˆë¥¼ ë¨¹ì¼ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
					return false;
				}
				return true;
			}
			else if (item->GetVnum() == ITEM_HORSE_FOOD_3)
			{
				if (IsDead())
				{
					from->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì£½ì€ ë§ì—ê²Œ ì‚¬ë£Œë¥¼ ë¨¹ì¼ ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
					return false;
				}
				return true;
			}
			else if (item->GetVnum() == ITEM_HORSE_FOOD_1 || item->GetVnum() == ITEM_HORSE_FOOD_2)
			{
				return false;
			}
			break;
	}

	//if (IS_SET(item->GetFlag(), ITEM_FLAG_QUEST_GIVE))
	{
		return true;
	}

	return false;
}

void CHARACTER::ReceiveItem(LPCHARACTER from, LPITEM item)
{
	if (IsPC())
		return;

	switch (GetRaceNum())
	{
		case fishing::CAMPFIRE_MOB:
			if (item->GetType() == ITEM_FISH && (item->GetSubType() == FISH_ALIVE || item->GetSubType() == FISH_DEAD))
				fishing::Grill(from, item);
			else
			{
				// TAKE_ITEM_BUG_FIX
				from->SetQuestNPCID(GetVID());
				// END_OF_TAKE_ITEM_BUG_FIX
				quest::CQuestManager::instance().TakeItem(from->GetPlayerID(), GetRaceNum(), item);
			}
			break;

			// DEVILTOWER_NPC 
		case DEVILTOWER_BLACKSMITH_WEAPON_MOB:
		case DEVILTOWER_BLACKSMITH_ARMOR_MOB:
		case DEVILTOWER_BLACKSMITH_ACCESSORY_MOB:
			if (item->GetRefinedVnum() != 0 && item->GetRefineSet() != 0 && item->GetRefineSet() < 500)
			{
				from->SetRefineNPC(this);
				from->RefineInformation(item->GetCell(), REFINE_TYPE_MONEY_ONLY);
			}
			else
			{
				from->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ ì•„ì´í…œì€ ê°œëŸ‰í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
			}
			break;
			// END_OF_DEVILTOWER_NPC

		case BLACKSMITH_MOB:
		case BLACKSMITH2_MOB:
		case BLACKSMITH_WEAPON_MOB:
		case BLACKSMITH_ARMOR_MOB:
		case BLACKSMITH_ACCESSORY_MOB:
			if (item->GetRefinedVnum())
			{
				from->SetRefineNPC(this);
				from->RefineInformation(item->GetCell(), REFINE_TYPE_NORMAL);
			}
			else
			{
				from->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ ì•„ì´í…œì€ ê°œëŸ‰í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
			}
			break;

		case 20101:
		case 20102:
		case 20103:
		case 20104:
		case 20105:
		case 20106:
		case 20107:
		case 20108:
		case 20109:
			if (item->GetVnum() == ITEM_REVIVE_HORSE_1 ||
					item->GetVnum() == ITEM_REVIVE_HORSE_2 ||
					item->GetVnum() == ITEM_REVIVE_HORSE_3)
			{
				from->ReviveHorse();
				item->SetCount(item->GetCount()-1);
				from->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë§ì—ê²Œ ì„ ì´ˆë¥¼ ì£¼ì—ˆìŠµë‹ˆë‹¤."));
			}
			else if (item->GetVnum() == ITEM_HORSE_FOOD_1 ||
					item->GetVnum() == ITEM_HORSE_FOOD_2 ||
					item->GetVnum() == ITEM_HORSE_FOOD_3)
			{
				from->FeedHorse();
				from->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë§ì—ê²Œ ì‚¬ë£Œë¥¼ ì£¼ì—ˆìŠµë‹ˆë‹¤."));
				item->SetCount(item->GetCount()-1);
				EffectPacket(SE_HPUP_RED);
			}
			break;

		default:
			sys_log(0, "TakeItem %s %d %s", from->GetName(), GetRaceNum(), item->GetName());
			from->SetQuestNPCID(GetVID());
			quest::CQuestManager::instance().TakeItem(from->GetPlayerID(), GetRaceNum(), item);
			break;
	}
}

bool CHARACTER::IsEquipUniqueItem(DWORD dwItemVnum) const
{
	{
		LPITEM u = GetWear(WEAR_UNIQUE1);

		if (u && u->GetVnum() == dwItemVnum)
			return true;
	}

	{
		LPITEM u = GetWear(WEAR_UNIQUE2);

		if (u && u->GetVnum() == dwItemVnum)
			return true;
	}

	// ì–¸ì–´ë°˜ì§€ì¸ ê²½ìš° ì–¸ì–´ë°˜ì§€(ê²¬ë³¸) ì¸ì§€ë„ ì²´í¬í•œë‹¤.
	if (dwItemVnum == UNIQUE_ITEM_RING_OF_LANGUAGE)
		return IsEquipUniqueItem(UNIQUE_ITEM_RING_OF_LANGUAGE_SAMPLE);

	return false;
}

// CHECK_UNIQUE_GROUP
bool CHARACTER::IsEquipUniqueGroup(DWORD dwGroupVnum) const
{
	{
		LPITEM u = GetWear(WEAR_UNIQUE1);

		if (u && u->GetSpecialGroup() == (int) dwGroupVnum)
			return true;
	}

	{
		LPITEM u = GetWear(WEAR_UNIQUE2);

		if (u && u->GetSpecialGroup() == (int) dwGroupVnum)
			return true;
	}

	return false;
}
// END_OF_CHECK_UNIQUE_GROUP

void CHARACTER::SetRefineMode(int iAdditionalCell)
{
	m_iRefineAdditionalCell = iAdditionalCell;
	m_bUnderRefine = true;
}

void CHARACTER::ClearRefineMode()
{
	m_bUnderRefine = false;
	SetRefineNPC( NULL );
}

bool CHARACTER::GiveItemFromSpecialItemGroup(DWORD dwGroupNum, std::vector<DWORD> &dwItemVnums, 
											std::vector<DWORD> &dwItemCounts, std::vector <LPITEM> &item_gets, int &count)
{
	const CSpecialItemGroup* pGroup = ITEM_MANAGER::instance().GetSpecialItemGroup(dwGroupNum);

	if (!pGroup)
	{
		sys_err("cannot find special item group %d", dwGroupNum);
		return false;
	}

	std::vector <int> idxes;
	int n = pGroup->GetMultiIndex(idxes);

	bool bSuccess;

	for (int i = 0; i < n; i++)
	{
		bSuccess = false;
		int idx = idxes[i];
		DWORD dwVnum = pGroup->GetVnum(idx);
		DWORD dwCount = pGroup->GetCount(idx);
		int	iRarePct = pGroup->GetRarePct(idx);
		LPITEM item_get = NULL;
		switch (dwVnum)
		{
			case CSpecialItemGroup::GOLD:
				PointChange(POINT_GOLD, dwCount);
				LogManager::instance().CharLog(this, dwCount, "TREASURE_GOLD", "");

				bSuccess = true;
				break;
			case CSpecialItemGroup::EXP:
				{
					PointChange(POINT_EXP, dwCount);
					LogManager::instance().CharLog(this, dwCount, "TREASURE_EXP", "");

					bSuccess = true;
				}
				break;

			case CSpecialItemGroup::MOB:
				{
					sys_log(0, "CSpecialItemGroup::MOB %d", dwCount);
					int x = GetX() + number(-500, 500);
					int y = GetY() + number(-500, 500);

					LPCHARACTER ch = CHARACTER_MANAGER::instance().SpawnMob(dwCount, GetMapIndex(), x, y, 0, true, -1);
					if (ch)
						ch->SetAggressive();
					bSuccess = true;
				}
				break;
			case CSpecialItemGroup::SLOW:
				{
					sys_log(0, "CSpecialItemGroup::SLOW %d", -(int)dwCount);
					AddAffect(AFFECT_SLOW, POINT_MOV_SPEED, -(int)dwCount, AFF_SLOW, 300, 0, true);
					bSuccess = true;
				}
				break;
			case CSpecialItemGroup::DRAIN_HP:
				{
					int iDropHP = GetMaxHP()*dwCount/100;
					sys_log(0, "CSpecialItemGroup::DRAIN_HP %d", -iDropHP);
					iDropHP = MIN(iDropHP, GetHP()-1);
					sys_log(0, "CSpecialItemGroup::DRAIN_HP %d", -iDropHP);
					PointChange(POINT_HP, -iDropHP);
					bSuccess = true;
				}
				break;
			case CSpecialItemGroup::POISON:
				{
					AttackedByPoison(NULL);
					bSuccess = true;
				}
				break;

			case CSpecialItemGroup::MOB_GROUP:
				{
					int sx = GetX() - number(300, 500);
					int sy = GetY() - number(300, 500);
					int ex = GetX() + number(300, 500);
					int ey = GetY() + number(300, 500);
					CHARACTER_MANAGER::instance().SpawnGroup(dwCount, GetMapIndex(), sx, sy, ex, ey, NULL, true);

					bSuccess = true;
				}
				break;
			default:
				{
					item_get = AutoGiveItem(dwVnum, dwCount, iRarePct);

					if (item_get)
					{
						bSuccess = true;
					}
				}
				break;
		}
	
		if (bSuccess)
		{
			dwItemVnums.push_back(dwVnum);
			dwItemCounts.push_back(dwCount);
			item_gets.push_back(item_get);
			count++;

		}
		else
		{
			return false;
		}
	}
	return bSuccess;
}

// NEW_HAIR_STYLE_ADD
bool CHARACTER::ItemProcess_Hair(LPITEM item, int iDestCell)
{
	if (item->CheckItemUseLevel(GetLevel()) == false)
	{
		// ë ˆë²¨ ì œí•œì— ê±¸ë¦¼
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì•„ì§ ì´ ë¨¸ë¦¬ë¥¼ ì‚¬ìš©í•  ìˆ˜ ì—†ëŠ” ë ˆë²¨ì…ë‹ˆë‹¤."));
		return false;
	}

	DWORD hair = item->GetVnum();

	switch (GetJob())
	{
		case JOB_WARRIOR :
			hair -= 72000; // 73001 - 72000 = 1001 ë¶€í„° í—¤ì–´ ë²ˆí˜¸ ì‹œì‘
			break;

		case JOB_ASSASSIN :
			hair -= 71250;
			break;

		case JOB_SURA :
			hair -= 70500;
			break;

		case JOB_SHAMAN :
			hair -= 69750;
			break;

		default :
			return false;
			break;
	}

	if (hair == GetPart(PART_HAIR))
	{
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë™ì¼í•œ ë¨¸ë¦¬ ìŠ¤íƒ€ì¼ë¡œëŠ” êµì²´í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
		return true;
	}

	item->SetCount(item->GetCount() - 1);

	SetPart(PART_HAIR, hair);
	UpdatePacket();

	return true;
}
// END_NEW_HAIR_STYLE_ADD

bool CHARACTER::ItemProcess_Polymorph(LPITEM item)
{
	if (IsPolymorphed())
	{
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì´ë¯¸ ë‘”ê°‘ì¤‘ì¸ ìƒíƒœì…ë‹ˆë‹¤."));
		return false;
	}

	if (true == IsRiding())
	{
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë‘”ê°‘í•  ìˆ˜ ì—†ëŠ” ìƒíƒœì…ë‹ˆë‹¤."));
		return false;
	}

	DWORD dwVnum = item->GetSocket(0);

	if (dwVnum == 0)
	{
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì˜ëª»ëœ ë‘”ê°‘ ì•„ì´í…œì…ë‹ˆë‹¤."));
		item->SetCount(item->GetCount()-1);
		return false;
	}

	const CMob* pMob = CMobManager::instance().Get(dwVnum);

	if (pMob == NULL)
	{
		ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì˜ëª»ëœ ë‘”ê°‘ ì•„ì´í…œì…ë‹ˆë‹¤."));
		item->SetCount(item->GetCount()-1);
		return false;
	}

	switch (item->GetVnum())
	{
		case 70104 :
		case 70105 :
		case 70106 :
		case 70107 :
		case 71093 :
			{
				// ë‘”ê°‘êµ¬ ì²˜ë¦¬
				sys_log(0, "USE_POLYMORPH_BALL PID(%d) vnum(%d)", GetPlayerID(), dwVnum);

				// ë ˆë²¨ ì œí•œ ì²´í¬
				int iPolymorphLevelLimit = MAX(0, 20 - GetLevel() * 3 / 10);
				if (pMob->m_table.bLevel >= GetLevel() + iPolymorphLevelLimit)
				{
					ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë‚˜ë³´ë‹¤ ë„ˆë¬´ ë†’ì€ ë ˆë²¨ì˜ ëª¬ìŠ¤í„°ë¡œëŠ” ë³€ì‹  í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
					return false;
				}

				int iDuration = GetSkillLevel(POLYMORPH_SKILL_ID) == 0 ? 5 : (5 + (5 + GetSkillLevel(POLYMORPH_SKILL_ID)/40 * 25));
				iDuration *= 60;

				DWORD dwBonus = 0;
				
				if (true == LC_IsYMIR() || true == LC_IsKorea())
				{
					dwBonus = GetSkillLevel(POLYMORPH_SKILL_ID) + 60;
				}
				else
				{
					dwBonus = (2 + GetSkillLevel(POLYMORPH_SKILL_ID)/40) * 100;
				}

				AddAffect(AFFECT_POLYMORPH, POINT_POLYMORPH, dwVnum, AFF_POLYMORPH, iDuration, 0, true);
				AddAffect(AFFECT_POLYMORPH, POINT_ATT_BONUS, dwBonus, AFF_POLYMORPH, iDuration, 0, false);
				
				item->SetCount(item->GetCount()-1);
			}
			break;

		case 50322:
			{
				// ë³´ë¥˜

				// ë‘”ê°‘ì„œ ì²˜ë¦¬
				// ì†Œì¼“0                ì†Œì¼“1           ì†Œì¼“2   
				// ë‘”ê°‘í•  ëª¬ìŠ¤í„° ë²ˆí˜¸   ìˆ˜ë ¨ì •ë„        ë‘”ê°‘ì„œ ë ˆë²¨
				sys_log(0, "USE_POLYMORPH_BOOK: %s(%u) vnum(%u)", GetName(), GetPlayerID(), dwVnum);

				if (CPolymorphUtils::instance().PolymorphCharacter(this, item, pMob) == true)
				{
					CPolymorphUtils::instance().UpdateBookPracticeGrade(this, item);
				}
				else
				{
				}
			}
			break;

		default :
			sys_err("POLYMORPH invalid item passed PID(%d) vnum(%d)", GetPlayerID(), item->GetOriginalVnum());
			return false;
	}

	return true;
}

bool CHARACTER::CanDoCube() const
{
	if (m_bIsObserver)	return false;
	if (GetShop())		return false;
	if (GetMyShop())	return false;
	if (m_bUnderRefine)	return false;
	if (IsWarping())	return false;

	return true;
}

bool CHARACTER::UnEquipSpecialRideUniqueItem()
{
	LPITEM Unique1 = GetWear(WEAR_UNIQUE1);
	LPITEM Unique2 = GetWear(WEAR_UNIQUE2);

	if( NULL != Unique1 )
	{
		if( UNIQUE_GROUP_SPECIAL_RIDE == Unique1->GetSpecialGroup() )
		{
			return UnequipItem(Unique1);
		}
	}

	if( NULL != Unique2 )
	{
		if( UNIQUE_GROUP_SPECIAL_RIDE == Unique2->GetSpecialGroup() )
		{
			return UnequipItem(Unique2);
		}
	}

	return true;
}

void CHARACTER::AutoRecoveryItemProcess(const EAffectTypes type)
{
	if (true == IsDead() || true == IsStun())
		return;

	if (false == IsPC())
		return;

	if (AFFECT_AUTO_HP_RECOVERY != type && AFFECT_AUTO_SP_RECOVERY != type)
		return;

	if (NULL != FindAffect(AFFECT_STUN))
		return;

	{
		const DWORD stunSkills[] = { SKILL_TANHWAN, SKILL_GEOMPUNG, SKILL_BYEURAK, SKILL_GIGUNG };

		for (size_t i=0 ; i < sizeof(stunSkills)/sizeof(DWORD) ; ++i)
		{
			const CAffect* p = FindAffect(stunSkills[i]);

			if (NULL != p && AFF_STUN == p->dwFlag)
				return;
		}
	}

	const CAffect* pAffect = FindAffect(type);
	const size_t idx_of_amount_of_used = 1;
	const size_t idx_of_amount_of_full = 2;

	if (NULL != pAffect)
	{
		LPITEM pItem = FindItemByID(pAffect->dwFlag);

		if (NULL != pItem && pItem->GetSocket(0) != 0)
		{
			if (false == CArenaManager::instance().IsArenaMap(GetMapIndex()))
			{
				const long amount_of_used = pItem->GetSocket(idx_of_amount_of_used);
				const long amount_of_full = pItem->GetSocket(idx_of_amount_of_full);
				
				const int32_t avail = amount_of_full - amount_of_used;

				int32_t amount = 0;

				if (AFFECT_AUTO_HP_RECOVERY == type)
				{
					amount = GetMaxHP() - (GetHP() + GetPoint(POINT_HP_RECOVERY));
				}
				else if (AFFECT_AUTO_SP_RECOVERY == type)
				{
					amount = GetMaxSP() - (GetSP() + GetPoint(POINT_SP_RECOVERY));
				}

				if (amount > 0)
				{
					if (avail > amount)
					{
						const int pct_of_used = amount_of_used * 100 / amount_of_full;
						const int pct_of_will_used = (amount_of_used + amount) * 100 / amount_of_full;

						bool bLog = false;
						// ì‚¬ìš©ëŸ‰ì˜ 10% ë‹¨ìœ„ë¡œ ë¡œê·¸ë¥¼ ë‚¨ê¹€
						// (ì‚¬ìš©ëŸ‰ì˜ %ì—ì„œ, ì‹­ì˜ ìë¦¬ê°€ ë°”ë€” ë•Œë§ˆë‹¤ ë¡œê·¸ë¥¼ ë‚¨ê¹€.)
						if ((pct_of_will_used / 10) - (pct_of_used / 10) >= 1)
							bLog = true;
						pItem->SetSocket(idx_of_amount_of_used, amount_of_used + amount, bLog);
					}
					else
					{
						amount = avail;

						ITEM_MANAGER::instance().RemoveItem( pItem );
					}

					if (AFFECT_AUTO_HP_RECOVERY == type)
					{
						PointChange( POINT_HP_RECOVERY, amount );
						EffectPacket( SE_AUTO_HPUP );
					}
					else if (AFFECT_AUTO_SP_RECOVERY == type)
					{
						PointChange( POINT_SP_RECOVERY, amount );
						EffectPacket( SE_AUTO_SPUP );
					}
				}
			}
			else
			{
				pItem->Lock(false);
				pItem->SetSocket(0, false);
				RemoveAffect( const_cast<CAffect*>(pAffect) );
			}
		}
		else
		{
			RemoveAffect( const_cast<CAffect*>(pAffect) );
		}
	}
}

bool CHARACTER::IsValidItemPosition(TItemPos Pos) const
{
	BYTE window_type = Pos.window_type;
	WORD cell = Pos.cell;
	
	switch (window_type)
	{
	case RESERVED_WINDOW:
		return false;

	case INVENTORY:
	case EQUIPMENT:
		return cell < (INVENTORY_AND_EQUIP_SLOT_MAX);

	case DRAGON_SOUL_INVENTORY:
		return cell < (DRAGON_SOUL_INVENTORY_MAX_NUM);

	case SAFEBOX:
		if (NULL != m_pkSafebox)
			return m_pkSafebox->IsValidPosition(cell);
		else
			return false;

	case MALL:
		if (NULL != m_pkMall)
			return m_pkMall->IsValidPosition(cell);
		else
			return false;
	default:
		return false;
	}
}


// ê·€ì°®ì•„ì„œ ë§Œë“  ë§¤í¬ë¡œ.. expê°€ trueë©´ msgë¥¼ ì¶œë ¥í•˜ê³  return false í•˜ëŠ” ë§¤í¬ë¡œ (ì¼ë°˜ì ì¸ verify ìš©ë„ë‘ì€ return ë•Œë¬¸ì— ì•½ê°„ ë°˜ëŒ€ë¼ ì´ë¦„ë•Œë¬¸ì— í—·ê°ˆë¦´ ìˆ˜ë„ ìˆê² ë‹¤..)
#define VERIFY_MSG(exp, msg)  \
	if (true == (exp)) { \
			ChatPacket(CHAT_TYPE_INFO, LC_TEXT(msg)); \
			return false; \
	}

		
/// í˜„ì¬ ìºë¦­í„°ì˜ ìƒíƒœë¥¼ ë°”íƒ•ìœ¼ë¡œ ì£¼ì–´ì§„ itemì„ ì°©ìš©í•  ìˆ˜ ìˆëŠ” ì§€ í™•ì¸í•˜ê³ , ë¶ˆê°€ëŠ¥ í•˜ë‹¤ë©´ ìºë¦­í„°ì—ê²Œ ì´ìœ ë¥¼ ì•Œë ¤ì£¼ëŠ” í•¨ìˆ˜
bool CHARACTER::CanEquipNow(const LPITEM item, const TItemPos& srcCell, const TItemPos& destCell) /*const*/
{
	const TItemTable* itemTable = item->GetProto();
	BYTE itemType = item->GetType();
	BYTE itemSubType = item->GetSubType();

	switch (GetJob())
	{
		case JOB_WARRIOR:
			if (item->GetAntiFlag() & ITEM_ANTIFLAG_WARRIOR)
				return false;
			break;

		case JOB_ASSASSIN:
			if (item->GetAntiFlag() & ITEM_ANTIFLAG_ASSASSIN)
				return false;
			break;

		case JOB_SHAMAN:
			if (item->GetAntiFlag() & ITEM_ANTIFLAG_SHAMAN)
				return false;
			break;

		case JOB_SURA:
			if (item->GetAntiFlag() & ITEM_ANTIFLAG_SURA)
				return false;
			break;
	}

	for (int i = 0; i < ITEM_LIMIT_MAX_NUM; ++i)
	{
		long limit = itemTable->aLimits[i].lValue;
		switch (itemTable->aLimits[i].bType)
		{
			case LIMIT_LEVEL:
				if (GetLevel() < limit)
				{
					ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë ˆë²¨ì´ ë‚®ì•„ ì°©ìš©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
					return false;
				}
				break;

			case LIMIT_STR:
				if (GetPoint(POINT_ST) < limit)
				{
					ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê·¼ë ¥ì´ ë‚®ì•„ ì°©ìš©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
					return false;
				}
				break;

			case LIMIT_INT:
				if (GetPoint(POINT_IQ) < limit)
				{
					ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì§€ëŠ¥ì´ ë‚®ì•„ ì°©ìš©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
					return false;
				}
				break;

			case LIMIT_DEX:
				if (GetPoint(POINT_DX) < limit)
				{
					ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ë¯¼ì²©ì´ ë‚®ì•„ ì°©ìš©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
					return false;
				}
				break;

			case LIMIT_CON:
				if (GetPoint(POINT_HT) < limit)
				{
					ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ì²´ë ¥ì´ ë‚®ì•„ ì°©ìš©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
					return false;
				}
				break;
		}
	}

	if (item->GetWearFlag() & WEARABLE_UNIQUE)
	{
		if ((GetWear(WEAR_UNIQUE1) && GetWear(WEAR_UNIQUE1)->IsSameSpecialGroup(item)) ||
			(GetWear(WEAR_UNIQUE2) && GetWear(WEAR_UNIQUE2)->IsSameSpecialGroup(item)))
		{
			ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê°™ì€ ì¢…ë¥˜ì˜ ìœ ë‹ˆí¬ ì•„ì´í…œ ë‘ ê°œë¥¼ ë™ì‹œì— ì¥ì°©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
			return false;
		}

		if (marriage::CManager::instance().IsMarriageUniqueItem(item->GetVnum()) && 
			!marriage::CManager::instance().IsMarried(GetPlayerID()))
		{
			ChatPacket(CHAT_TYPE_INFO, LC_TEXT("ê²°í˜¼í•˜ì§€ ì•Šì€ ìƒíƒœì—ì„œ ì˜ˆë¬¼ì„ ì°©ìš©í•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤."));
			return false;
		}

		// MR-8: Prevent mounting in Nemere's Watchtower
		if (item->GetSpecialGroup() == UNIQUE_GROUP_SPECIAL_RIDE)
		{
			long lMapIndex = GetMapIndex();
			bool isInNemereDungeon = lMapIndex >= 352 * 10000 && lMapIndex < 353 * 10000;

			if (isInNemereDungeon && !IsRiding())
			{
				ChatPacket(CHAT_TYPE_INFO, LC_TEXT("You cannot ride your horse in Nemere's Watchtower."));
				return false;
			}
		}
		// MR-8: -- END OF -- Prevent mounting in Nemere's Watchtower
	}

	return true;
}

/// Checks if the currently equipped item can be unequipped based on the character's state, and if not, informs the character of the reason.
bool CHARACTER::CanUnequipNow(const LPITEM item, const TItemPos& srcCell, const TItemPos& destCell) /*const*/
{	

	if (ITEM_BELT == item->GetType())
		VERIFY_MSG(CBeltInventoryHelper::IsExistItemInBeltInventory(this), "ë²¨íŠ¸ ì¸ë²¤í† ë¦¬ì— ì•„ì´í…œì´ ì¡´ì¬í•˜ë©´ í•´ì œí•  ìˆ˜ ì—†ìŠµë‹ˆë‹¤.");

	// Items that can never be unequipped
	if (IS_SET(item->GetFlag(), ITEM_FLAG_IRREMOVABLE))
		return false;

	// Check if there is enough space in the inventory when moving an item during unequip
	// Skip this check if destCell is specified (swapping with another item)
	if (destCell.IsValidItemPosition() == false)
	{
		int pos = -1;

		if (item->IsDragonSoul())
			pos = GetEmptyDragonSoulInventory(item);
		else
			pos = GetEmptyInventory(item->GetSize());

		VERIFY_MSG( -1 == pos, "ì†Œì§€í’ˆì— ë¹ˆ ê³µê°„ì´ ì—†ìŠµë‹ˆë‹¤." );
	}


	return true;
}

