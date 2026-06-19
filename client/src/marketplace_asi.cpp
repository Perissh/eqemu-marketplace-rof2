// EQ Marketplace ASI -- standalone client mod, carved from BOZ.asi (RoF2).
// In-game shop window (categories / tiles / buy / funds) + 3D item preview.
// Single translation unit. Client-build offsets + rebase plumbing: offsets.h.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include "offsets.h"


// ==== Log (verbatim 528-541) ====
// ---- Tiny logger ------------------------------------------------------------
namespace Log {
    static constexpr const char* PATH = "C:\\BOZ-Client-RoF2\\boz.log";
    inline void Write(const char* fmt, ...) {
        FILE* f = fopen(PATH, "a");
        if (!f) return;
        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fputc('\n', f);
        fclose(f);
    }
}

// ==== IsReadable (verbatim 578-586) ====
static bool IsReadable(DWORD addr, SIZE_T bytes) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)))
        return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    auto end = static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(mbi.BaseAddress) + mbi.RegionSize);
    return (addr + bytes) <= end;
}

// ==== FnCreateActor_t typedef (verbatim 1800-1802) ====
typedef unsigned int (__thiscall *FnCreateActor_t)(void* this_, int* entity, void* p2,
                                                   unsigned int p3, unsigned int p4,
                                                   unsigned int* p5, unsigned int p6);

// ==== CopyField (verbatim 3125-3134) ====
static const char* CopyField(const char* src, char* dst, size_t dstSize) {
    size_t i = 0;
    while (*src && *src != '|' && i + 1 < dstSize) {
        dst[i++] = *src++;
    }
    dst[i] = '\0';
    while (*src && *src != '|') src++;  // skip rest of field if truncated
    if (*src == '|') src++;
    return src;
}

// ==== SendServerCommand + FnChatSend_t (verbatim 4237-4264) ====
typedef void (__thiscall *FnChatSend_t)(void* this_, char* text, char* target, char flag1, char flag2);

static void SendServerCommand(const char* cmd) {
    if (!cmd) return;
    DWORD chatMgrAddr = Resolve(Off::ChatMgrPtr);
    if (!IsReadable(chatMgrAddr, 4)) {
        Log::Write("[BOZ-RoF2] SendServerCommand: ChatMgrPtr addr unreadable");
        return;
    }
    void* chatMgr = *reinterpret_cast<void**>(chatMgrAddr);
    if (!chatMgr) {
        Log::Write("[BOZ-RoF2] SendServerCommand: ChatMgr is null (not in-game?)");
        return;
    }
    auto fnSend = reinterpret_cast<FnChatSend_t>(Resolve(Off::FnChatSend));
    __try {
        // Send writable string -- some downstream paths may mutate the buffer.
        char buf[256] = {0};
        size_t n = strlen(cmd);
        if (n >= sizeof(buf)) n = sizeof(buf) - 1;
        memcpy(buf, cmd, n);
        char target[1] = {0};  // empty target = global chat / GM-command path
        fnSend(chatMgr, buf, target, 1, 1);
        Log::Write("[BOZ-RoF2] SendServerCommand: '%s' dispatched", buf);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("[BOZ-RoF2] SendServerCommand: EXCEPTION sending '%s'", cmd);
    }
}

