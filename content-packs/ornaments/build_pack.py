#!/usr/bin/env python3
"""
Build ornaments/pack.json from a reference EQEmu DB.

Weapon ornaments are augments (itemtype 54, augtype 524288) whose `idfile` points
at a weapon model (ITxxx). They render ONLY if the client actually ships that
model -- otherwise the weapon shows the broken-placeholder model. So this pack is
filtered to ornaments whose model exists in the client, using the
`boz_client_weapon_models` table that orn_filter.py builds by scanning your
client's gequip*.s3d / *.eqg archives.

To (re)build for YOUR client:
  1. python orn_filter.py      # scans your client -> (re)builds boz_client_weapon_models
  2. python build_pack.py      # emits pack.json = ornaments whose model your client has

The shipped pack.json is the stock-RoF2 result: 391 renderable of 665 total weapon
ornaments (the other 274 reference models RoF2 lacks). install.py consumes pack.json.

Each item is emitted by splitting columns: constants -> a shared base template,
anything that varies (Name, model, icon, ...) -> per-item overrides, so installs
reproduce the original ornament definitions faithfully.
"""
import json, os, subprocess

MYSQL = r"C:\Program Files\MySQL\MySQL Server 8.0\bin\mysql.exe"

def q(sql):
    r = subprocess.run([MYSQL, "-ueqemu", "-peqemu", "peq", "--batch",
                        "--skip-column-names", "-e", sql], capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(r.stderr)
    return [l.split("\t") for l in r.stdout.replace("\r", "").split("\n") if l.strip()]

cols = [c[0] for c in q("SHOW COLUMNS FROM items")]
collist = ",".join("`%s`" % c for c in cols)

# renderable weapon ornaments = aug (54 / 524288) whose ITxxx model is in the
# client. One bulk query for all full rows.
raw = q("SELECT %s FROM items "
        "WHERE itemtype=54 AND augtype=524288 AND idfile LIKE 'IT%%' "
        "AND CAST(SUBSTRING(idfile,3) AS UNSIGNED) IN (SELECT model FROM boz_client_weapon_models) "
        "ORDER BY Name, id" % collist)
rows = [{c: (None if x == "NULL" else x) for c, x in zip(cols, v)} for v in raw]

# split constant -> base template, varying -> per-item overrides
const, varying = {}, []
for c in cols:
    if len({r[c] for r in rows}) == 1:
        const[c] = rows[0][c]
    else:
        varying.append(c)
for k in ("id", "Name"):
    const.pop(k, None)
    if k in varying:
        varying.remove(k)

items = [{"name": r["Name"], "overrides": {c: r[c] for c in varying}} for r in rows]

pack = {
    "pack": "ornaments",
    "default_base_id": 4004000, "default_price": 10, "default_currency": "Platinum",
    "base_item": const,
    "categories": [{"parent": "Ornaments", "chunk_size": 60, "items": items}],
}
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pack.json")
json.dump(pack, open(out, "w", encoding="utf-8"), indent=1)
print("ornaments=%d (renderable)  per-item fields: %s  -> %s"
      % (len(items), ", ".join(varying), out))
