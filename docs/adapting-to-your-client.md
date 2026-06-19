# Adapting the Marketplace to Your Client

The BoZ Marketplace client UI is a native ASI mod (BOZ.asi) that pokes bytes and reads structures at specific numeric addresses inside one exact compiled RoF2 client build. Because every one of those addresses is a product of how that single build was laid out at compile time, the prebuilt binary is only valid for that binary. If your client's binaries match the reference hash **and** the BoZ UI skin + server feed are present, drop in the prebuilt ASI and you are done. If the binaries do not match, this guide shows you how to re-derive every offset from scratch and rebuild the mod for your client.

---

## The RoF2 client and why offsets are bound to it

The standard EQEmu RoF2 client is the EverQuest game executable from the Rain of Fear era — the second and final RoF client revision. It is the last and most widely supported modern client on EQEmu private servers: the most complete UI, the largest inventory and slot model, the most expansion content, and the broadest tooling support, which is why nearly every actively developed EQEmu server standardizes on it. **For Blood of Zek (BoZ), RoF2 is the ONLY supported client.**

Two binaries matter:

- **`eqgame.exe`** — the game itself.
- **`EQGraphicsDX9.dll`** — the graphics / rendering DLL (the 3D preview engine lives here).

Every hardcoded address in BOZ.asi was located by reverse-engineering one specific compiled build of these two binaries — for example the cross-class class-gate patch at image offset `0x0044A1B0`, the combat-dispatcher JG flip at `0x0045B345`, and the skill-use JZ flip at `0x005A55F2`. The ASI is built against, and only against, that one exact `eqgame.exe` + `EQGraphicsDX9.dll` build.

### Why the offsets are bound to one build

A native patch does not call functions by name. It pokes bytes at — and reads/writes data at — specific numeric addresses inside the loaded binary. Those addresses are a direct product of how one particular compiler/linker laid out that one build: the exact byte offset of every function, every branch instruction the patch flips, and every static data structure was fixed at compile time. A different compile of the same client (a different patch level, a different community build, or a recompile with different flags) shifts code around, and every offset moves or stops pointing at the instruction we expect.

The second reason is **ASLR / relocation**. RoF2's `eqgame.exe` is linked with `DYNAMICBASE`, so Windows loads it at a randomized base each run rather than the preferred image base of `0x00400000`. Offsets are therefore recorded as image-base-relative RVAs, and at runtime the ASI rebases them:

```
liveAddress = GetModuleHandleA(NULL) + (offset - 0x00400000)
```

This rebasing is only correct if the RVAs themselves are correct for the loaded binary — that is, if the binary is byte-for-byte the build we reverse-engineered. **ASLR changes WHERE the image sits in memory; it does NOT change the internal layout.** So the same prebuilt ASI works across machines as long as the on-disk binary is identical. The moment the binary differs, the RVAs are wrong, rebasing produces a valid-looking but incorrect pointer, and the patch lands in unrelated code or data — exactly the kind of fault that crashes the client or corrupts the UI.

### Verify your client matches the reference (go / no-go)

This is a hard checklist. Do not skip it, and do not substitute a download URL or a version banner for it — the only trustworthy test for the binaries is hash equality. **But note up front: a binary hash match is NECESSARY, not SUFFICIENT.** The ASI also hard-depends on two non-binary prerequisites covered below.

1. **Locate the two binaries** in your client install directory (for BoZ, the PatchTest / Live client folder): `eqgame.exe` and `EQGraphicsDX9.dll`.
   `eqgame.exe` is intentionally **NOT** distributed by the BoZ patcher (the manifest mirrors ~5100+ client files but excludes `eqgame.exe`), so every player keeps their own copy and verifying it is the admin's or player's responsibility.

2. **Compute the SHA-256 of each binary.**

   On Windows PowerShell:
   ```powershell
   Get-FileHash -Algorithm SHA256 eqgame.exe
   Get-FileHash -Algorithm SHA256 EQGraphicsDX9.dll
   ```

   On a POSIX shell:
   ```sh
   sha256sum eqgame.exe EQGraphicsDX9.dll
   ```

3. **Compare each computed hash, character for character, against the published reference SHA-256** for the supported RoF2 build (the hash the ASI was built and tested against). The comparison must be EXACT — a single differing hex digit means a different binary.

   **Reference SHA-256 (the supported RoF2 build):**

   | File | SHA-256 |
   | --- | --- |
   | `eqgame.exe` | `2a8702ad9f722704f01355c0750be7d6f164a8b9c9128ba0cf286ea32b405b0e` |
   | `EQGraphicsDX9.dll` | `ade556b68e98f6f39c5578f70431e11c0b9babcc15b8571eb22cf1e2b654417e` |

   These two values were observed identical across multiple independent copies of the RoF2 client — the practical evidence that the RoF2 binary population is effectively one canonical build, and why a single prebuilt ASI works for the overwhelming majority of servers.

4. **Decide:**
   - Both hashes match **AND** the two non-binary prerequisites below are satisfied → the prebuilt BOZ.asi can be trusted; its offsets and ASLR rebasing will resolve to the correct instructions. **Use the prebuilt binary and stop.**
   - Either hash differs → do NOT run the prebuilt ASI against that client. The offsets are not valid for it; treat it as a **port**, not a drop-in.

#### The hash match is necessary, NOT sufficient — two more prerequisites

A byte-identical `eqgame.exe` + `EQGraphicsDX9.dll` only guarantees the offsets resolve. The shipped marketplace will still be empty or broken unless BOTH of these are also true:

- **(a) The native marketplace skin is present and intact.** The mod reuses the client's **stock** `EQUI_MarketplaceWnd.xml` (e.g. `uifiles/default/EQUI_MarketplaceWnd.xml`) — the one that ships with every RoF2 client. It is *not* a separate BoZ file you install; the standard skin already defines the `MKPW_*` child controls (`MKPW_Item_Label`, `MKPW_Item_Icon`, `MKPW_ItemCost_Label`, `MKPW_BuyBtn`, …) and the item-tile template that every Kind-1 anchor and `FindChild('MKPW_*')` call resolves through. The only failure mode here is a server running a heavily customized UI that **stripped or altered** the marketplace window — that client would load the ASI cleanly and then never fill a single tile. (BoZ ships no custom marketplace skin; if you build your own, preserve the `MKPW_*` names and the tile template id.)
- **(b) The server emits the BoZ feed.** The server must implement the `#mkt` / `#mktbuy` / `#mktview` commands and emit the `[BOZ_MKT]` catalog rows (`[BOZ_MKT_START]` … `[BOZ_MKT_END]`). A hash-identical client pointed at a vanilla EQEmu server with no BoZ feed produces an empty or garbage shop, not a working one.

So the full go condition is: **both binaries hash-match AND the client's stock `EQUI_MarketplaceWnd.xml` (with its `MKPW_*` controls) is intact AND the server emits the `[BOZ_MKT]` feed + `#mkt`/`#mktbuy`/`#mktview` handlers.** Only then is the prebuilt ASI a true drop-in.

> **Why a single hash check is reliable (for the binaries):** Multiple independent copies of the RoF2 client, obtained from different sources, have repeatedly come out byte-for-byte hash-identical for `eqgame.exe` and `EQGraphicsDX9.dll`. The population of the RoF2 client is effectively one canonical binary, so a single prebuilt ASI works for the overwhelming majority of servers and players, and a simple hash-equality check is a definitive go/no-go test for the binary layout. It says nothing about the skin or the server feed — verify those separately.

> **Always hash `eqgame.exe` itself**, not just the patched/distributed assets. Because `eqgame.exe` is the player's own file and is not pushed by the patcher, a client can pass on every patched asset and still be running the wrong game executable.

### Symptoms of a mismatch

| Symptom | What it means |
| --- | --- |
| Crash on opening the Marketplace | The window construction + populate path is where several offset-bound hooks fire, so a wrong offset faults the instant the shop opens. |
| Garbage, blank, or scrambled tiles | The tile-fill and data-feed addresses resolve to the wrong code/data; tiles render empty, mis-ordered, or filled with junk. |
| **Shop opens cleanly but is empty** | Usually NOT an offset problem — a missing/altered `EQUI_MarketplaceWnd.xml` skin (no `MKPW_*` children to fill) or a server that isn't emitting the `[BOZ_MKT]` feed. Verify the skin file and the `#mkt` handlers before touching any offset. |
| 3D preview / avatar-preview crash | The clone-and-repaint path depends on offset-bound rendering hooks and on `EQGraphicsDX9.dll` specifically; a mismatched binary crashes when a preview opens. |
| Any reproducible fault at a consistent step | Open shop / hover a tile / open a preview, on a client whose hashes do not match — treat as a binary mismatch **first**, before debugging the feature. |

### RoF2-ONLY — read this loudly

**Everything here applies exclusively to the Rain of Fear (RoF2) client.** Earlier EQEmu clients — **Titanium, Underfoot, Seeds of Destruction (SoD)** — are NOT supported and CANNOT be made to work by tweaking offsets. Those clients are entirely different binaries with different code layouts, different UI architectures, and in many cases they lack the engine structures these features hook. Supporting one would be a full re-port — re-finding every function, re-deriving every patch, possibly redesigning the feature — not an offset adjustment.