// ==== ArmorOrn slim -- preview repaint only ====
namespace ArmorOrn {
    typedef void (__thiscall *FnArmorApply_t)(void*, unsigned int, unsigned int,
                                              int, int, int, unsigned int, int, int);
    static FnArmorApply_t g_armorApply = nullptr;
    // Repaint one armor slot on an arbitrary spawn (the marketplace preview clone),
    // calling FUN_00594E50 directly (we never hook it here).
    void PreviewApply(void* spawn, unsigned int renderSlot, unsigned int material, unsigned int color) {
        if (!g_armorApply) g_armorApply = reinterpret_cast<FnArmorApply_t>(Resolve(Off::FnArmorApply));
        if (!g_armorApply || !spawn || renderSlot > 6) return;
        __try { g_armorApply(spawn, renderSlot, material, 0, 0, 0, 0, static_cast<int>(color), 1); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

// ==== PwDraw hook decls (verbatim 2010-2012) ====
typedef int (__thiscall *FnPwDraw_t)(void* this_);
static FnPwDraw_t g_orig_PwDraw = nullptr;
static bool       g_hookPwDraw_installed = false;

// ==== dsp_chat hook decls (hand) ====
typedef void (__thiscall *FnDspChat_t)(void* this_, char* text, int color, int echo, char brackets);
static FnDspChat_t g_orig_DspChat = nullptr;
static bool        g_hookDspChatInstalled = false;
static void*       g_dspChatThis = nullptr;   // CEverQuest singleton, read by MktMaybeHint

// ==== banner (verbatim 2284-2297) ====
// ============================================================================
// [BOZ] MARKETPLACE HYBRID -- drive the native CMarketplaceWnd with our own
// categories/items instead of the (dead) Station Cash catalog. The window, its
// populate functions, and the node layout were reverse-engineered live (see the
// project_marketplace_shop memory). v1: custom categories. Items/currency next.
//
//   *0x00D1FD08         -> CMarketplaceWnd*    (created at character-select)
//   FUN_006DD260(wnd)   -> builds the 3 default category nodes + sub-populate
//   FUN_006DCF40(wnd)   -> re-renders the Categories CTreeViewWnd from the nodes
//   wnd+0x196           -> CXWnd visible byte (1 = open)
//   wnd+0x280 / +0x284  -> category node count / array
//   catNode+0x10        -> internal-name CXStr (the TEXT the tree shows)
//   catNode+0x30        -> item count (>0 required for the node to render)
// ============================================================================

// ==== MktRenameCategory (verbatim 2366-2375) ====
static void MktRenameCategory(DWORD node, const char* name) {
    DWORD cx = *reinterpret_cast<DWORD*>(node + MktOff::NodeIntName);
    if (!cx) return;
    char* text = reinterpret_cast<char*>(cx + MktOff::CXStrText);
    DWORD len = 0;
    while (name[len] && len < 60) { text[len] = name[len]; len++; }
    text[len] = '\0';
    *reinterpret_cast<DWORD*>(cx + MktOff::CXStrLen)   = len;
    *reinterpret_cast<DWORD*>(node + MktOff::NodeCount) = 1;   // >0 -> renders
}

// ==== MktMakeNode (verbatim 2381-2394) ====
static DWORD MktMakeNode(const char* name, int type, DWORD parent) {
    DWORD node = reinterpret_cast<DWORD>(
        reinterpret_cast<FnMktAlloc_t>(Resolve(MktOff::NodeAlloc))(0x34));
    if (!node) return 0;
    DWORD dispCX = 0, intCX = 0;   // CXStr = single rep pointer
    auto cx = reinterpret_cast<FnMktCXStr_t>(Resolve(MktOff::CXStrCtor));
    cx(&dispCX, nullptr, name);
    cx(&intCX,  nullptr, name);
    reinterpret_cast<FnMktNodeCtor_t>(Resolve(MktOff::NodeCtor))(
        reinterpret_cast<void*>(node), nullptr, type,
        &dispCX, &intCX, reinterpret_cast<void*>(parent), 1);
    *reinterpret_cast<DWORD*>(node + MktOff::NodeCount) = 1;   // +0x30 render gate
    return node;
}

// ==== MktAddBranch (verbatim 2399-2413) ====
static void MktAddBranch(DWORD window, const char* parentName,
                         const char* const* children, int nChildren) {
    DWORD parent = MktMakeNode(parentName, 0, 0);   // type 0 = folder
    if (!parent) return;
    for (int i = 0; i < nChildren; i++)
        MktMakeNode(children[i], 1, parent);        // type 1 = browse view (shows item grid)
    DWORD cnt = *reinterpret_cast<DWORD*>(window + MktOff::CatCount);
    DWORD arr = *reinterpret_cast<DWORD*>(window + MktOff::CatArray);
    DWORD cap = *reinterpret_cast<DWORD*>(window + MktOff::CatCapacity);
    // MktPopulate grows the backing array to fit every parent and sets capacity; respect it.
    // (Was a hardcoded 24 back when the parent list itself was hardcoded.)
    if (!arr || cnt >= cap) return;
    *reinterpret_cast<DWORD*>(arr + cnt * 4) = parent;
    *reinterpret_cast<DWORD*>(window + MktOff::CatCount) = cnt + 1;
}

// ==== MktAddGearBranch fwd (verbatim 2418-2418) ====
static void MktAddGearBranch(DWORD, const char*, const char* const*, int, bool withMyLevel = true); // data-driven gear leaves (defined after g_mktItems)

// ==== struct MktItem + feed cache (moved ABOVE MktPopulate: the data-driven tree needs them) ====
struct MktItem {
    char  parent[40];     // top category, e.g. "Cross-Class Spells"
    char  subcat[48];     // leaf label shown in the tree, e.g. "Necromancer" / "General"
    DWORD item_id;
    char  name[64];
    DWORD icon;           // real items.icon gfx id
    DWORD price;
    char  currency[16];
    int   level;          // sort key / display (lowest mirror level for spells)
    char  desc[160];      // stat line shown in the General-tab detail pane on select
    int   model;          // [BOZ] equip model number (idfile "ITxxx" -> xxx) for the 3D preview; 0 = none
    int   pslot;          // [BOZ] preview equip-slot idx (7=primary weapon, 8=secondary); -1 = none
    DWORD color;          // [BOZ] preview TINT 0xAARRGGBB (natural = 0xFF000000)
};
static MktItem g_mktItems[4000];   // catalog feed cache (parse guard derives from sizeof)
static int     g_mktItemCount = 0;
static bool    g_mktCollecting = false;

// ==== MktPopulate (verbatim 2419-2553) ====
static void MktPopulate(DWORD window) {
    reinterpret_cast<FnMktVoidWnd_t>(Resolve(MktOff::CreateCats))(reinterpret_cast<void*>(window));
    DWORD count = *reinterpret_cast<DWORD*>(window + MktOff::CatCount);
    DWORD arr   = *reinterpret_cast<DWORD*>(window + MktOff::CatArray);
    if (!arr) return;
    // [BOZ] FULLY DATA-DRIVEN TREE: discover the distinct PARENTS from the [BOZ_MKT] feed (feed
    // order, which is alphabetical) so the ENTIRE category tree comes from boz_marketplace_catalog
    // -- a server owner adds a catalog row with a new `parent` and it appears, with ZERO ASI
    // changes. Before the feed lands g_mktItemCount==0 -> no parents yet; the [BOZ_MKT_END] repop
    // (g_mktNeedRepop) re-runs this once the catalog is cached.
    char parents[64][40];
    int  nParents = 0;
    for (int i = 0; i < g_mktItemCount && nParents < 64; i++) {
        const char* p = g_mktItems[i].parent;
        if (!p[0]) continue;
        bool seen = false;
        for (int j = 0; j < nParents; j++) if (lstrcmpA(parents[j], p) == 0) { seen = true; break; }
        if (!seen) { lstrcpynA(parents[nParents], p, sizeof(parents[0])); nParents++; }
    }
    // Grow the category ArrayClass to hold the 3 native defaults + every parent (+headroom) from
    // its OWN allocator (so the window's destructor-free stays valid). MktAddBranch reads this
    // capacity, so raising it here is what lets the tree exceed the stock ~10-slot array.
    DWORD need = 3 + (DWORD)nParents + 4;
    if (need < 24) need = 24;
    if (*reinterpret_cast<DWORD*>(window + MktOff::CatCapacity) < need) {
        DWORD bigger = reinterpret_cast<DWORD>(
            reinterpret_cast<FnMktAlloc_t>(Resolve(MktOff::NodeAlloc))(need * sizeof(DWORD)));
        if (bigger) {
            for (DWORD i = 0; i < count; i++)
                *reinterpret_cast<DWORD*>(bigger + i * 4) = *reinterpret_cast<DWORD*>(arr + i * 4);
            *reinterpret_cast<DWORD*>(window + MktOff::CatArray)    = bigger;
            *reinterpret_cast<DWORD*>(window + MktOff::CatCapacity) = need;
            arr = bigger;
        }
    }
    // Keep default node 0 (native "New and Featured" home); HIDE the other two
    // defaults (Player Studio, All Items) by failing the render gate (+0x30 = 0).
    // They MUST stay PRESENT in the array -- the client's leaf-select view-switch
    // falls back to the "All Items" node, so dropping it breaks tile display.
    for (DWORD i = 1; i < count && i < 3; i++) {
        DWORD node = *reinterpret_cast<DWORD*>(arr + i * 4);
        if (node) *reinterpret_cast<DWORD*>(node + MktOff::NodeCount) = 0;
    }
    // One branch per discovered parent; the LEAVES (subcats) are data-driven -- MktAddGearBranch
    // reads each parent's distinct subcats from the feed. withMyLevel auto-detects: a parent gets
    // a "My Level and Below" tab only if any of its items is level-gated (level > 0), so leveled
    // content (spells/gear) gets the tab and flat content (ornaments/mounts) doesn't. A null
    // fallback is safe: every discovered parent has items -> at least one real subcat, so
    // MktAddGearBranch always takes the data-driven path (never the fallback).
    //
    // NOTE on the old per-parent conventions, now data-driven via the catalog: a "General" tab is
    // just a subcat named "General" (prefix a space, " General", to sort it near the top); the
    // "  My Level and Below" tab (two leading spaces) is synthesized by MktAddGearBranch when
    // withMyLevel is on. Cross-class class tabs, gear-tier slot leaves, ornament weapon types --
    // ALL of it is now whatever the server put in boz_marketplace_catalog. No ASI edits to add a
    // category. (Parent order = feed order = alphabetical by parent.)
    for (int pi = 0; pi < nParents; pi++) {
        bool hasLevels = false;
        for (int k = 0; k < g_mktItemCount; k++)
            if (g_mktItems[k].level > 0 && lstrcmpA(g_mktItems[k].parent, parents[pi]) == 0) { hasLevels = true; break; }
        MktAddGearBranch(window, parents[pi], nullptr, 0, hasLevels);
    }
    reinterpret_cast<FnMktVoidWnd_t>(Resolve(MktOff::RenderCats))(reinterpret_cast<void*>(window));
}

// ==== ptr-safety fwd decls (verbatim 2555-2558) ====
// Pointer-safety guards (defined further down, after the tile helpers).
static bool MktReadable(DWORD p, DWORD size);
static bool MktExecutable(DWORD p);
static bool MktTileLooksValid(DWORD wnd);

// ==== MktFindChild (verbatim 2563-2568) ====
static DWORD MktFindChild(DWORD tile, const char* name) {
    DWORD cx = 0;
    reinterpret_cast<FnMktCXStr_t>(Resolve(MktOff::CXStrCtor))(&cx, nullptr, name);
    return reinterpret_cast<DWORD>(reinterpret_cast<FnMktFind_t>(Resolve(MktOff::FindChild))(
        reinterpret_cast<void*>(tile), nullptr, &cx));
}

// ==== MktSetChildText (verbatim 2571-2578) ====
static void MktSetChildText(DWORD tile, const char* childName, const char* text) {
    DWORD child = MktFindChild(tile, childName);
    if (!child) return;
    DWORD tx = 0;
    reinterpret_cast<FnMktCXStr_t>(Resolve(MktOff::CXStrCtor))(&tx, nullptr, text);
    DWORD m = *reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(child) + MktOff::VtSetText);
    reinterpret_cast<FnMktVText_t>(m)(reinterpret_cast<void*>(child), nullptr, &tx);
}

// ==== MktSetWidgetText (verbatim 2581-2587) ====
static void MktSetWidgetText(DWORD widget, const char* text) {
    if (!widget) return;
    DWORD tx = 0;
    reinterpret_cast<FnMktCXStr_t>(Resolve(MktOff::CXStrCtor))(&tx, nullptr, text);
    DWORD m = *reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(widget) + MktOff::VtSetText);
    reinterpret_cast<FnMktVText_t>(m)(reinterpret_cast<void*>(widget), nullptr, &tx);
}

// ==== MktHideChild (verbatim 2590-2595) ====
static void MktHideChild(DWORD tile, const char* childName) {
    DWORD child = MktFindChild(tile, childName);
    if (!child) return;
    DWORD m = *reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(child) + MktOff::VtSetVis);
    reinterpret_cast<FnMktVFlags_t>(m)(reinterpret_cast<void*>(child), nullptr, 0, 1);
}

// ==== MktReadable/Executable/TileLooksValid (verbatim 2603-2627) ====
static bool MktReadable(DWORD p, DWORD size) {
    if (p < 0x10000) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(reinterpret_cast<void*>(p), &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    DWORD end = reinterpret_cast<DWORD>(mbi.BaseAddress) + static_cast<DWORD>(mbi.RegionSize);
    return (p + size) <= end;
}
static bool MktExecutable(DWORD p) {
    if (p < 0x10000) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(reinterpret_cast<void*>(p), &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD)) return false;
    return (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                           PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}
// A node "looks like" one of our live tiles: itself readable, its vtable
// readable, and its SetVisible slot points at real code.
static bool MktTileLooksValid(DWORD wnd) {
    if (!MktReadable(wnd, 0x10)) return false;
    DWORD vt = *reinterpret_cast<DWORD*>(wnd);
    if (!MktReadable(vt + MktOff::VtSetVis, 4)) return false;
    return MktExecutable(*reinterpret_cast<DWORD*>(vt + MktOff::VtSetVis));
}

// ==== MktSafe* (verbatim 2631-2646) ====
static bool MktSafeSetVisible(DWORD wnd, int show) {
    if (!MktTileLooksValid(wnd)) return false;
    DWORD m = *reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(wnd) + MktOff::VtSetVis);
    reinterpret_cast<FnMktVFlags_t>(m)(reinterpret_cast<void*>(wnd), nullptr, show, 1);
    return true;
}
static bool MktSafeHide(DWORD wnd) { return MktSafeSetVisible(wnd, 0); }
// Relayout the grid, validated (the grid vtable method must be real code).
static void MktSafeRelayout(DWORD grid) {
    if (!MktReadable(grid, 4)) return;
    DWORD vt = *reinterpret_cast<DWORD*>(grid);
    if (!MktReadable(vt + MktOff::VtLayout, 4)) return;
    DWORD m = *reinterpret_cast<DWORD*>(vt + MktOff::VtLayout);
    if (!MktExecutable(m)) return;
    reinterpret_cast<FnMktVFlags_t>(m)(reinterpret_cast<void*>(grid), nullptr, 0, 0);
}

// ==== MktHideAllTiles (verbatim 2650-2660) ====
static void MktHideAllTiles(DWORD grid) {
    if (!MktReadable(grid + 0x10, 4)) return;
    DWORD ch = *reinterpret_cast<DWORD*>(grid + 0x10);   // CXWnd child-list head
    int guard = 0;
    while (ch && guard++ < 400) {
        if (!MktReadable(ch + 0x08, 4)) break;           // can't safely read the link -> stop
        DWORD next = *reinterpret_cast<DWORD*>(ch + 0x08);
        if (!MktSafeHide(ch)) break;                     // garbage/foreign node -> list corrupt, stop
        ch = next;
    }
}

