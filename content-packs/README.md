# Marketplace content packs

Optional, drop-in content for the data-driven Marketplace. Each pack adds a batch
of items and the catalog rows that make them show up as Marketplace categories.
Install only the packs you want; skip or remove any at will.

| Pack | Adds | Category/ies | Default price |
|------|------|--------------|---------------|
| **illusions** | 196 player self-illusions | Illusions | 10 |
| **familiars** | 55 companion familiars | Familiars | 10 |
| **pet-illusions** | 37 combat-pet re-skins | Pet Illusions | 10 |
| **mounts** | 200 rideable mounts | Mounts | 50 |
| **ornaments** | 391 weapon-appearance augments | Ornaments | 10 |

All content is curated to actually **render** on a RoF2 client — mounts to
defined+renderable mount models, ornaments to weapon models the client ships, and
illusions to real RoF2 race models. Many NPC-race illusions need a one-time client
**make-global** step to render (see *Making NPC-race illusions render* below); the
rest render on a stock client. (Each pack's README explains its filter and how to
re-validate for a different client.)

---

## Your database is safe — and here's exactly why

This installer is built so it **cannot clobber your data**. Every guarantee below
is enforced in code, before anything is written, and was verified with live tests
(including planting foreign "admin" items inside a pack's own id range and category
and confirming they survive both install and uninstall untouched).

1. **It never overwrites an item it didn't create.** Before creating anything it
   checks the target id range. If it finds *even one* item it didn't make itself,
   it **aborts and changes nothing**, and tells you to pick a free range.

2. **It never merges into your categories.** If a category name it would use
   already exists with items that aren't from this pack, it **aborts** — you can
   rename the pack's category in `config.json` instead. It will not mix its items
   into yours.

3. **It tracks exactly what it created.** Every item id is recorded in a
   `mkt_pack_items` table. A re-install updates those in place; nothing leaks.

4. **Uninstall is surgical — in any mode.** `--uninstall` removes precisely the
   ids it recorded creating, read from the tracking table, **not** from the
   configured range. So even if you changed the range afterward, it removes exactly
   what it made and leaves everything else — including foreign items that happen to
   share a category — completely untouched.

5. **Manual or auto, same protection.** Choose exact ids (`base_id: 4002000`) or
   let it find free space for you (`base_id: "auto"`). The never-overwrite guard
   applies either way; `auto` simply picks a block above your existing items.

6. **No dependency on your existing items.** Each pack ships its *own* complete
   item definitions, so it never assumes some stock item exists, and never edits
   your items.

7. **No client changes by default.** Packs are pure database rows. The Marketplace
   builds its menu from the catalog, so a pack's tab appears when you install it and
   vanishes when you remove it — no recompiling, no patching, no client files. The one
   opt-in exception is `--client`, which adds illusion model lines to `GlobalLoad.txt`
   so NPC-race illusions render (see *Making NPC-race illusions render*); never automatic.

If anything is in the way, the installer's default is always **stop and tell you**,
never overwrite.

---

## Install

Run on the machine with database access (same as the catalog builder):

```
pip install pymysql                 # one-time
python install.py illusions         # install a pack
python install.py mounts
python install.py ornaments
```

Then regenerate shared memory and restart your zones so the new items load.

Settings live in `config.json` (a `db` block plus per-pack `base_id` / `price` /
`currency`). Any can be overridden per run:

```
python install.py mounts --price 25 --currency "Donation Token"
python install.py illusions --base-id 5000000
```

Useful subcommands:

```
python install.py <pack> --status       # how many items this pack has installed
python install.py <pack> --uninstall    # cleanly remove it
```

## Pick and choose

Packs are fully independent — install only what fits your server. Want player
illusions but not familiars? Install `illusions`, skip `familiars`. **Mounts are a
Luclin-era+ feature**, so a classic/Kunark (e.g. P99-style) server can simply not
install the mounts pack: no Mounts category, no mount items, zero footprint. You can
also trim *within* a pack — `pack.json` is a plain list, so delete entries before
installing if you want only some of it.

## Choosing where items go (id range)

Per pack in `config.json`:

- **A number** (`"base_id": 4002000`) — items are created at exactly that range.
  If it's occupied, the install aborts (it won't overwrite); pick another.
