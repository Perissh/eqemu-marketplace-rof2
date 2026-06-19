#!/usr/bin/env python3
"""
EQ Marketplace -- Catalog Builder (local web tool)

Build your in-game shop visually instead of writing SQL: connect to your server's
database, search the items table, make categories/subcategories, drop items in with
a price + currency each, preview the tree, and write it straight to the catalog (or
export it as SQL).

Run:
    python catalog_builder.py
    # then open http://127.0.0.1:8090  (it tries to open your browser automatically)

DB connection (defaults shown; override with flags or env vars):
    --host 127.0.0.1   (MKT_DB_HOST)
    --port 3306        (MKT_DB_PORT)
    --user eqemu       (MKT_DB_USER)
    --password eqemu   (MKT_DB_PASS)
    --database peq     (MKT_DB_NAME)
    --bind 127.0.0.1   --webport 8090   --no-browser

Requires: Python 3.8+ and PyMySQL  (pip install pymysql)
"""
import argparse, json, os, re, subprocess, sys, threading, webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

try:
    import pymysql
except ImportError:
    print("PyMySQL not found -- installing it (pip install pymysql) ...")
    try:
        subprocess.check_call([sys.executable, "-m", "pip", "install", "--quiet", "pymysql"])
        import pymysql
    except Exception:
        sys.exit("PyMySQL is required and the automatic install failed.  Run:  pip install pymysql")

HERE = os.path.dirname(os.path.abspath(__file__))
CATALOG_TABLE = "boz_marketplace_catalog"
CONNECT_CFG = os.path.join(HERE, "connect.json")   # saved SSH target for --connect

CFG = {}  # filled by main()


# ---------------------------------------------------------------------------
# DB layer
# ---------------------------------------------------------------------------
def db():
    return pymysql.connect(
        host=CFG["host"], port=CFG["port"], user=CFG["user"],
        password=CFG["password"], database=CFG["database"],
        charset="utf8mb4", autocommit=True, cursorclass=pymysql.cursors.DictCursor)


def q(sql, args=None):
    con = db()
    try:
        with con.cursor() as cur:
            cur.execute(sql, args or ())
            return cur.fetchall()
    finally:
        con.close()


def x(sql, args=None):
    con = db()
    try:
        with con.cursor() as cur:
            cur.execute(sql, args or ())
            return cur.rowcount
    finally:
        con.close()


def ensure_table():
    q("""CREATE TABLE IF NOT EXISTS `%s` (
        `parent` varchar(48) NOT NULL, `subcat` varchar(48) NOT NULL,
        `sort_order` int NOT NULL DEFAULT 0, `item_id` int NOT NULL,
        `name` varchar(96) NOT NULL, `icon` int NOT NULL DEFAULT 0,
        `price` int NOT NULL DEFAULT 0, `currency` varchar(16) NOT NULL DEFAULT 'Platinum',
        `level` int NOT NULL DEFAULT 0, `descr` varchar(255) NOT NULL DEFAULT '',
        KEY `idx_cat` (`parent`,`subcat`,`sort_order`)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4""" % CATALOG_TABLE)


# Mirror the server feed: derive the 3D-preview slot from an item's own fields so the
# UI can tell the operator whether (and how) a row will preview on the avatar.
def preview_of(it):
    slots = int(it.get("slots") or 0)
    idfile = (it.get("idfile") or "")
    itemtype = int(it.get("itemtype") or 0)
    augtype = int(it.get("augtype") or 0)
    weap = len(idfile) >= 2 and idfile[0] in "Ii" and idfile[1] in "Tt"
    if ((itemtype == 54 and (augtype & 524288)) or itemtype == 11) and weap:
        return "weapon ornament"
    if slots & 8192:
        return "weapon (primary)"
    if slots & 16384:
        return "weapon (secondary)"
    if (slots & 923268) == 923268:
        return "full armor suit"
    for bit, label in ((4, "head"), (131072, "chest"), (128, "arms"),
                       (512 | 1024, "wrists"), (4096, "hands"), (262144, "legs"), (524288, "feet")):
        if slots & bit:
            return label + " armor"
    return None  # not visibly worn -> bare avatar


