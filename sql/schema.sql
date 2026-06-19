-- ============================================================================
--  EQ Marketplace -- catalog table
-- ============================================================================
--  The ONE table the Marketplace reads. Every row is one item shown in the shop,
--  under `parent` (top-level category) -> `subcat` (tab). The category tree is
--  built entirely from the distinct parents/subcats here -- add a row with a new
--  `parent` and it becomes a new top-level category, no client changes.
--
--  INSTALL:  mysql -u <user> -p <database> < schema.sql
--  THEN load some content:  mysql ... < sample_catalog.sql   (the demo)
--  ...or write your own rows (sample_catalog.sql is the template).
-- ============================================================================

CREATE TABLE IF NOT EXISTS `boz_marketplace_catalog` (
  `parent`     varchar(48)  NOT NULL,                 -- top-level category (any name)
  `subcat`     varchar(48)  NOT NULL,                 -- tab under the category (any name)
  `sort_order` int          NOT NULL DEFAULT 0,       -- order of items within a subcat
  `item_id`    int          NOT NULL,                 -- items.id of the thing being sold
  `name`       varchar(96)  NOT NULL,                 -- tile label (usually the item name)
  `icon`       int          NOT NULL DEFAULT 0,       -- tile icon gfx id (usually items.icon)
  `price`      int          NOT NULL DEFAULT 0,       -- cost in `currency`; 0 = free
  `currency`   varchar(16)  NOT NULL DEFAULT 'Platinum',  -- 'Platinum' (carried coin) by default
  `level`      int          NOT NULL DEFAULT 0,       -- if > 0, feeds the "My Level and Below" tab
  `descr`      varchar(255) NOT NULL DEFAULT '',      -- tooltip / detail-pane text
  KEY `idx_cat` (`parent`,`subcat`,`sort_order`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
