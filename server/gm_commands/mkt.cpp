/*	EQEmu: EQEmulator
	Copyright (C) 2001-2026 EQEmu Development Team
*/
#include "zone/client.h"

// [BOZ] #mkt -- push the Marketplace catalog to the BOZ.asi client ON DEMAND.
//
// BOZ.asi sends this once when the player OPENS the Marketplace window (and its
// catalog cache is empty). Delivering the ~274-row [BOZ_MKT] feed only to
// players actually browsing the shop -- instead of on every zone-in -- avoids
// bursting hundreds of chat packets at everyone, which would lag the zone and
// could crash low-end clients. Players without BOZ.asi just see the suppressed
// marker rows as harmless noise.
void command_mkt(Client *c, const Seperator *sep)
{
	if (!c) {
		return;
	}
	c->SendBozMarketplace();
}
