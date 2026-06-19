// offsets.h — EQ Marketplace ASI client-build offset table.
//
// THIS IS THE ONE FILE YOU EDIT TO PORT TO A DIFFERENT CLIENT BUILD.
// Every constant is an RVA = (absolute address in the supported RoF2 eqgame.exe) - 0x00400000.
// At load the ASI rebases each via Resolve(): liveAddress = GetModuleHandleA(NULL) + rva.
// ASLR changes WHERE the image loads, not its internal layout, so these RVAs are valid for
// ANY load of the reference build. If your eqgame.exe hash differs from the reference, every
// value here must be re-derived — see docs/adapting-to-your-client.md.
//
// Reference build SHA-256:
//   eqgame.exe        2a8702ad9f722704f01355c0750be7d6f164a8b9c9128ba0cf286ea32b405b0e
//   EQGraphicsDX9.dll ade556b68e98f6f39c5578f70431e11c0b9babcc15b8571eb22cf1e2b654417e
#pragma once
#include <windows.h>

// ---- Runtime rebasing -------------------------------------------------------
static DWORD g_baseAddr = 0;
inline bool  ResolveBase() { g_baseAddr = reinterpret_cast<DWORD>(GetModuleHandleA(nullptr)); return g_baseAddr != 0; }
inline DWORD Resolve(DWORD rva) { return g_baseAddr + rva; }

// ---- Marketplace-window offsets (self-contained) ----------------------------
// Carved verbatim from BOZ.asi MktOff. Mixed RVAs (absolute fns/globals, rebased
// via Resolve) and struct/vtable byte-offsets (used as-is, never rebased).
namespace MktOff {
    constexpr DWORD pWnd       = 0x00D1FD08 - 0x00400000;  // &CMarketplaceWnd*
    constexpr DWORD CreateCats = 0x006DD260 - 0x00400000;  // FUN_006dd260(wnd)
    constexpr DWORD RenderCats = 0x006DCF40 - 0x00400000;  // FUN_006dcf40(wnd)
    constexpr DWORD Visible    = 0x196;   // CXWnd visible byte
    constexpr DWORD CatCount    = 0x280;  // category ArrayClass: count (struct start)
    constexpr DWORD CatArray    = 0x284;  //   data ptr
    constexpr DWORD CatCapacity = 0x288;  //   capacity (slots allocated)
    constexpr DWORD NodeIntName= 0x10;    // catNode -> CXStr (shown name)
    constexpr DWORD NodeCount  = 0x30;    // catNode item count (>0 = renders)
    constexpr DWORD CXStrLen   = 0x08;    // CXStr length field
    constexpr DWORD CXStrText  = 0x14;    // CXStr inline text
    // --- data-driven NESTED tree ---
    constexpr DWORD NodeAlloc  = 0x008DBB3B - 0x00400000; // FUN_008dbb3b(size) -> operator new
    constexpr DWORD NodeCtor   = 0x006E4A70 - 0x00400000; // FUN_006e4a70(node,type,&dispCX,&intCX,parent,vis)
    constexpr DWORD CXStrCtor  = 0x00805C20 - 0x00400000; // FUN_00805c20(&out, cstr)
    constexpr DWORD NodeVisible= 0x18;    // node visible byte (1)
    constexpr DWORD NodeParent = 0x1c;    // node parent ptr (0 = top-level)
    constexpr DWORD NodeChildCnt=0x20;    // node child-array count (0 = leaf)
    // --- item tiles on category-select ---
    constexpr DWORD SelNode    = 0x25c;   // *(win+0x25c) = selected category node
    constexpr DWORD Grid       = 0x384;   // *(win+0x384) = item grid (CTileLayoutWnd)
    constexpr DWORD UiMgr      = 0x015D3D08 - 0x00400000; // *(base+..) = SIDL mgr (DAT_015d3d08)
    constexpr DWORD MakeTile   = 0x0086E5E0 - 0x00400000; // FUN_0086e5e0(uiMgr, grid, tmpl)
    constexpr DWORD TplResolve = 0x00870040 - 0x00400000; // FUN_00870040(uiMgr, id)
    constexpr DWORD FindChild  = 0x00868330 - 0x00400000; // FUN_00868330(parent, &cxstr)
    constexpr DWORD SetIcon    = 0x006D1C80 - 0x00400000; // FUN_006d1c80(iconCtrl, icon) -> sets frame (icon-500)
    constexpr DWORD IconAnim   = 0x228;    // icon control -> CTextureAnimation* (the rendered atlas)
    constexpr DWORD AnimFind   = 0x0086E010 - 0x00400000; // FindAnimation(uiMgr, &CXStr) -> CTextureAnimation*
    constexpr DWORD AnimCtor   = 0x0087A9A0 - 0x00400000; // CTextureAnimation ctor (on a 0x4c-byte alloc)
    constexpr DWORD AnimCopy   = 0x0041F2D0 - 0x00400000; // CTextureAnimation copy-init(dest, src)
    constexpr DWORD TileTplId  = 3211409;  // SIDL id of the item-tile template (BUILD/SKIN-SPECIFIC)
    constexpr DWORD VtSetText  = 0x124;    // CXWnd vtable: SetWindowText(&CXStr)
    constexpr DWORD VtSetVis   = 0xd8;     // CXWnd vtable: SetVisible(show, resize)
    constexpr DWORD VtLayout   = 0xa0;     // CTileLayoutWnd vtable: relayout(0,0)
    constexpr DWORD NameColor  = 0x12c;    // tile name-label ARGB text color
    // --- category-select detection via the CTreeViewWnd line index ---
    constexpr DWORD TreeCtrl    = 0x3b4;   // *(win+0x3b4) = CTreeViewWnd
    constexpr DWORD TreeCount   = 0x1d8;   // visible line count
    constexpr DWORD TreeArr     = 0x1dc;   // visible line array
    constexpr DWORD TreeSelIdx  = 0x1f8;   // selected line index (-1 = none)
    constexpr DWORD TreeStride  = 0x128;   // line struct stride
    constexpr DWORD TreeLineNode= 0x10;    // line -> tree-item node
    constexpr DWORD TreeItemLabel=0x14;    // tree-item -> label char* (C string)
    constexpr DWORD LeafVtable   = 0x009DED30 - 0x00400000; // CTreeViewNode leaf vtable
    constexpr DWORD BranchVtable = 0x009DE8B8 - 0x00400000; // CTreeViewNode branch (folder) vtable
    constexpr DWORD BranchLabel  = 0x18;   // branch node -> label char* (leaves use +0x14)
}

