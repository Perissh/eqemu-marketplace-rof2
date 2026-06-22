# Ornaments content pack

Adds **391 weapon ornaments** to the Marketplace as a single **Ornaments**
category. Each is an augment (itemtype 54) that changes a weapon's *appearance*
without touching its stats — drop it in a weapon's ornament slot.

## What's validated

A weapon ornament references a weapon model (`idfile` = `ITxxx`). It only renders
if the **client actually ships that model** — otherwise the weapon shows a broken
placeholder. The shipped `pack.json` is filtered to the **391 ornaments that
render on a stock RoF2 client** (out of 665 total weapon ornaments; the other 274
point at models RoF2 doesn't have and are excluded).

### Re-validating for a different client

If you run a non-RoF2 client (more or fewer models), rebuild the filter:

```
python orn_filter.py "C:/path/to/EverQuest"   # scans gequip*.s3d / *.eqg in your client
python build_pack.py                           # regenerates pack.json for that client
```

`orn_filter.py` reads every `ITxxx` model your client ships and rebuilds the
`boz_client_weapon_models` table; `build_pack.py` then keeps only the ornaments
whose model is present. (DB connection comes from `../config.json`.)

## Install

1. In `../config.json`, under `packs.ornaments`, set `base_id` (free item-id
   range — a number, or `"auto"`), `price`, and `currency` (defaults `4004000`,
   `10`, `Platinum`).
2. `python ../install.py ornaments`
3. Regenerate shared memory and restart zones.

If your id range is occupied, or an `Ornaments` category already exists with items
that aren't ours, the installer aborts and tells you how to resolve it — it never
overwrites anything.

## Uninstall

```
python ../install.py ornaments --uninstall
```

Removes exactly what this pack created (tracked in `mkt_pack_items`) — nothing else.

## Files

- `pack.json` — the renderable ornament list + a self-contained base-item template.
- `build_pack.py` — regenerates `pack.json` from the model table.
- `orn_filter.py` — scans your client and (re)builds the renderable-model table.
