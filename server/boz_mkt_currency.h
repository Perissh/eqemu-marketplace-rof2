/*	EQEmu: EQEmulator
	Copyright (C) 2001-2026 EQEmu Development Team
*/
// boz_mkt_currency.h -- shared Marketplace currency resolution.
//
// Drop this file into your server's `zone/` source directory. It turns a catalog
// `currency` string (boz_marketplace_catalog.currency) into either "carried platinum"
// or an EQEmu alternate-currency id, and reads a player's balance of it. Used by BOTH
// the purchase command (gm_commands/mktbuy.cpp) and the funds feed (SendBozFunds in
// client.cpp) so they ALWAYS agree on what a currency means.
//
// Resolution order:
//   1. "Platinum" / "Plat" / "pp"  -> the player's carried platinum (works anywhere).
//   2. Built-in friendly aliases   -> Souls=100, Conquest Points=101, Raid Tokens=102.
//      These are the demo's optional custom currencies; their display names differ from
//      their alt-currency item names, so they're mapped directly. HARMLESS on servers
//      that don't define those ids (they simply never match). Delete or edit them if you
//      don't use them -- nothing else depends on them.
//   3. alternate_currency by name  -> match the catalog string to an alternate currency's
//      item Name (items.Name). Covers stock currencies -- Doubloon, Ebon Crystal, Radiant
//      Crystal, Orum, ... -- AND any custom currency you create: just name the catalog
//      `currency` to match the currency item's name.
//   4. Anything else               -> unresolved (ok=false); the purchase is rejected.
#ifndef BOZ_MKT_CURRENCY_H
#define BOZ_MKT_CURRENCY_H

#include "zone/client.h"
#include "common/strings.h"
#include <cstdint>
#include <string>

struct MktCurrency {
	bool        ok      = false;  // false -> could not resolve; reject the purchase
	bool        is_plat = false;  // true  -> carried platinum (id unused)
	uint32      id      = 0;      // alternate-currency id when !is_plat
	std::string name;             // the catalog display string, as given
};

inline MktCurrency MktResolveCurrency(const std::string &currency)
{
	MktCurrency r;
	r.name = currency;
	if (currency == "Platinum" || currency == "Plat" || currency == "pp") {
		r.ok = true; r.is_plat = true; return r;
	}
	// Optional demo currencies -- edit/remove if your server doesn't use them.
	if (currency == "Souls")           { r.ok = true; r.id = 100; return r; }
	if (currency == "Conquest Points") { r.ok = true; r.id = 101; return r; }
	if (currency == "Raid Tokens")     { r.ok = true; r.id = 102; return r; }
	if (currency.empty()) {
		return r;  // nothing to resolve
	}
	// Data-driven: match the catalog string to any alternate-currency item's name.
	auto res = database.QueryDatabase(fmt::format(
		"SELECT ac.id FROM alternate_currency ac "
		"JOIN items i ON i.id = ac.item_id WHERE i.Name = '{}' LIMIT 1",
		Strings::Escape(currency)));
	if (res.Success() && res.RowCount() > 0) {
		for (auto row : res) { r.id = Strings::ToUnsignedInt(row[0]); break; }
		if (r.id != 0) {
			r.ok = true;
		}
	}
	return r;
}

// The player's balance of a resolved currency; -1 if unresolved / no client.
inline int64_t MktCurrencyBalance(Client *c, const MktCurrency &cur)
{
	if (!c || !cur.ok) {
		return -1;
	}
	return cur.is_plat ? static_cast<int64_t>(c->GetCarriedPlatinum())
	                   : static_cast<int64_t>(c->GetAlternateCurrencyValue(cur.id));
}

// Singular unit for clean grammar ("1 Soul", "1 Doubloon", "pp" for platinum).
inline std::string MktCurrencyUnit(const MktCurrency &cur, uint32 n)
{
	if (cur.is_plat) {
		return "pp";
	}
	if (n == 1) {
		if (cur.name == "Souls")           return "Soul";
		if (cur.name == "Conquest Points") return "Conquest Point";
		if (cur.name == "Raid Tokens")     return "Raid Token";
		// Generic: drop a trailing 's' for a rough singular (Doubloons -> Doubloon).
		if (cur.name.size() > 1 && cur.name.back() == 's') {
			return cur.name.substr(0, cur.name.size() - 1);
		}
	}
	return cur.name;
}

#endif // BOZ_MKT_CURRENCY_H
