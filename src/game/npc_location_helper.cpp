#include "stdafx.h"
#ifdef ENABLE_NPC_LOCATION_HELPER

#include <algorithm>
#include <fstream>
#include <sstream>
#include "char.h"
#include "packet_structs.h"
#include "npc_location_helper.h"
#include "desc.h"
#include "questmanager.h"
#include "questpc.h"
#include "locale_service.h"
#include "config.h"

namespace
{
	const DWORD NPC_LOCATION_HELPER_ACTIVATE_ITEM = 79667;
	const DWORD NPC_LOCATION_HELPER_WARP_STONE = 79669;
	const DWORD NPC_LOCATION_HELPER_WARP_STONE_PLUS = 79668;
	const int NPC_LOCATION_HELPER_COOLDOWN = 300;

	std::vector<std::string> Split(const std::string& line)
	{
		std::istringstream iss(line);
		std::vector<std::string> out;
		std::string token;
		while (iss >> token)
			out.push_back(token);
		return out;
	}

	bool IsCommentOrEmpty(const std::string& line)
	{
		for (size_t i = 0; i < line.size(); ++i)
		{
			if (line[i] == '#')
				return true;
			if (!isspace(static_cast<unsigned char>(line[i])))
				return false;
		}
		return true;
	}

	std::string BuildFlag(const char* name)
	{
		return std::string("npc_location_helper.") + name;
	}

	std::string BuildUnlockedMapFlag(long mapIndex)
	{
		char buf[64];
		snprintf(buf, sizeof(buf), "unlocked_map.%ld", mapIndex);
		return BuildFlag(buf);
	}
}

CNpcLocationHelperManager::CNpcLocationHelperManager()
	: m_bLoaded(false)
{
}

void CNpcLocationHelperManager::Initialize()
{
	Load();
}

bool CNpcLocationHelperManager::Load()
{
	if (m_bLoaded)
		return true;

	const std::vector<std::string> bases =
	{
		LocaleService_GetBasePath() + "/common/map/npc_location_helper",
		LocaleService_GetBasePath() + "/map/npc_location_helper",
		LocaleService_GetBasePath() + "/npc_location_helper",
	};

	for (const std::string& base : bases)
	{
		if (LoadMapInfo(base + "/npc_location_helper.txt"))
		{
			for (const auto& it : m_mapInfo)
				LoadPointFile(base + "/../", it.second);

			m_bLoaded = true;
			sys_log(0, "NPC_LOCATION_HELPER: loaded %u maps, %u npc points",
				static_cast<unsigned>(m_mapInfo.size()),
				static_cast<unsigned>(m_vecNpcInfo.size()));
			return true;
		}
	}

	sys_err("NPC_LOCATION_HELPER: cannot load npc_location_helper.txt");
	return false;
}

bool CNpcLocationHelperManager::LoadMapInfo(const std::string& filename)
{
	std::ifstream file(filename.c_str());
	if (!file.is_open())
		return false;

	m_mapInfo.clear();
	std::string line;
	bool inMapInfo = false;
	while (std::getline(file, line))
	{
		if (line.find("Group") != std::string::npos && line.find("MapInfo") != std::string::npos)
		{
			inMapInfo = true;
			continue;
		}
		if (!inMapInfo || IsCommentOrEmpty(line) || line.find('{') != std::string::npos)
			continue;
		if (line.find('}') != std::string::npos)
			break;

		const std::vector<std::string> tokens = Split(line);
		if (tokens.size() < 13)
			continue;

		SMapInfo info;
		info.mapIndex = atol(tokens[0].c_str());
		info.mapName = tokens[1];
		info.mapDir = tokens[2];
		info.category = atoi(tokens[3].c_str());
		info.minLevel = atoi(tokens[4].c_str());
		info.minConquerorLevel = atoi(tokens[5].c_str()); // stored but not checked (m2dev has no GetConquerorLevel)
		info.empire = atoi(tokens[8].c_str());
		info.showAtlas = atoi(tokens[9].c_str());
		info.showNpcList = atoi(tokens[10].c_str());
		info.warpNeedYang = atoi(tokens[11].c_str());
		info.warpNeedItemVnum = 0;
		info.warpNeedItemCount = 0;

		const size_t commaPos = tokens[12].find(',');
		if (commaPos != std::string::npos)
		{
			info.warpNeedItemVnum = static_cast<DWORD>(atol(tokens[12].substr(0, commaPos).c_str()));
			info.warpNeedItemCount = atoi(tokens[12].substr(commaPos + 1).c_str());
		}

		m_mapInfo[info.mapIndex] = info;
	}

	return !m_mapInfo.empty();
}