// (struct MktItem + g_mktItems / g_mktItemCount / g_mktCollecting were moved ABOVE MktPopulate --
//  the data-driven tree builder references them. See there.)

// ==== g_mktView (verbatim 2691-2693) ====
// Items for the currently selected leaf (rebuilt on category change).
static const MktItem* g_mktView[64];
static int             g_mktViewN = 0;

// ==== MktAddGearBranch def (verbatim 2702-2717) ====
static void MktAddGearBranch(DWORD window, const char* parent, const char* const* fallback, int fbn, bool withMyLevel) {
    if (g_mktItemCount > 0) {
        const char* leaves[48];
        int n = 0;
        if (withMyLevel) leaves[n++] = "  My Level and Below";  // [BOZ] ornaments/mounts skip it (no level requirement -> the tab would be a capped partial)
        for (int i = 0; i < g_mktItemCount && n < 47; i++) {
            if (lstrcmpA(g_mktItems[i].parent, parent) != 0) continue;
            bool seen = false;
            for (int j = 0; j < n; j++)   // j=0: the "My Level" sentinel (if present) never matches a real subcat
                if (lstrcmpA(leaves[j], g_mktItems[i].subcat) == 0) { seen = true; break; }
            if (!seen) leaves[n++] = g_mktItems[i].subcat;
        }
        if (n > (withMyLevel ? 1 : 0)) { MktAddBranch(window, parent, leaves, n); return; }  // had real subcats
    }
    MktAddBranch(window, parent, fallback, fbn);   // feed not loaded yet -> fallback
}

// ==== g_mktPlayerLevel (verbatim 2719-2721) ====
// Player level, pushed by the server as [BOZ_MKT_LEVEL]|<n> alongside the #mkt
// catalog. Drives the "My Level and Below" tab (items with level <= this).
static int             g_mktPlayerLevel = 0;

// ==== g_mktSouls etc (verbatim 2723-2729) ====
// Player currency balances, pushed by the server as [BOZ_FUNDS]|souls|conquest|raid
// alongside the #mkt catalog (and re-pushed after each #mktbuy). The funds display
// morphs to show the selected category's currency + the matching balance.
static int  g_mktSouls      = 0;
static int  g_mktConquest   = 0;
static int  g_mktRaid       = 0;
static int  g_mktPlat       = 0;   // [BOZ] carried Platinum -- the DEFAULT Marketplace currency
static bool g_mktFundsValid = false;

// [BOZ] Generic currency balances, keyed by the catalog currency NAME. The server appends
// these as trailing "<name>=<balance>" fields on [BOZ_FUNDS] (after the four legacy
// positional fields), one per distinct currency the catalog uses. This lets the funds
// display show ANY currency by name -- Platinum, the demo currencies, or an operator's own
// alt-currency (Doubloon, Ebon Crystal, ...) -- not just the four legacy ones. Capacity 40
// is far above any real shop's distinct-currency count.
static char g_mktCurName[40][24];
static int  g_mktCurBal[40];
static int  g_mktCurCount = 0;

// Balance for a currency name, or -1 if the server didn't send a pair for it.
static int MktFundsLookup(const char* name) {
    for (int i = 0; i < g_mktCurCount; ++i)
        if (lstrcmpA(g_mktCurName[i], name) == 0) return g_mktCurBal[i];
    return -1;
}

// ==== WndNotif/buy/selection globals (verbatim 2731-2750) ====
// --- click-to-buy (3d): revived "Buy Now" button + per-instance WndNotification
// hook on the native CMarketplaceWnd. A tile click selects (records the item);
// the revived MKPW_BuyBtn (force-enabled each frame) purchases the selection.
// All proven via Frida 2026-06-04: tile-select fires code 0x20 with the tile (or
// a sub-widget) as the notify child; the Buy button fires code 0x1 when clicked;
// widget+0x1a is the CXWnd Enabled byte.
typedef int (__thiscall *MktWndNotifyFn)(void*, void*, int, void*);
static MktWndNotifyFn g_origMktWndNotif = nullptr;
static void** g_mktVtableCopy = nullptr;
static DWORD  g_mktHookedWnd  = 0;     // window whose vtable[34] we swapped
static DWORD  g_mktBuyBtn     = 0;     // MKPW_BuyBtn widget (cached per open)
static DWORD  g_mktInspectBtn = 0;     // MKPW_DetailsInspect widget (General-tab "Inspect")
static int    g_mktSelIdx     = -1;    // selected grid tile index (-1 = none)
static DWORD  g_mktSelItemId  = 0;     // item_id of the selected tile (0 = none)
static DWORD  g_mktHiLabel    = 0;     // name label of the gold-highlighted tile
static DWORD  g_mktFundsNum   = 0;     // MKPW_AvailableFundsUpper (the balance number)
static DWORD  g_mktFundsLabel = 0;     // the "Current Funds" label -> morphed to currency name
static char   g_mktFundsKey[48] = ""; // last "<currency>|<balance>" shown (change-gate; fits long currency names)
static DWORD  g_mktDetailId  = 0xFFFFFFFF; // selected item whose detail desc/price is shown (change-gate)
static const int       MKT_MAX_TILES = 50;   // grid scrolls beyond what fits (view array holds 64; T3 1H Weapons = 41)

// ==== MktStrip (verbatim 2754-2758) ====
static const char* MktStrip(const char* s) {
    if (!s) return "";
    while (*s == ' ' || *s == '+' || *s == '-') s++;
    return s;
}

// ==== MktLookupCategory (verbatim 2764-2789) ====
static void MktLookupCategory(const char* parent, const char* subcat) {
    g_mktViewN = 0;
    const char* p = MktStrip(parent);
    const char* s = MktStrip(subcat);
    // "My Level and Below": every item in this parent the player can use now
    // (level <= g_mktPlayerLevel), gathered across ALL subcats and sorted by
    // level ascending. Falls back to subcat-exact-match for every other leaf.
    if (lstrcmpA(s, "My Level and Below") == 0) {
        for (int i = 0; i < g_mktItemCount && g_mktViewN < 64; i++) {
            if (p[0] && lstrcmpA(g_mktItems[i].parent, p) != 0) continue;
            if (g_mktItems[i].level > g_mktPlayerLevel)         continue;
            g_mktView[g_mktViewN++] = &g_mktItems[i];
        }
        for (int a = 1; a < g_mktViewN; a++) {        // insertion sort by level (<=64)
            const MktItem* key = g_mktView[a]; int b = a - 1;
            while (b >= 0 && g_mktView[b]->level > key->level) { g_mktView[b + 1] = g_mktView[b]; b--; }
            g_mktView[b + 1] = key;
        }
        return;
    }
    for (int i = 0; i < g_mktItemCount && g_mktViewN < 64; i++) {
        if (lstrcmpA(g_mktItems[i].subcat, s) == 0 &&
            (!p[0] || lstrcmpA(g_mktItems[i].parent, p) == 0))
            g_mktView[g_mktViewN++] = &g_mktItems[i];
    }
}

// ==== g_mktFilledCat etc (verbatim 2794-2799) ====
static char g_mktFilledCat[128] = {0};
static int  g_mktFilledN        = 0;
// Set once a category is fully displayed -> the tick early-outs and stops
// touching the grid, so it never spins on tiles the engine freed on a branch
// collapse. Cleared on category change / corruption / window close.
static bool g_mktDone           = false;

// ==== g_mktIconAnim+MktItemIconAnim (verbatim 2812-2834) ====
static DWORD g_mktIconAnim[MKT_MAX_TILES] = {0};
static DWORD MktItemIconAnim(int idx) {
    if (idx < 0 || idx >= MKT_MAX_TILES) return 0;
    if (!g_mktIconAnim[idx]) {
        DWORD uiMgr = *reinterpret_cast<DWORD*>(Resolve(MktOff::UiMgr));
        if (!uiMgr) return 0;
        DWORD cx = 0;
        reinterpret_cast<FnMktCXStr_t>(Resolve(MktOff::CXStrCtor))(&cx, nullptr, "A_DragItem");
        DWORD src = reinterpret_cast<DWORD>(reinterpret_cast<FnMktAnimFind_t>(
            Resolve(MktOff::AnimFind))(reinterpret_cast<void*>(uiMgr), nullptr, &cx));
        if (!src) return 0;
        DWORD obj = reinterpret_cast<DWORD>(
            reinterpret_cast<FnMktAlloc_t>(Resolve(MktOff::NodeAlloc))(0x4c));
        if (!obj) return 0;
        DWORD copy = reinterpret_cast<DWORD>(reinterpret_cast<FnMktAnimCtor_t>(
            Resolve(MktOff::AnimCtor))(reinterpret_cast<void*>(obj), nullptr));
        if (!copy) return 0;
        reinterpret_cast<FnMktAnimCopy_t>(Resolve(MktOff::AnimCopy))(
            reinterpret_cast<void*>(copy), nullptr, reinterpret_cast<void*>(src));
        g_mktIconAnim[idx] = copy;
    }
    return g_mktIconAnim[idx];
}