// ---- Shared eqgame offsets the marketplace borrows (trimmed from BOZ.asi Off) ----
// Only the 10 the carved marketplace path actually references.
namespace Off {
    constexpr DWORD FnDspChat            = 0x0051F1A0 - 0x00400000; // CEverQuest::dsp_chat (catalog feed IN; forked hook)
    constexpr DWORD FnPlayerWndDraw      = 0x00718CF0 - 0x00400000; // CPlayerWnd::Draw (per-frame tick; forked hook)
    constexpr DWORD FnChatSend           = 0x005369E0 - 0x00400000; // CChatManager::SendChatMessage (SendServerCommand)
    constexpr DWORD ChatMgrPtr           = 0x00E67CCC - 0x00400000; // *(.) = CChatManager*
    // 3D preview (eqgame side)
    constexpr DWORD FnCreatePlayerActor  = 0x0048F4B0 - 0x00400000; // CDisplay::CreatePlayerActor
    constexpr DWORD GblPreviewSubject    = 0x00DD2630 - 0x00400000; // *(.) = local player / preview-subject spawn
    constexpr DWORD GblCDisplay          = 0x00DD2660 - 0x00400000; // *(.) = CDisplay singleton
    constexpr DWORD FnDisplayActorCleanup= 0x00490A10 - 0x00400000; // reap replaced actor (anti-ghost)
    constexpr DWORD FnPaperDollPreview   = 0x00576CA0 - 0x00400000; // viewport paper-doll clone+render
    constexpr DWORD FnSetWeaponSlot      = 0x005923F0 - 0x00400000; // FUN_005923f0 re-attach held weapon after rebuild
    constexpr DWORD FnArmorApply         = 0x00594E50 - 0x00400000; // FUN_00594E50 per-slot armor apply (preview repaint)
}

// ---- Marketplace function-pointer typedefs (carved from BOZ.asi) ------------
typedef void (__fastcall* FnMktVoidWnd_t)(void*);   // __fastcall fn(wnd) -> wnd in ECX
typedef void* (__cdecl*    FnMktAlloc_t)(unsigned int size);                          // FUN_008dbb3b
// FUN_006e4a70 / FUN_00805c20 are __thiscall -> called via the __fastcall(ecx=this, edx=dummy, ...stack) trick.
typedef void* (__fastcall* FnMktNodeCtor_t)(void* node, void* edx, int type,
                                            void* dispCX, void* intCX, void* parent, int visible);
typedef void* (__fastcall* FnMktCXStr_t)(void* out, void* edx, const char* cstr);
typedef void* (__fastcall* FnMktMakeTile_t)(void* uiMgr, void* edx, void* grid, void* tmpl); // FUN_0086e5e0
typedef void* (__fastcall* FnMktTpl_t)(void* uiMgr, void* edx, unsigned int id);             // FUN_00870040
typedef void* (__fastcall* FnMktFind_t)(void* parent, void* edx, void* cxstr);               // FUN_00868330
typedef void  (__cdecl*    FnMktSetIcon_t)(unsigned int iconCtrl, int icon);                 // FUN_006d1c80
typedef void* (__fastcall* FnMktAnimFind_t)(void* uiMgr, void* edx, void* cx);   // FUN_0086e010 FindAnimation
typedef void* (__fastcall* FnMktAnimCtor_t)(void* obj, void* edx);               // FUN_0087a9a0 CTextureAnimation ctor
typedef void* (__fastcall* FnMktAnimCopy_t)(void* dest, void* edx, void* src);   // FUN_0041f2d0 copy-init
typedef void  (__fastcall* FnMktVText_t)(void* thisp, void* edx, void* cxstr);  // vtable SetWindowText(&CXStr)
typedef void  (__fastcall* FnMktVFlags_t)(void* thisp, void* edx, int a, int b); // vtable SetVisible/relayout
