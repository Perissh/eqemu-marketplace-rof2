-- ============================================================================
--  EQ Marketplace -- SAMPLE catalog (demo content; safe to delete)
-- ============================================================================
--  A tiny, self-contained demo so the Marketplace shows something the moment you
--  install it. It uses STOCK classic-EQ item ids that ship in a vanilla PEQ
--  `items` table, so it renders on any server -- no custom items required.
--
--  It adds ONE category, "Armor Preview Test", with three tabs that show off the
--  live 3D preview: Robes, Chest Pieces, Full Suits. (Price is 0 = free, so you
--  can also try the Buy button without needing any currency.)
--
--  INSTALL:
--      mysql -u <user> -p <database> < sample_catalog.sql
--      -- or paste the statements below into your DB console.
--
--  CONFIRM IT WORKS (in game):
--      Open the Marketplace -> you should see an "Armor Preview Test" category.
--      Expand it -> Robes / Chest Pieces / Full Suits -> click an item and watch
--      your avatar wear it in the Preview pane (robes render over your worn gear).
--
--  >>> REMOVE THE DEMO once you've confirmed it works: <<<
--      DELETE FROM boz_marketplace_catalog WHERE parent = 'Armor Preview Test';
--
--  The tree is fully DATA-DRIVEN: to build your real shop, just INSERT rows with
--  your own `parent` / `subcat` / `item_id` -- any parent name becomes a top-level
--  category, with zero client/ASI changes. This file is the template. See docs/.
-- ============================================================================

DELETE FROM boz_marketplace_catalog WHERE parent = 'Armor Preview Test';

-- Robes (material 10-16): the preview renders the robe OVER your worn gear and spreads it to the sleeves.
INSERT INTO boz_marketplace_catalog (parent, subcat, sort_order, item_id, name, icon, price, currency, level, descr)
SELECT 'Armor Preview Test','Robes',0,id,Name,icon,0,'Platinum',0,'Sample robe -- preview renders it over worn gear' FROM items WHERE id=1356;
INSERT INTO boz_marketplace_catalog (parent, subcat, sort_order, item_id, name, icon, price, currency, level, descr)
SELECT 'Armor Preview Test','Robes',1,id,Name,icon,0,'Platinum',0,'Sample robe (red) -- preview keeps its color tint' FROM items WHERE id=1601;
INSERT INTO boz_marketplace_catalog (parent, subcat, sort_order, item_id, name, icon, price, currency, level, descr)
SELECT 'Armor Preview Test','Robes',2,id,Name,icon,0,'Platinum',0,'Sample robe -- preview over worn gear' FROM items WHERE id=11641;

-- Chest Pieces (single chest slot): the chest renders, worn arms/wrists stay.
INSERT INTO boz_marketplace_catalog (parent, subcat, sort_order, item_id, name, icon, price, currency, level, descr)
SELECT 'Armor Preview Test','Chest Pieces',0,id,Name,icon,0,'Platinum',0,'Sample leather chest -- preview over worn gear' FROM items WHERE id=1064;
INSERT INTO boz_marketplace_catalog (parent, subcat, sort_order, item_id, name, icon, price, currency, level, descr)
SELECT 'Armor Preview Test','Chest Pieces',1,id,Name,icon,0,'Platinum',0,'Sample chain chest -- preview over worn gear' FROM items WHERE id=3004;

-- Full Suit (item covering all 7 armor slots): whole-body armor preview.
INSERT INTO boz_marketplace_catalog (parent, subcat, sort_order, item_id, name, icon, price, currency, level, descr)
SELECT 'Armor Preview Test','Full Suits',0,id,Name,icon,0,'Platinum',0,'Sample full suit -- whole-body armor preview' FROM items WHERE id=8294;