As concrete scale: the historical Titanium cross-class class-gate patch needed roughly **30** hardcoded addresses and complex hooks, where the equivalent RoF2 change is only a couple of byte-patches. Do not, however, read "a couple" as the scope of the *marketplace* port. **The marketplace alone depends on 100+ distinct anchors across the five kinds below** (window globals, the category-tree fns, the tile factory, the CXStr primitives, the whole `Off::` preview family, the EQGraphicsDX9 vtable family, and dozens of struct offsets). Porting the marketplace to a new build is a multi-session reverse-engineering effort, not an offset adjustment.

---

## What you must adapt: the offset inventory

This is the complete set of offsets the Marketplace depends on. Each entry lists its **Symbol**, the **Value** as derived against the reference build, its **Kind**, **what it is**, and the **Anchor** — the stable string, structure, or behavior you use to re-find it. Numeric values WILL move on a different build; the Anchor column is what ports across builds.

### MktOff — window globals & vtable hook

> Window class = `CMarketplaceWnd`; vtable `0x009efaf8`; ctor `FUN_006ddb90`. `WndNotification` is vtable slot 34. Hook install at `InstallMktVTableHook`; hook body at `MyHook_MktWndNotification`.

| Symbol | Value | Kind | What it is | Anchor |
| --- | --- | --- | --- | --- |
| `MktOff::pWnd` | `0x00D1FD08` | global_ptr | Global holding `&CMarketplaceWnd*` (active window instance); read in TickMarketplace, null / visByte!=1 ⇒ closed. | `DAT_00D1FD08`; same as preview-window global in recon |
| `MktOff::Visible` | `0x196` | struct_offset | CXWnd visible byte on the window (==1 when open). | `*(BYTE*)(window+0x196)==1` open-gate in TickMarketplace |
| MktWnd vtable slot 34 (WndNotification) | `34` (vtbl+0x88) | vtable_slot | `CMarketplaceWnd::WndNotification`; swapped per-instance to the hook; orig at `vtbl[34]`. | `origVtable[34]`; native target ~`0x6e1f70`; `kVtableSlots=256` copied |
| MktNotify code `0x20` (tile select) | `0x20` | misc | WndNotification code for a grid tile / sub-widget being selected; drives selection + preview. | `code==0x20` in MyHook_MktWndNotification |
| MktNotify code `0x1` (button activated) | `0x1` | misc | WndNotification code for Buy / Inspect button click activation. | `code==0x1`; MKPW_BuyBtn / MKPW_DetailsInspect |
| Notify sub-widget parent walk | `+0xc` then `-0x10` | struct_offset | When the notify child is a tile sub-widget: tile container = `*(notify+0xc) - 0x10`. | `DWORD parent = *(notify+0xc) - 0x10` |

### MktOff — category tree (ArrayClass + node ctor)

| Symbol | Value | Kind | What it is | Anchor |
| --- | --- | --- | --- | --- |
| `MktOff::CreateCats` | `0x006DD260` | abs_eqgame_addr | `FUN_006dd260(wnd)`: builds the 3 stock category nodes (New & Featured / Player Studio / All Items). `__fastcall(ecx=wnd)`. | `FnMktVoidWnd_t`; called at top of MktPopulate |
| `MktOff::RenderCats` | `0x006DCF40` | abs_eqgame_addr | `FUN_006dcf40(wnd)`: renders category nodes; only renders nodes with `+0x30` (NodeCount) > 0. `__fastcall(ecx=wnd)`. | `FnMktVoidWnd_t`; called at end of MktPopulate; render gate = NodeCount |
| `MktOff::CatCount` | `0x280` | struct_offset | Category ArrayClass element count (struct start) on the window. | `window+0x280` |
| `MktOff::CatArray` | `0x284` | struct_offset | Category ArrayClass data pointer (array of node ptrs). | `window+0x284`; `arr=*(window+0x284)` |
| `MktOff::CatCapacity` | `0x288` | struct_offset | Category ArrayClass capacity; code grows it to 24 via NodeAlloc. | `window+0x288`; `<24` triggers realloc |
| `MktOff::NodeAlloc` | `0x008DBB3B` | abs_eqgame_addr | `FUN_008dbb3b(size)` → `operator new`; allocs nodes (0x34), the 24-slot cat array, and the 0x4c anim copy. `__cdecl`. | `FnMktAlloc_t`; `NodeAlloc(0x34)` |
| `MktOff::NodeCtor` | `0x006E4A70` | abs_eqgame_addr | `FUN_006e4a70(node,type,&dispCX,&intCX,parent,visible)`: category-node ctor; auto-links child into parent. `__thiscall` via `__fastcall`. | `FnMktNodeCtor_t`; type 0=folder, 1=browse view |
| `MktOff::NodeIntName` | `0x10` | struct_offset | catNode → internal-name `CXStr*` (shown name). Used by MktRenameCategory. | `node+0x10` |
| `MktOff::NodeVisible` | `0x18` | struct_offset | Category node visible byte (set 1 by ctor arg). | `node+0x18` |
| `MktOff::NodeParent` | `0x1c` | struct_offset | Category node parent pointer (0 = top-level). | `node+0x1c` |
| `MktOff::NodeChildCnt` | `0x20` | struct_offset | Category node child-array count (0 = leaf). | `node+0x20` |
| `MktOff::NodeCount` | `0x30` | struct_offset | Item-count / render gate; RenderCats only renders when >0 (=1 show, =0 hide). | `node+0x30`; render gate in FUN_006dcf40 |
| `MktOff::SelNode` | `0x25c` | struct_offset | `*(window+0x25c)` = selected category node (unreliable for custom nodes ⇒ code reads the tree control instead). | `window+0x25c` |
| MktAddBranch cat-array cap guard | `24` | misc | Hard cap: MktAddBranch refuses to append when `CatCount>=24`; MktPopulate pre-grows to 24. | `cnt>=24 return` |

### MktOff — CXStr primitives

| Symbol | Value | Kind | What it is | Anchor |
| --- | --- | --- | --- | --- |
| `MktOff::CXStrCtor` | `0x00805C20` | abs_eqgame_addr | `FUN_00805c20(&out, cstr)`: constructs a CXStr (single rep ptr) from a C string. SAME address as `Off::FnCxstrCtor`. | `FnMktCXStr_t`; used wherever a `&CXStr` arg is needed |
| `MktOff::CXStrLen` | `0x08` | struct_offset | CXStr length field (written by MktRenameCategory after editing inline text). | `cx+0x08` |
| `MktOff::CXStrText` | `0x14` | struct_offset | CXStr inline text buffer (chars); new category name written here in place. | `cx+0x14`; rep+0x14 chars |

### MktOff — item grid / tile factory

> The item-tile SIDL template id is build-specific — re-derive it per skin / build.

| Symbol | Value | Kind | What it is | Anchor |
| --- | --- | --- | --- | --- |
| `MktOff::Grid` | `0x384` | struct_offset | `*(window+0x384)` = the item grid (`CTileLayoutWnd`) tiles are made into. | `window+0x384` |
| `MktOff::UiMgr` | `0x015D3D08` | global_ptr | `*(0x015D3D08)` = `CSidlManager*`. Used for TplResolve / MakeTile / AnimFind. SAME as `Off::SidlMgrPtr`. | `DAT_015d3d08` |
| `MktOff::MakeTile` | `0x0086E5E0` | abs_eqgame_addr | `FUN_0086e5e0(uiMgr, grid, tmpl)`: instantiate one item tile into the grid; one tile per draw frame. `__fastcall(ecx=uiMgr)`. | `FnMktMakeTile_t`; phase-1 tile pump |
| `MktOff::TplResolve` | `0x00870040` | abs_eqgame_addr | `FUN_00870040(uiMgr, id)`: resolve a SIDL template by numeric id → template ptr. `__fastcall(ecx=uiMgr)`. | `FnMktTpl_t` |
| `MktOff::TileTplId` | `3211409` (`0x310091`) | sidl_template_id | SIDL template id of the item-tile template. **BUILD-SPECIFIC** — re-derive per skin/build. | passed to TplResolve |
| `MktOff::FindChild` | `0x00868330` | abs_eqgame_addr | `FUN_00868330(parent, &cxstr)`: find a named child control inside a window/tile. `__fastcall(ecx=parent)`. | `FnMktFind_t` |
| `MktOff::SetIcon` | `0x006D1C80` | abs_eqgame_addr | `FUN_006d1c80(iconCtrl, icon)`: set the icon control's frame; internally subtracts 500 (frame = icon-500 within A_DragItem). `__cdecl`. | `FnMktSetIcon_t` |
| `MktOff::IconAnim` | `0x228` | struct_offset | icon control → `CTextureAnimation*`; each tile's icon ctrl points at its OWN copy. | `iconCtrl+0x228` |
| `MktOff::AnimFind` | `0x0086E010` | abs_eqgame_addr | `FindAnimation(uiMgr, &CXStr 'A_DragItem')` → `CTextureAnimation*`. `__fastcall(ecx=uiMgr)`. | `FnMktAnimFind_t` |
| `MktOff::AnimCtor` | `0x0087A9A0` | abs_eqgame_addr | `CTextureAnimation` ctor, run on a 0x4c-byte alloc. `__fastcall(ecx=obj)`. | `FnMktAnimCtor_t`; `obj=NodeAlloc(0x4c)` |
| `MktOff::AnimCopy` | `0x0041F2D0` | abs_eqgame_addr | `CTextureAnimation` copy-init(dest, src). `__fastcall(ecx=dest)`. Models the client's own `FUN_006f4d20`. | `FnMktAnimCopy_t` |
| CTextureAnimation alloc size | `0x4c` | misc | Byte size of a CTextureAnimation object. | `NodeAlloc(0x4c)` in MktItemIconAnim |
| Category-node alloc size | `0x34` | misc | Byte size of a category tree node. | `NodeAlloc(0x34)` in MktMakeNode |
| CXWnd child-list head | `+0x10` | struct_offset | Grid/tile child-list HEAD pointer (first child); walked to enumerate tiles. `tile+0x10` = name label (first child). | `grid+0x10`; `tile+0x10` |
| CXWnd child-list next | `+0x08` | struct_offset | CXWnd sibling 'next' link in the child list. | `ch=*(ch+0x08)` loop |

