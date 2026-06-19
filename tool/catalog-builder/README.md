# Catalog Builder

A local web tool to build your Marketplace shop **visually** — search the items
table, make categories and tabs, add items with a price + currency each, preview the
tree, and write it straight to the catalog (or export it as portable SQL). No hand-
written SQL.

## Requirements
- **Python 3.8+** (most EQEmu hosts already have it)
- **PyMySQL** — `run.bat` / `run.sh` auto-installs it; or `pip install pymysql`

## Run
- **Windows:** double-click **`run.bat`**
- **Mac/Linux:** `./run.sh`
- or directly: `python catalog_builder.py`

It connects to your server database and opens **http://127.0.0.1:8090** in your
browser.

## Use it on your own server
This is **not** local-only — it works against any EQEmu database. Two ways to run it,
both using the credentials from your server's `eqemu_config.json` (the `database` block):

**A) On the server box (recommended, simplest).** Copy this folder to the machine your
server runs on and launch it there. The defaults (`127.0.0.1:3306`, user `eqemu`, db
`peq`) match a standard EQEmu install, so most operators just double-click `run.bat`
(Windows) / `./run.sh` (Linux) and it connects. Then browse to it from that same machine,
or from your PC at `http://<server-ip>:8090` if you start it with `--bind 0.0.0.0`.

**B) From your own PC, pointing at the server's DB.** Pass the connection flags:
```
python catalog_builder.py --host db.example.com --user eqemu --password secret --database peq
```
> Heads-up: most EQEmu MySQL installs only accept connections from `localhost`, and the
> DB user is often granted for `localhost` only. If a remote connection is refused, either
> run it on the server box (option A) or grant remote access on the DB
> (`CREATE USER 'eqemu'@'%' …; GRANT … ;` and open MySQL's port). Option A avoids all of this.

## Database connection
Defaults: `127.0.0.1:3306`, user `eqemu`, db `peq`. Override with flags or env vars:

```
python catalog_builder.py --host 127.0.0.1 --user eqemu --password secret --database peq
# env: MKT_DB_HOST  MKT_DB_PORT  MKT_DB_USER  MKT_DB_PASS  MKT_DB_NAME
# web: --bind 127.0.0.1  --webport 8090  --no-browser
```

## Using it
- **Add an item** (right panel): pick or type a **Category** and **Tab**, search the
  items table, select an item, set a **price + currency**, and click *Add to catalog*.
  Typing a **brand-new Category name creates a new top-level category in game** — no
  client changes, it just appears.
- **Currency dropdown** is built from *your* server: `Platinum` plus every currency in
  your `alternate_currency` table (Doubloon, Ebon Crystal, your own custom tokens, …) and
  any currency name your catalog already uses. To offer a brand-new currency, create its
  `alternate_currency` row, then reload — it appears in the list.
- **Duplicate guard**: if you add an item that's already listed in another category, the
  tool tells you where and warns. Buying resolves to the *lowest* price across all listings,
  and listing one item under two different currencies is ambiguous — so the warning is
  loudest when the currencies differ.
- **Tree**: categories and tabs start collapsed so you see everything at a glance; click a
  row (or its ▸) to expand it. *Expand all* / *Collapse all* are in the header. Your
  expand/collapse state is kept as you add and edit — adding an item just opens the one
  branch it landed in.
- **Edit / remove**: every item has *edit* (price / currency / level / tooltip) and
  delete; tabs and categories can be renamed or deleted.
- **Export SQL**: produces a portable `catalog.sql` you can version-control or hand to
  another server. (Your edits also apply live to the DB as you make them.)
- The **3D preview is automatic** from each item's own data; the item picker shows
  what each item will preview as (weapon / chest armor / full suit / …).

## Safety
- It only reads the `items` table and reads/writes `boz_marketplace_catalog`.
- Binds to `127.0.0.1` (local only) by default. Run it on the machine with DB access,
  or point it at a remote DB with `--host`.