void CNpcLocationHelperManager::LoadPointFile(const std::string& basePath, const SMapInfo& mapInfo)
{
	const std::string filename = basePath + mapInfo.mapDir + "_point.txt";
	std::ifstream file(filename.c_str());
	if (!file.is_open())
		return;

	std::string line;
	while (std::getline(file, line))
	{
		if (IsCommentOrEmpty(line))
			continue;

		const std::vector<std::string> tokens = Split(line);
		if (tokens.size() < 6)
			continue;
		if (atoi(tokens[4].c_str()) == 0)
			continue;

		SNpcInfo npc;
		npc.mapIndex = mapInfo.mapIndex;
		npc.x = atol(tokens[1].c_str()) / 100;
		npc.y = atol(tokens[2].c_str()) / 100;
		npc.vnum = static_cast<DWORD>(atol(tokens[3].c_str()));
		m_vecNpcInfo.push_back(npc);
	}
}

const CNpcLocationHelperManager::SMapInfo* CNpcLocationHelperManager::FindMap(long mapIndex) const
{
	auto it = m_mapInfo.find(mapIndex);
	return it == m_mapInfo.end() ? NULL : &it->second;
}

bool CNpcLocationHelperManager::FindNpc(long mapIndex, DWORD vnum, long x, long y, SNpcInfo& out) const
{
	for (const SNpcInfo& npc : m_vecNpcInfo)
	{
		if (npc.mapIndex != mapIndex || npc.vnum != vnum)
			continue;
		if (labs(npc.x - x) > 3 || labs(npc.y - y) > 3)
			continue;

		out = npc;
		return true;
	}

	return false;
}

bool CNpcLocationHelperManager::IsActive(LPCHARACTER ch) const
{
	if (!ch)
		return false;

	quest::PC* pc = quest::CQuestManager::instance().GetPCForce(ch->GetPlayerID());
	return pc && pc->GetFlag(BuildFlag("active")) > 0;
}

void CNpcLocationHelperManager::Activate(LPCHARACTER ch)
{
	if (!ch)
		return;

	quest::PC* pc = quest::CQuestManager::instance().GetPCForce(ch->GetPlayerID());
	if (!pc)
		return;

	if (pc->GetFlag(BuildFlag("active")) > 0)
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("NPC Location Helper is already active."));
		SendPacket(ch, NPCLocationHelperSub::GC::STATUS, RESULT_OK);  // reconnect sync: client m_bActive=false olabilir, STATUS paketi gonder
		return;
	}

	pc->SetFlag(BuildFlag("active"), 1);
	ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("NPC Location Helper has been activated."));
	SendPacket(ch, NPCLocationHelperSub::GC::STATUS, RESULT_OK);
}

bool CNpcLocationHelperManager::UnlockMapByItem(LPCHARACTER ch, DWORD itemVnum, long mapIndex)
{
	(void)itemVnum;

	if (!ch)
		return false;

	Load();

	const SMapInfo* mapInfo = FindMap(mapIndex);
	if (!mapInfo || mapInfo->showAtlas != 2)
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("This item cannot be used for that map."));
		return false;
	}

	quest::PC* pc = quest::CQuestManager::instance().GetPCForce(ch->GetPlayerID());
	if (!pc)
		return false;

	const std::string flagName = BuildUnlockedMapFlag(mapIndex);
	if (pc->GetFlag(flagName) > 0)
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("This map is already unlocked."));
		return false;
	}

	pc->SetFlag(flagName, 1);
	ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("Map unlocked."));
	SendPacket(ch, NPCLocationHelperSub::GC::STATUS, RESULT_OK);
	return true;
}

