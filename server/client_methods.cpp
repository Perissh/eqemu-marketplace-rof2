// ============================================================================
//  EQ Marketplace -- Client methods to add to zone/client.cpp
// ============================================================================
//  Paste BOTH methods below into zone/client.cpp (anywhere among the other
//  Client:: method definitions), and add their declarations to zone/client.h
//  in the `public:` section of class Client:
//
//      void SendBozMarketplace();   // push the catalog feed to the client
//      void SendBozFunds();         // push the player's currency balances
//
//  Also drop boz_mkt_currency.h into your zone/ source directory and add, with the
//  other includes near the top of client.cpp:
//
//      #include "zone/boz_mkt_currency.h"
//
//  CURRENCY MODEL  (see boz_mkt_currency.h -- the buy flow uses the SAME resolver)
//  --------------
//  The catalog `currency` column is just a name. It resolves to:
//    * "Platinum"                 -> the player's carried platinum (works anywhere).
//    * a demo alias               -> Souls / Conquest Points / Raid Tokens (ids 100/101/
//                                    102; optional -- read 0 if your server lacks them).
//    * any alternate_currency     -> matched by the currency item's name (Doubloon, Ebon
//                                    Crystal, ...). To add your OWN currency, create the
//                                    alternate_currency row and set `currency` to its
//                                    item name. Nothing here needs recompiling per name.
//  SendBozFunds emits a "<currency>=<balance>" pair for every currency the catalog uses,
//  so the client's funds display can show ANY of them by name.
// ============================================================================

// [BOZ] Push the Marketplace catalog to the client. The client's chat hook parses
// each [BOZ_MKT] row into its cache and suppresses it from chat; selecting a
// category leaf renders the matching rows as tiles. The catalog lives in the
// boz_marketplace_catalog table (one row per item; see sql/).
//   [BOZ_MKT_START]
//   [BOZ_MKT_LEVEL]|<player level>
//   [BOZ_MKT]|<parent>|<subcat>|<item_id>|<name>|<icon>|<price>|<currency>|<level>|<desc>|<model>|<pslot>|<color>
//   [BOZ_MKT_END]
void Client::SendBozMarketplace()
{
	auto res = database.QueryDatabase(
		"SELECT c.parent, c.subcat, c.item_id, c.name, c.icon, c.price, c.currency, c.level, c.descr, "
		"COALESCE(i.idfile,''), COALESCE(i.slots,0), COALESCE(i.material,0), "
		"COALESCE(i.itemtype,0), COALESCE(i.augtype,0), "
		"COALESCE(LPAD(HEX(i.color),8,'0'),'FF000000') "   // preview tint 0xAARRGGBB; NULL -> natural
		"FROM boz_marketplace_catalog c LEFT JOIN items i ON i.id = c.item_id "
		"ORDER BY c.parent, c.subcat, c.sort_order, c.level, c.name");
	if (!res.Success() || res.RowCount() == 0) {
		return;
	}
	Message(Chat::White, "[BOZ_MKT_START]");
	// Tell the client this player's level so the Marketplace can build a
	// "My Level and Below" tab (items with level <= this) atop each section.
	Message(Chat::White, fmt::format("[BOZ_MKT_LEVEL]|{}", GetLevel()).c_str());
	for (auto row : res) {
		// Derive the 3D-preview model + avatar equip slot from the joined item so the
		// client can render the avatar wearing it. Weapons use the weapon model number
		// parsed from idfile "ITxxx"; visible armor uses the item's MATERIAL dropped into
		// its body slot. pslot = avatar equip-array index the client pokes:
		//   0 head, 1 chest, 2 arms, 3 wrists, 4 hands, 5 legs, 6 feet, 7 primary, 8 secondary.
		// Anything not visibly worn -> pslot -1 (client shows the bare avatar).
		const char* idfile   = row[9] ? row[9] : "";
		uint32      slots     = row[10] ? (uint32)strtoul(row[10], nullptr, 10) : 0;
		int         material  = row[11] ? atoi(row[11]) : 0;
		int         itemtype  = row[12] ? atoi(row[12]) : 0;
		uint32      augtype   = row[13] ? (uint32)strtoul(row[13], nullptr, 10) : 0;
		const bool  weapModel = (idfile[0]=='I'||idfile[0]=='i') && (idfile[1]=='T'||idfile[1]=='t');
		int preview_model = 0;
		int preview_slot  = -1;
		// Weapon ornament -- ornamentation aug (itemtype 54, augtype bit 524288) or
		// drag-on "Ornamentation" item (itemtype 11). Both carry a weapon IT model.
		if (((itemtype == 54 && (augtype & 524288)) || itemtype == 11) && weapModel) {
			preview_slot  = 7;
			preview_model = atoi(idfile + 2);
		} else if (slots & 8192) {                  // primary weapon
			preview_slot = 7;
			if (weapModel) preview_model = atoi(idfile + 2);
		} else if (slots & 16384) {          // secondary weapon
			preview_slot = 8;
			if (weapModel) preview_model = atoi(idfile + 2);
		} else {                             // visible armor -> material into the body slot
			const uint32 kAllArmorSlots = 923268;   // head|arms|wrists|hands|chest|legs|feet
			if ((slots & kAllArmorSlots) == kAllArmorSlots) {
				preview_slot  = 9;                   // full-suit sentinel (poke render slots 0-6)
				preview_model = material;
			}
			else if (slots & 4)             preview_slot = 0;   // head
			else if (slots & 131072)        preview_slot = 1;   // chest
			else if (slots & 128)           preview_slot = 2;   // arms
			else if (slots & (512 | 1024))  preview_slot = 3;   // wrists
			else if (slots & 4096)          preview_slot = 4;   // hands
			else if (slots & 262144)        preview_slot = 5;   // legs
			else if (slots & 524288)        preview_slot = 6;   // feet
			if (preview_slot >= 0) preview_model = material;
		}
		// "%s" guard: names/descriptions may contain a literal '%'; passing the line as
		// the format string would make vsnprintf read bogus varargs and crash the zone.
		// Always pass it as an argument, never as the format string.
		Message(Chat::White, "%s", fmt::format(
			"[BOZ_MKT]|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}",
			row[0], row[1], row[2], row[3], row[4], row[5], row[6], row[7],
			row[8] ? row[8] : "", preview_model, preview_slot,
			row[14] ? row[14] : "FF000000").c_str());
	}
	Message(Chat::White, "[BOZ_MKT_END]");
	SendBozFunds();   // piggyback the player's currency balances on the same response
}

