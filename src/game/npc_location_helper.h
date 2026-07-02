#pragma once
#ifdef ENABLE_NPC_LOCATION_HELPER

#include <map>
#include <string>
#include <vector>
#include "packet_structs.h"

class CHARACTER;
typedef CHARACTER* LPCHARACTER;

class CNpcLocationHelperManager : public singleton<CNpcLocationHelperManager>
{
public:
	enum EResult : BYTE
	{
		RESULT_OK,
		RESULT_NOT_ACTIVE,
		RESULT_CANNOT_USE,
		RESULT_COOLDOWN,
		RESULT_INVALID_TARGET,
		RESULT_LEVEL_LIMIT,
		RESULT_EMPIRE_LIMIT,
		RESULT_NEED_ITEM,
		RESULT_NEED_YANG,
	};

	CNpcLocationHelperManager();

	void Initialize();
	void RequestStatus(LPCHARACTER ch);
	void HandlePacket(LPCHARACTER ch, const TPacketCGNPCLocationHelper& packet);
	void Activate(LPCHARACTER ch);
	bool UnlockMapByItem(LPCHARACTER ch, DWORD itemVnum, long mapIndex);
	bool IsActive(LPCHARACTER ch) const;

private:
	struct SMapInfo
	{
		long mapIndex;
		std::string mapName;
		std::string mapDir;
		int category;
		int minLevel;
		int minConquerorLevel;
		int empire;
		int showAtlas;
		int showNpcList;
		int warpNeedYang;
		DWORD warpNeedItemVnum;
		int warpNeedItemCount;
	};

	struct SNpcInfo
	{
		long mapIndex;
		DWORD vnum;
		long x;
		long y;
	};

	bool Load();
	bool LoadMapInfo(const std::string& filename);
	void LoadPointFile(const std::string& basePath, const SMapInfo& mapInfo);
	bool FindNpc(long mapIndex, DWORD vnum, long x, long y, SNpcInfo& out) const;
	const SMapInfo* FindMap(long mapIndex) const;
	void WarpToNPC(LPCHARACTER ch, const TPacketCGNPCLocationHelper& packet);
	void SendPacket(LPCHARACTER ch, BYTE subheader, BYTE result) const;
	void SendPacket(LPCHARACTER ch, BYTE subheader, BYTE result, const SNpcInfo* npc) const;
	DWORD GetCooldownRemain(LPCHARACTER ch) const;
	int GetStoneCost(long fromMapIndex, long toMapIndex) const;
	bool ConsumeWarpStones(LPCHARACTER ch, int count) const;
	bool CanUseWindowState(LPCHARACTER ch) const;

	std::map<long, SMapInfo> m_mapInfo;
	std::vector<SNpcInfo> m_vecNpcInfo;
	bool m_bLoaded;
};

#endif // ENABLE_NPC_LOCATION_HELPER