// ==== MktFillOneTile (verbatim 2836-2871) ====
static bool MktFillOneTile(DWORD tile, const MktItem* it, int idx) {
    __try {
        // bail early (treated as not-ready) if the label control isn't there yet
        if (!MktFindChild(tile, "MKPW_Item_Label")) return false;
        // (re)show -- tiles get hidden when leaving a leaf for a branch
        { DWORD sm = *reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(tile) + MktOff::VtSetVis);
          reinterpret_cast<FnMktVFlags_t>(sm)(reinterpret_cast<void*>(tile), nullptr, 1, 1); }
        // name (fall back to a placeholder if the feed hasn't arrived)
        char nm[96];
        if (it) { _snprintf(nm, sizeof(nm) - 1, "%s", it->name); nm[sizeof(nm) - 1] = '\0'; }
        else    { _snprintf(nm, sizeof(nm) - 1, "Item %d", idx + 1); nm[sizeof(nm) - 1] = '\0'; }
        MktSetChildText(tile, "MKPW_Item_Label", nm);
        // price
        char pr[24];
        _snprintf(pr, sizeof(pr) - 1, "%lu", it ? (unsigned long)it->price : 0UL); pr[sizeof(pr) - 1] = '\0';
        MktSetChildText(tile, "MKPW_ItemCost_Label", pr);
        // icon -- give this tile its own copy of the A_DragItem atlas (tiles otherwise
        // share one animation object and all show the last item's frame), then set the
        // frame to the real item icon. FUN_006d1c80 subtracts 500, so passing it->icon
        // selects A_DragItem frame (icon-500) = the correct item icon.
        DWORD ic = MktFindChild(tile, "MKPW_Item_Icon");
        if (ic) {
            DWORD anim = MktItemIconAnim(idx);
            if (anim) *reinterpret_cast<DWORD*>(ic + MktOff::IconAnim) = anim;
            reinterpret_cast<FnMktSetIcon_t>(Resolve(MktOff::SetIcon))(ic, it ? (int)it->icon : 500);
        }
        static const char* kHide[] = { "MKPW_New_Label","MKPW_Was_Label","MKPW_OnSaleFor_Label",
            "MKPW_SaleTimeLeft_Label","MKPW_AvailableTimeLeft_Label","MKPW_TimeLeft_Label",
            "MKPW_ItemCost_CreditCardMoneyTypeLabel" };
        for (const char* h : kHide) MktHideChild(tile, h);
        // MKPW_TimeLeft_Label has a hardcoded "Time Left:" caption that the
        // engine re-shows; blank its text too so it can't linger over the tile.
        MktSetChildText(tile, "MKPW_TimeLeft_Label", "");
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ==== MktTickTiles (verbatim 2881-2943) ====
static void MktTickTiles(DWORD window, const char* parent, const char* subcat) {
    DWORD uiMgr = *reinterpret_cast<DWORD*>(Resolve(MktOff::UiMgr));
    DWORD grid  = *reinterpret_cast<DWORD*>(window + MktOff::Grid);
    if (!uiMgr || !grid) return;

    // combined "<parent>/<subcat>" key (stripped) for change-detection
    char key[128];
    _snprintf(key, sizeof(key) - 1, "%s/%s", MktStrip(parent), MktStrip(subcat));
    key[sizeof(key) - 1] = '\0';

    // on category change, rebuild the view + restart the fill from tile 0
    if (lstrcmpA(key, g_mktFilledCat) != 0) {
        lstrcpynA(g_mktFilledCat, key, sizeof(g_mktFilledCat));
        g_mktFilledN = 0;
        MktLookupCategory(parent, subcat);
        // new category -> drop any stale selection + its tile highlight
        if (g_mktHiLabel) { *reinterpret_cast<DWORD*>(g_mktHiLabel + MktOff::NameColor) = 0xFFFFFFFF; g_mktHiLabel = 0; }
        g_mktSelIdx = -1; g_mktSelItemId = 0;
    }
    int target = g_mktViewN; if (target > MKT_MAX_TILES) target = MKT_MAX_TILES;

    // collect ALL the grid's child tiles (the persistent pool, reused across
    // categories). Counting EVERY child -- including a just-made tile that's
    // partial for a frame -- is what stops phase 1 from over-making (over-making
    // exhausts the factory and it starts handing back garbage tiles).
    DWORD existing[64]; int n = 0;
    { DWORD ch = *reinterpret_cast<DWORD*>(grid + 0x10); int g = 0;
      while (ch && n < 64 && g++ < 600) { existing[n++] = ch; ch = *reinterpret_cast<DWORD*>(ch + 0x08); } }

    // phase 1: make exactly one tile per frame until the pool reaches the item
    // count (factory yields one per draw; a fresh tile's children build a frame
    // later -- MktFillOneTile's find-child gate waits for them).
    if (n < target) {
        DWORD tmpl = reinterpret_cast<DWORD>(reinterpret_cast<FnMktTpl_t>(Resolve(MktOff::TplResolve))(
            reinterpret_cast<void*>(uiMgr), nullptr, MktOff::TileTplId));
        DWORD tile = tmpl ? reinterpret_cast<DWORD>(reinterpret_cast<FnMktMakeTile_t>(Resolve(MktOff::MakeTile))(
            reinterpret_cast<void*>(uiMgr), nullptr, reinterpret_cast<void*>(grid), reinterpret_cast<void*>(tmpl))) : 0;
        if (tile) {
            DWORD sm = *reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(tile) + MktOff::VtSetVis);
            reinterpret_cast<FnMktVFlags_t>(sm)(reinterpret_cast<void*>(tile), nullptr, 1, 1);
        }
        return;
    }

    // phase 2: fill the tiles from the cached view, one per frame as each
    // becomes ready; stop at the first not-ready tile and retry next frame.
    bool any = false;
    while (g_mktFilledN < target && g_mktFilledN < n) {
        const MktItem* it = (g_mktFilledN < g_mktViewN) ? g_mktView[g_mktFilledN] : nullptr;
        if (MktFillOneTile(existing[g_mktFilledN], it, g_mktFilledN)) { g_mktFilledN++; any = true; }
        else break;
    }
    // hide any tiles beyond the ones this category uses
    for (int i = target; i < n; i++) {
        DWORD sm = *reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(existing[i]) + MktOff::VtSetVis);
        reinterpret_cast<FnMktVFlags_t>(sm)(reinterpret_cast<void*>(existing[i]), nullptr, 0, 1);
    }
    // relayout the grid only when something actually changed
    if (any) {
        DWORD m = *reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(grid) + MktOff::VtLayout);
        reinterpret_cast<FnMktVFlags_t>(m)(reinterpret_cast<void*>(grid), nullptr, 0, 0);
    }
}

// ==== MktGridIndexOf (verbatim 2949-2961) ====
static int MktGridIndexOf(DWORD window, DWORD target) {
    if (!window || !target) return -1;
    DWORD grid = *reinterpret_cast<DWORD*>(window + MktOff::Grid);
    if (!grid) return -1;
    DWORD ch = *reinterpret_cast<DWORD*>(grid + 0x10);
    int i = 0, guard = 0;
    while (ch && guard++ < 600) {
        if (ch == target) return i;
        ch = *reinterpret_cast<DWORD*>(ch + 0x08);
        i++;
    }
    return -1;
}

// ==== tick fwd decls + populate-state globals (verbatim 2966-2971) ====
static void   SendServerCommand(const char* cmd);   // defined further down
static void   InstallMktVTableHook(DWORD wnd);       // defined with the WndNotification hooks
static bool  g_mktPopulated = false;
static bool  g_mktRequested = false;   // already asked the server for the catalog this open?
static bool  g_mktNeedRepop = false;   // feed arrived after first build -> rebuild gear leaves data-driven
static DWORD g_mktLastSel   = 0;