- **`"auto"`** (`"base_id": "auto"`) — the installer finds a free block above your
  existing items and uses it.

Either way, your data is never overwritten.

## Categories

If a pack's category name (e.g. `Illusions`) collides with one you already use,
rename the pack's copy in `config.json`:

```json
"packs": { "illusions": { "categories": { "Illusions": "Player Illusions" } } }
```

## Currency & price

`currency` defaults to `Platinum` (carried coin, works on any server). Set it to
any currency name your Marketplace knows (e.g. a custom token), per pack or via
`--currency`.

## Icons

Each pack ships curated per-item icons, so cosmetics show their real inventory art
out of the box. As a safety net, on install the tool also **fills any item still on
the pack's placeholder icon** with the art of the stock item that casts the same
spell on *your* server — so a regenerated or hand-trimmed pack won't show a blank
where your database already has the right icon.

It's deliberately conservative:

- It only ever **fills a placeholder**. A curated per-item icon is never changed, so
  distinct looks (every mount, every illusion) are never collapsed onto one shared
  icon.
- It matches only against genuine **stock** items — never another pack's items — by
  the clicked spell, taking the most common icon for that spell.
- An item whose spell no stock item casts simply keeps the pack's own icon.

Skip the pass with `--no-icon-match` to keep exactly the icons the pack shipped.

### Items with no stock art

A handful of cosmetics cast a spell that *no* stock item casts (e.g. a few rare
illusions), so there's nothing to borrow an icon from — those keep the placeholder.
List them with:

```
python install.py illusions --list-missing
```

It prints each such item with its spell and exits without changing anything. Give
them a real icon in either of two ways:

- **One for all** — `--placeholder-icon <id>` puts that icon on every item nothing
  else could fill:

  ```
  python install.py illusions --placeholder-icon 645
  ```

- **Case-by-case** — a per-item map in `config.json`, keyed by item name. It takes
  precedence over the one-for-all value, so you can set a few by hand and let the
  rest fall back:

  ```json
  "packs": { "illusions": { "placeholder_icons": {
      "Illusion: Ancient Rallos Zek": 1631,
      "Illusion: Amygdalan": 660
  } } }
  ```

After every install the tool reports how many items still have no icon, so you
always know whether there's anything left to assign.

---

## Removing the stat buffs (`--no-buffs`)

Many mounts, illusions, and familiars carry stat buffs — the classic mount
"blessing" that adds HP/AC, a familiar's endurance, an illusion's stat tweak. If
you'd rather sell the *look* without the *power*, add `--no-buffs` when you install:

```
python install.py mounts --no-buffs
python install.py illusions --no-buffs
python install.py familiars --no-buffs
```

Per cosmetic the pack casts, it:
- **neuters the separate "blessing"** a mount/illusion triggers (the +HP/+AC spell), and
- **blanks direct stat effects** baked into a familiar/illusion spell,

while **keeping the look + utility** — model size, levitate, run speed, invis,
see-invis, water-breathing all stay.

**It never touches a spell a class can actually learn.** Many `Illusion:` spells are
real class spells (enchanter / druid / ranger, etc.); those keep their stats so the
classes that cast them are unaffected — only cosmetic-only spells are stripped.

Every changed spell is backed up to `mkt_spell_backup`; `--uninstall` restores them
(shared-spell safe: a spell is only restored once no installed pack still needs it
neutered).

### Server-enforced vs. client-displayed

The server strip removes the stats the server *enforces* — that's the real fix, no
actual advantage. But the client computes the numbers it *shows* from its **own**
`spells_us.txt`, so a buff that lingers (familiars, classic illusions) may still
*display* its old stats even though they're never applied. It's a cosmetic mismatch,
not a real stat.

To make the display match too, run the included editor against your client's spell
file, then push the result through your own patcher:

```
python nobuffs_client_patch.py "C:/path/to/EverQuest/spells_us.txt"
```

It mirrors the server's neutered effects onto **only** the spell lines the installer
changed (matched by id), writes a one-time backup first, and leaves every other
spell byte-for-byte alone — your custom spells are untouched.

---

## Making NPC-race illusions render (`--client`, `--laa`, `--global-only`)