// [BOZ] Push the player's currency balances to the client. The client caches these
// for the Marketplace funds display, which morphs per selected category to show that
// category's currency + the player's balance of it. The line is:
//   [BOZ_FUNDS]|<souls>|<conquest>|<raid_tokens>|<platinum>|<name>=<bal>|<name>=<bal>|...
// The first four positional fields are the demo aliases (ids 100/101/102) + carried
// platinum, kept for older clients. After them comes a generic "<currency>=<balance>"
// pair for every DISTINCT currency the catalog uses, resolved the same way #mktbuy
// charges it -- so the funds display can show ANY currency by name (Platinum, the demo
// currencies, or your own alt-currency). Requires boz_mkt_currency.h (see header above).
void Client::SendBozFunds()
{
	// Legacy positional fields -- older clients read exactly these four.
	uint32 souls    = GetAlternateCurrencyValue(100);  // optional custom currency
	uint32 conquest = GetAlternateCurrencyValue(101);  // optional custom currency
	uint32 raid     = GetAlternateCurrencyValue(102);  // optional custom currency
	uint32 plat     = GetCarriedPlatinum();            // the DEFAULT Marketplace currency
	std::string line = fmt::format("[BOZ_FUNDS]|{}|{}|{}|{}", souls, conquest, raid, plat);

	// Generic "<currency>=<balance>" pairs for every currency the catalog uses.
	auto cs = database.QueryDatabase("SELECT DISTINCT currency FROM boz_marketplace_catalog");
	if (cs.Success()) {
		for (auto row : cs) {
			std::string cname = row[0] ? row[0] : "";
			if (cname.empty() || cname.find('|') != std::string::npos ||
			    cname.find('=') != std::string::npos) {
				continue;  // skip empty / delimiter-bearing names
			}
			const MktCurrency cur = MktResolveCurrency(cname);
			const int64_t     bal = MktCurrencyBalance(this, cur);
			if (bal < 0) {
				continue;  // unresolved currency -> nothing to show
			}
			line += fmt::format("|{}={}", cname, bal);
		}
	}
	Message(Chat::White, "%s", line.c_str());   // "%s" guard: a name may contain '%'
}
