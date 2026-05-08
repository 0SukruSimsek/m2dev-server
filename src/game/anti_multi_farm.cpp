#include "stdafx.h"

#ifdef ENABLE_ANTI_MULTIPLE_FARM

#include "anti_multi_farm.h"
#include "char.h"
#include "desc.h"
#include "desc_manager.h"
#include "config.h"
#include "constants.h"

#include <algorithm>
#include <cstring>
#include <vector>

// --- Config (config.cpp'de tanımlı) -----------------------------------------
extern bool g_bAntiMultiFarmEnabled;
extern int  g_iAntiMultiFarmMaxFull;   // ilk N slot FULL (default 2)
extern int  g_iAntiMultiFarmMaxNoExp;  // sonraki M slot drop+yang only (default 2)
extern int  g_iAntiMultiFarmMaxShop;   // bir sonraki K slot shop only (default 1)
extern bool g_bAntiMultiFarmExemptBoss;
// Toplam giriş cap'i = MaxFull + MaxNoExp + MaxShop  (default 2+2+1 = 5)

namespace
{
	inline bool IsLoopbackHost(const char* host)
	{
		if (!host) return false;
		// 127.x.x.x veya localhost
		if (!std::strcmp(host, "localhost")) return true;
		if (!std::strncmp(host, "127.", 4))  return true;
		return false;
	}

	// Aynı IP'den online ve karaktere sahip descriptor'ları PlayerID'ye göre
	// sıralı toplar. pkChar'ın bu listedeki indeksini de döner (-1 yoksa).
	int CollectSameIpSlot(CHARACTER* pkChar, std::vector<DWORD>* pOutPlayerIds = nullptr)
	{
		if (!pkChar) return -1;
		LPDESC d = pkChar->GetDesc();
		if (!d) return -1;
		const char* host = d->GetHostName();
		if (!host || !*host) return -1;

		const DESC_MANAGER::DESC_SET& cont = DESC_MANAGER::instance().GetClientSet();

		std::vector<DWORD> ids;
		ids.reserve(8);

		for (auto it = cont.begin(); it != cont.end(); ++it)
		{
			LPDESC od = *it;
			if (!od) continue;
			LPCHARACTER ch = od->GetCharacter();
			if (!ch) continue;
			const char* ohost = od->GetHostName();
			if (!ohost) continue;
			if (std::strcmp(host, ohost) != 0) continue;
			ids.push_back(ch->GetPlayerID());
		}

		std::sort(ids.begin(), ids.end());

		const DWORD myId = pkChar->GetPlayerID();
		int idx = -1;
		for (size_t i = 0; i < ids.size(); ++i)
		{
			if (ids[i] == myId) { idx = static_cast<int>(i); break; }
		}

		if (pOutPlayerIds) pOutPlayerIds->swap(ids);
		return idx;
	}
}

namespace AntiMultiFarm
{
	ETier GetTier(CHARACTER* pkChar)
	{
		if (!g_bAntiMultiFarmEnabled || !pkChar)
			return TIER_FULL;

		// GM her zaman muaf
		if (pkChar->IsGM())
			return TIER_FULL;

		LPDESC d = pkChar->GetDesc();
		if (!d) return TIER_FULL;

		// Loopback / aynı makine her zaman muaf (test ortamı)
		if (IsLoopbackHost(d->GetHostName()))
			return TIER_FULL;

		const int slot = CollectSameIpSlot(pkChar);
		if (slot < 0) return TIER_FULL;

		const int full   = std::max(0, g_iAntiMultiFarmMaxFull);
		const int noExp  = std::max(0, g_iAntiMultiFarmMaxNoExp);
		const int shop   = std::max(0, g_iAntiMultiFarmMaxShop);

		if (slot < full)                       return TIER_FULL;
		if (slot < full + noExp)               return TIER_NO_EXP;
		if (slot < full + noExp + shop)        return TIER_SHOP;
		return TIER_REJECT;
	}

	bool CanReceiveExp(CHARACTER* pkChar)
	{
		const ETier t = GetTier(pkChar);
		return (t == TIER_FULL);
	}

	bool CanReceiveDrop(CHARACTER* pkChar)
	{
		const ETier t = GetTier(pkChar);
		return (t == TIER_FULL || t == TIER_NO_EXP);
	}

	bool CanReceiveYang(CHARACTER* pkChar)
	{
		const ETier t = GetTier(pkChar);
		return (t == TIER_FULL || t == TIER_NO_EXP);
	}

	bool CheckLoginAllowed(DESC* pkDesc)
	{
		if (!g_bAntiMultiFarmEnabled || !pkDesc)
			return true;

		LPCHARACTER pkChar = pkDesc->GetCharacter();
		if (!pkChar) return true;

		// GM ve loopback exempt
		if (pkChar->IsGM()) return true;
		if (IsLoopbackHost(pkDesc->GetHostName())) return true;

		const int slot = CollectSameIpSlot(pkChar);
		if (slot < 0) return true;

		const int full  = std::max(0, g_iAntiMultiFarmMaxFull);
		const int noExp = std::max(0, g_iAntiMultiFarmMaxNoExp);
		const int shop  = std::max(0, g_iAntiMultiFarmMaxShop);
		const int cap   = full + noExp + shop;

		if (slot >= cap)
		{
			sys_log(0, "ANTI_MULTI_FARM: REJECT %s ip=%s slot=%d cap=%d",
				pkChar->GetName(), pkDesc->GetHostName(), slot, cap);

			// Kullanıcıya bilgi mesajı + kapatma
			pkChar->ChatPacket(CHAT_TYPE_INFO,
				"[Anti-Multi-Farm] Bu IP icin maksimum %d hesap online olabilir.",
				cap);
			pkDesc->DelayedDisconnect(3);
			return false;
		}
		return true;
	}

	bool IsExemptKillTarget(CHARACTER* pkVictim)
	{
		if (!g_bAntiMultiFarmExemptBoss || !pkVictim)
			return false;

		// Metin taşı her zaman muaf (party stone farm meşru aktivite)
		if (pkVictim->IsStone())
			return true;

		// Boss ve king rank muaf
		if (pkVictim->GetMobRank() >= MOB_RANK_BOSS)
			return true;

		return false;
	}
}

#endif // ENABLE_ANTI_MULTIPLE_FARM