# ---------------------------------------------------------------------------
# API
# ---------------------------------------------------------------------------
# Build the currency dropdown from what THIS server actually supports, so operators
# pick a real currency instead of guessing a name. Order: Platinum (carried coin, works
# anywhere) -> any currency already used in the catalog (keeps existing/custom names you
# rely on) -> every alternate_currency by its item name (Doubloon, Ebon Crystal, your own
# custom tokens, ...). The buy flow (server/gm_commands/mktbuy.cpp) resolves the catalog
# `currency` string by exactly this set of rules, so anything offered here is purchasable.
def list_currencies():
    out, seen = ["Platinum"], {"platinum"}
    def add(nm):
        nm = (nm or "").strip()
        if nm and nm.lower() not in seen:
            out.append(nm); seen.add(nm.lower())
    try:
        for r in q("SELECT DISTINCT currency FROM `%s`" % CATALOG_TABLE):
            add(r["currency"])
    except Exception:
        pass
    try:
        for r in q("SELECT i.Name AS name FROM alternate_currency ac "
                   "JOIN items i ON i.id = ac.item_id "
                   "WHERE i.Name IS NOT NULL AND i.Name <> '' ORDER BY i.Name"):
            add(r["name"])
    except Exception:
        pass  # no alternate_currency table / not reachable -> Platinum (+ catalog names) only
    return out


def api_state():
    rows = q("SELECT parent, subcat, sort_order, item_id, name, icon, price, currency, level, descr "
             "FROM `%s` ORDER BY parent, subcat, sort_order, name" % CATALOG_TABLE)
    return {"rows": rows, "currencies": list_currencies(), "db": CFG["database"]}


def api_items(query, limit=60):
    query = (query or "").strip()
    if not query:
        return {"items": []}
    args, where = [], ""
    if query.isdigit():
        where = "i.id = %s OR i.Name LIKE %s"
        args = [int(query), "%" + query + "%"]
    else:
        where = "i.Name LIKE %s"
        args = ["%" + query + "%"]
    args.append(int(limit))
    rows = q("SELECT i.id, i.Name AS name, i.icon, i.slots, i.material, i.idfile, "
             "i.itemtype, i.augtype FROM items i WHERE " + where +
             " ORDER BY (i.Name = %s) DESC, i.id LIMIT %s", args[:-1] + [query, args[-1]])
    for r in rows:
        r["preview"] = preview_of(r)
        for k in ("slots", "material", "augtype"):
            r.pop(k, None)
    return {"items": rows}


def api_add(d):
    ensure_table()
    # next sort_order within the subcat
    cur = q("SELECT COALESCE(MAX(sort_order), -1) + 1 AS so FROM `%s` WHERE parent=%%s AND subcat=%%s"
            % CATALOG_TABLE, (d["parent"], d["subcat"]))
    so = cur[0]["so"] if cur else 0
    x("INSERT INTO `%s` (parent, subcat, sort_order, item_id, name, icon, price, currency, level, descr) "
      "VALUES (%%s,%%s,%%s,%%s,%%s,%%s,%%s,%%s,%%s,%%s)" % CATALOG_TABLE,
      (d["parent"], d["subcat"], so, int(d["item_id"]), d["name"], int(d.get("icon") or 0),
       int(d.get("price") or 0), d.get("currency") or "Platinum", int(d.get("level") or 0),
       (d.get("descr") or "")[:255]))
    return {"ok": True}


def api_update(d):
    x("UPDATE `%s` SET price=%%s, currency=%%s, name=%%s, level=%%s, descr=%%s "
      "WHERE parent=%%s AND subcat=%%s AND item_id=%%s" % CATALOG_TABLE,
      (int(d.get("price") or 0), d.get("currency") or "Platinum", d["name"],
       int(d.get("level") or 0), (d.get("descr") or "")[:255],
       d["parent"], d["subcat"], int(d["item_id"])))
    return {"ok": True}


def api_delete(d):
    if "item_id" in d and d["item_id"] not in (None, ""):
        x("DELETE FROM `%s` WHERE parent=%%s AND subcat=%%s AND item_id=%%s" % CATALOG_TABLE,
          (d["parent"], d["subcat"], int(d["item_id"])))
    elif "subcat" in d and d["subcat"]:
        x("DELETE FROM `%s` WHERE parent=%%s AND subcat=%%s" % CATALOG_TABLE, (d["parent"], d["subcat"]))
    else:
        x("DELETE FROM `%s` WHERE parent=%%s" % CATALOG_TABLE, (d["parent"],))
    return {"ok": True}


def api_rename(d):
    if d.get("old_subcat") is not None:
        x("UPDATE `%s` SET subcat=%%s WHERE parent=%%s AND subcat=%%s" % CATALOG_TABLE,
          (d["new"], d["parent"], d["old_subcat"]))
    else:
        x("UPDATE `%s` SET parent=%%s WHERE parent=%%s" % CATALOG_TABLE, (d["new"], d["parent"]))
    return {"ok": True}


def sql_escape(s):
    return str(s).replace("\\", "\\\\").replace("'", "''")