### MktOff — CXWnd vtable slots & text color

| Symbol | Value | Kind | What it is | Anchor |
| --- | --- | --- | --- | --- |
| `MktOff::VtSetText` | `0x124` | vtable_slot | CXWnd vtable byte-offset for `SetWindowText(&CXStr)`. Called as `*(*(child)+0x124)`. | `vt+0x124`; MktSetChildText/MktSetWidgetText |
| `MktOff::VtSetVis` | `0xd8` | vtable_slot | CXWnd vtable byte-offset for `SetVisible(show, resize)` (= slot 54). Shows/hides tiles + validated in MktTileLooksValid. | `vt+0xd8`; `FnMktVFlags_t(wnd,show,1)` |
| `MktOff::VtLayout` | `0xa0` | vtable_slot | `CTileLayoutWnd` vtable byte-offset for `relayout(0,0)` (re-flow the grid). | `vt+0xa0`; MktSafeRelayout |
| `MktOff::NameColor` | `0x12c` | struct_offset | Tile name-label ARGB text-color field. `0xFFFFD700` (gold) for selection, `0xFFFFFFFF` (white) to clear. | `label+0x12c` |

### MktOff — category tree control (CTreeViewWnd)

> Read each frame to detect the selected leaf + its parent branch. Branch/leaf node vtables are build-specific and classify each line.

| Symbol | Value | Kind | What it is | Anchor |
| --- | --- | --- | --- | --- |
| `MktOff::TreeCtrl` | `0x3b4` | struct_offset | `*(window+0x3b4)` = `CTreeViewWnd` (the category tree control). | `window+0x3b4` |
| `MktOff::TreeCount` | `0x1d8` | struct_offset | CTreeViewWnd visible line count. | `tree+0x1d8` |
| `MktOff::TreeArr` | `0x1dc` | struct_offset | CTreeViewWnd visible-line array base. | `tree+0x1dc` |
| `MktOff::TreeSelIdx` | `0x1f8` | struct_offset | CTreeViewWnd selected-line index (-1 = none). | `tree+0x1f8` |
| `MktOff::TreeStride` | `0x128` | struct_offset | Per-line struct stride in the visible-line array. | `la + i*0x128` |
| `MktOff::TreeLineNode` | `0x10` | struct_offset | Tree line struct → tree-item node ptr. | `line+0x10` |
| `MktOff::TreeItemLabel` | `0x14` | struct_offset | LEAF tree-item node → label `char*` (C string). | `leafNode+0x14` |
| `MktOff::BranchLabel` | `0x18` | struct_offset | BRANCH (folder) tree-item node → label `char*`. Branches use `+0x18`, leaves `+0x14`. | `branchNode+0x18` |
| `MktOff::LeafVtable` | `0x009DED30` | abs_eqgame_addr | CTreeViewNode LEAF vtable; `*(node)==LeafVtable` identifies a selectable leaf. | `*(selItem)==Resolve(LeafVtable)` |
| `MktOff::BranchVtable` | `0x009DE8B8` | abs_eqgame_addr | CTreeViewNode BRANCH (folder) vtable; `*(node)==BranchVtable` identifies a parent folder line. | `*(node)==Resolve(BranchVtable)` |

### Tile child control NAMES (SIDL `EQUI_MarketplaceWnd.xml`)

> These string names are the **cross-build identifiers** — they come from the SIDL XML and are stable across skins where present. Anchor the addresses above on these.

| Symbol | Value | Kind | What it is | Anchor |
| --- | --- | --- | --- | --- |
| `MKPW_Item_Label` | tile name label | misc | Tile item-name label; also the ready-gate (absent ⇒ tile children not built yet, retry). | `MktFindChild(tile,'MKPW_Item_Label')` |
| `MKPW_ItemCost_Label` | tile price label | misc | Tile price label. | `MktSetChildText(tile,'MKPW_ItemCost_Label',price)` |
| `MKPW_Item_Icon` | tile icon control | misc | Tile icon control (CTextureAnimation host at +0x228). | `MktFindChild(tile,'MKPW_Item_Icon')` |
| `A_DragItem` | icon atlas name | misc | Shared item-icon texture-atlas animation; per-tile copy gives each its own frame. | `AnimFind('A_DragItem')`; frame=icon-500 |
| `MKPW_BuyBtn` | buy button | misc | Native Buy button; force-enabled each frame (`+0x1a=1`); click (code 1) ⇒ `#mktbuy <id>`. | `MktFindChild(window,'MKPW_BuyBtn')` |
| `MKPW_DetailsInspect` | inspect button | misc | General-tab Inspect button; force-enabled; click ⇒ `#mktview <id>` (server ItemPacketViewLink). | `g_mktInspectBtn`; `#mktview` |
| `MKPW_AvailableFundsUpper` | funds number widget | misc | Funds balance number; morphed to the category currency balance. | `MktFindChild(window,'MKPW_AvailableFundsUpper')` |
| `MKPW_DetailsName` | detail name label | misc | Detail-pane item name, set on tile select. | `MktSetChildText(window,'MKPW_DetailsName',name)` |
| `MKPW_AdditionalDetails` | detail desc widget | misc | Detail-pane description/stat-line text from `MktItem.desc`. | `MktFindChild(window,'MKPW_AdditionalDetails')` |
| `MKPW_BuyPrice` | detail price widget | misc | Detail-pane price text from `MktItem.price`. | `MktFindChild(window,'MKPW_BuyPrice')` |
| Tile sale-decoration labels (hidden) | `MKPW_New_Label`, `MKPW_Was_Label`, `MKPW_OnSaleFor_Label`, `MKPW_SaleTimeLeft_Label`, `MKPW_AvailableTimeLeft_Label`, `MKPW_TimeLeft_Label`, `MKPW_ItemCost_CreditCardMoneyTypeLabel` | misc | Sale / credit-card decoration labels hidden per tile; `MKPW_TimeLeft_Label` also blanked. | `kHide[]` in MktFillOneTile |
| Funds label child walk | `+0xc` then `-0x10`, child[0] at `+0x10` | struct_offset | From MKPW_AvailableFundsUpper: parent = `*(num+0xc)-0x10`; "Current Funds" label = `*(parent+0x10)` (first child). | `g_mktFundsLabel` derivation |
| CXWnd Enabled byte | `+0x1a` | struct_offset | CXWnd enabled byte; set 1 each frame on Buy/Inspect to keep them clickable. | `*(BYTE*)(btn+0x1a)=1` |

### Marketplace 3D preview — eqgame resolvers & globals (`Off::`)

> Paper-doll + actor rebuild + globals. Image base `0x400000`. **The shipped preview drives `FnPaperDollPreview`; the `MKPW_PreviewRenderArea` / native Station-Cash preview page is intentionally UNUSED — do not anchor preview work on it.**

