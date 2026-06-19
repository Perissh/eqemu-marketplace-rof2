/*	EQEmu: EQEmulator
	Copyright (C) 2001-2026 EQEmu Development Team
*/
#include "zone/client.h"
#include "zone/boz_mkt_currency.h"   // [BOZ] shared currency resolver (Platinum / aliases / alt-currency by name)

// [BOZ] #mktbuy <item_id> -- purchase a Marketplace item.
//
// The BOZ.asi client sends this when the player buys a tile. The SERVER is
// authoritative: it re-reads price + currency from boz_marketplace_catalog (the
// ONLY thing trusted from the client is the item id), checks the player's balance
// of that currency, atomically deducts it, and grants the exact catalog item.
// The catalog `currency` string is resolved by MktResolveCurrency (boz_mkt_currency.h):
// Platinum (carried coin), the demo aliases (Souls/Conquest Points/Raid Tokens), or ANY
// alternate-currency matched by name (Doubloon, Ebon Crystal, a custom token...).
// Free (price 0) items are granted without a deduction. Currencies that don't resolve
// to anything are rejected.
//
// Currency Exchange tiles (a row in boz_marketplace_swaps) are intercepted first:
// they convert one alt-currency into another instead of granting an item.

// [BOZ] Alt-currency id -> display name, for swap + purchase messaging.
static const char* BozCurrencyName(uint32 id)
{
	switch (id) {
		case 100: return "Souls";
		case 101: return "Conquest Points";
		case 102: return "Raid Tokens";
		default:  return "currency";
	}
}

// [BOZ] Currency Exchange (boz_marketplace_swaps) is an OPTIONAL feature. On a server
// WITHOUT that table the lookup below would fail and log a "table doesn't exist" error
// on every purchase. Probe once (cached per zone process) and skip the swap path when
// the table is absent -- so a stock install never sees the error.
static bool MktSwapsTableExists()
{
	static int cached = -1;   // -1 unknown, 0 no, 1 yes
	if (cached < 0) {
		auto r = database.QueryDatabase(
			"SELECT 1 FROM information_schema.tables "
			"WHERE table_schema = DATABASE() AND table_name = 'boz_marketplace_swaps' LIMIT 1");
		cached = (r.Success() && r.RowCount() > 0) ? 1 : 0;
	}
	return cached == 1;
}

