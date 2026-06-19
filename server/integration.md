# Server integration

The Marketplace server side is a small set of source additions to your EQEmu
server, plus one DB table. It's not a drop-in binary — server operators build
EQEmu from source, so these are source changes. Five steps, then rebuild.

There are **no new network opcodes**: the catalog streams to the client over the
chat channel (the client mod parses and hides the marker lines), so nothing in the
netcode/protocol layer changes.

---

## 1. Create the catalog table

```sh
mysql -u <user> -p <database> < ../sql/schema.sql
```

## 2. Add the three commands + the currency header

Copy these files into your tree:

```
server/gm_commands/mkt.cpp      ->  zone/gm_commands/mkt.cpp
server/gm_commands/mktbuy.cpp   ->  zone/gm_commands/mktbuy.cpp
server/gm_commands/mktview.cpp  ->  zone/gm_commands/mktview.cpp
server/boz_mkt_currency.h       ->  zone/boz_mkt_currency.h
```

`boz_mkt_currency.h` is the shared currency resolver used by **both** `mktbuy.cpp`
and `SendBozFunds` (step 3). It's header-only — no build-list entry needed.

Then add the three commands to the zone build — in **`zone/CMakeLists.txt`**, alongside
the other `gm_commands/*.cpp` entries in the zone source list:

```cmake
    gm_commands/mkt.cpp
    gm_commands/mktbuy.cpp
    gm_commands/mktview.cpp
```

## 3. Add the two Client methods

Paste both methods from **`client_methods.cpp`** into **`zone/client.cpp`** (anywhere
among the `Client::` definitions), and declare them in **`zone/client.h`** in the
`public:` section of `class Client`:

```cpp
    void SendBozMarketplace();   // push the catalog feed to the client
    void SendBozFunds();         // push the player's currency balances
```

`SendBozFunds` uses the currency resolver from step 2, so add its include with the
other includes near the top of **`zone/client.cpp`**:

```cpp
    #include "zone/boz_mkt_currency.h"
```

## 4. Register the commands

In **`zone/command.cpp`**, add three lines to the `command_add(...) ||` chain inside
`command_init()`:

```cpp
        command_add("mkt", " - Deliver the Marketplace catalog (sent automatically when you open the Marketplace).", AccountStatus::Player, command_mkt) ||
        command_add("mktbuy", "[item_id] - Purchase a Marketplace item.", AccountStatus::Player, command_mktbuy) ||
        command_add("mktview", "[item_id] - Open the item-inspect window for a Marketplace item.", AccountStatus::Player, command_mktview) ||
```

And declare them in **`zone/command.h`**:

```cpp
void command_mkt(Client *c, const Seperator *sep);
void command_mktbuy(Client *c, const Seperator *sep);
void command_mktview(Client *c, const Seperator *sep);
```

## 5. Rebuild EQEmu

Build as you normally do (the changes are confined to the `zone` target).

---

## Verify

1. Install the client mod (`client/bin/Marketplace.asi`) into your RoF2 client.
2. Load the demo: `mysql ... < sql/sample_catalog.sql`.
3. Log in, open the **Marketplace** → you should see an **"Armor Preview Test"**
   category; click items to see the 3D preview, and Buy a free one.
4. Remove the demo once confirmed:
   `DELETE FROM boz_marketplace_catalog WHERE parent = 'Armor Preview Test';`

## Currency notes

The catalog `currency` column is just a name. `MktResolveCurrency` (in
`boz_mkt_currency.h`) turns it into something to charge:

- **Platinum** (the default) deducts the player's carried platinum — works on any
  server, no setup.
- **Any alternate currency, by name.** If the `currency` string matches the *item
  name* of a row in your `alternate_currency` table, that currency is charged. This
  covers stock EQ currencies out of the box — `Doubloon`, `Ebon Crystal`, `Radiant
  Crystal`, `Orum`, … — and **your own**: to add a custom currency, create the
  `alternate_currency` row and set `currency` to its item name. No recompiling per
  currency.
- **Demo aliases.** `Souls` / `Conquest Points` / `Raid Tokens` are mapped directly to
  ids `100` / `101` / `102` (their display names differ from their item names). They're
  optional and harmless on servers that don't define those ids — edit or delete them in
  `boz_mkt_currency.h` if you don't use them.
- Anything that resolves to nothing is rejected at purchase (the item can't be bought).

If the **same item is listed in more than one category**, the purchase charges the
**lowest** price across those listings — a buyer is never billed more than the tile they
clicked showed, and an item that's free anywhere stays free. Listing one item under two
*different currencies* is ambiguous (lowest is a naive numeric compare across currencies),
so keep an item to a single currency — the Catalog Builder warns you if you try to add an
item that's already listed.

The **Catalog Builder** tool reads your `alternate_currency` table and offers exactly
these currencies in its dropdown, so you pick a real one instead of guessing a name.
`SendBozFunds` sends the player's balance for every currency your catalog uses, so the
in-shop funds display shows the right amount for whichever currency a category is in.

The optional **Currency Exchange** feature (a `boz_marketplace_swaps` table that lets
tiles convert one currency to another) is auto-detected: if the table doesn't exist, the
feature is skipped. It is **not** included in this bundle — Platinum + alt-currencies
cover the common case.