| Symbol | Value | Kind | What it is | Anchor |
| --- | --- | --- | --- | --- |
| `Off::FnPaperDollPreview` | `0x00576CA0` | abs_eqgame_addr | `FUN_00576ca0(viewport, dummy)`: clones the local player (source = GblPreviewSubject) into the viewport on a fresh actor. **`__thiscall(ecx=viewport)`; the second arg is a DUMMY that only balances RET 4.** THIS is the shipped preview driver. | clone read at `vp+0x1e0` |
| `Off::FnViewportSetModel` | `0x005768C0` | abs_eqgame_addr | `FUN_005768c0(viewport, sourceSpawn, charFrame)`: viewport SetModel; clones source actor into the preview region. `__thiscall`. **SUPERSEDED — do NOT call; use FnPaperDollPreview.** It takes `source`+`frameflag` STACK args; calling it instead of PaperDollPreview passes the wrong args. Resolver retained for reference only. | `Off::FnViewportSetModel` |
| `Off::FnSetWeaponSlot` | `0x005923F0` | abs_eqgame_addr | `FUN_005923f0(clone, slot, 'ITxxxx', 0, 1)`: set one held-weapon model by idfile string; render-only on a clone. `__thiscall`. | `MktSetWeaponFn` |
| `Off::FnAppearanceApply` | `0x00596B20` | abs_eqgame_addr | `FUN_00596b20(spawn, appearanceData, flag)`: apply appearance struct + rebuild actor. NOT used in shipped preview (AVs on null struct); resolver retained. | `data+0x64` equip array (9x5), `+0x140` apply flag |
| `Off::FnCreatePlayerActor` | `0x0048F4B0` | abs_eqgame_addr | `CDisplay::CreatePlayerActor`: rebuilds a spawn's render actor. **`cd` is the thiscall receiver (ECX); there are then 6 STACK args.** Callee pops 6 dwords (RET 0x18). **p5 (the 5th stack arg) MUST be literal `(uint*)1`.** | `FnCreateActor_t` |
| `Off::FnDisplayActorCleanup` | `0x00490A10` | abs_eqgame_addr | `FUN_00490a10(CDisplay, oldActor)`: reap the replaced old actor (anti-ghost). Call after each CreatePlayerActor when actor ptr changed. `__thiscall`. | reap `a0!=a1` |
| `Off::GblPreviewSubject` | `0x00DD2630` | global_ptr | `*(0x00DD2630)` = current preview-subject spawn = the local player. Source for the clone + slot pokes. | `DAT_00DD2630` |
| `Off::GblCDisplay` | `0x00DD2660` | global_ptr | `*(0x00DD2660)` = CDisplay singleton ptr (cd arg to CreatePlayerActor + actor reap). | `DAT_00DD2660` |
| `Off::GblEngine` | `0x015D46A4` | global_ptr | `*(0x015D46A4)` = EQGraphicsDX9 render-engine object ptr; its vtable (in the DLL) holds the preview-region methods. | `DAT_015d46a4`; `engine->vtbl+0x110` SetPreviewModel |

**`FnCreatePlayerActor` exact convention.** The typedef is:

```c
typedef unsigned int(__thiscall* FnCreateActor_t)(
    void* this_,   // cd  -> ECX (thiscall receiver, NOT a stack arg)
    int*  entity,  // stack arg 1
    void* p2,      // stack arg 2
    int   p3,      // stack arg 3
    int   p4,      // stack arg 4
    unsigned int* p5,  // stack arg 5  -- MUST be literal (uint*)1
    int   p6);     // stack arg 6
```

Call it as `createActor(cd, (int*)player, (void*)0, 1, 2, (uint*)1, 1)` — `cd` lands in ECX; the 6 stack args are `(entity, p2=0, p3=1, p4=2, p5=(uint*)1, p6=1)`; the callee pops 6 dwords (`RET 0x18`). Do **not** push `cd` onto the stack as one of "six stack args" — that corrupts ECX, shifts the stack by one dword, breaks the `RET` balance, and produces a crash or a no-op rebuild that silently leaks clones (the exact §90 failure). The `p5` sentinel must be the literal `(uint*)1`, never `&localVar`.

### Marketplace 3D preview — struct offsets on player/spawn & viewport

> Equip arrays, tint array, actor ptr, viewport clone ptr — poked by MktRenderClone / MktRenderCloneBareRebuild / MktShowPreview.

| Symbol | Value | Kind | What it is | Anchor |
| --- | --- | --- | --- | --- |
| Preview viewport ptr | `window+0x4cc` | struct_offset | `*(CMarketplaceWnd+0x4cc)` = the built 3D preview viewport object (fed to the paper-doll). **This member, NOT the native MKPW_PreviewRenderArea page, is the preview anchor.** | `vp=*(window+0x4cc)` |
| Equip array (primary) | `player+0xf30` | struct_offset | Spawn equipment array: 9 slots × 0x14 bytes (5 ints). int[0]=Material/ModelId. Slot = `+0xf30 + slot*0x14`. | `player+0xf30 + s*0x14 + i*4` |
| Equip array (parallel copy) | `player+0x1da0` | struct_offset | Parallel copy of the 9×0x14 array (FUN_00596b20 writes both); preview pokes both. | `player+0x1da0 + s*0x14 + i*4` |
| Per-slot tint array | `player+0xefc` | struct_offset | 9-DWORD per-slot armor TINT array (`0xAARRGGBB`). FUN_005a4360 returns `*(spawn+0xefc+slot*4)`; fed to FUN_00594E50. | `player+0xefc + s*4`; tint, NOT equip int[4] |
| Render actor ptr | `player/spawn+0x101c` | struct_offset | Spawn → render actor pointer. Read before/after CreatePlayerActor to detect change (reap gate). | `a0=*(player+0x101c)` |
| Viewport clone ptr | `viewport+0x1e0` | struct_offset | After paper-doll, `*(viewport+0x1e0)` = the preview clone spawn; PreviewApply repaints its slots. | `clone=*(vpd+0x1e0)` |
| Chest render-slot int0 (robe probe) | `player+0xf44` | struct_offset | Render slot 1 (chest) int0 = `+0xf30 + 1*0x14`; 10..16 ⇒ a robe is equipped. | `curChest=*(player+0xf44)` |
| Robe-material range | `10..16` | misc | Chest material 10-16 denotes a full-body robe model (drives robe bare-rebuild + hint). | `MktMaybeHint`; `MktRenderClone ps==1` |
| CreatePlayerActor call signature | `cd in ECX; stack (player, 0, 1, 2, (uint*)1, 1)` | misc | Exact arg pattern; p5 = literal `(uint*)1` sentinel (NOT `&localVar`) or rebuild no-ops and clones leak. cd is the receiver, NOT a stack arg. | `createActor(cd,(int*)player,(void*)0,1,2,(uint*)1,1)` |
| Preview bare/repaint masks | `ps9=0x7F/0x7F`; `ps1=0x0E/0x02` | misc | Slot bitmasks (bits 0-6 = head/chest/arms/wrists/hands/legs/feet) for full-suit (ps9) and robe (ps1). The full-suit "9" mask covers render slots 0-6 only (rebuild loop is `s<7`). | `MktRenderCloneBareRebuild bareMask/repaintMask` |
| pslot indices | `0..8` armor/weapon; `9` = full-suit SENTINEL | misc | 0 head, 1 chest, 2 arms, 3 wrists, 4 hands, 5 legs, 6 feet, 7 primary, 8 secondary. **`9` is a SENTINEL meaning "apply the 0x7F masks to render slots 0-6", NOT a 9th array index — never index a 9-wide array with it.** | `MktItem.pslot`; MktRenderClone switch |

### EQGraphicsDX9.dll preview engine vtable (RVAs, image base `0x10000000`)

> Cross-DLL engine methods reached via `*(GblEngine 0x015D46A4)->vtbl`. RVAs into `EQGraphicsDX9.dll`. The shipped path uses FnPaperDollPreview, but the SET-MODEL family is what a porter needs to drive a preview directly.

| Symbol | Value | Kind | What it is | Anchor |
| --- | --- | --- | --- | --- |
| engine vtbl+0x108 (enable region) | RVA `0x861a0` | eqgfx_rva | Enable/disable preview render region (FUN_576070 calls this). `FUN_100861a0`. | `vtbl+0x108` |
| engine vtbl+0x10c | RVA `0x861d0` | eqgfx_rva | Region getter (slot-ready byte at slot+0x10). `FUN_100861d0`. | `vtbl+0x10c` |
| engine vtbl+0x110 (SetPreviewModel) | RVA `0x861f0` | eqgfx_rva | `SetPreviewModel(modelObj, slot)`: slot 0=object/OPW, 1=avatar/marketplace; `engine+0xd7d8` = array[2] of preview slots. THE set-actor call. `FUN_100861f0`. | `vtbl+0x110`; `modelObj->vtbl[0xb8]` renderable |
| engine vtbl+0x114 (render) | RVA `0x86240` | eqgfx_rva | Render region (viewport Draw slot 3 calls with centerX,centerY,halfW,halfH,mode). `FUN_10086240`. | `vtbl+0x114`; viewport Draw FUN_00575ac0 |
| engine vtbl+0x118 (set region+mode) | RVA `0x86270` | eqgfx_rva | Set region rect + mode flag (FUN_576070 calls; mode from viewport+0x1d8). `FUN_10086270`. | `vtbl+0x118` |
| engine vtbl+0x11c | RVA `0x862a0` | eqgfx_rva | Sets global `DAT_10179110+0xbc` (mode push/pop around SetModel in FUN_005768c0). `FUN_100862a0`. | `vtbl+0x11c` |
| engine vtbl+0x120 | RVA `0x862c0` | eqgfx_rva | Set vec3 (light/cam pos via FUN_100b85a0). `FUN_100862c0`. | `vtbl+0x120` |
| engine vtbl+0x124 | RVA `0x86310` | eqgfx_rva | Setter (byte). `FUN_10086310`. | `vtbl+0x124` |
| engine vtbl+0x128 | RVA `0x86330` | eqgfx_rva | Last preview-region method (vtable ends ~+0x138). `FUN_10086330`. | `vtbl+0x128` |
| FUN_10017e50 (slot bind) | RVA `0x17e50` | eqgfx_rva | SetPreviewModel helper: stores renderable at slot+4; reads bbox via renderable vtbl+0x70/+0x74; light at slot+0x14, scene node at slot+0x18. | called by RVA `0x861f0`; `modelObj->vtbl+0xb8` renderable |
| render-mgr global (engine+4) | `0x015D46A8` | global_ptr | `DAT_015d46a8` = render manager (engine+4); vtbl+0x64 clones an actor (the clone-leak source). | `DAT_015d46a8` |
| render-state global (engine+8) | `0x015D46AC` | global_ptr | `DAT_015d46ac` (engine+8); vtbl+0x24 transfers render state old→new actor (investigated, not in shipped path). | `DAT_015d46ac` |

