#!/usr/bin/env python3
"""
EQ Marketplace -- content-pack installer.

Installs a content pack (cosmetics / ornaments / mounts / ...) into an EQEmu
database: it creates the pack's items at a CONFIGURABLE id range and adds the
catalog rows the data-driven Marketplace reads. Because the Marketplace builds
its category tree from `boz_marketplace_catalog`, a pack's tab appears the moment
its rows exist and disappears when they're gone -- no client changes, ever.

Safe by design ("look before you leap"):
  * It NEVER blind-deletes an id range. Before creating items it checks the target
    range; if it finds ANY item it did not create itself, it ABORTS and tells you
    to pick a free base id. Your existing items are never touched.
  * It records exactly which ids it created (table `mkt_pack_items`), so a
    re-install updates in place and `--uninstall` removes precisely what it added.
  * Self-contained: each pack ships a full base-item template, so it does not
    depend on any particular stock item already existing on your server.

Run it on the box with local DB access (same as the catalog builder):
  python install.py illusions                 # install
  python install.py illusions --no-buffs      # install + strip cosmetic stat buffs
  python install.py illusions --status        # show what's installed
  python install.py illusions --uninstall     # remove it cleanly (restores any buffs)

Settings come from config.json (a `db` block + per-pack base_id/price/currency).
Flags override: --base-id  --auto  --no-buffs  --price  --currency
                --host --user --password --database
"""
import os, sys, json, argparse

try:
    import pymysql
except ImportError:
    sys.exit("This installer needs PyMySQL:  pip install pymysql")

HERE = os.path.dirname(os.path.abspath(__file__))