// ==== TickMarketplace (verbatim 2972-3121) ====
static void TickMarketplace() {
    __try {
        DWORD window = *reinterpret_cast<DWORD*>(Resolve(MktOff::pWnd));
        if (!window || *reinterpret_cast<BYTE*>(window + MktOff::Visible) != 1) {
            g_mktPopulated = false;   // window closed/torn down -> arm for next open
            g_mktLastSel   = 0;
            g_mktFilledCat[0] = '\0'; // reset tile state so a reopen rebuilds clean
            g_mktFilledN   = 0;
            g_mktDone      = false;
            g_mktRequested = false;   // re-arm the catalog request for the next open
            g_mktSelIdx    = -1;      // clear the buy selection so a reopen starts fresh
            g_mktSelItemId = 0;
            g_mktDetailId  = 0xFFFFFFFF;  // re-arm the detail-pane fill for next open
            if (g_mktHiLabel) { *reinterpret_cast<DWORD*>(g_mktHiLabel + MktOff::NameColor) = 0xFFFFFFFF; g_mktHiLabel = 0; }
            if (g_mktFundsKey[0]) {   // restore the generic funds label once on close
                if (g_mktFundsLabel) MktSetWidgetText(g_mktFundsLabel, "Current Funds");
                if (g_mktFundsNum)   MktSetWidgetText(g_mktFundsNum, "");
                g_mktFundsKey[0] = '\0';
            }
            return;
        }
        // Feed arrived after the tree was first built (with the hardcoded fallback)
        // -> rebuild once so the gear leaves come from the live catalog. Reset the
        // category count to 0 so the populate gate below fires a fresh rebuild.
        if (g_mktNeedRepop && g_mktPopulated) {
            g_mktNeedRepop = false;
            *reinterpret_cast<DWORD*>(window + MktOff::CatCount) = 0;
            g_mktPopulated = false;
        }
        DWORD count = *reinterpret_cast<DWORD*>(window + MktOff::CatCount);
        if (!g_mktPopulated && count == 0) {
            MktPopulate(window);
            g_mktPopulated = true;
            g_mktNeedRepop = false;   // tree (re)built with the current feed state
        }
        // On open, if we have no catalog cached yet, ask the server to push it
        // ONCE (#mkt -> Client::SendBozMarketplace). On-demand by design: the
        // ~274-row feed is fetched only by players who open the shop, never on
        // every zone-in (which would burst packets at everyone).
        if (g_mktItemCount == 0 && !g_mktRequested) {
            SendServerCommand("#mkt");
            g_mktRequested = true;
        }
        // [BOZ] Click-to-buy: hook this window's WndNotification once (per-instance
        // vtable swap) so we intercept tile-selects + the Buy button, and revive
        // the native "Buy Now" button (MKPW_BuyBtn) by force-enabling it every
        // frame (the native code keeps it disabled with no Station Cash item).
        if (window != g_mktHookedWnd) {
            InstallMktVTableHook(window);
            g_mktHookedWnd = window;
            g_mktBuyBtn    = MktFindChild(window, "MKPW_BuyBtn");  // cache once per open
            g_mktInspectBtn = MktFindChild(window, "MKPW_DetailsInspect"); // General-tab Inspect
            for (int i = 0; i < MKT_MAX_TILES; i++) g_mktIconAnim[i] = 0; // new window -> rebuild per-tile icon anims
            g_mktFundsNum  = MktFindChild(window, "MKPW_AvailableFundsUpper");
            { DWORD fp = g_mktFundsNum ? (*reinterpret_cast<DWORD*>(g_mktFundsNum + 0xc) - 0x10) : 0;
              g_mktFundsLabel = fp ? *reinterpret_cast<DWORD*>(fp + 0x10) : 0; }  // child[0] = "Current Funds"
            g_mktFundsKey[0] = '\0';
            g_mktSelIdx    = -1;
            g_mktSelItemId = 0;
        }
        if (g_mktBuyBtn) *reinterpret_cast<BYTE*>(g_mktBuyBtn + 0x1a) = 1;
        if (g_mktInspectBtn) *reinterpret_cast<BYTE*>(g_mktInspectBtn + 0x1a) = 1;  // keep Inspect enabled
        // Item tiles: read the highlighted row from the category tree
        // (win+0x25c is unreliable for our custom nodes -> resolves to "All Items").
        // We run the tile maintenance EVERY frame so the pool can build up one
        // tile per frame (the factory yields one per draw) -- not just on change.
        DWORD tree = *reinterpret_cast<DWORD*>(window + MktOff::TreeCtrl);
        DWORD selItem = 0;
        const char* parentName = "";
        if (tree) {
            int lc = *reinterpret_cast<int*>(tree + MktOff::TreeCount);
            int si = *reinterpret_cast<int*>(tree + MktOff::TreeSelIdx);
            DWORD la = *reinterpret_cast<DWORD*>(tree + MktOff::TreeArr);
            if (la && si >= 0 && si < lc) {
                selItem = *reinterpret_cast<DWORD*>(la + si * MktOff::TreeStride + MktOff::TreeLineNode);
                // walk backwards to the nearest BRANCH line = this leaf's parent
                // (the tree is exactly 2 levels, so the closest branch is it).
                for (int k = si - 1; k >= 0; k--) {
                    DWORD node = *reinterpret_cast<DWORD*>(la + k * MktOff::TreeStride + MktOff::TreeLineNode);
                    if (node && *reinterpret_cast<DWORD*>(node) == Resolve(MktOff::BranchVtable)) {
                        const char* pn = *reinterpret_cast<const char**>(node + MktOff::BranchLabel);
                        if (pn) parentName = pn;
                        break;
                    }
                }
            }
        }
        // gate on the CTreeViewNode leaf vtable (folders are branches)
        const char* leafName = nullptr;
        if (selItem && *reinterpret_cast<DWORD*>(selItem) == Resolve(MktOff::LeafVtable)) {
            const char* name = *reinterpret_cast<const char**>(selItem + MktOff::TreeItemLabel);
            if (name && name[0] && lstrcmpA(name, "New and Featured") != 0) leafName = name;
        }
        if (leafName) {
            MktTickTiles(window, parentName, leafName);
        } else if (g_mktItemCount > 0) {
            // No real leaf selected (fresh open / "New and Featured" home) -> land
            // on the player's usable items: Cross-Class Spells "My Level and Below".
            MktTickTiles(window, "Cross-Class Spells", "My Level and Below");
        } else if (g_mktFilledCat[0]) {
            // catalog not loaded yet and we'd shown something -> blank the grid
            MktHideAllTiles(*reinterpret_cast<DWORD*>(window + MktOff::Grid));
            g_mktFilledCat[0] = '\0';
        }
        // Keep the selected tile's name gold -- re-applied each frame so it
        // survives any engine/fill repaint of the label color.
        if (g_mktHiLabel)
            *reinterpret_cast<DWORD*>(g_mktHiLabel + MktOff::NameColor) = 0xFFFFD700;
        // 3c: morph "Current Funds" -> the current category's currency name, and the
        // amount -> the player's balance of it. Set only on change (no per-frame text
        // alloc); restore the generic label when off the shopping leaves.
        if (g_mktFundsValid && g_mktFundsLabel && g_mktFundsNum) {
            if (leafName && g_mktViewN > 0 && g_mktView[0]) {
                // Follow the SELECTED tile's currency so mixed-currency categories show the
                // right funds; fall back to the first tile's currency before anything's picked.
                const MktItem* fundsIt = (g_mktSelIdx >= 0 && g_mktSelIdx < g_mktViewN && g_mktView[g_mktSelIdx])
                                       ? g_mktView[g_mktSelIdx] : g_mktView[0];
                const char* cur = fundsIt->currency;
                // Prefer the generic funds map (any currency by name); fall back to the
                // four legacy positional balances for older servers that don't send pairs.
                int bal = MktFundsLookup(cur);
                if (bal < 0) {
                    if      (lstrcmpA(cur, "Platinum") == 0)        bal = g_mktPlat;
                    else if (lstrcmpA(cur, "Souls") == 0)           bal = g_mktSouls;
                    else if (lstrcmpA(cur, "Conquest Points") == 0) bal = g_mktConquest;
                    else if (lstrcmpA(cur, "Raid Tokens") == 0)     bal = g_mktRaid;
                    else bal = 0;
                }
                char key[48]; _snprintf(key, sizeof(key) - 1, "%s|%d", cur, bal); key[sizeof(key) - 1] = '\0';
                if (lstrcmpA(key, g_mktFundsKey) != 0) {
                    char num[16]; _snprintf(num, sizeof(num) - 1, "%d", bal); num[sizeof(num) - 1] = '\0';
                    // "Conquest Points" overflows the funds label; show a short form.
                    const char* disp = (lstrcmpA(cur, "Conquest Points") == 0) ? "Conquest Pts" : cur;
                    MktSetWidgetText(g_mktFundsLabel, disp);
                    MktSetWidgetText(g_mktFundsNum, num);
                    lstrcpynA(g_mktFundsKey, key, sizeof(g_mktFundsKey));
                }
            } else if (g_mktFundsKey[0]) {
                MktSetWidgetText(g_mktFundsLabel, "Current Funds");
                MktSetWidgetText(g_mktFundsNum, "");
                g_mktFundsKey[0] = '\0';
            }
        }
        // Detail pane: fill the selected item's description + price -- the native
        // panel leaves these blank for our custom tiles. Re-set only on selection
        // change (gated on the selected item id).
        if (g_mktSelItemId != g_mktDetailId) {
            g_mktDetailId = g_mktSelItemId;
            if (g_mktSelIdx >= 0 && g_mktSelIdx < g_mktViewN && g_mktView[g_mktSelIdx]) {
                const MktItem* it = g_mktView[g_mktSelIdx];
                DWORD dw = MktFindChild(window, "MKPW_AdditionalDetails");
                if (dw) MktSetWidgetText(dw, it->desc);
                char pr[24]; _snprintf(pr, sizeof(pr) - 1, "%lu", (unsigned long)it->price);
                pr[sizeof(pr) - 1] = '\0';
                DWORD pw = MktFindChild(window, "MKPW_BuyPrice");
                if (pw) MktSetWidgetText(pw, pr);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ==== ParseBozMkt (verbatim 3160-3185) ====
static void ParseBozMkt(const char* payload) {
    if (g_mktItemCount >= (int)(sizeof(g_mktItems) / sizeof(g_mktItems[0]))) return;
    MktItem& it = g_mktItems[g_mktItemCount];
    memset(&it, 0, sizeof(it));
    char buf[32];
    payload = CopyField(payload, it.parent,   sizeof(it.parent));
    payload = CopyField(payload, it.subcat,   sizeof(it.subcat));
    payload = CopyField(payload, buf, sizeof(buf));        it.item_id = (DWORD)atoi(buf);
    payload = CopyField(payload, it.name,     sizeof(it.name));
    payload = CopyField(payload, buf, sizeof(buf));        it.icon    = (DWORD)atoi(buf);
    payload = CopyField(payload, buf, sizeof(buf));        it.price   = (DWORD)atoi(buf);
    payload = CopyField(payload, it.currency, sizeof(it.currency));
    payload = CopyField(payload, buf, sizeof(buf));        it.level   = atoi(buf);
    payload = CopyField(payload, it.desc,     sizeof(it.desc));
    // [BOZ] Optional trailing fields appended to the wire row for the 3D preview:
    // |<model>|<pslot>|<color>. Absent on older feeds -> model 0 / pslot -1 / natural tint.
    payload = CopyField(payload, buf, sizeof(buf));        it.model = atoi(buf);
    payload = CopyField(payload, buf, sizeof(buf));        it.pslot = buf[0] ? atoi(buf) : -1;
    // Tint is hex 0xAARRGGBB. Guard the PARSED value, not just presence: a DB color of 0
    // (or an empty/old feed) must fall back to 0xFF000000 (natural), never 0x00000000
    // (fully-transparent), which would render the preview slot invisible/black.
    payload = CopyField(payload, buf, sizeof(buf));
    DWORD parsedColor = buf[0] ? (DWORD)strtoul(buf, nullptr, 16) : 0;
    it.color = parsedColor ? parsedColor : 0xFF000000u;
    g_mktItemCount++;
}

// ==== preview section header (verbatim 4742-4755) ====
// ---- Marketplace 3D item preview -------------------------------------------
// Show the local player's avatar in the Marketplace preview pane, wearing the
// selected item. Reverse-engineered 2026-06-14/15 (see memory
// project_marketplace_preview_recon). The whole native preview machinery (render
// viewport, camera, pan/rotate/zoom) is already built into CMarketplaceWnd; the
// missing piece was driving it. We use the engine's OWN paper-doll, which never
// touches the in-world player:
//   1. FUN_00576ca0(viewport) clones the local player into the preview viewport on
//      a FRESH, independent actor with full appearance (== char-window 3D avatar).
//   2. FUN_005923f0(clone, slot, "ITxxxx", 0, 1) overlays the ornament's held model
//      onto the CLONE only -- its packet path is gated to the live player, so on a
//      clone it's a render-only swap.
// (Earlier we poked the LIVE player's equip array + CreatePlayerActor-rebuilt it;
// that dropped the torso material + held weapon and leaked ghost clones -- gone.)

// ==== preview typedefs (verbatim 4756-4761) ====
typedef DWORD (__thiscall *MktPaperDollFn)(void* viewport, void* dummy);  // FUN_00576ca0
typedef char  (__thiscall *MktSetWeaponFn)(void* spawn, unsigned char slot,
                                           const char* idfile, int p3, char applyTint);  // FUN_005923f0
typedef char  (__thiscall *MktApplyFn)(void* spawn, const void* data, char flag);        // FUN_00596b20
typedef void  (__thiscall *MktSetPreviewFn)(void* engine, void* actor, void* region);    // engine vtbl+0x110
typedef void  (__thiscall *MktActorReapFn)(void* cd, void* oldActor);                     // FUN_00490a10 (anti-ghost)

// ==== preview state globals (verbatim 4763-4774) ====
// Debounce + re-entrancy guard. The native grid fires selection notifications
// REPEATEDLY (every hover/refresh, not just on click). One refresh = one paper-doll
// render; running it per-notification is wasteful, so only refresh when the selected
// item actually CHANGES, and never re-enter mid-render. (Reset on category change so
// reopening/switching still previews.)
static DWORD g_mktPreviewLastId = 0xFFFFFFFF;
static bool  g_mktPreviewBusy   = false;

// ENABLED 2026-06-15 (clean native-paper-doll rewrite). The live player is never
// touched: FUN_00576ca0 renders a fresh independent clone, FUN_005923f0 overlays the
// ornament on that clone only. Flip false to disable. See project_marketplace_preview_recon.
static bool g_mktPreviewEnabled = true;

// Re-hang the held weapons (slots 7/8) from their untouched render models. Has its OWN SEH so it
// can be called from the rebuild's __finally below, where a lexical __except is illegal (C2702).
static void MktReattachWeapons(DWORD player) {
    auto setWeapon = reinterpret_cast<MktSetWeaponFn>(Resolve(Off::FnSetWeaponSlot));
    if (!setWeapon) return;
    __try {
        for (int w = 7; w <= 8; ++w) {
            DWORD wm = *reinterpret_cast<DWORD*>(player + 0xf30 + w * 0x14);   // int[0] = weapon IT model #
            if (!wm) continue;
            char idf[16];
            _snprintf(idf, sizeof(idf) - 1, "IT%lu", (unsigned long)wm);
            idf[sizeof(idf) - 1] = '\0';
            setWeapon(reinterpret_cast<void*>(player), (unsigned char)w, idf, 1, 1);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ==== MktRenderCloneBareRebuild (verbatim 4796-4853) ====
static void MktRenderCloneBareRebuild(DWORD player, void* vp, MktPaperDollFn paperDoll,
                                      unsigned bareMask, unsigned repaintMask, DWORD model, DWORD tint) {
    DWORD cd           = *reinterpret_cast<DWORD*>(Resolve(Off::GblCDisplay));
    auto  createActor  = reinterpret_cast<FnCreateActor_t>(Resolve(Off::FnCreatePlayerActor));
    auto  reapActor    = reinterpret_cast<MktActorReapFn>(Resolve(Off::FnDisplayActorCleanup));
    const DWORD vpd    = reinterpret_cast<DWORD>(vp);

    if (!cd || !createActor) {                 // can't rebuild -> plain clone+repaint (6/7)
        __try { paperDoll(vp, (void*)0); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        DWORD c = 0;
        __try { c = *reinterpret_cast<DWORD*>(vpd + 0x1e0); } __except (EXCEPTION_EXECUTE_HANDLER) { c = 0; }
        if (c) for (int s = 0; s < 7; ++s) if (repaintMask & (1u << s))
            ArmorOrn::PreviewApply(reinterpret_cast<void*>(c), (unsigned int)s, model, tint);
        return;
    }

    DWORD saveF[7][5], saveD[7][5], saveT[7];
    for (int s = 0; s < 7; ++s) {
        for (int i = 0; i < 5; ++i) {
            saveF[s][i] = *reinterpret_cast<DWORD*>(player + 0xf30  + s * 0x14 + i * 4);
            saveD[s][i] = *reinterpret_cast<DWORD*>(player + 0x1da0 + s * 0x14 + i * 4);
        }
        saveT[s] = *reinterpret_cast<DWORD*>(player + 0xefc + s * 4);
    }
    for (int s = 0; s < 7; ++s) if (bareMask & (1u << s)) {   // bare the masked slots
        for (int i = 0; i < 5; ++i) {
            *reinterpret_cast<DWORD*>(player + 0xf30  + s * 0x14 + i * 4) = 0;
            *reinterpret_cast<DWORD*>(player + 0x1da0 + s * 0x14 + i * 4) = 0;
        }
        *reinterpret_cast<DWORD*>(player + 0xefc + s * 4) = 0;
    }
    __try {
        DWORD a0 = *reinterpret_cast<DWORD*>(player + 0x101c);
        createActor(reinterpret_cast<void*>(cd), reinterpret_cast<int*>(player), (void*)0, 1, 2,
                    reinterpret_cast<unsigned int*>(1), 1);   // p5 = 0x1 (a flag sentinel, NOT a deref'd ptr)
        DWORD a1 = *reinterpret_cast<DWORD*>(player + 0x101c);
        if (reapActor && a0 && a0 != a1) reapActor(reinterpret_cast<void*>(cd), reinterpret_cast<void*>(a0));

        DWORD clone = 0;
        __try { paperDoll(vp, (void*)0); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        __try { clone = *reinterpret_cast<DWORD*>(vpd + 0x1e0); } __except (EXCEPTION_EXECUTE_HANDLER) { clone = 0; }
        if (clone) for (int s = 0; s < 7; ++s) if (repaintMask & (1u << s))
            ArmorOrn::PreviewApply(reinterpret_cast<void*>(clone), (unsigned int)s, model, tint);
    } __finally {
        for (int s = 0; s < 7; ++s) {          // restore worn gear
            for (int i = 0; i < 5; ++i) {
                *reinterpret_cast<DWORD*>(player + 0xf30  + s * 0x14 + i * 4) = saveF[s][i];
                *reinterpret_cast<DWORD*>(player + 0x1da0 + s * 0x14 + i * 4) = saveD[s][i];
            }
            *reinterpret_cast<DWORD*>(player + 0xefc + s * 4) = saveT[s];
        }
        DWORD a2 = *reinterpret_cast<DWORD*>(player + 0x101c);
        createActor(reinterpret_cast<void*>(cd), reinterpret_cast<int*>(player), (void*)0, 1, 2,
                    reinterpret_cast<unsigned int*>(1), 1);   // p5 = 0x1 (a flag sentinel, NOT a deref'd ptr)
        DWORD a3 = *reinterpret_cast<DWORD*>(player + 0x101c);
        if (reapActor && a2 && a2 != a3) reapActor(reinterpret_cast<void*>(cd), reinterpret_cast<void*>(a2));

        // [BOZ 2026-06-19] The direct CreatePlayerActor rebuild restores the equip DATA above but
        // drops the VISUAL of the held weapons (7/8) + item-bound armor -- WHICH slots depends on
        // what's worn (a normal suit drops legs/feet; a robe also drops the welded chest/arms).
        // Rather than guess, re-render EVERY visible armor slot from its saved material/tint
        // (re-applying a slot that's fine is idempotent; the chest's robe material re-spreads to
        // the sleeves) + re-attach the weapons -> a preview always leaves the live player whole,
        // for every wearer. Local-only, SEH-guarded (ArmorOrn::PreviewApply guards internally).
        for (int s = 0; s < 7; ++s)
            ArmorOrn::PreviewApply(reinterpret_cast<void*>(player), (unsigned)s, saveF[s][0], saveT[s]);
        MktReattachWeapons(player);   // re-hang held weapons (own SEH -- __except can't be in this __finally)
    }
}

// ==== MktRenderClone (verbatim 4873-4910) ====
static void MktRenderClone(DWORD player, void* vp, MktPaperDollFn paperDoll,
                           int ps, DWORD model, DWORD tint) {
    // [BOZ 2026-06-16] Cases where worn gear's item-bound CHEST mesh resists a poke go through the
    // bare-rebuild helper (see MktRenderCloneBareRebuild above):
    //   * FULL SUIT (ps 9): bare+repaint all 7 (0x7F/0x7F).
    //   * ROBE (ps 1): bare chest/arms/wrists (0x0E), repaint the chest (0x02) -- FUN_00594E50
    //     spreads the robe to the sleeves + renders its model; worn helm/hands/legs/feet stay.
    if (ps == 9) { MktRenderCloneBareRebuild(player, vp, paperDoll, 0x7Fu, 0x7Fu, model, tint); return; }
    if (ps == 1) {
        // [BOZ 2026-06-19] Pick the bare mask by material. A ROBE (material 10-16) covers the
        // whole upper body and FUN_00594E50 spreads it to the sleeves -> bare chest+arms+wrists
        // (0x0E). A NON-robe chest only occupies the chest slot -> bare ONLY the chest (0x02) so
        // the worn arms + wrists stay; baring them left non-robe chest previews with empty forearms.
        const unsigned bare = (model >= 10 && model <= 16) ? 0x0Eu : 0x02u;
        MktRenderCloneBareRebuild(player, vp, paperDoll, bare, 0x02u, model, tint);
        return;
    }

    // ---- single armor slot (ps 0,2-6) or weapon (ps 7-8): poke the player's render arrays,
    // build the clone, restore. (These work over worn gear today.)
    int slots[7];
    int nslots = 0;
    slots[nslots++] = ps;
    // Poke each slot's MATERIAL (int[0] in both equip arrays player+0xf30 / +0x1da0) and the
    // per-slot TINT at player+0xefc+slot*4 (the field FUN_005a4360 feeds to FUN_00594E50 for
    // a PC spawn; the equip int[4] is "new_armor_type", not a tint). Restored in the __finally.
    DWORD addrs[24];
    DWORD olds[24];
    int np = 0;
    for (int s = 0; s < nslots; ++s) {
        DWORD a  = player + 0xf30  + (DWORD)slots[s] * 0x14;   // equip int[0] (material)
        DWORD b  = player + 0x1da0 + (DWORD)slots[s] * 0x14;   // parallel copy
        DWORD ct = player + 0xefc  + (DWORD)slots[s] * 4;      // per-slot tint (color)
        addrs[np] = a;  olds[np] = *reinterpret_cast<DWORD*>(a);  ++np;
        addrs[np] = b;  olds[np] = *reinterpret_cast<DWORD*>(b);  ++np;
        addrs[np] = ct; olds[np] = *reinterpret_cast<DWORD*>(ct); ++np;
        *reinterpret_cast<DWORD*>(a)  = model;
        *reinterpret_cast<DWORD*>(b)  = model;
        *reinterpret_cast<DWORD*>(ct) = tint;
    }
    __try {
        paperDoll(vp, (void*)0);
    } __finally {
        for (int i = 0; i < np; ++i) *reinterpret_cast<DWORD*>(addrs[i]) = olds[i];
    }
}

// ==== g_mktRobeHintShown+MktMaybeHint (verbatim 4919-4942) ====
static bool g_mktRobeHintShown = false;
static void MktMaybeHint(DWORD player, const MktItem* it) {
    // NOTE: dsp_chat (FUN_0051F1A0) is a FREE function -- it takes (text,color,echo,
    // brackets) on the stack and uses the global chat mgr (DAT_00e67ccc); the `this`
    // our __thiscall typedef passes is burned in ECX and ignored. So do NOT gate on
    // g_dspChatThis -- it's a captured-garbage ECX that is null here and was silently
    // swallowing the hint. (Verified via direct call 2026-06-15.) g_dspChatThis stays
    // as the ignored first arg only to satisfy the typedef.
    // Gate per-hint (NOT one shared early-return): a shown robe hint must not block the
    // full-suit hint from ever firing. echo=1 is REQUIRED to actually paint the line.
    if (!it || !g_orig_DspChat) return;
    if (it->pslot == 1 && !g_mktRobeHintShown) {                     // previewing a chest
        DWORD curChest = *reinterpret_cast<DWORD*>(player + 0xf44);  // render slot 1 int0
        if (curChest >= 10 && curChest <= 16) {                      // a robe is equipped
            g_orig_DspChat(g_dspChatThis, const_cast<char*>(
                "[Marketplace] Unequip your robe to preview chest armor accurately."),
                0xd, 1, 1);
            g_mktRobeHintShown = true;
        }
    }
    // [BOZ 2026-06-16] The full-suit (pslot 9) "unequip your armor" hint was REMOVED: the
    // full-suit preview now repaints the clone's actor directly (MktRenderClone ps==9 ->
    // ArmorOrn::PreviewApply), so it renders correctly WITH gear on -- no unequip needed.
}

// ==== MktShowPreview (verbatim 4944-4964) ====
static void MktShowPreview(DWORD window, const MktItem* it) {
    if (!g_mktPreviewEnabled) return;
    DWORD id = it ? it->item_id : 0;
    if (g_mktPreviewBusy || id == g_mktPreviewLastId) return;
    g_mktPreviewBusy   = true;
    g_mktPreviewLastId = id;
    __try {
        DWORD player = *reinterpret_cast<DWORD*>(Resolve(Off::GblPreviewSubject));
        DWORD vp     = *reinterpret_cast<DWORD*>(window + 0x4cc);   // preview viewport
        if (player && vp) {
            auto paperDoll = reinterpret_cast<MktPaperDollFn>(Resolve(Off::FnPaperDollPreview));
            if (it && it->model > 0 && it->pslot >= 0) {
                MktRenderClone(player, (void*)vp, paperDoll, it->pslot, (DWORD)it->model, it->color);
            } else {
                paperDoll((void*)vp, (void*)0);   // bare avatar (dummy arg balances RET 4)
            }
            MktMaybeHint(player, it);   // one-time robe / 2-hander preview-limit hints
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_mktPreviewBusy = false;   // ALWAYS clear -- on every path incl. exception
}

// ==== MyHook_MktWndNotification (verbatim 4975-5037) ====
static int __fastcall MyHook_MktWndNotification(void* this_ecx, void* /*edx*/,
                                                void* child, int code, void* data) {
    DWORD notify = reinterpret_cast<DWORD>(child);
    DWORD window = reinterpret_cast<DWORD>(this_ecx);

    // Revived Buy button activated -> purchase the selection, swallow the click.
    if (notify && notify == g_mktBuyBtn && code == 0x1) {
        if (g_mktSelItemId != 0) {
            char cmd[48];
            _snprintf(cmd, sizeof(cmd) - 1, "#mktbuy %u", g_mktSelItemId);
            cmd[sizeof(cmd) - 1] = '\0';
            SendServerCommand(cmd);
        }
        return 0;   // do NOT call orig -- the native purchase handler is dead
    }

    // Inspect button activated -> open the native item-view window for the
    // selected tile (server replies with ItemPacketViewLink). Swallow the click;
    // the native Inspect path expects a Station Cash item we don't have.
    if (notify && notify == g_mktInspectBtn && code == 0x1) {
        if (g_mktSelItemId != 0) {
            char cmd[48];
            _snprintf(cmd, sizeof(cmd) - 1, "#mktview %u", g_mktSelItemId);
            cmd[sizeof(cmd) - 1] = '\0';
            SendServerCommand(cmd);
        }
        return 0;
    }

    // Everything else: run the native handler first (incl. its detail-pane fill),
    // then layer our selection state + feedback on top.
    int r = g_origMktWndNotif ? g_origMktWndNotif(this_ecx, child, code, data) : 0;
    if (notify && code == 0x20) {   // a tile was selected
        __try {
            DWORD tile = 0;
            int idx = MktGridIndexOf(window, notify);
            if (idx >= 0) {
                tile = notify;                       // notify IS the tile container
            } else {                                 // notify was a sub-widget
                DWORD parent = *reinterpret_cast<DWORD*>(notify + 0xc) - 0x10;
                idx = MktGridIndexOf(window, parent);
                if (idx >= 0) tile = parent;
            }
            if (idx >= 0 && idx < g_mktViewN && g_mktView[idx] && tile) {
                g_mktSelIdx    = idx;
                g_mktSelItemId = g_mktView[idx]->item_id;
                MktSetChildText(window, "MKPW_DetailsName", g_mktView[idx]->name);
                // [BOZ] Drive the 3D preview: show our avatar wearing this item.
                MktShowPreview(window, g_mktView[idx]);
                // Highlight: the name label is the tile container's first child.
                // Restore the previously-selected name to white, remember this one
                // -- TickMarketplace re-applies the gold each frame.
                DWORD label = *reinterpret_cast<DWORD*>(tile + 0x10);
                if (label && label != g_mktHiLabel) {
                    if (g_mktHiLabel)
                        *reinterpret_cast<DWORD*>(g_mktHiLabel + MktOff::NameColor) = 0xFFFFFFFF;
                    g_mktHiLabel = label;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return r;
}

// ==== InstallMktVTableHook (verbatim 5039-5066) ====
static void InstallMktVTableHook(DWORD wnd) {
    if (!wnd) return;
    void** origVtable = *reinterpret_cast<void***>(wnd);
    if (!origVtable) return;
    constexpr int kVtableSlots = 256;
    if (g_mktVtableCopy) {
        HeapFree(GetProcessHeap(), 0, g_mktVtableCopy);
        g_mktVtableCopy = nullptr;
    }
    g_mktVtableCopy = static_cast<void**>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, kVtableSlots * sizeof(void*)));
    if (!g_mktVtableCopy) {
        Log::Write("[BOZ-RoF2] InstallMktVTableHook: HeapAlloc failed");
        return;
    }
    __try {
        memcpy(g_mktVtableCopy, origVtable, kVtableSlots * sizeof(void*));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HeapFree(GetProcessHeap(), 0, g_mktVtableCopy);
        g_mktVtableCopy = nullptr;
        return;
    }
    g_origMktWndNotif   = reinterpret_cast<MktWndNotifyFn>(origVtable[34]);
    g_mktVtableCopy[34] = reinterpret_cast<void*>(&MyHook_MktWndNotification);
    *reinterpret_cast<void***>(wnd) = g_mktVtableCopy;
    Log::Write("[BOZ-RoF2] InstallMktVTableHook: slot 34 -> MyHook_MktWndNotification (orig 0x%p)",
               reinterpret_cast<void*>(g_origMktWndNotif));
}

// ==== MyHook_PwDraw (forked: keep g_orig + TickMarketplace) ====
static int __fastcall MyHook_PwDraw(void* this_ecx, void* /*edx_dummy*/) {
    int ret = 0;
    if (g_orig_PwDraw) ret = g_orig_PwDraw(this_ecx);
    TickMarketplace();   // inject categories/tiles/funds when the window is open
    return ret;
}

// ==== InstallHook_PwDraw (verbatim 2057-2106) ====
static bool InstallHook_PwDraw() {
    if (g_hookPwDraw_installed) return true;
    DWORD target = Resolve(Off::FnPlayerWndDraw);
    if (!IsReadable(target, 10)) {
        Log::Write("[BOZ-RoF2] HookPwDraw: target @ 0x%08X not readable", target);
        return false;
    }
    auto* o = reinterpret_cast<BYTE*>(target);
    // Prologue: PUSH ESI; MOV ESI,ECX; MOVZX EAX,byte ptr [ESI+0x280]
    if (!(o[0]==0x56 && o[1]==0x8B && o[2]==0xF1 && o[3]==0x0F && o[4]==0xB6 &&
          o[5]==0x86 && o[6]==0x80 && o[7]==0x02 && o[8]==0x00 && o[9]==0x00)) {
        Log::Write("[BOZ-RoF2] HookPwDraw: unexpected prologue "
                   "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X -- abort",
                   o[0],o[1],o[2],o[3],o[4],o[5],o[6],o[7],o[8],o[9]);
        return false;
    }
    // Trampoline: 10 stolen bytes + 5-byte JMP back to target+10.
    BYTE* tramp = static_cast<BYTE*>(VirtualAlloc(
        nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!tramp) {
        Log::Write("[BOZ-RoF2] HookPwDraw: VirtualAlloc failed (err=%lu)", GetLastError());
        return false;
    }
    memcpy(tramp, o, 10);
    tramp[10] = 0xE9;
    *reinterpret_cast<DWORD*>(tramp + 11) =
        static_cast<DWORD>((target + 10) - (reinterpret_cast<DWORD>(tramp) + 15));
    g_orig_PwDraw = reinterpret_cast<FnPwDraw_t>(tramp);
    // Patch target: 5-byte JMP to MyHook + 5 NOPs (over the 10-byte stolen region).
    DWORD oldProt = 0;
    if (!VirtualProtect(reinterpret_cast<LPVOID>(target), 10, PAGE_EXECUTE_READWRITE, &oldProt)) {
        Log::Write("[BOZ-RoF2] HookPwDraw: VirtualProtect failed (err=%lu)", GetLastError());
        VirtualFree(tramp, 0, MEM_RELEASE);
        g_orig_PwDraw = nullptr;
        return false;
    }
    BYTE patch[10];
    patch[0] = 0xE9;
    *reinterpret_cast<DWORD*>(patch + 1) =
        static_cast<DWORD>(reinterpret_cast<DWORD>(&MyHook_PwDraw) - (target + 5));
    for (int i = 5; i < 10; ++i) patch[i] = 0x90;
    memcpy(reinterpret_cast<LPVOID>(target), patch, 10);
    DWORD tmp = 0;
    VirtualProtect(reinterpret_cast<LPVOID>(target), 10, oldProt, &tmp);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPVOID>(target), 10);
    g_hookPwDraw_installed = true;
    Log::Write("[BOZ-RoF2] HookPwDraw: INSTALLED @ 0x%08X -> 0x%p (trampoline @ 0x%p)",
               target, reinterpret_cast<void*>(&MyHook_PwDraw), tramp);
    return true;
}

// ==== MyHook_DspChat (forked: marketplace feed branches only) ====
static void __fastcall MyHook_DspChat(void* this_ecx, void* /*edx_dummy*/,
                                       char* text, int color, int echo, char brackets) {
    if (!g_dspChatThis) g_dspChatThis = this_ecx;
    if (text && text[0] == '[' && text[1] == 'B' && text[2] == 'O' && text[3] == 'Z' && text[4] == '_') {
        if (strncmp(text, "[BOZ_MKT_START]", 15) == 0) {
            g_mktItemCount = 0;
            g_mktCollecting = true;
            return;
        }
        if (strncmp(text, "[BOZ_MKT_LEVEL]|", 16) == 0) {
            g_mktPlayerLevel = atoi(text + 16);
            return;
        }
        if (strncmp(text, "[BOZ_MKT_END]", 13) == 0) {
            g_mktCollecting = false;
            g_mktFilledCat[0] = '\0';
            g_mktNeedRepop  = true;
            Log::Write("[BOZMkt] [BOZ_MKT] catalog = %d items", g_mktItemCount);
            return;
        }
        if (strncmp(text, "[BOZ_MKT]|", 10) == 0) {
            ParseBozMkt(text + 10);
            return;
        }
        if (strncmp(text, "[BOZ_FUNDS]|", 12) == 0) {
            const char* p = text + 12;
            char a[16], b[16], cc[16], dd[16];
            p = CopyField(p, a,  sizeof(a));
            p = CopyField(p, b,  sizeof(b));
            p = CopyField(p, cc, sizeof(cc));
            p = CopyField(p, dd, sizeof(dd));   // [BOZ] 4th field = carried Platinum (0 if an older server omits it)
            g_mktSouls      = atoi(a);
            g_mktConquest   = atoi(b);
            g_mktRaid       = atoi(cc);
            g_mktPlat       = atoi(dd);
            // [BOZ] Any remaining fields are generic "<currency>=<balance>" pairs -- one per
            // distinct currency the catalog uses. Parse them into the funds map so the display
            // can show ANY currency by name, not just the four legacy fields above.
            g_mktCurCount = 0;
            while (*p && g_mktCurCount < 40) {
                char field[48];
                p = CopyField(p, field, sizeof(field));
                char* eq = strchr(field, '=');
                if (eq) {
                    *eq = '\0';
                    lstrcpynA(g_mktCurName[g_mktCurCount], field, sizeof(g_mktCurName[0]));
                    g_mktCurBal[g_mktCurCount] = atoi(eq + 1);
                    ++g_mktCurCount;
                }
            }
            g_mktFundsValid = true;
            return;
        }
    }
    if (g_orig_DspChat) g_orig_DspChat(this_ecx, text, color, echo, brackets);
}

// ==== InstallHook_DspChat (verbatim 3909-3964) ====
static bool InstallHook_DspChat() {
    if (g_hookDspChatInstalled) return true;
    DWORD target = Resolve(Off::FnDspChat);
    if (!IsReadable(target, 6)) {
        Log::Write("[BOZ-RoF2] HookDspChat: target @ 0x%08X not readable", target);
        return false;
    }

    // Sanity: first instruction must be `MOV EAX, FS:[0x0]` (64 A1 00 00 00 00).
    auto* origBytes = reinterpret_cast<BYTE*>(target);
    if (origBytes[0] != 0x64 || origBytes[1] != 0xA1 ||
        origBytes[2] != 0x00 || origBytes[3] != 0x00 ||
        origBytes[4] != 0x00 || origBytes[5] != 0x00) {
        Log::Write("[BOZ-RoF2] HookDspChat: unexpected prologue %02X %02X %02X %02X %02X %02X (want 64 A1 00 00 00 00) — abort",
                   origBytes[0], origBytes[1], origBytes[2], origBytes[3], origBytes[4], origBytes[5]);
        return false;
    }

    BYTE* tramp = static_cast<BYTE*>(VirtualAlloc(
        nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!tramp) {
        Log::Write("[BOZ-RoF2] HookDspChat: VirtualAlloc failed (err=%lu)", GetLastError());
        return false;
    }

    // Steal 6 bytes; trampoline does stolen + JMP back to target+6.
    memcpy(tramp, origBytes, 6);
    tramp[6] = 0xE9;
    DWORD relBack = static_cast<DWORD>((target + 6) - (reinterpret_cast<DWORD>(tramp) + 11));
    *reinterpret_cast<DWORD*>(tramp + 7) = relBack;
    g_orig_DspChat = reinterpret_cast<FnDspChat_t>(tramp);

    DWORD oldProt = 0;
    if (!VirtualProtect(reinterpret_cast<LPVOID>(target), 6, PAGE_EXECUTE_READWRITE, &oldProt)) {
        Log::Write("[BOZ-RoF2] HookDspChat: VirtualProtect failed (err=%lu)", GetLastError());
        VirtualFree(tramp, 0, MEM_RELEASE);
        g_orig_DspChat = nullptr;
        return false;
    }

    BYTE patch[6];
    patch[0] = 0xE9;
    DWORD relToHook = static_cast<DWORD>(reinterpret_cast<DWORD>(&MyHook_DspChat) - (target + 5));
    *reinterpret_cast<DWORD*>(patch + 1) = relToHook;
    patch[5] = 0x90;
    memcpy(reinterpret_cast<LPVOID>(target), patch, 6);

    DWORD tmp = 0;
    VirtualProtect(reinterpret_cast<LPVOID>(target), 6, oldProt, &tmp);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPVOID>(target), 6);

    g_hookDspChatInstalled = true;
    Log::Write("[BOZ-RoF2] HookDspChat: INSTALLED @ 0x%08X -> 0x%p (trampoline @ 0x%p)",
               target, reinterpret_cast<void*>(&MyHook_DspChat), tramp);
    return true;
}

// ==== InitThread + DllMain ====
static DWORD WINAPI InitThread(LPVOID) {
    if (!ResolveBase()) { Log::Write("[BOZMkt] GetModuleHandle(NULL) failed -- abort"); return 1; }
    Log::Write("[BOZMkt] loaded -- runtime base = 0x%08X", g_baseAddr);
    InstallHook_DspChat();   // catalog feed IN
    InstallHook_PwDraw();    // per-frame tick -> TickMarketplace
    return 0;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