### `Off::` shared symbols referenced by `Mkt*` functions

> Constants the marketplace + node-fight code share. Image base `0x400000`. **The send/chat primitives below — not any Station-Cash opcode — are how the marketplace talks to the server.**

| Symbol | Value | Kind | What it is | Anchor |
| --- | --- | --- | --- | --- |
| `Off::FnCxstrCtor` | `0x00805C20` | abs_eqgame_addr | `CXStr::CXStr(this, char* src)` from a literal. Same address as `MktOff::CXStrCtor`. | `Off::FnCxstrCtor` |
| `Off::FnCxstrCStr` | `0x008061C0` | abs_eqgame_addr | `CXStr::c_str()` → `const char*`. Reads typed bid text; available to Mkt text reads. | `Off::FnCxstrCStr 0x008061C0` |
| `Off::SidlMgrPtr` | `0x015D3D08` | global_ptr | `*DAT_015D3D08` = `CSidlManager*`. SAME as `MktOff::UiMgr`. | `Off::SidlMgrPtr` |
| `Off::FnComboAddItem` | `0x00858C20` | abs_eqgame_addr | `CComboWnd::InsertChoice(list,&CXStr,width,0,flags)`. Used by PopulateTimeCombo (shares the Mkt helper region). | `combo+0x1D8` internal list |
| `Off::FnListClearList` | `0x00854220` | abs_eqgame_addr | `CListWnd` deselect-all (NOT a truncate). | `FUN_00854220` |
| `Off::FnListAddString` | `0x008580C0` | abs_eqgame_addr | `CListWnd::AddString(list,&name,color,id,0,NULL)`→row. | `FUN_008580C0` |
| `Off::FnListSetItemText` | `0x008575B0` | abs_eqgame_addr | `CListWnd::SetItemText(list,row,col,&text)`. | `FUN_008575B0` |
| `Off::FnChatSend` | `0x005369E0` | abs_eqgame_addr | `CChatManager::SendChatMessage`; backs SendServerCommand for Buy (`#mktbuy`) / Inspect (`#mktview`) / catalog request (`#mkt`). **THIS is the only network primitive the marketplace needs.** | `ChatMgrPtr 0x00E67CCC` |
| `Off::ChatMgrPtr` | `0x00E67CCC` | global_ptr | `*DAT_00E67CCC` = CChatManager singleton (target of FnChatSend). | `Off::ChatMgrPtr` |
| `Off::FnDspChat` | `0x0051F1A0` | abs_eqgame_addr | `CEverQuest::dsp_chat(this,text,color,echo,brackets)`: free-form chat print; preview robe hint uses `g_orig_DspChat`. | `Off::FnDspChat`; MktMaybeHint echo=1 |
| `Off::FnArmorApply` | `0x00594E50` | abs_eqgame_addr | `FUN_00594E50`: per-slot armor apply+repaint primitive. PreviewApply / RefreshWornSlot call the trampoline g_orig to repaint the clone's slots. `__thiscall`, prologue `6A FF 68`; 9 args. | `g_orig(spawn,renderSlot,material,0,0,0,0,color,1)` |
| `Off::FnGetItem` | `0x0042DEC0` | abs_eqgame_addr | `GetItem(container,&out,wornSlot)`: worn-item walk used by ArmorOrn (shared with preview RefreshWornSlot). | `Off::FnGetItem` |
| `Off::GblLocalPC` | `0x00DD261C` | global_ptr | `*(0x00DD261C)` = local PC base for the worn-item walk (ArmorOrn). Distinct from GblPreviewSubject. | `Off::GblLocalPC` |
| `FUN_005a4360` (per-slot tint getter) | `0x005A4360` | abs_eqgame_addr | For a PC spawn returns `*(spawn+0xefc+slot*4)`; the value FUN_00594E50 uses as the slot color. Documents why the preview pokes +0xefc. | `'FUN_005a4360 feeds to FUN_00594E50'` |

### MktItem cache & view limits

> Not addresses — build-tuning constants the porter must match to the feed.

| Symbol | Value | Kind | What it is | Anchor |
| --- | --- | --- | --- | --- |
| `g_mktItems` capacity | `4000` | misc | Catalog cache size; parse guard derives from `sizeof(g_mktItems)`. Grew 800→2000→4000 (840 armor ornaments pushed catalog to 2141). Must exceed total catalog rows or — because the feed is ORDER BY parent — the LAST parent's tiles vanish. Raising it means enlarging the array (the guard is sizeof-derived), NOT just bumping a constant. | `static MktItem g_mktItems[4000]` |
| `g_mktView` capacity | `64` | misc | Per-leaf view array size (max tiles CONSIDERED for one category). A separate ceiling from g_mktItems. | `g_mktView[64]`; `g_mktViewN<64` |
| `MKT_MAX_TILES` | `50` | misc | Max tiles MATERIALIZED per leaf; also sizes the per-tile icon-anim array. Tiles beyond ~50 in ONE category go missing here — distinct from the whole-category vanish g_mktItems causes. | `MKT_MAX_TILES=50` |
| `MktItem` wire row | `[BOZ_MKT]|parent|subcat|itemid|name|icon|price|currency|level|desc|model|pslot|color` | misc | Server feed row parsed by ParseBozMkt; trailing `model\|pslot\|color` drive the 3D preview (color hex `0xAARRGGBB`, default `0xFF000000`). | `ParseBozMkt`; `[BOZ_MKT_START]/[BOZ_MKT_END]` |

---

## How to re-derive each kind of offset

> ### Setup & gotchas (read before you start — applies to every recipe)
>
> **Ghidra environment**
> - Recent Ghidra (12-era) **dropped Jython**: `.py` postScripts fail with *"not started with PyGhidra"*. Write GhidraScripts in **`.java`** and run via `analyzeHeadless.bat`; for interactive work use the **ghidra-mcp** bridge.
> - `analyzeHeadless.bat` does NOT read Ghidra's `launch.properties` — you MUST set `JAVA_HOME` to the JDK your Ghidra build requires and prepend its `\bin` to `PATH`, or it silently fails/hangs. **Check the JDK version your build's `launch.properties` actually specifies** rather than assuming one — the requirement moves between Ghidra releases (recent builds want JDK 21).
> - **Project lock:** `-readOnly` does NOT bypass the lock in recent Ghidra — a GUI with the project open throws `LockException` even for read-only headless jobs. **Close the GUI's project** (File → Close Project) before any headless run; headless and live ghidra-mcp are mutually exclusive per session.
> - The ghidra-mcp tools query whatever program is **OPEN in the running CodeBrowser** — the binary must be opened in a CodeBrowser tab (double-click it), not merely have the project open, or MCP calls return nothing.
> - Windows **Python 3.14** on PATH crashes Ghidra's LaunchSupport (regex crash). Scrub PATH down to `JAVA_HOME\bin;System32;wbem` for Ghidra invocations.
>
> **Frida environment**
> - **ASLR is ON.** `eqgame.exe` base varies every launch — recompute it each session. On recent Frida (16+/17) prefer `Process.findModuleByName('eqgame.exe').base`; `Module.findBaseAddress` may be removed on your major (check before assuming — older Frida still has it). For the graphics engine use `Process.findModuleByName('EQGraphicsDX9.dll').base`.
> - Frida `globalThis` does NOT persist across separate `execute_in_session` calls — make every inject script **fully self-contained** (recompute base, re-resolve addresses, hardcode captured singletons inline).
> - **Keep scripts TINY — 1-2 operations per call.** Large `execute_in_session` scripts hang the session.
> - Anything touching DX9 (render, SetModel, per-frame UI factories) must run on the **MAIN / render thread** — ride a main-thread hook (`WndNotification` onEnter, or a Draw hook), not a Frida background thread, or it won't render / will behave differently off-thread.
> - The client must run **NON-elevated** for Frida to attach. If it was launched RUNASADMIN, remove the RUNASADMIN AppCompatFlags layer (`HKCU\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers` entry for the `eqgame.exe` path). **NOTE:** a crash RE-ADDS the RUNASADMIN layer on the next run, so re-clear it after every crash before re-attaching.
>
> **Static ↔ runtime conversions (keep the two conventions separate)**
> - `eqgame.exe`: runtime = `base + (ghidraAddr - 0x400000)`. Image base `0x400000`.
> - `EQGraphicsDX9.dll`: Ghidra-static = `0x10000000 + RVA`; runtime = `dllBase + RVA`. Image base `0x10000000`. **Do not cross the two.**
>
> **Anchoring discipline**
> - **Prefer string / child-name anchors** (`MKPW_*` widget names, the `MarketplaceWnd` SIDL class name) over raw addresses. Strings are the build-portable signal; **every numeric address and member offset will move** on a different build — re-derive from anchors, **never copy a number.**
> - Probe unknown CXStr offsets with an **SEH/try-guarded `memcmp` at rep+0x14**, never call `CStr` — calling `CStr` mutates the rep refcount and can cause an uncatchable heap crash.
> - Ghidra's analysis of RoF2 UI code is **patchy**: many vtable targets and handlers are *"No function found"*. Recover them by disassembling from a known instruction boundary (live vtable slot pointer + `Instruction.parse` loop), not by expecting a named function.
>
> **What you do NOT need to reverse**
> - **The BoZ marketplace does NOT use the native Station-Cash request/response opcodes.** The catalog arrives as a chat-channel TEXT feed (`[BOZ_MKT_START]` … `[BOZ_MKT_END]` rows parsed by `ParseBozMkt`); Buy/Inspect are issued as SLASH COMMANDS (`#mktbuy` / `#mktview`) via `CChatManager::SendChatMessage`; populate is pure client-side tile injection. **Do not spend time reversing the catalog packet, the SC item objects (~8.5KB, monetization-entangled), or the receive-side CMP/JZ opcode tree — the ASI never touches them.** The only network primitive you need is `FnChatSend` (`0x005369E0`) to emit the slash commands.

