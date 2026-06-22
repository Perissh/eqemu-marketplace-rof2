# Pet Illusions content pack

Adds **37 pet-illusion clickies** to the Marketplace as a **Pet Illusions**
category. Right-click to re-skin your combat pet.

Every clicky is reusable by all classes/races (NoDrop), targets the **Pet** — so it
illusions your pet, not you — and uses the real inventory icon of a stock item that
clicks the same spell where one exists.

## What's validated

Pet illusions that are Pet-targeted (`targettype` 14) — some "Pet Illusion:" spells
are Self-targeted and illusion the *caster* instead of the pet, which is a bug, so
those are excluded. Race must also be ≤ 732 (RoF2-renderable). Re-validate with
`python build_pack.py`.

## Install

1. In `../config.json`, under `packs.pet-illusions`, set `base_id` (a number, or
   `"auto"`), `price`, `currency` (defaults `4002500` / `10` / `Platinum`).
2. `python ../install.py pet-illusions`
3. Regenerate shared memory + restart zones.

The installer aborts rather than overwrite an occupied id range or merge into an
existing `Pet Illusions` category — see [../README.md](../README.md) for the full
safety model.

## Uninstall

`python ../install.py pet-illusions --uninstall` — removes exactly what it created.

## Files

- `pack.json` — the validated list + a self-contained base-item template.
- `build_pack.py` — regenerates `pack.json` from a reference database.