def api_export():
    rows = q("SELECT parent, subcat, sort_order, item_id, name, icon, price, currency, level, descr "
             "FROM `%s` ORDER BY parent, subcat, sort_order, name" % CATALOG_TABLE)
    out = ["-- Marketplace catalog export (generated by Catalog Builder)",
           "-- Apply with:  mysql -u <user> -p <database> < catalog.sql", ""]
    parents = sorted({r["parent"] for r in rows})
    for p in parents:
        out.append("DELETE FROM boz_marketplace_catalog WHERE parent = '%s';" % sql_escape(p))
    out.append("")
    for r in rows:
        out.append(
            "INSERT INTO boz_marketplace_catalog (parent, subcat, sort_order, item_id, name, icon, price, currency, level, descr) "
            "VALUES ('%s','%s',%d,%d,'%s',%d,%d,'%s',%d,'%s');" % (
                sql_escape(r["parent"]), sql_escape(r["subcat"]), r["sort_order"], r["item_id"],
                sql_escape(r["name"]), r["icon"], r["price"], sql_escape(r["currency"]),
                r["level"], sql_escape(r["descr"])))
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------
# HTTP
# ---------------------------------------------------------------------------
class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _send(self, code, body, ctype="application/json"):
        if isinstance(body, (dict, list)):
            body = json.dumps(body)
        data = body.encode("utf-8") if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _file(self, name, ctype):
        path = os.path.join(HERE, name)
        if not os.path.exists(path):
            return self._send(404, {"error": name + " not found"})
        with open(path, "rb") as f:
            self._send(200, f.read(), ctype)

    def do_GET(self):
        u = urlparse(self.path)
        try:
            if u.path in ("/", "/index.html"):
                return self._file("index.html", "text/html; charset=utf-8")
            if u.path == "/api/state":
                return self._send(200, api_state())
            if u.path == "/api/items":
                qs = parse_qs(u.query)
                return self._send(200, api_items(qs.get("q", [""])[0], int(qs.get("limit", ["60"])[0])))
            if u.path == "/api/export":
                return self._send(200, api_export(), "text/plain; charset=utf-8")
            return self._send(404, {"error": "not found"})
        except Exception as e:
            return self._send(500, {"error": str(e)})

    def do_POST(self):
        try:
            n = int(self.headers.get("Content-Length", 0))
            d = json.loads(self.rfile.read(n) or b"{}")
            route = {"/api/add": api_add, "/api/update": api_update,
                     "/api/delete": api_delete, "/api/rename": api_rename}.get(urlparse(self.path).path)
            if not route:
                return self._send(404, {"error": "not found"})
            return self._send(200, route(d))
        except Exception as e:
            return self._send(500, {"error": str(e)})


# ---------------------------------------------------------------------------
# Remote mode: run the tool ON your server (where the DB is) and use it from your
# PC. We open an SSH tunnel to the server, start the tool there, and point your
# browser at the tunnel -- so the database is only ever reached locally on the
# server and nothing is exposed to the network. The SSH target + remote path are
# saved (connect.json) so next time it's a one-liner: `--connect`.
# ---------------------------------------------------------------------------
def run_remote(a):
    cfg = {}
    if os.path.exists(CONNECT_CFG):
        try:
            with open(CONNECT_CFG) as f:
                cfg = json.load(f)
        except Exception:
            cfg = {}
    ssh_target = a.ssh or cfg.get("ssh") or ""
    remote_dir = a.remote_dir or cfg.get("remote_dir") or ""
    remote_py = a.remote_python or cfg.get("remote_python") or ""
    if not ssh_target:
        ssh_target = input("  Your server SSH login (user@host): ").strip()
    if not remote_dir:
        remote_dir = input("  Path to this tool's folder ON the server: ").strip()
    if not ssh_target or not remote_dir:
        sys.exit("Need both an SSH login (user@host) and the remote tool path.")

    # A drive-letter or backslash path means a Windows server -> use 'python'; otherwise a
    # Linux/Mac server -> 'python3'. Normalize to forward slashes (Python accepts them on
    # Windows too) so the launch command below is identical no matter the server's shell.
    is_windows = bool(re.match(r"^[A-Za-z]:", remote_dir)) or "\\" in remote_dir
    if not remote_py:
        remote_py = "python" if is_windows else "python3"
    script = remote_dir.replace("\\", "/").rstrip("/") + "/catalog_builder.py"
    try:
        with open(CONNECT_CFG, "w") as f:
            json.dump({"ssh": ssh_target, "remote_dir": remote_dir, "remote_python": remote_py}, f)
    except Exception:
        pass

    port = a.webport
    url = "http://localhost:%d" % port
    # ONE command, no shell operators (no &&, ;, cd) -> runs the same in PowerShell, cmd,
    # and sh. The tool finds its own folder, so no 'cd' is needed; it binds to the server's
    # localhost, which the -L tunnel reaches. (PyMySQL auto-installs on first run if absent.)
    remote = '%s "%s" --no-browser --webport %d' % (remote_py, script, port)
    # Allocate a PTY only when we actually have a terminal: gives clean Ctrl+C when run
    # interactively, and still works when launched from a script or a double-click (no tty).
    tflag = ["-t"] if (sys.stdin and sys.stdin.isatty()) else []
    # Forward to 127.0.0.1 (not "localhost"): the tool binds IPv4 127.0.0.1, but "localhost"
    # can resolve to IPv6 ::1 on the server, which would refuse the forwarded connection.
    # Keepalives hold the tunnel open through idle periods and detect a dead link promptly.
    keepalive = ["-o", "ServerAliveInterval=30", "-o", "ServerAliveCountMax=3"]
    cmd = ["ssh"] + tflag + keepalive + ["-L", "%d:127.0.0.1:%d" % (port, port), ssh_target, remote]
    print("Connecting to %s over SSH ...  (Ctrl+C to stop)" % ssh_target)
    print("  Browser: %s   (if it can't connect yet, give it a few seconds and refresh)" % url)
    if not a.no_browser:
        threading.Timer(5.0, lambda: webbrowser.open(url)).start()
    try:
        subprocess.call(cmd)
    except KeyboardInterrupt:
        pass
    except FileNotFoundError:
        sys.exit("Couldn't find 'ssh'. Install OpenSSH (Windows 10/11 includes it) and retry.")
    print("Tunnel closed.")