### Kind 1 — Absolute function addresses (`eqgame.exe`) via Ghidra string-anchor xref

*Image base `0x400000`; runtime = `moduleBase + (ghidraAddr - 0x400000)`.*

**Tools**
- Ghidra GUI + CodeBrowser (binary **OPEN**, not just the project).
- ghidra-mcp tools: `list_strings`, `get_xrefs_to`, `get_xrefs_from`, `decompile_function_by_address`, `search_functions_by_name`, `list_functions`.
- Headless `analyzeHeadless.bat` + a `.java` GhidraScript (batch alternative when the GUI is closed).

**Anchor strategy.** Anchor on a STABLE string the function MUST reference, then walk xrefs INTO the referencing function and decompile to confirm. Best anchors for the marketplace family, in order of reliability:
1. **Child-widget name string literals** the populate code passes to FindChild — `MKPW_Item_Icon`, `MKPW_Item_Label`, `MKPW_ItemCost_Label`, `MKPW_BuyBtn`, `MKPW_AvailableFundsUpper`, `MKPW_DetailsInspect`. These names are build-stable across RoF2 builds because they come from `EQUI_MarketplaceWnd.xml`. **Do NOT anchor on `MKPW_PreviewRenderArea` / the native preview page — the shipped BoZ preview does not use it (see preview anchors below).**
2. The window-class / SIDL name **`MarketplaceWnd`** referenced by the ctor.

> **No opcode hunt needed.** Earlier guidance to capture a "catalog request opcode" and reverse a `(conn, channel=4, buf, len)` send primitive + receive CMP/JZ tree applies to the native Station-Cash protocol, which the BoZ marketplace does NOT ride. Skip it. If you ever do need the chat path, the single primitive is `CChatManager::SendChatMessage` (`FnChatSend 0x005369E0`) — anchor it on the `ChatMgrPtr` global it consumes, not on a wire opcode.

**Steps**
1. In Ghidra, `list_strings` and find a marketplace child-name string (`MKPW_Item_Label`) or the SIDL class name `MarketplaceWnd`.
2. `get_xrefs_to` that string's address; the referencing functions are the populate / find-child / detail handlers. Decompile each to confirm role (a tile-populate walks a catalog list and FindChilds the `MKPW_*` names; a detail handler fills label controls on select).
3. **Find the ctor**: the function that writes the window vtable and stores child-control pointers at fixed member offsets — anchor via the `MarketplaceWnd` SIDL build call, or via the global window pointer's single writer (the ctor is assigned into the global). The ctor gives you **ALL** the member offsets (preview viewport at `window+0x4cc`, funds label, tree control at `window+0x3b4`, tile grid at `window+0x384`) by reading consecutive stores.
4. **For the Buy/Inspect/catalog requests**: these are slash commands, not packets. Find `CChatManager::SendChatMessage` (`FnChatSend`) and the `#mktbuy` / `#mktview` / `#mkt` literal strings near the call sites; that is the entire server-talk surface.
5. **Confirm** every recovered address by decompiling it and checking the body matches the expected behavior; **rename it in Ghidra** so later xref work is readable.

**Gotchas**
- Ghidra analysis is PATCHY on RoF2 — many vtable targets and handlers come back *"No function found"* (the WndNotification slot, several viewport Draw methods). Recover those by disassembling from a known instruction boundary, not by expecting a named function.
- The catalog/item Station-Cash objects are huge (~8.5KB structs, mostly 3D-preview data) and entangled with the USD/Station-Cash monetization protocol — don't try to reproduce them. The tractable path is the child-widget tile-fill, **not** the SC object.
- A constant appearing in ~13 unrelated functions is a red herring — confirm by decompiling, not by frequency.
- Child-name string anchors are the most build-portable signal; raw addresses and member offsets WILL move — always re-derive from the strings, never copy a number.

### Kind 2 — Struct field offsets (live, via Frida hook + compare-to-known-values)

*Validated at runtime; recorded as static base-`0x400000` offsets.*

**Tools**
- frida-mcp: `attach_to_process` / `get_process_by_name`, `create_interactive_session`, `execute_in_session`, `get_session_messages` (keep scripts tiny — 1-2 ops/call).
- A known function address from the Ghidra phase to hook (WndNotification, the populate fn, a per-frame draw hook, or the chat-dispatch) so you have a live object pointer in a register.
- Ghidra open in parallel for cross-checking the static offset the live value confirms.

**Anchor strategy.** Hook a function whose `this` (or an argument) is the object whose layout you need, captured at `onEnter` from the correct register (thiscall ⇒ ecx). With a live object pointer, read candidate offsets and COMPARE against values you already know (a category count you set, the model number of the weapon you're holding, a widget's ARGB text color). Walk linked structures by their head/next pointers (CXWnd child list = `+0x10` head, `+0x08` next) rather than guessing array strides. Convert every confirmed live offset back to static: `static = runtime - moduleBase + 0x400000`.

**Steps**
1. **Pick a hook** whose object you need: WndNotification (ecx = the window), a per-frame Draw hook, or the chat dispatch (ecx = the CEverQuest singleton). Capture the pointer at `onEnter`.
2. From the live pointer, read candidate offsets as u32/ptr and **match against a KNOWN value**: e.g. the category array count after you populate; the player equip slot int0 == the model number of the item you're wearing (slot 7 = primary weapon); a tile name-label's ARGB color at `+0x12c`. The offset whose value matches is confirmed.
3. **For arrays/lists, prefer walking pointers over assuming stride**: CXWnd children via `+0x10` (head) / `+0x08` (next); tree lines have an explicit count + array + stride you can verify by reading N entries and seeing sane labels.
4. **Distinguish leaf vs branch / PC vs non-PC** by a discriminator field you confirm live (node vtable ptr = leaf-vtable vs branch-vtable; `spawn+0x125==1` means a PC spawn). Critically: a value can live at DIFFERENT offsets per node type — **leaf label at `+0x14`, branch label at `+0x18`** — so verify the label reads as clean ASCII for EACH type, not just one.
5. **Mutate-and-observe to prove a write offset.** Use a mutation that actually exists in the marketplace path, e.g. poke a tile name-label's color at `label+0x12c` to gold (`0xFFFFD700`) and watch the highlight change, or set a child's text via the `VtSetText` (`+0x124`) slot and watch the label update, or poke an equip-slot int0 on the preview clone and watch the avatar's weapon/armor model change. (There is no `SetSessionId`/session-id field in the marketplace path — the funds widget `MKPW_AvailableFundsUpper` is morphed to show the CATEGORY CURRENCY BALANCE, not a session id; don't chase a `+0x23c` "session id" offset, it belongs to a different window.) A successful mutate-and-observe proves both the offset and the rebuild/redraw primitive.
6. **Record** the confirmed offset as static (subtract live base, add `0x400000`) and cross-check the static offset's consecutive members in Ghidra's ctor decompile.

**Gotchas**
- ASLR is ON — recompute base each session via `Process.findModuleByName('eqgame.exe').base`; runtime = `base + (ghidraAddr - 0x400000)`.
- Frida `globalThis` does NOT persist across `execute_in_session` calls — every inject script must be self-contained (recompute base, re-resolve addresses, hardcode captured singletons inline).
- Keep scripts tiny (1-2 ops per call) — large scripts hang the session.
- Some structures differ in a draw-thread/main-thread context vs a Frida background thread (a template factory that returns a cached object off-thread but null on a 2nd in-draw-frame call). Validate the offset/behavior in the SAME execution context you'll use in the DLL — ride a main-thread hook for anything touching DX9 or per-frame factories.
- Reading an unknown CXStr offset by calling `CStr` mutates the rep refcount and can heap-crash; probe string bytes with an SEH/try-guarded `memcmp` at rep+0x14 instead.
- The client must run NON-elevated for Frida to attach; if launched RUNASADMIN, clear that layer first.

### Kind 3 — Vtable slots (disassemble the vtable from a known boundary)

*Slot byte offset = slot index × 4; e.g. slot 34 = `+0x88`.*

