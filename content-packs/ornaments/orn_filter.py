#!/usr/bin/env python3
"""
orn_filter.py -- find which weapon-ornament models YOUR client can actually render.

Weapon ornaments reference a weapon model (idfile ITxxx). If the client doesn't
ship that model the weapon shows a broken-placeholder instead. This scans a
client's gequip*.s3d / *.eqg / loose it*.* assets, collects every IT model it
finds, and (re)builds the `boz_client_weapon_models` table that build_pack.py
JOINs against to keep only renderable ornaments.

Usage:
    python orn_filter.py "C:/path/to/EverQuest"     # your client folder
    python build_pack.py                            # then regenerate pack.json

DB connection comes from ../config.json (same db block install.py uses).
Needs PyMySQL (pip install pymysql), same dependency as install.py.

You only need this if you run a NON-RoF2 client -- the shipped ornaments/pack.json
is already filtered for a stock RoF2 client.
"""
import struct, zlib, glob, re, os, sys, json

try:
    import pymysql
except ImportError:
    sys.exit("needs PyMySQL: pip install pymysql")

XOR = bytes([0x95, 0x3A, 0xC5, 0x2A, 0x95, 0x7A, 0x95, 0x6A])


# ---- EQ archive (PFS) + world (WLD) parsing ------------------------------
def inflate(d, off, size):
    out = bytearray(); p = off
    while len(out) < size:
        dl, il = struct.unpack_from('<II', d, p); p += 8
        out += zlib.decompress(d[p:p + dl]); p += dl
    return bytes(out)


def read_pfs(path):
    d = open(path, 'rb').read()
    diro = struct.unpack_from('<I', d, 0)[0]
    cnt = struct.unpack_from('<I', d, diro)[0]
    ents = [struct.unpack_from('<III', d, diro + 4 + i * 12) for i in range(cnt)]
    fn = None; de = []
    for crc, o, s in ents:
        if crc == 0x61580AC9: fn = (o, s)
        else: de.append((o, s))
    fd = inflate(d, *fn); fc = struct.unpack_from('<I', fd, 0)[0]; p = 4; names = []
    for _ in range(fc):
        nl = struct.unpack_from('<I', fd, p)[0]; p += 4
        names.append(fd[p:p + nl].split(b'\x00')[0].decode('latin-1')); p += nl
    de.sort(key=lambda e: e[0]); out = {}
    for (o, s), x in zip(de, names): out[x] = inflate(d, o, s)
    return out


def read_pfs_names(path):
    with open(path, 'rb') as fh:
        diro = struct.unpack('<I', fh.read(4))[0]
        fh.seek(diro)
        cnt = struct.unpack('<I', fh.read(4))[0]
        ent = fh.read(cnt * 12)
        fn = None
        for i in range(cnt):
            crc, o, s = struct.unpack_from('<III', ent, i * 12)
            if crc == 0x61580AC9: fn = (o, s); break
        if not fn: return []
        fh.seek(fn[0]); comp = fh.read()
    fd = inflate(comp, 0, fn[1])
    fc = struct.unpack_from('<I', fd, 0)[0]; p = 4; names = []
    for _ in range(fc):
        nl = struct.unpack_from('<I', fd, p)[0]; p += 4
        names.append(fd[p:p + nl].split(b'\x00')[0].decode('latin-1')); p += nl
    return names


def nm(sh, ref):
    if ref >= 0: return None
    o = -ref; e = sh.find(b'\x00', o); return sh[o:e].decode('latin-1', 'ignore')


def wld_actordefs(wld):
    shs = struct.unpack_from('<I', wld, 0x14)[0]
    sh = bytearray(wld[0x1C:0x1C + shs])
    for i in range(len(sh)): sh[i] ^= XOR[i % 8]
    fc2 = struct.unpack_from('<I', wld, 8)[0]
    pos = 0x1C + shs; names = set()
    for i in range(fc2):
        if pos + 8 > len(wld): break
        sz, ft = struct.unpack_from('<II', wld, pos)
        nr = struct.unpack_from('<i', wld, pos + 8)[0]
        if ft == 0x14:
            n = nm(sh, nr)
            if n: names.add(n)
        pos += 8 + sz
    return names


# ---- scan the client + rebuild the model table ---------------------------
CLIENT = sys.argv[1] if len(sys.argv) > 1 else None
if not CLIENT or not os.path.isdir(CLIENT):
    sys.exit("usage: python orn_filter.py <client_dir>   (folder holding gequip*.s3d / *.eqg)")

models = set()
for f in sorted(glob.glob(os.path.join(CLIENT, "gequip*.s3d"))):
    if "original" in f.lower():
        continue
    try:
        arc = read_pfs(f); cnt = 0
        for n, data in arc.items():
            if n.lower().endswith(".wld"):
                for a in wld_actordefs(data):
                    m = re.match(r"IT(\d+)_ACTORDEF", a)
                    if m: models.add(int(m.group(1))); cnt += 1
        print("  %s: +%d IT models" % (os.path.basename(f), cnt))
    except Exception as e:
        print("  ERR", f, e)

for f in glob.glob(os.path.join(CLIENT, "*.eqg")):
    m = re.match(r"it(\d+)\.eqg$", os.path.basename(f).lower())
    if m: models.add(int(m.group(1)))
    try:
        for n in read_pfs_names(f):
            mm = re.match(r"it(\d+)[._]", n.lower())
            if mm: models.add(int(mm.group(1)))
    except Exception:
        pass

for f in glob.glob(os.path.join(CLIENT, "it*.*")):
    mm = re.match(r"it(\d+)[._]", os.path.basename(f).lower())
    if mm: models.add(int(mm.group(1)))

models.discard(63)   # IT63 = the client's broken-placeholder fallback, not a real model
print("distinct client IT models (excl IT63):", len(models))
if not models:
    sys.exit("found 0 models -- is the client_dir right? (it should contain gequip*.s3d)")

cfg = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "config.json"), encoding="utf-8"))
db = cfg.get("db", {})
conn = pymysql.connect(host=db.get("host", "127.0.0.1"), port=int(db.get("port", 3306)),
                       user=db.get("user", "eqemu"), password=db.get("password", "eqemu"),
                       database=db.get("database", "peq"), charset="utf8mb4", autocommit=True)
cur = conn.cursor()
cur.execute("DROP TABLE IF EXISTS boz_client_weapon_models")
cur.execute("CREATE TABLE boz_client_weapon_models (model INT PRIMARY KEY)")
cur.executemany("INSERT INTO boz_client_weapon_models (model) VALUES (%s)", [(m,) for m in sorted(models)])
print("rebuilt boz_client_weapon_models (%d models). Now run:  python build_pack.py" % len(models))
