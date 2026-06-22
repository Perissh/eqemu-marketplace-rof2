# Illusions content pack

Adds **196 player-illusion clickies** to the Marketplace as an **Illusions**
category. Right-click to turn your character into another race or NPC.

Every clicky is reusable by all classes/races (NoDrop), targets **Self** — so it
always illusions *you*, never your current target or pet — and uses the real
inventory icon of a stock item that clicks the same spell where one exists.

## What's validated

Illusions whose race the RoF2 client can render (race ≤ 732; later race models
cast but show nothing) and that are Self-targeted. Re-validate for another client
with `python build_pack.py`.

## Install

1. In `../config.json`, under `packs.illusions`, set `base_id` (a number, or
   `"auto"`), `price`, `currency` (defaults `4002000` / `10` / `Platinum`).
2. `python ../install.py illusions`
3. Regenerate shared memory + restart zones.

The installer aborts rather than overwrite an occupied id range or merge into an
existing `Illusions` category — see [../README.md](../README.md) for the full
safety model.

## Uninstall

`python ../install.py illusions --uninstall` — removes exactly what it created.

## Files

- `pack.json` — the validated list + a self-contained base-item template.
- `build_pack.py` — regenerates `pack.json` from a reference database.
