#ifndef __INC_METIN_II_GAME_ANTI_MULTI_FARM_H__
#define __INC_METIN_II_GAME_ANTI_MULTI_FARM_H__

// Anti Multi-Farm: aynı IP'den giren karakterlerin kazançlarını
// kademeli olarak kısıtlar.
//
// Tier düzeni (varsayılan, config'den ayarlanabilir):
//   Slot 0..1  -> FULL    : EXP + drop + yang
//   Slot 2..3  -> NO_EXP  : drop + yang (EXP yok)
//   Slot 4     -> SHOP    : login + pazar/alışveriş, kazanç yok
//   Slot 5+    -> REJECT  : Entergame'de kick
//
// Slot sıralaması: aynı IP'den online olan karakterler PlayerID'sine göre
// artan sırada dizilir (yaşı büyük karakter öncelikli) — relog/sıra
// manipülasyonuna karşı deterministik.
//
// GM hesapları ve 127.0.0.1 her zaman FULL tier'a sayılır.
//
// Tüm kod ENABLE_ANTI_MULTIPLE_FARM flag'i ile sarmalıdır; flag OFF
// olduğunda hiçbir hook çağrılmaz, davranış stock'a eşit kalır.

#ifdef ENABLE_ANTI_MULTIPLE_FARM

class CHARACTER;
class DESC;

namespace AntiMultiFarm
{
	enum ETier
	{
		TIER_FULL    = 0,	// EXP + drop + yang
		TIER_NO_EXP  = 1,	// drop + yang (EXP yok)
		TIER_SHOP    = 2,	// kazanç yok, sadece pazar/login
		TIER_REJECT  = 3,	// entergame'de kick
	};

	// Karakterin mevcut IP-tabanlı slot sırasını ve tier'ını döner.
	ETier GetTier(CHARACTER* pkChar);

	// EXP, drop, yang kazanç yetkileri (karakter başına çağrılır).
	bool CanReceiveExp(CHARACTER* pkChar);
	bool CanReceiveDrop(CHARACTER* pkChar);
	bool CanReceiveYang(CHARACTER* pkChar);

	// Entergame anında çağrılır. true dönerse karakter girebilir;
	// false dönerse Entergame iptal edilmeli ve descriptor kapatılmalıdır.
	bool CheckLoginAllowed(DESC* pkDesc);

	// Killer ile ölen mob'a göre cap tamamen muaf mı?
	// (Boss/metin/king cap dışında — meşru party PvE akışını korur.)
	bool IsExemptKillTarget(CHARACTER* pkVictim);
}

#endif // ENABLE_ANTI_MULTIPLE_FARM

#endif