Most marketplace illusions use **NPC race models** (gorilla, drachnid, dragon, …).
On a **stock RoF2 client** those models aren't loaded globally, so the client falls
back to a plain human — you cast the illusion and nothing changes. The fix is to add
the model's archive to the client's `Resources\GlobalLoad.txt`. The model files
themselves (`.eqg` / `_chr.s3d`) are **stock RoF2 assets every client already has**,
so **only `GlobalLoad.txt` is ever touched** — no new art, no patch, no other client
file changes.

The race ids that need this (and the exact `GlobalLoad.txt` lines to add) ship in
`illusion_globalload.json` next to `install.py`. Illusions whose race is *not* in
that list already render on a stock client.

### `--client <EQ client root>` — wire up the models

After a normal install, point the tool at your EQ client folder and it appends the
missing model lines to `Resources\GlobalLoad.txt`:

```
python install.py illusions --client "C:/EverQuest-RoF2"
```

It is **safe and idempotent**:

- It inserts the lines just before the file's phase-4 block (or at the end if there
  is none), and **skips any model archive already present**, so re-running adds
  nothing the second time (`already current`).
- It **backs up the original once** to `GlobalLoad.txt.bak` before the first change,
  and **preserves the file's newline style** (CRLF vs LF).
- It **aborts with a clear message** if `Resources\GlobalLoad.txt` isn't found —
  that means `--client` is pointing at the wrong folder, and nothing is changed.

You can also set a default client path in `config.json` (`packs.<pack>.client` or a
top-level `"client"`); the `--client` flag wins when both are present.

If you install illusions **without** `--client`, the tool tells you how many of the
just-installed illusions need their models made global, so you know to re-run with
`--client` (or to use `--global-only`).

### `--laa` — make more room for illusions

Loading the full NPC-model set globally adds roughly **285 MB** of always-resident
models. The 32-bit RoF2 client is capped at ~2 GB of address space, and that extra
load can push it over and crash. `--laa` (used together with `--client`) sets the
PE **`LARGE_ADDRESS_AWARE`** flag on `eqgame.exe`, raising the cap to ~3.5 GB so
there's headroom for the always-loaded models:

```
python install.py illusions --client "C:/EverQuest-RoF2" --laa
```

It's a one-flag PE patch and is **idempotent**: if the flag is already set it reports
`already LAA` and does nothing; otherwise it backs the exe up once to
`eqgame.exe.preLAA` before flipping the single bit. (`--laa` requires `--client`.)

### `--global-only` — install only the stock-renderable illusions

If you'd rather **not** edit the client at all, `--global-only` installs only the
illusions whose race already renders on a stock client and **skips** the ones that
would need `GlobalLoad.txt` edits:

```
python install.py illusions --global-only
```

It reports how many illusions it skipped. (The skipped ids are simply left out; the
range may have gaps, which is fine.)

### Uninstall leaves `GlobalLoad.txt` alone

`--uninstall` removes the pack's database rows but **never touches `GlobalLoad.txt`**.
The model archives it added are **shared between the `illusions` and `pet-illusions`
packs** (same race models), so removing one pack must not pull a model the other
still uses. If you want to revert the client edit, restore `GlobalLoad.txt.bak`
(and `eqgame.exe.preLAA`) by hand.

---

## Requirements

- Python 3 + PyMySQL (`pip install pymysql`)
- The data-driven Marketplace (this repository) installed
- Database access from wherever you run `install.py`
- A `shared_memory` regen + zone restart after installing/uninstalling (new items)

## Re-validating for a non-RoF2 client

The shipped lists are curated for a stock RoF2 client. Each pack ships its
`build_pack.py` generator (and ornaments ships `orn_filter.py`, which scans your
client's models) so you can regenerate the list against your own client/DB — see
each pack's README.

## Files

```
content-packs/
  install.py          one installer for every pack (safety logic + --no-buffs + make-global)
  nobuffs_client_patch.py   mirror --no-buffs onto a client spells_us.txt (display fix)
  illusion_globalload.json  GlobalLoad.txt lines + race ids for --client / --global-only
  config.json         db connection + per-pack base_id / price / currency / categories / client
  illusions/  familiars/  pet-illusions/  mounts/  ornaments/
      pack.json       the curated item list + a self-contained base-item template
      build_pack.py   regenerates pack.json from a reference database
      README.md       pack details
  ornaments/orn_filter.py   scans a client and rebuilds the renderable-model table
```