**Tools**
- Ghidra: list the vtable as data from the address the ctor stores into the object (the ctor's first store of a code-table pointer).
- ghidra-mcp `get_xrefs_from` / disassemble around each slot target; `decompile_function_by_address` on the slot's target.
- Frida for slots Ghidra leaves undefined: read the live vtable pointer (`object[0]`), read `slot = *(vtable + index*4)`, then `Instruction.parse`-loop to disassemble the target.

**Anchor strategy.** Find the vtable address from the constructor (the ctor writes the vtable pointer as the object's first member). That fixes slot 0 = vtable base. Each method is `base + slot*4` (32-bit). Identify the slot by its known semantic position in the CXWnd-derived hierarchy (WndNotification = slot 34; SetWindowText = the `+0x124` method; SetVisible = the `+0xd8` method; dtor = slot 0 / `+0x2c` deleting-dtor). Confirm by decompiling the slot target and matching behavior, or by hooking it live and checking the notify code/args.

**Steps**
1. From the ctor (found via the SIDL class-name anchor or the global-pointer writer), read the vtable pointer it stores into the object — that's the vtable base; the slot count spans until the next data symbol (e.g. 95 virtuals).
2. **Index the slot by its known hierarchy position.** For CXWnd-derived UI: WndNotification is slot 34; widget SetWindowText at byte `+0x124`; SetVisible(show,resize) at `+0xd8`; FindChild is a separate engine function, NOT a slot.
3. **Disassemble/decompile the slot target.** If Ghidra says *"No function found"* (common here), read the slot live in Frida (`vtable = *object; target = (vtable+index*4).readPointer()`) and `Instruction.parse` from there.
4. **Confirm the slot's identity by hooking it live**: hook slot 34 and verify it fires with a notify code (`0x20` select, `0x1` activate) and a widget pointer argument you can map back to a tile.
5. **Record** the byte offset (`+0x88`, etc.) AND the resolved static address; in the DLL call it as `__thiscall` via `__fastcall` (this in ecx, null edx filler).

**Gotchas**
- UI vtable methods are frequently UNDEFINED in Ghidra — plan to Frida-disassemble from the live slot pointer; don't assume a named function exists.
- **Mind byte-offset vs slot-index**: slot N = byte offset N×4. Notes mix both conventions (slot 34 vs `+0x124`) — keep them straight.
- Calling a freshly-created widget's vtable method before the engine has finished building its children crashes; a tile's child controls aren't built until a LATER pump/frame, so a vtable call that validates the vtable can reject a not-yet-ready tile — **gate on the child existing (`MKPW_Item_Label` present), retry next frame, don't over-validate.**
- `__thiscall` via `__fastcall`: this lands in ecx as arg1, pass a null edx as the dummy second arg; both are callee-pops so the `RET N` matches.

### Kind 4 — Global pointers (Ghidra `DAT_` xref)

*Static base `0x400000`; runtime = `base + (datAddr-0x400000)`, then dereference for the object.*

**Tools**
- ghidra-mcp `list_data_items` / `list_strings` to find the `DAT_` symbol, `get_xrefs_to` to enumerate users.
- `decompile_function_by_address` on the single WRITER of the global (where it's assigned) to confirm what it points to.
- Frida to read `*(base + (datAddr-0x400000))` live and confirm it's the expected object.

**Anchor strategy.** Identify each global by its UNIQUE WRITER and its many readers. The window singleton is the `DAT_` assigned exactly once, in an init function, from the ctor's return — and read by dozens of marketplace functions. The chat manager pointer is the `DAT_` consumed by `FnChatSend`. The local-player / CDisplay / preview-subject / engine globals are the `DAT_`s passed into the actor and render functions you already located. Anchor each on its role (which known function consumes it), then confirm live.

**Steps**
1. From a known marketplace function (ctor, populate, chat-send), note the `DAT_` globals it references (`get_xrefs_from`) — window singleton, chat manager ptr, UI/template manager, local-player, CDisplay, engine ptr.
2. For each candidate global, `get_xrefs_to` to see its writer(s). A true singleton has ONE writer (its init/ctor) and many readers; confirm the writer assigns the right object type.
3. **Confirm live in Frida**: `ptr = (base + (datAddr-0x400000)).readPointer()`; validate by reading a known field off the pointed-to object (e.g. the window's visible byte at `+0x196`, the category array at `window+0x284`, or the player's equip array at `player+0xf30`).
4. **Record** the static `DAT` address; dereference at runtime. Distinguish a pointer-to-object global from an inline-object global by whether code dereferences it once or treats it as a base.

**Gotchas**
- A `DAT_` may be a POINTER to the object (deref once) or the object/buffer itself (use as base) — check how the code uses it before dereferencing.
- The window global lives in `eqgame.exe` but the object it eventually drives (the render engine) has its VTABLE in `EQGraphicsDX9.dll` — a runtime vtable far outside eqgame's range is the tell you've crossed into the DLL.
- Several globals are only meaningful in a specific state (the preview-subject global is the local player only while previewing) — read them at the right moment.
- Still ASLR-relative: every global is `base + (datAddr-0x400000)`; recompute base per session.

### Kind 5 — EQGraphicsDX9.dll engine RVAs (load the DLL into Ghidra at image base `0x10000000`)

*Ghidra addr = `0x10000000 + RVA`; runtime = `dllBase + RVA`.*

**Tools**
- Ghidra: import `EQGraphicsDX9.dll` into the SAME project, image base `0x10000000` (Ghidra addr = `0x10000000 + RVA`); re-run Auto Analyze.
- Frida: read the live engine object's vtable (engine ptr is an eqgame `DAT_`), compute the DLL base via `Process.findModuleByName('EQGraphicsDX9.dll').base`, map runtime vtable → RVA.
- `decompile_function_by_address` across the viewport-method vtable family to find SetModel / render / region calls.
- **Hash-verify the DLL matches the running client** (all client folders' `eqgame.exe` + `EQGraphicsDX9.dll` were byte-identical in the reference population) so static RE is valid.

**Anchor strategy.** Start from the eqgame-side viewport Draw call: the viewport's Draw method loads the global engine pointer (an eqgame `DAT_`) and calls `engine->vtable[+0x114]` with only rect+mode (no actor) — so the model/actor lives engine-side. That call pins the engine vtable family: enable-region (`+0x108`), set-region+mode (`+0x118`), render (`+0x114`), and the unknown neighbors. The engine vtable is in `EQGraphicsDX9.dll` (runtime vtable address far above eqgame's range). Map that runtime vtable to an RVA via the DLL base, find the vtable in the imported DLL, and decompile each family member to identify **SET-MODEL** (the one that takes a model object + slot, reads its renderable via a vtbl getter, frames the camera from its bbox).

**Steps**
1. In `eqgame.exe`, locate the viewport Draw method (often undefined — Frida-disasm it). It reads the engine global and calls `engine->vtbl[+0x114](rect,mode)`. That gives you the engine vtable family offsets (`+0x108`..`+0x128`).
2. **Read the engine object live**: `engine = deref(eqgame engine DAT_)`; `vtable = *engine`. The vtable address is far above eqgame's base ⇒ it's in the DLL. Compute `dllBase = Process.findModuleByName('EQGraphicsDX9.dll').base`; `RVA = vtableRuntime - dllBase`.
3. **Import the DLL into the Ghidra project** at image base `0x10000000` and Auto-Analyze. The vtable family targets are at `0x10000000 + RVA`.
4. **Decompile each family member** to label them: enable/disable region, getter (slot-ready byte), **SET-MODEL** (param = model obj, slot; if non-null gets renderable via `modelObj->vtbl[+0xb8]`, stores it, reads bbox via renderable `vtbl+0x70/+0x74` to frame the camera, links a scene node; if null clears the slot), render-region, set-region+mode, set vec3 (light/cam pos).
5. **Confirm SET-MODEL** by calling it live on the running viewport with a real engine actor. **HAZARD — read before you do this:** cloning the LIVE player's actor (the obvious test subject) is exactly what caused the world-clone LEAK and the live-player avatar DEGRADATION that cost multiple sessions during bring-up (§90). The shipped fix had to bare-rebuild, reap the replaced actor (`FnDisplayActorCleanup 0x00490A10`, gated on the actor-ptr at `+0x101c` changing), and restore the poked slots in a `__finally`. So: if your live SET-MODEL test leaves ghost actors or degrades your real avatar, that is a MISSING REAP/RESTORE in your test harness, **not** a wrong RVA. Test on a clone you can reap, reap on every actor-ptr change, and restore poked slots in a `__finally`. Validate visually (avatar renders in the box, no ghost left behind, live avatar unchanged after closing the preview).
6. **Record both** the RVA (for source) and verify the slot byte-offset (`+0x110`, etc.) against the eqgame-side calls that use it.

**Gotchas**
- Re-run Auto Analyze on `eqgame.exe` after importing the DLL — many viewport methods stay undefined otherwise.
- Hash-verify the DLL you RE matches the running client before trusting static analysis (multiple client copies existed; they were byte-identical, which made the RE valid — don't assume).
- The engine holds the actor/model SLOT-side: per-viewport preview slots live in an array on the engine object (`engine+0xd7d8` = array[2], slot 0 object-mode, slot 1 avatar-mode); the slot object must be non-null or SET-MODEL no-ops.
- **mode-1 (avatar mode) does NOT auto-render the local player** — the viewport draws blank until you explicitly assign an actor via SET-MODEL. Don't expect a free avatar.
- The model object you pass must be a standard engine actor with the right vtbl shape (`+0xb8` get-renderable; renderable has `+0x70/+0x74` bbox and a node getter) — an arbitrary struct won't render.
- Runtime base for the DLL is ASLR'd too; resolve it per session via `Process.findModuleByName`. The Ghidra-static `0x10000000 + RVA` convention is SEPARATE from eqgame's `0x400000` — don't cross them.

---

## Porting checklist

Work top to bottom. The first decision short-circuits everything.

- [ ] **1. Verify the hash.** Compute SHA-256 of `eqgame.exe` AND `EQGraphicsDX9.dll`; compare exactly against the published reference.
- [ ] **2. Verify the two non-binary prerequisites.** Confirm the client's stock `EQUI_MarketplaceWnd.xml` (in `uifiles/...`, with its `MKPW_*` controls) is present and unmodified AND the server emits the `[BOZ_MKT]` feed + `#mkt`/`#mktbuy`/`#mktview` handlers. A hash match alone is necessary but NOT sufficient.
- [ ] **3. If BOTH binaries match AND both prerequisites hold → use the prebuilt BOZ.asi and STOP.** No re-derivation needed. ASLR rebasing will resolve correctly because the layout is identical.
- [ ] **4. If EITHER binary differs → this is a port.** Continue below. (And confirm you are on RoF2 at all — Titanium/Underfoot/SoD are a full re-port, not in scope here.)
- [ ] **5. Set up Ghidra + Frida.** Open the exact `eqgame.exe` (and import `EQGraphicsDX9.dll` at `0x10000000`) in a CodeBrowser tab; set `JAVA_HOME` to the JDK your build's `launch.properties` specifies and scrub PATH if going headless; close the GUI project before any headless run. Launch the client non-elevated (clear any RUNASADMIN AppCompatFlags layer) so Frida can attach.
- [ ] **6. Re-resolve absolute addresses via string anchors** (Kind 1). Start from the `MKPW_*` widget-name strings and the `MarketplaceWnd` SIDL class name; walk xrefs to the populate / find-child / detail handlers, the ctor, and the chat-send primitive (`FnChatSend` → `#mktbuy`/`#mktview`/`#mkt`). Do NOT reverse the native SC catalog packet — it's a chat-text feed. Rename each in Ghidra as you confirm it.
- [ ] **7. Recover global pointers** (Kind 4) — window singleton (unique writer), chat manager ptr, UI/template manager, local-player, CDisplay, engine ptr.
- [ ] **8. Recover vtable slots** (Kind 3) — WndNotification (slot 34), SetWindowText (`+0x124`), SetVisible (`+0xd8`), relayout (`+0xa0`). Frida-disassemble any Ghidra leaves undefined.
- [ ] **9. Re-probe struct offsets via Frida** (Kind 2) — hook WndNotification / a Draw hook; compare-to-known-values for the cat array, tree control, equip arrays, tint array, viewport (`window+0x4cc`) + clone (`viewport+0x1e0`) ptrs; mutate-and-observe (tile color at `+0x12c`, text via `+0x124`) to prove write offsets. Record each as static base-`0x400000`.
- [ ] **10. Re-derive the EQGraphicsDX9 RVAs** (Kind 5) if you need the preview path — map the live engine vtable to RVAs, identify SET-MODEL, confirm it renders an actor in the box. Reap + restore in your test harness or you'll leak clones / degrade your live avatar.
- [ ] **11. Re-derive the build-specific `TileTplId`** (currently `3211409` / `0x310091`) for your SIDL skin.
- [ ] **12. Rebuild the offset header** with every re-derived value (record statics as base-`0x400000` RVAs so the ASI's runtime rebasing stays correct).
- [ ] **13. Test: window opens.** Open the Marketplace — no crash, categories render.
- [ ] **14. Test: buy.** Select a tile, click Buy (`#mktbuy <id>`), confirm the funds widget + server round-trip.
- [ ] **15. Test: preview.** Select an ornament/armor tile, open the 3D preview, confirm the avatar renders and repaints without ghosting or crash, and that your LIVE avatar is unchanged after closing it.

---

## Troubleshooting

| Observed failure | Likely cause / offset class at fault |
| --- | --- |
| Crash the instant the Marketplace window opens | The window ctor / populate **absolute addresses** (Kind 1) or the **WndNotification vtable slot** (Kind 3) — the hook install / populate path fires at construction. |
| **Hash matches but the shop opens EMPTY (no crash)** | Not an offset problem. Missing or altered `EQUI_MarketplaceWnd.xml` skin (no `MKPW_*` children to fill), OR the server isn't emitting the `[BOZ_MKT]` feed / `#mkt` handlers. Verify the SIDL XML in `uifiles` and the server commands first. |
| Window opens but categories are missing / never render | **Category-tree absolute fns** `CreateCats`/`RenderCats` (Kind 1) or the `NodeCount` render-gate **struct offset** `+0x30` (Kind 2); also the `CatCount`/`CatArray`/`CatCapacity` window offsets. |
| Tiles appear blank or never fill | The **child-name string anchors** (`MKPW_Item_Label` ready-gate) or `FindChild`/`MakeTile`/`TplResolve` **absolute fns** (Kind 1); a wrong **`TileTplId`** SIDL id. |
| Tiles fill but have the wrong icon / no icon | `SetIcon`, `AnimFind`, `AnimCtor`, `AnimCopy` **absolute fns** (Kind 1) and the `IconAnim` **struct offset** `+0x228` (Kind 2). |
| Tiles fill but show wrong / garbage text | `VtSetText` **vtable slot** `+0x124` (Kind 3) or the `CXStr` text/len **struct offsets** `+0x14`/`+0x08` (Kind 2). |
| Wrong tile highlights / selection color off | `NameColor` **struct offset** `+0x12c` (Kind 2); the WndNotification select code `0x20` and the sub-widget parent walk (`+0xc`, `-0x10`). |
| Selecting a category does nothing / wrong category | The **CTreeViewWnd struct offsets** (TreeCtrl/Count/Arr/SelIdx/Stride, Kind 2) or the **leaf/branch vtable** discriminators (Kind 4) — leaf label `+0x14` vs branch label `+0x18`. |
| Buy / Inspect buttons greyed out or do nothing | The CXWnd **Enabled byte** `+0x1a` (Kind 2), the button **child-name anchors** (`MKPW_BuyBtn`/`MKPW_DetailsInspect`), or the chat-send **absolute fn** `FnChatSend` + `ChatMgrPtr` **global** (Kinds 1/4). |
| Funds number never updates / shows wrong value | `MKPW_AvailableFundsUpper` **child anchor** + the funds-label child walk **struct offsets** (Kind 2). The widget shows the category currency balance, not a session id. |
| 3D preview opens blank (no avatar) | The **EQGraphicsDX9 SET-MODEL RVA** family (Kind 5) — mode-1 won't auto-render; or the **viewport / clone ptr** struct offsets `window+0x4cc` / `viewport+0x1e0` (Kind 2). Anchor on these, NOT on the native `MKPW_PreviewRenderArea` page (intentionally unused). |
| 3D preview crashes on open | `FnPaperDollPreview` / `FnCreatePlayerActor` **absolute fns** (Kind 1) — check `cd` is in ECX (not a stack arg) and `p5` is literal `(uint*)1` — or `EQGraphicsDX9.dll` itself mismatches the reference hash. |
| Preview avatar leaves ghost actors / leaks / degrades live avatar | `FnDisplayActorCleanup` **absolute fn** + the **render actor ptr** struct offset `+0x101c` reap gate (Kinds 1/2); missing reap-on-change or missing `__finally` slot-restore; wrong CreatePlayerActor `p5` sentinel. Cloning the LIVE player without reaping is the known §90 leak. |
| Preview shows wrong armor color / tint | The **per-slot tint array** struct offset `player+0xefc` (Kind 2) and `FnArmorApply` (`FUN_00594E50`) **absolute fn** (Kind 1) — tint lives at `+0xefc`, NOT equip int[4]. |
| Preview shows wrong weapon/armor model | The **equip arrays** `player+0xf30` / parallel `player+0x1da0` (Kind 2), `FnSetWeaponSlot` **absolute fn** (Kind 1). |
| Whole LAST category's tiles silently vanish | The `g_mktItems` cache is smaller than your total catalog rows. Because the feed is ORDER BY parent, the last parent truncates first. Fix = ENLARGE the `g_mktItems[]` array (the parse guard is `sizeof`-derived, so just changing a constant isn't enough). |
| Tiles beyond ~50 in ONE category missing | A different ceiling — the per-leaf `g_mktView[64]` / `MKT_MAX_TILES[50]` limits, NOT `g_mktItems`. Raise those if a single category legitimately needs more tiles. |
| Patch lands in unrelated code, intermittent corruption | Almost always a **binary mismatch** — the RVAs are valid-looking but wrong after rebasing. Re-verify the SHA-256 of `eqgame.exe` itself before debugging any single offset. |