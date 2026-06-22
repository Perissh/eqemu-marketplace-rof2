# Mounts content pack

Adds **200 mounts** to the Marketplace as a single **Mounts** category. Each is a
right-click clicky (itemtype 68) that summons a rideable mount.

## Era gate — read this first

Mounts are a **Luclin-era and later** feature. If your server isn't up to Luclin
(classic/Kunark-only, P99-style, etc.), **do not install this pack** — just skip
it. Packs are independent; skipping this one leaves no Mounts category and no
mount items at all. See [../README.md](../README.md).

## What's validated

The shipped `pack.json` is the set that **renders on a stock RoF2 client** (200
mounts). A mount's appearance comes from its summon *spell*, not the item model,
so renderability is client-dependent. On a different client some may not display;
trim `pack.json` (it's a plain list) to what your client supports, or rebuild it.

`build_pack.py` reproduces the pack from a reference database, splitting each
item's columns into a shared template (constants) + per-item overrides (anything
that varies — stats, class restrictions, `clicktype`, model, icon, summon spell),
so installs are faithful to the original mount definitions.

## Install

1. In `../config.json`, under `packs.mounts`, set `base_id` (free item-id range),
   `price`, and `currency` (defaults: `4003000`, `50`, `Platinum`).
2. `python ../install.py mounts`
3. Regenerate shared memory and restart zones.

If your id range is occupied, or a `Mounts` category already exists with items
that aren't ours, the installer aborts and tells you how to resolve it (free
`base_id`, or rename the category in config).

## Uninstall

```
python ../install.py mounts --uninstall
```

Removes exactly what this pack created (tracked in `mkt_pack_items`) — nothing else.

## Files

- `pack.json` — the validated mount list + a self-contained base-item template.
- `build_pack.py` — regenerates `pack.json` from a reference database.
