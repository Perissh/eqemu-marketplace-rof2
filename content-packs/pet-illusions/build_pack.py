#!/usr/bin/env python3
"""
Build pet-illusions/pack.json from a reference EQEmu DB.

Re-skins your combat pet. Filtered to:
  * targettype 14 (Pet) -- so the clicky illusions the PET, not the caster
    (the targettype-6 "Pet Illusion:" spells illusion the player, a bug), and
  * race <= 732 -- post-RoF2 race models render nothing on a RoF2 client.
Each clicky reuses the real icon of a stock item that clicks the same spell where
one exists.

Re-run against your own DB/client to re-validate. install.py consumes pack.json.
"""
import json, os, subprocess

MYSQL = r"C:\Program Files\MySQL\MySQL Server 8.0\bin\mysql.exe"

PACK = "pet-illusions"
CATEGORY = "Pet Illusions"
DEFAULT_BASE = 4002500
PRICE = 10
WHERE = "s.name LIKE 'Pet Illusion:%' AND s.targettype=14 AND NOT (s.effectid1=58 AND s.effect_base_value1>732)"


def q(sql):
    r = subprocess.run([MYSQL, "-ueqemu", "-peqemu", "peq", "--batch", "--skip-column-names", "-e", sql],
                       capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(r.stderr)
    return [l.split("\t") for l in r.stdout.replace("\r", "").split("\n") if l.strip()]


ICON_SUBQ = "(SELECT MIN(it.icon) FROM items it WHERE it.clickeffect=s.id AND it.icon>0 AND it.id<4000000)"

seen = {}
for sid, name, icon in q("SELECT s.id, s.name, %s FROM spells_new s WHERE %s ORDER BY s.id" % (ICON_SUBQ, WHERE)):
    if name in seen:
        continue
    ov = {"clickeffect": int(sid)}
    if icon not in ("NULL", "", None):
        ov["icon"] = int(icon)
    seen[name] = ov
items = [{"name": n, "overrides": ov} for n, ov in sorted(seen.items())]

cols = [c[0] for c in q("SHOW COLUMNS FROM items")]
vals = q("SELECT %s FROM items WHERE id=64702" % ",".join("`%s`" % c for c in cols))[0]
base = {c: (None if v == "NULL" else v) for c, v in zip(cols, vals)}
for k in ("id", "Name", "clickeffect"):
    base.pop(k, None)

pack = {"pack": PACK, "default_base_id": DEFAULT_BASE, "default_price": PRICE, "default_currency": "Platinum",
        "base_item": base, "categories": [{"parent": CATEGORY, "chunk_size": 60, "items": items}]}
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pack.json")
json.dump(pack, open(out, "w", encoding="utf-8"), indent=1)
print("%s: %d items (%d with real icons) -> %s"
      % (PACK, len(items), sum("icon" in i["overrides"] for i in items), out))
