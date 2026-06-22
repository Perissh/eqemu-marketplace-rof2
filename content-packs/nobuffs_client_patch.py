#!/usr/bin/env python3
"""
no-buffs CLIENT patch -- mirror the server-side --no-buffs neutering onto a client
spells_us.txt, so the stripped buffs don't *display* their old stats either.

Why this exists: `install.py --no-buffs` removes the buffs the SERVER enforces, but
the CLIENT computes the numbers it shows you from its OWN spells_us.txt. So a
familiar/illusion whose buff persists will still *display* its old +stats even
though the server never applies them (a cosmetic mismatch, not a real advantage).
This editor makes the display match: it blanks the same stat effects on the client
file, which you then push through your own patcher.

Safe by design:
  * It edits ONLY the spell lines the installer neutered (read from `mkt_pack_spells`),
    matched by spell id, and only their effect-id fields. Every other line is left
    byte-for-byte alone, so your custom spells/edits are untouched.
  * It writes a one-time backup (spells_us.txt.nobuffs-bak) before changing anything;
    restore = copy that back.

Usage (run AFTER `install.py <pack> --no-buffs`):
    python nobuffs_client_patch.py "C:/path/to/EverQuest/spells_us.txt"
DB connection comes from config.json (same db block install.py uses).

NOTE: effect-id fields live at columns 86-97 (effectid1..12) in the RoF2/UF-era
spells_us.txt. If your client uses a different layout, set EFFECTID_COL0 to match.
"""
import sys, os, shutil

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import install   # reuse db_connect / load_json / HERE

EFFECTID_COL0 = 86   # 0-based field index of effectid1; effectid1..12 = 86..97


def main():
    if len(sys.argv) < 2:
        sys.exit('usage: python nobuffs_client_patch.py <client spells_us.txt>')
    spell_file = sys.argv[1]
    if not os.path.isfile(spell_file):
        sys.exit('not found: ' + spell_file)

    cfg = install.load_json(os.path.join(install.HERE, 'config.json'))
    conn = install.db_connect(cfg); cur = conn.cursor()
    cur.execute("SELECT DISTINCT spell_id FROM mkt_pack_spells")
    targets = [r[0] for r in cur.fetchall()]
    if not targets:
        sys.exit('mkt_pack_spells is empty -- run install.py --no-buffs server-side first.')

    cols = ",".join("effectid%d" % n for n in range(1, 13))
    eff = {}
    for sid in targets:
        cur.execute("SELECT %s FROM spells_new WHERE id=%%s" % cols, (sid,))
        row = cur.fetchone()
        if row:
            eff[sid] = [str(x) for x in row]

    bak = spell_file + ".nobuffs-bak"
    if not os.path.exists(bak):
        shutil.copy2(spell_file, bak)

    with open(spell_file, encoding='latin-1') as fh:
        lines = fh.read().split('\n')

    changed = 0
    for i, line in enumerate(lines):
        if '^' not in line:
            continue
        f = line.split('^')
        try:
            sid = int(f[0])
        except ValueError:
            continue
        e = eff.get(sid)
        if e and len(f) > EFFECTID_COL0 + 11:
            for n in range(12):
                f[EFFECTID_COL0 + n] = e[n]
            lines[i] = '^'.join(f)
            changed += 1

    with open(spell_file, 'w', encoding='latin-1', newline='') as fh:
        fh.write('\n'.join(lines))

    print("patched %d / %d target spell lines in %s" % (changed, len(targets), spell_file))
    print("backup: %s   (restore = copy it back over the file)" % bak)
    print("now push the edited spells_us.txt through your patcher so players get it.")


if __name__ == '__main__':
    main()
