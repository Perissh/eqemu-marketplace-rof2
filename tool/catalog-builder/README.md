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

## Running it on your server
Run the tool **on the machine that hosts your database** — your EQEmu server box, not a
separate PC. It connects to MySQL over `localhost`, so no DB credentials cross the network
and nothing gets exposed. It uses the credentials from your server's `eqemu_config.json`
(`database` block); the defaults (`127.0.0.1:3306`, user `eqemu`, db `peq`) match a standard
install, so you can often just double-click `run.bat` (Windows) / `./run.sh` (Linux), or
pass `--user/--password/--database` to match yours.

**Seeing the page from your PC** depends on what kind of server you have:
- **A machine you use directly** — a home PC, or a Windows VPS over Remote Desktop: open
  the browser right there on that machine (the tool launches it for you). Nothing else to do.
- **A headless server** — an SSH-only Linux VPS: forward the web port over SSH and view it
  locally. This is the secure option:
  ```
  # on the server:  python catalog_builder.py --no-browser
  ssh -L 8090:localhost:8090 user@your-server     # then open http://localhost:8090 on your PC
  ```

> ⚠ **The tool has no login of its own.** Don't expose its web port to an untrusted network.
> Binding it open with `--bind 0.0.0.0` lets *anyone* who can reach port 8090 edit your
> catalog — fine on a trusted home/LAN network, not on a public server. For a public box use
> the SSH tunnel above: it keeps the tool bound to `localhost`, reachable only through your
> authenticated SSH session.

**Don't want to run it remotely at all?** Build your catalog against any EQEmu DB you can
reach (your server box, or a local PEQ copy), hit **Export SQL**, and apply the resulting
`catalog.sql` to your live server however you normally run SQL — or skip the tool entirely
and write plain `INSERT` rows. The catalog is just one table.

*(Advanced: you can instead point the tool at a remote DB from your PC with `--host`, but
most EQEmu MySQL installs only accept `localhost` connections and grant the DB user for
`localhost` only — so running it on the server, as above, is almost always simpler.)*

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
- **No authentication** — anyone who can reach its web port can edit your catalog. It binds
  to `127.0.0.1` (local only) by default; keep it that way on a public server and reach it
  over an SSH tunnel rather than `--bind 0.0.0.0`.
- Run it on the machine with local DB access (your server box); see *Running it on your
  server* above.