def main():
    ap = argparse.ArgumentParser(description="EQ Marketplace Catalog Builder")
    ap.add_argument("--host", default=os.environ.get("MKT_DB_HOST", "127.0.0.1"))
    ap.add_argument("--port", type=int, default=int(os.environ.get("MKT_DB_PORT", "3306")))
    ap.add_argument("--user", default=os.environ.get("MKT_DB_USER", "eqemu"))
    ap.add_argument("--password", default=os.environ.get("MKT_DB_PASS", "eqemu"))
    ap.add_argument("--database", default=os.environ.get("MKT_DB_NAME", "peq"))
    ap.add_argument("--bind", default="127.0.0.1")
    ap.add_argument("--webport", type=int, default=8090)
    ap.add_argument("--no-browser", action="store_true")
    ap.add_argument("--ssh", metavar="USER@HOST",
                    help="Use against a REMOTE server over an SSH tunnel (run this on your PC; saved for next time).")
    ap.add_argument("--remote-dir", metavar="PATH",
                    help="Path to this tool's folder on the server (used with --ssh).")
    ap.add_argument("--remote-python", metavar="CMD",
                    help="Python command to run on the server (default: auto -- 'python' on Windows, 'python3' on Linux/Mac).")
    ap.add_argument("--connect", action="store_true",
                    help="Reconnect to the server saved from a previous --ssh.")
    a = ap.parse_args()

    # Remote mode: tunnel to the server and run the tool there (see run_remote).
    if a.ssh or a.connect:
        return run_remote(a)

    CFG.update(host=a.host, port=a.port, user=a.user, password=a.password, database=a.database)

    try:
        q("SELECT 1")
    except Exception as e:
        # No database reachable here. If we're interactive, offer to connect to a
        # remote server over SSH instead of just failing -- the common "my server is
        # on another box" case becomes plug-and-play.
        if sys.stdin and sys.stdin.isatty():
            print("Could not reach a database at %s:%d  (%s)" % (a.host, a.port, e))
            if input("Is your EQEmu server on another machine -- connect over SSH? [y/N]: ").strip().lower().startswith("y"):
                return run_remote(a)
        sys.exit("Could not connect to the database (%s@%s:%d/%s): %s\n"
                 "Match your server with --user/--password/--database/--host, or use --ssh user@host for a remote server."
                 % (a.user, a.host, a.port, a.database, e))
    ensure_table()

    url = "http://127.0.0.1:%d" % a.webport
    print("EQ Marketplace Catalog Builder")
    print("  DB:  %s@%s:%d/%s" % (a.user, a.host, a.port, a.database))
    print("  Web: %s   (Ctrl+C to stop)" % url)
    if a.bind in ("127.0.0.1", "localhost"):
        print("  (Using it from another computer? Run this on that PC with  --ssh user@this-server  -- see README.)")
    if not a.no_browser:
        threading.Timer(0.6, lambda: webbrowser.open(url)).start()
    ThreadingHTTPServer((a.bind, a.webport), H).serve_forever()


if __name__ == "__main__":
    main()