DWORD CNpcLocationHelperManager::GetCooldownRemain(LPCHARACTER ch) const
{
	if (!ch)
		return 0;

	quest::PC* pc = quest::CQuestManager::instance().GetPCForce(ch->GetPlayerID());
	if (!pc)
		return 0;

	const int lastTime = pc->GetFlag(BuildFlag("last_warp_time"));
	const int elapsed = static_cast<int>(time(0)) - lastTime;
	return elapsed >= NPC_LOCATION_HELPER_COOLDOWN ? 0 : NPC_LOCATION_HELPER_COOLDOWN - elapsed;
}

void CNpcLocationHelperManager::SendPacket(LPCHARACTER ch, BYTE subheader, BYTE result) const
{
	SendPacket(ch, subheader, result, NULL);
}

void CNpcLocationHelperManager::SendPacket(LPCHARACTER ch, BYTE subheader, BYTE result, const SNpcInfo* npc) const
{
	if (!ch || !ch->GetDesc())
		return;

	TPacketGCNPCLocationHelper packet;
	memset(&packet, 0, sizeof(packet));
	packet.header = GC::NPC_LOCATION_HELPER;
	packet.length = sizeof(packet);
	packet.subheader = subheader;
	packet.result = result;
	packet.active = IsActive(ch) ? 1 : 0;
	packet.cooldownRemain = GetCooldownRemain(ch);
	if (npc)
	{
		packet.mapIndex = static_cast<int32_t>(npc->mapIndex);
		packet.vnum = npc->vnum;
		packet.x = static_cast<int32_t>(npc->x);
		packet.y = static_cast<int32_t>(npc->y);
	}
	ch->GetDesc()->Packet(&packet, sizeof(packet));
}

bool CNpcLocationHelperManager::CanUseWindowState(LPCHARACTER ch) const
{
	return ch && !ch->GetExchange() && !ch->GetMyShop() && !ch->GetShopOwner() && !ch->IsOpenSafebox() && !ch->IsCubeOpen();
}

int CNpcLocationHelperManager::GetStoneCost(long fromMapIndex, long toMapIndex) const
{
	if (fromMapIndex == toMapIndex)
		return 1;

	const long fromRegion = fromMapIndex / 10;
	const long toRegion = toMapIndex / 10;
	return fromRegion == toRegion ? 2 : 3;
}

bool CNpcLocationHelperManager::ConsumeWarpStones(LPCHARACTER ch, int count) const
{
	if (!ch || count <= 0)
		return false;

	int remain = count;
	const int normalCount = ch->CountSpecifyItem(NPC_LOCATION_HELPER_WARP_STONE);
	const int removeNormal = std::min(normalCount, remain);
	if (removeNormal > 0)
	{
		ch->RemoveSpecifyItem(NPC_LOCATION_HELPER_WARP_STONE, removeNormal);
		remain -= removeNormal;
	}

	if (remain > 0)
		ch->RemoveSpecifyItem(NPC_LOCATION_HELPER_WARP_STONE_PLUS, remain);

	return true;
}

void CNpcLocationHelperManager::RequestStatus(LPCHARACTER ch)
{
	Load();
	SendPacket(ch, NPCLocationHelperSub::GC::STATUS, RESULT_OK);
}

void CNpcLocationHelperManager::HandlePacket(LPCHARACTER ch, const TPacketCGNPCLocationHelper& packet)
{
	if (!ch)
		return;

	Load();

	switch (packet.subheader)
	{
		case NPCLocationHelperSub::CG::REQUEST_STATUS:
			RequestStatus(ch);
			break;
		case NPCLocationHelperSub::CG::WARP_TO_NPC:
			WarpToNPC(ch, packet);
			break;
		case NPCLocationHelperSub::CG::USE_TICKET:
			SendPacket(ch, NPCLocationHelperSub::GC::STATUS, RESULT_OK);
			break;
		case NPCLocationHelperSub::CG::REQUEST_GUILD_LAND:
			SendPacket(ch, NPCLocationHelperSub::GC::GUILD_LAND, RESULT_OK);
			break;
		default:
			SendPacket(ch, NPCLocationHelperSub::GC::WARP_RESULT, RESULT_INVALID_TARGET);
			break;
	}
}