def load_json(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def db_connect(cfg):
    db = cfg.get("db", {})
    return pymysql.connect(
        host=db.get("host", "127.0.0.1"), port=int(db.get("port", 3306)),
        user=db.get("user", "eqemu"), password=db.get("password", "eqemu"),
        database=db.get("database", "peq"), charset="utf8mb4", autocommit=False)


def ensure_tracking(cur):
    cur.execute("""CREATE TABLE IF NOT EXISTS mkt_pack_items (
        pack VARCHAR(32) NOT NULL, item_id INT NOT NULL,
        PRIMARY KEY (pack, item_id), KEY k_item (item_id))""")


def next_free_base(cur, start, count, ours):
    """Pick a base id for `auto` mode. On a re-install, reuse our existing block;
    otherwise return the first id at/above `start` that sits above every existing
    item, so [base, base+count-1] is guaranteed free. (count is accepted for
    clarity/future use; the 'above everything' result is free regardless.)"""
    if ours:
        return min(ours)
    cur.execute("SELECT COALESCE(MAX(id), 0) FROM items WHERE id >= %s", (start,))
    return max(start, cur.fetchone()[0] + 1)


# --- optional --no-buffs: strip stat buffs from the cosmetics a pack casts ------
# KEPT effects: the core look + movement/vision utility + the triggers (whose
# blessing spell is neutered separately). Everything else (stats, AC, HP, resists,
# regen, procs, ...) is blanked. Every changed spell is backed up to
# mkt_spell_backup so --uninstall restores it.
KEEP_SPA = (58, 108, 113,        # Illusion / Familiar / SummonHorse -- the core effect
            3, 57, 89,           # MovementSpeed / Levitate / ModelSize
            12, 13, 14, 65, 66,  # Invis / SeeInvis / WaterBreathing / Infra / UltraVision
            99, 10,              # Root (tree illusion) / CHA spacer (no-op)
            368, 425,            # FactionMod / Display (not implemented)
            340, 374)            # SpellTrigger / ApplyEffect (their blessing is neutered)


def ensure_spell_tables(cur):
    cur.execute("CREATE TABLE IF NOT EXISTS mkt_spell_backup LIKE spells_new")
    cur.execute("""CREATE TABLE IF NOT EXISTS mkt_pack_spells (
        pack VARCHAR(32) NOT NULL, spell_id INT NOT NULL,
        PRIMARY KEY (pack, spell_id), KEY k_sp (spell_id))""")


def strip_buffs(cur, pack, spell_ids):
    """Strip stat buffs from the spells a pack's clickies cast, backing each changed
    spell up to mkt_spell_backup first:
      (a) a blessing the spell TRIGGERS (SPA 374/340) -> fully neutered (its whole
          job is the stat buff); and
      (b) a DIRECT stat effect on the spell itself -> blanked in place, keeping the
          core look + movement/vision utility, so the clicky still casts a spell the
          client knows (clean cast bar) and still does its cosmetic job."""
    if not spell_ids:
        return 0
    ensure_spell_tables(cur)
    spin = ",".join(map(str, spell_ids))
    keep = ",".join(map(str, KEEP_SPA))

    # (a) spells these trigger -- the triggered id sits in the 374/340 limit. A
    # triggered spell with NO core cosmetic (Illusion/Familiar/Mount) is a pure stat
    # blessing -> full neuter. One that DOES carry a cosmetic (e.g. an illusion that
    # randomly picks its model via a trigger -- "Illusion: Ancient God") must NOT be
    # wiped; the selective pass blanks any stats on it while keeping the look.
    union = " UNION ".join(
        "SELECT effect_limit_value%d v FROM spells_new WHERE id IN (%s) AND effectid%d IN (374,340)"
        % (n, spin, n) for n in range(2, 13))
    cur.execute("SELECT DISTINCT v FROM (%s) t WHERE v > 0" % union)
    triggered = {r[0] for r in cur.fetchall()}

    # NEVER touch a spell a class can actually learn -- those are real class spells
    # (enchanter / druid / ranger illusions, etc.) that classes cast and rely on for
    # their stats. A spell is "cosmetic-only" when no class can learn it: every
    # classesN is the not-learnable sentinel (255). (>=254 to be safe.)
    notlearn = "LEAST(%s) >= 254" % ",".join("classes%d" % n for n in range(1, 17))

    blessings = set()
    if triggered:
        tin = ",".join(map(str, triggered))
        cosm = " OR ".join("effectid%d IN (58,108,113)" % n for n in range(1, 13))
        cur.execute("SELECT id FROM spells_new WHERE id IN (%s) AND NOT (%s) AND %s" % (tin, cosm, notlearn))
        blessings = {r[0] for r in cur.fetchall()}

    # (b) selective: any source OR triggered-cosmetic spell carrying a direct stat ->
    # blank the stats, keep the look + utility. (Blessings are full-neutered instead.)
    cand = ",".join(map(str, set(spell_ids) | triggered))
    cond = " OR ".join("effectid%d NOT IN (254,%s)" % (n, keep) for n in range(1, 13))
    cur.execute("SELECT id FROM spells_new WHERE id IN (%s) AND (%s) AND %s" % (cand, cond, notlearn))
    direct = {r[0] for r in cur.fetchall()} - blessings

    n_changed = 0
    for bid in blessings:
        cur.execute("INSERT IGNORE INTO mkt_spell_backup SELECT * FROM spells_new WHERE id=%s", (bid,))
        cur.execute("UPDATE spells_new SET %s, buffduration=0, buffdurationformula=0 WHERE id=%%s"
                    % ", ".join("effectid%d=254" % n for n in range(1, 13)), (bid,))
        cur.execute("INSERT IGNORE INTO mkt_pack_spells (pack,spell_id) VALUES (%s,%s)", (pack, bid))
        n_changed += 1
    for sid in direct:
        cur.execute("INSERT IGNORE INTO mkt_spell_backup SELECT * FROM spells_new WHERE id=%s", (sid,))
        cur.execute("UPDATE spells_new SET %s WHERE id=%%s"
                    % ", ".join("effectid%d=IF(effectid%d IN (%s),effectid%d,254)" % (n, n, keep, n)
                                for n in range(1, 13)), (sid,))
        cur.execute("INSERT IGNORE INTO mkt_pack_spells (pack,spell_id) VALUES (%s,%s)", (pack, sid))
        n_changed += 1
    return n_changed


def restore_buffs(cur, pack):
    """Undo strip_buffs for a pack: restore each spell it changed from the backup,
    unless another pack still has that spell neutered (shared-spell safe)."""
    ensure_spell_tables(cur)
    cur.execute("SELECT spell_id FROM mkt_pack_spells WHERE pack=%s", (pack,))
    mine = [r[0] for r in cur.fetchall()]
    n = 0
    for sid in mine:
        cur.execute("SELECT COUNT(*) FROM mkt_pack_spells WHERE spell_id=%s AND pack<>%s", (sid, pack))
        if cur.fetchone()[0] == 0:
            cur.execute("REPLACE INTO spells_new SELECT * FROM mkt_spell_backup WHERE id=%s", (sid,))
            cur.execute("DELETE FROM mkt_spell_backup WHERE id=%s", (sid,))
            n += 1
    cur.execute("DELETE FROM mkt_pack_spells WHERE pack=%s", (pack,))
    return n


def flatten(pack, renames):
    """(offset, name, overrides, parent, subcat, sort) per item -- parent names get
    any config rename applied; subcats chunked at chunk_size so none exceeds the
    client's per-tab tile cap."""
    out, off = [], 0
    for cat in pack["categories"]:
        parent = renames.get(cat["parent"], cat["parent"])
        chunk, items = cat.get("chunk_size", 60), cat["items"]
        multi = len(items) > chunk
        for i, it in enumerate(items):
            subcat = ("%s %d" % (parent, i // chunk + 1)) if multi else parent
            out.append((off, it["name"], it.get("overrides", {}), parent, subcat, i))
            off += 1
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pack", help="pack folder name, e.g. cosmetics")
    ap.add_argument("--uninstall", action="store_true")
    ap.add_argument("--status", action="store_true")
    ap.add_argument("--no-buffs", action="store_true",
                    help="also strip stat buffs from the cosmetics this pack casts")
    ap.add_argument("--base-id", type=int)
    ap.add_argument("--auto", action="store_true", help="auto-pick a free id block")
    ap.add_argument("--price", type=int)
    ap.add_argument("--currency")
    for k in ("host", "user", "password", "database"):
        ap.add_argument("--" + k)
    a = ap.parse_args()

    cfg_path = os.path.join(HERE, "config.json")
    cfg = load_json(cfg_path) if os.path.exists(cfg_path) else {}
    for k in ("host", "user", "password", "database"):
        if getattr(a, k):
            cfg.setdefault("db", {})[k] = getattr(a, k)

    pack = load_json(os.path.join(HERE, a.pack, "pack.json"))
    pcfg = cfg.get("packs", {}).get(a.pack, {})
    base_id  = a.base_id if a.base_id is not None else pcfg.get("base_id", pack["default_base_id"])
    price    = a.price   if a.price   is not None else pcfg.get("price", pack.get("default_price", 0))
    currency = a.currency or pcfg.get("currency") or pack.get("default_currency", "Platinum")

    renames = pcfg.get("categories", {})
    rows = flatten(pack, renames)

    conn = db_connect(cfg); cur = conn.cursor()
    ensure_tracking(cur)
    cur.execute("SELECT item_id FROM mkt_pack_items WHERE pack=%s", (a.pack,))
    ours = {r[0] for r in cur.fetchall()}

    if a.status:
        rng = ("  ids %d..%d" % (min(ours), max(ours))) if ours else ""
        print("pack '%s': %d items installed%s" % (a.pack, len(ours), rng))
        return

    if a.uninstall:
        restored = restore_buffs(cur, a.pack)    # undo any --no-buffs strip first
        if not ours:
            conn.commit()
            extra = ("  (restored %d buff spell(s))" % restored) if restored else ""
            print("nothing installed for '%s'.%s" % (a.pack, extra)); return
        idl = ",".join(map(str, ours))
        cur.execute("DELETE FROM items WHERE id IN (%s)" % idl)
        cur.execute("DELETE FROM boz_marketplace_catalog WHERE item_id IN (%s)" % idl)
        cur.execute("DELETE FROM mkt_pack_items WHERE pack=%s", (a.pack,))
        conn.commit()
        extra = (" + %d buff spell(s) restored" % restored) if restored else ""
        print("uninstalled '%s' (%d items removed%s)." % (a.pack, len(ours), extra))
        return

    # base id: a number you pick (manual, the default), or auto (--auto flag or
    # base_id:"auto") to have a free block found for you. Either way the guards
    # below never clobber your data.
    if a.auto or str(base_id).lower() == "auto":
        dflt = pack.get("default_base_id", 4000000)
        start = 4000000 if str(dflt).lower() == "auto" else int(dflt)
        base_id = next_free_base(cur, start, len(rows), ours)
        print("auto base id -> %d" % base_id)
    else:
        base_id = int(base_id)
    lo, hi = base_id, base_id + len(rows) - 1

    # ---- guard 1: never merge into a category name someone else already uses -
    seen = set()
    for cat in pack["categories"]:
        orig = cat["parent"]; eff = renames.get(orig, orig)
        if eff in seen:
            continue
        seen.add(eff)
        if ours:
            idl = ",".join(map(str, ours))
            cur.execute("SELECT COUNT(*) FROM boz_marketplace_catalog WHERE parent=%s "
                        "AND item_id NOT IN (" + idl + ")", (eff,))
        else:
            cur.execute("SELECT COUNT(*) FROM boz_marketplace_catalog WHERE parent=%s", (eff,))
        n = cur.fetchone()[0]
        if n:
            sys.exit(
                "ABORT: a Marketplace category named '%s' already exists with %d item(s) "
                "not from this pack. Nothing was changed.\nRename this pack's category: add "
                "  packs.%s.categories = {\"%s\": \"Your New Name\"}  to config.json."
                % (eff, n, a.pack, orig))

    # ---- guard 2: target id range must be free (or already ours) -------------
    cur.execute("SELECT id FROM items WHERE id BETWEEN %s AND %s", (lo, hi))
    foreign = {r[0] for r in cur.fetchall()} - ours
    if foreign:
        sys.exit(
            "ABORT: id range %d..%d holds %d item(s) this pack did NOT create "
            "(e.g. %s). Nothing was changed.\nPick a free range (set "
            "packs.%s.base_id or pass --base-id), or set base_id to \"auto\" to "
            "have a free block chosen for you."
            % (lo, hi, len(foreign), sorted(foreign)[:5], a.pack))

    # clean re-install: remove our prior items/rows first (never touches others)
    if ours:
        idl = ",".join(map(str, ours))
        cur.execute("DELETE FROM items WHERE id IN (%s)" % idl)
        cur.execute("DELETE FROM boz_marketplace_catalog WHERE item_id IN (%s)" % idl)
        cur.execute("DELETE FROM mkt_pack_items WHERE pack=%s", (a.pack,))

    base = dict(pack["base_item"])
    if pcfg.get("icon"):                    # per-pack default icon for items lacking one
        base["icon"] = pcfg["icon"]
    base_icon = base.get("icon", 0)
    for off, name, ov, parent, subcat, sort in rows:
        iid = base_id + off
        row = dict(base); row.update(ov); row["id"] = iid; row["Name"] = name
        cols = list(row.keys())
        cur.execute(
            "INSERT INTO items (%s) VALUES (%s)" %
            (",".join("`%s`" % c for c in cols), ",".join(["%s"] * len(cols))),
            [row[c] for c in cols])
        cur.execute("INSERT INTO mkt_pack_items (pack,item_id) VALUES (%s,%s)", (a.pack, iid))
        cur.execute(
            "INSERT INTO boz_marketplace_catalog "
            "(parent,subcat,sort_order,item_id,name,icon,price,currency,level,descr) "
            "VALUES (%s,%s,%s,%s,%s,%s,%s,%s,0,'')",
            (parent, subcat, sort, iid, name, ov.get("icon", base_icon), price, currency))
    conn.commit()
    cats = " / ".join(c["parent"] for c in pack["categories"])
    print("installed '%s': %d items at ids %d..%d  (%s)  @ %d %s."
          % (a.pack, len(rows), lo, hi, cats, price, currency))
    if a.no_buffs:
        spell_ids = sorted({ov.get("clickeffect") for _, _, ov, *_ in rows if ov.get("clickeffect")})
        n = strip_buffs(cur, a.pack, spell_ids)
        conn.commit()
        print("--no-buffs: stripped stat buffs from %d spell(s), backed up for restore." % n)
    print("new items need a shared_memory regen + zone restart to appear in-game.")


if __name__ == "__main__":
    main()
