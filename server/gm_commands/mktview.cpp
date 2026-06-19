/*	EQEmu: EQEmulator
	Copyright (C) 2001-2026 EQEmu Development Team
*/
#include "zone/client.h"

// [BOZ] #mktview <item_id> -- open the native item-inspect window for a
// Marketplace item. The BOZ.asi client sends this when the player clicks the
// "Inspect" button (MKPW_DetailsInspect) in the Marketplace General tab. The
// server builds the item and sends it as ItemPacketViewLink -- the same packet
// the client gets when an item link is clicked in chat -- which pops the
// standard item display window with full stats + icon. No item is given.
//
// Restricted to items actually sold in the Marketplace so it can't be used as a
// general item-inspector.
void command_mktview(Client *c, const Seperator *sep)
{
	if (!c) {
		return;
	}
	if (!sep->IsNumber(1)) {
		c->Message(Chat::White, "Usage: #mktview [item_id]");
		return;
	}

	uint32 item_id = Strings::ToUnsignedInt(sep->arg[1]);
	if (!item_id) {
		return;
	}

	// Only items in the Marketplace catalog may be viewed this way.
	auto res = database.QueryDatabase(fmt::format(
		"SELECT 1 FROM boz_marketplace_catalog WHERE item_id = {} LIMIT 1",
		item_id));
	if (!res.Success() || res.RowCount() == 0) {
		return;
	}

	EQ::ItemInstance *inst = database.CreateItem(item_id);
	if (inst) {
		c->SendItemPacket(0, inst, ItemPacketViewLink);
		safe_delete(inst);
	}
}
