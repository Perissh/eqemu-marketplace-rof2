#!/usr/bin/env python3
"""
Build mounts/pack.json from a reference EQEmu DB.

Mounts are itemtype-68 clickies whose click casts a mount spell. Whether a given
mount actually *renders* depends on the client's race models, so this set is
curated + validated for a stock RoF2 client (200 mounts). The shipped pack.json
is that result; install.py consumes it.

Source = the validated Marketplace catalog (parent='Mounts'). Every item is
emitted by splitting its columns: the ones that VARY across the set become
per-item overrides, the ones that are constant become a shared base template.
So the install reproduces the original item definitions faithfully while staying
compact, and `clicktype` (which differs 5/3/1 across mounts) is carried per item.

NOTE: mounts are a Luclin-era+ feature. Pre-Luclin servers should simply not
install this pack -- packs are independent (see ../README.md).

Re-run against your own DB to reproduce/audit. For a non-RoF2 client, trim
pack.json to the mounts your client can display (or rebuild from your own list).
"""
import json, os, subprocess

MYSQL = r"C:\Program Files\MySQL\MySQL Server 8.0\bin\mysql.exe"
PARENT = "Mounts"

def q(sql):
    r = subprocess.run([MYSQL, "-ueqemu", "-peqemu", "peq", "--batch",
                        "--skip-column-names", "-e", sql], capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(r.stderr)
    return [l.split("\t") for l in r.stdout.replace("\r", "").split("\n") if l.strip()]

cols = [c[0] for c in q("SHOW COLUMNS FROM items")]
collist = ",".join("`%s`" % c for c in cols)
ids = [r[0] for r in q("SELECT i.id FROM boz_marketplace_catalog c JOIN items i ON i.id=c.item_id "
                       "WHERE c.parent='%s' ORDER BY i.Name" % PARENT)]
rows = []
for iid in ids:
    v = q("SELECT %s FROM items WHERE id=%s" % (collist, iid))[0]
    rows.append({c: (None if x == "NULL" else x) for c, x in zip(cols, v)})

# split columns: constant -> shared base template, varying -> per-item overrides
const, varying = {}, []
for c in cols:
    if len({r[c] for r in rows}) == 1:
        const[c] = rows[0][c]
    else:
        varying.append(c)
for k in ("id", "Name"):            # id is set by the installer; Name is the item label
    const.pop(k, None)
    if k in varying:
        varying.remove(k)

items = [{"name": r["Name"], "overrides": {c: r[c] for c in varying}} for r in rows]

pack = {
    "pack": "mounts",
    "default_base_id": 4003000, "default_price": 50, "default_currency": "Platinum",
    "base_item": const,
    "categories": [{"parent": "Mounts", "chunk_size": 60, "items": items}],
}
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pack.json")
json.dump(pack, open(out, "w", encoding="utf-8"), indent=1)
print("mounts=%d  per-item fields: %s  -> %s" % (len(items), ", ".join(varying), out))