void command_mktbuy(Client *c, const Seperator *sep)
{
	if (!c) {
		return;
	}
	if (!sep->IsNumber(1)) {
		c->Message(Chat::White, "Usage: #mktbuy [item_id]");
		return;
	}

	uint32 item_id = Strings::ToUnsignedInt(sep->arg[1]);
	if (!item_id) {
		c->Message(Chat::White, "Enter a valid item ID.");
		return;
	}

	// [BOZ] Currency-exchange tiles. A row in boz_marketplace_swaps marks this
	// catalog item_id as a currency conversion, not an item grant. The debit +
	// credit are read authoritatively from that table (never the client); 1:1
	// today (to_amount == from_amount). Handled BEFORE the item path so a swap id
	// never falls through to SummonItem. OPTIONAL: only consulted when the table
	// exists, so a stock server skips it cleanly (no error logged).
	if (MktSwapsTableExists()) {
		auto sw = database.QueryDatabase(fmt::format(
			"SELECT from_currency_id, from_amount, to_currency_id, to_amount "
			"FROM boz_marketplace_swaps WHERE item_id = {} LIMIT 1",
			item_id));
		if (sw.Success() && sw.RowCount() > 0) {
			uint32 from_cur = 0, from_amt = 0, to_cur = 0, to_amt = 0;
			for (auto row : sw) {
				from_cur = Strings::ToUnsignedInt(row[0]);
				from_amt = Strings::ToUnsignedInt(row[1]);
				to_cur   = Strings::ToUnsignedInt(row[2]);
				to_amt   = Strings::ToUnsignedInt(row[3]);
				break;
			}
			if (from_cur == 0 || to_cur == 0 || from_amt == 0 || to_amt == 0) {
				c->Message(Chat::Red, "That exchange is not available.");
				return;
			}
			// Atomic check-and-debit the source; credit the destination only on
			// success (RemoveAlternateCurrencyValue changes nothing if short).
			if (!c->RemoveAlternateCurrencyValue(from_cur, from_amt)) {
				c->Message(Chat::Red, "%s", fmt::format(
					"You don't have enough {} for that exchange ({} needed).",
					BozCurrencyName(from_cur), from_amt).c_str());
				return;
			}
			c->AddAlternateCurrencyValue(to_cur, static_cast<int>(to_amt));
			c->Message(Chat::White, "%s", fmt::format(
				"Exchanged {} {} for {} {}. Balances now -- {}: {}, {}: {}.",
				from_amt, BozCurrencyName(from_cur), to_amt, BozCurrencyName(to_cur),
				BozCurrencyName(from_cur), c->GetAlternateCurrencyValue(from_cur),
				BozCurrencyName(to_cur),   c->GetAlternateCurrencyValue(to_cur)).c_str());
			c->SendBozFunds();   // refresh the client's cached balances + funds display
			return;
		}
	}

	// Server-authoritative price + currency -- never trust the client's price. If the same
	// item_id is listed in more than one category, charge the LOWEST price, so a buyer is
	// never billed more than the tile they clicked showed (and an item that's free anywhere
	// stays free). NOTE: listing one item under multiple CURRENCIES is ambiguous (lowest is
	// a naive numeric compare across currencies) -- the Catalog Builder warns you before you
	// create that case, so keep an item to a single currency.
	auto res = database.QueryDatabase(fmt::format(
		"SELECT price, currency, name FROM boz_marketplace_catalog "
		"WHERE item_id = {} ORDER BY price ASC LIMIT 1",
		item_id));
	if (!res.Success() || res.RowCount() == 0) {
		c->Message(Chat::Red, "That item is not sold in the Marketplace.");
		return;
	}

	uint32      price = 0;
	std::string currency;
	std::string item_name = "item";
	for (auto row : res) {
		price     = Strings::ToUnsignedInt(row[0]);
		currency  = row[1] ? row[1] : "";
		item_name = row[2] ? row[2] : "item";
		break;
	}

	// Resolve the catalog currency: Platinum (carried coin), a demo alias, or ANY
	// alternate-currency matched by name (Doubloon, Ebon Crystal, your own custom
	// token, ...). See boz_mkt_currency.h. Unresolved currencies are rejected.
	const MktCurrency cur = MktResolveCurrency(currency);
	if (!cur.ok) {
		c->Message(Chat::Red, "This item cannot be purchased yet.");
		return;
	}

	// Validate the item BEFORE taking any currency, so a bad row can never debit
	// the player without granting anything.
	const auto *item = database.GetItem(item_id);
	if (!item) {
		c->Message(Chat::Red, "That item no longer exists.");
		return;
	}

	// Atomic check-and-debit. Platinum spends carried coin via TakeMoneyFromPP
	// (1 pp = 1000 copper); alt-currencies spend through RemoveAlternateCurrencyValue,
	// which returns false (and changes nothing) when short, persists the new value, and
	// refreshes the client's currency tab. Free items skip the deduction.
	if (price > 0) {
		const bool ok = cur.is_plat
			? c->TakeMoneyFromPP(static_cast<uint64>(price) * 1000, true)
			: c->RemoveAlternateCurrencyValue(cur.id, price);
		if (!ok) {
			c->Message(Chat::Red, fmt::format(
				"You don't have enough {}. {} costs {} {}.",
				cur.is_plat ? std::string("Platinum") : cur.name,
				item_name, price, MktCurrencyUnit(cur, price)).c_str());
			return;
		}
	}

	// Charged clickies must be granted with their full charge count (or -1 for
	// unlimited) -- a hardcoded 1 left them with a single click, then 0. Worn gear
	// has MaxCharges 0, so it stays at quantity 1. Mirrors the standard merchant
	// (merchant.cpp: charges = item->MaxCharges for charged items).
	int16 give_charges = (item->MaxCharges != 0) ? (int16)item->MaxCharges : (int16)1;
	c->SummonItem(item_id, give_charges);

	if (price > 0) {
		const int64_t remaining = MktCurrencyBalance(c, cur);
		c->Message(Chat::White, fmt::format(
			"Purchased {} for {} {}. Remaining: {} {}.",
			item_name, price, MktCurrencyUnit(cur, price),
			remaining, MktCurrencyUnit(cur, static_cast<uint32>(remaining))).c_str());
	} else {
		c->Message(Chat::White, fmt::format("Received {} (free).", item_name).c_str());
	}

	c->SendBozFunds();   // refresh the client's cached balances + funds display
}
