# Familiars content pack

Adds **55 familiar clickies** to the Marketplace as a **Familiars** category.
Right-click to summon a cosmetic companion pet that follows you around.

Every clicky is reusable by all classes/races (NoDrop) and uses the real inventory
icon of a stock item that clicks the same spell where one exists.

## What's validated

Familiars whose summoned pet is defined in the `pets` table — `MakePet` does an
exact `type =` match with no fallback, so a familiar with no pet row would summon
nothing, and those are excluded. Re-validate for your DB with `python build_pack.py`.

## Install

1. In `../config.json`, under `packs.familiars`, set `base_id` (a number, or
   `"auto"`), `price`, `currency` (defaults `4002300` / `10` / `Platinum`).
2. `python ../install.py familiars`
3. Regenerate shared memory + restart zones.

The installer aborts rather than overwrite an occupied id range or merge into an
existing `Familiars` category — see [../README.md](../README.md) for the full
safety model.

## Uninstall

`python ../install.py familiars --uninstall` — removes exactly what it created.

## Files

- `pack.json` — the validated list + a self-contained base-item template.
- `build_pack.py` — regenerates `pack.json` from a reference database.