void CNpcLocationHelperManager::WarpToNPC(LPCHARACTER ch, const TPacketCGNPCLocationHelper& packet)
{
	if (!ch || !ch->GetDesc())
		return;

	if (!IsActive(ch))
	{
		SendPacket(ch, NPCLocationHelperSub::GC::WARP_RESULT, RESULT_NOT_ACTIVE);
		return;
	}

	if (ch->IsDead() || ch->IsRiding() || !ch->CanWarp() || ch->GetDungeon() || ch->GetMapIndex() >= 10000 || !CanUseWindowState(ch))
	{
		SendPacket(ch, NPCLocationHelperSub::GC::WARP_RESULT, RESULT_CANNOT_USE);
		return;
	}

	if (GetCooldownRemain(ch) > 0)
	{
		SendPacket(ch, NPCLocationHelperSub::GC::WARP_RESULT, RESULT_COOLDOWN);
		return;
	}

	const SMapInfo* mapInfo = FindMap(packet.mapIndex);
	if (!mapInfo || mapInfo->showAtlas == 0 || mapInfo->showNpcList == 0 || !map_allow_find(packet.mapIndex))
	{
		SendPacket(ch, NPCLocationHelperSub::GC::WARP_RESULT, RESULT_INVALID_TARGET);
		return;
	}

	quest::PC* pc = quest::CQuestManager::instance().GetPCForce(ch->GetPlayerID());
	if (mapInfo->showAtlas == 2 && (!pc || pc->GetFlag(BuildUnlockedMapFlag(packet.mapIndex)) <= 0))
	{
		SendPacket(ch, NPCLocationHelperSub::GC::WARP_RESULT, RESULT_INVALID_TARGET);
		return;
	}

	SNpcInfo npc;
	if (!FindNpc(packet.mapIndex, packet.vnum, static_cast<long>(packet.x), static_cast<long>(packet.y), npc))
	{
		SendPacket(ch, NPCLocationHelperSub::GC::WARP_RESULT, RESULT_INVALID_TARGET);
		return;
	}

	// NOTE: GetConquerorLevel() does not exist in m2dev — conqueror check skipped (minConquerorLevel stored but unused)
	if (ch->GetLevel() < mapInfo->minLevel)
	{
		SendPacket(ch, NPCLocationHelperSub::GC::WARP_RESULT, RESULT_LEVEL_LIMIT);
		return;
	}

	if (mapInfo->empire != 0 && mapInfo->empire != static_cast<int>(ch->GetEmpire()))
	{
		SendPacket(ch, NPCLocationHelperSub::GC::WARP_RESULT, RESULT_EMPIRE_LIMIT);
		return;
	}

	if (mapInfo->warpNeedYang > 0 && ch->GetGold() < mapInfo->warpNeedYang)
	{
		SendPacket(ch, NPCLocationHelperSub::GC::WARP_RESULT, RESULT_NEED_YANG);
		return;
	}

	if (mapInfo->warpNeedItemVnum > 0 && ch->CountSpecifyItem(mapInfo->warpNeedItemVnum) < mapInfo->warpNeedItemCount)
	{
		SendPacket(ch, NPCLocationHelperSub::GC::WARP_RESULT, RESULT_NEED_ITEM);
		return;
	}

	const int stoneCost = GetStoneCost(ch->GetMapIndex(), packet.mapIndex);
	if (ch->CountSpecifyItem(NPC_LOCATION_HELPER_WARP_STONE) + ch->CountSpecifyItem(NPC_LOCATION_HELPER_WARP_STONE_PLUS) < stoneCost)
	{
		SendPacket(ch, NPCLocationHelperSub::GC::WARP_RESULT, RESULT_NEED_ITEM);
		return;
	}

	if (mapInfo->warpNeedYang > 0)
		ch->PointChange(POINT_GOLD, -(long)mapInfo->warpNeedYang);
	if (mapInfo->warpNeedItemVnum > 0)
		ch->RemoveSpecifyItem(mapInfo->warpNeedItemVnum, mapInfo->warpNeedItemCount);
	ConsumeWarpStones(ch, stoneCost);

	if (pc)
		pc->SetFlag(BuildFlag("last_warp_time"), static_cast<int>(time(0)));

	SendPacket(ch, NPCLocationHelperSub::GC::WARP_RESULT, RESULT_OK, &npc);
	ch->WarpSet(npc.x * 100, npc.y * 100, npc.mapIndex);
}

#endif // ENABLE_NPC_LOCATION_HELPER
