# EQ Marketplace (RoF2) — a data-driven in-game shop for EQEmu

Turns the dead RoF2 **Marketplace** button into a fully working custom shop —
categories, item tiles, a Buy button, currency display, and a live **3D preview**
of armor/weapons on your character — all driven by a single database table. No new
network opcodes; the catalog streams over the chat channel.

**The whole shop is data-driven:** you define your categories and items in the
`boz_marketplace_catalog` table. Add a row with a new `parent` and it becomes a
top-level category — **no client/ASI recompiling.**

> ⚠️ **RoF2 only.** The client mod is built against the standard "RoF2 trilogy"
> client. If your `eqgame.exe` doesn't match the reference hash, you'll need to
> re-derive the offsets — see [docs/adapting-to-your-client.md](docs/adapting-to-your-client.md).

---

## What's in here

| Path | What it is |
|---|---|
| `client/` | The standalone Marketplace ASI: source (`src/`), the **one offset file** to edit when porting (`src/offsets.h`), a build file, and a prebuilt binary (`bin/Marketplace.asi`). |
| `server/` | The server-side additions — 3 commands (`gm_commands/`), two `Client` methods (`client_methods.cpp`), and a step-by-step **`integration.md`**. |
| `sql/` | `schema.sql` (the catalog table) + `sample_catalog.sql` (a tiny demo so the shop shows something immediately). |
| `tool/catalog-builder/` | A local **web tool** to build your shop visually — search items, make categories/tabs, set price + currency per item, preview the tree, export SQL. No hand-written SQL. |
| `docs/adapting-to-your-client.md` | How to verify your client matches, and how to re-derive every offset if it doesn't. |

---

## Quick start

1. **Client:** drop `client/bin/Marketplace.asi` into your RoF2 client folder (the
   one your ASI loader scans). The client already ships the stock
   `EQUI_MarketplaceWnd.xml`; nothing else to install client-side.
2. **Server:** apply the server-side additions — see
   [server/integration.md](server/integration.md) (3 commands + 2 `Client` methods +
   the catalog table from `sql/schema.sql`). No new opcodes; build the `zone` target as usual.
3. **Sample:** load the demo so you can see it working immediately:
   ```sh
   mysql -u <user> -p <database> < sql/sample_catalog.sql
   ```
4. In game, open the **Marketplace** → you'll see an **"Armor Preview Test"**
   category → expand it → click a robe/chest/suit and watch your avatar wear it in
   the Preview pane.

---

## 🧹 Remove the demo once you've confirmed it works

The sample is **only there to prove the shop renders** — delete it before you go
live (or once you've added your own catalog):

```sql
DELETE FROM boz_marketplace_catalog WHERE parent = 'Armor Preview Test';
```

That's it — the "Armor Preview Test" category disappears. Empty categories never
show (a parent with no items isn't rendered), so removing the rows removes the tab.

---

## Build your own shop

**The easy way — the Catalog Builder tool.** Run `tool/catalog-builder/run.bat`
(or `run.sh`), and build your whole shop in the browser: search items, make
categories and tabs, set a price + currency per item, and export it as SQL. No
hand-written SQL needed. See [tool/catalog-builder/README.md](tool/catalog-builder/README.md).

**By hand.** `sql/sample_catalog.sql` is the template. Each row is one item under
`parent` → `subcat`:

```sql
INSERT INTO boz_marketplace_catalog
  (parent, subcat, sort_order, item_id, name, icon, price, currency, level, descr)
VALUES
  ('My Shop', 'Weapons', 0, <item_id>, '<name>', <icon>, <price>, 'Platinum', 0, '<tooltip>');
```

- **`parent`** = the top-level category (any name; new names become new categories).
- **`subcat`** = the tab under it (any name).
- **`currency`** = what it costs in. `Platinum` (the default) spends carried coin and
  works on any server. You can also use **any alternate currency by name** — stock ones
  like `Doubloon` / `Ebon Crystal`, or your own: just create the `alternate_currency`
  row and set `currency` to its item name. (See [server/integration.md](server/integration.md#currency-notes).)
- The 3D preview model/slot/color are derived automatically from the item's own
  `items` fields — you don't specify them.

To build it (if you change the ASI source), see `client/CMakeLists.txt`:
`cmake -G "Visual Studio 17 2022" -A Win32 -B build && cmake --build build --config Release`.

---

## Compatibility

- **Client:** RoF2 ("RoF2 trilogy" build). Other `eqgame.exe` builds need the offsets
  re-derived — see [docs/adapting-to-your-client.md](docs/adapting-to-your-client.md).
- **Server:** EQEmu (built from source). The server side is a handful of source
  additions + one table; there are **no new network opcodes**.

## License

GPLv3 — see [LICENSE](LICENSE). The server portions are derived from / link against
EQEmu (itself GPLv3), so the project as a whole is GPLv3. You're free to use, modify, and
redistribute it; derivative works must stay GPLv3.

## Credits

Built for the **Blood of Zek** EQEmu server and shared with the EQEmu community. Issues
and pull requests welcome.
