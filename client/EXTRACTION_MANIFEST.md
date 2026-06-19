# Marketplace Carve-Out — EXTRACTION MANIFEST

**Source:** `C:/BOZ-Client-RoF2/src/dllmain_rof2.cpp` (6365 lines, monolithic BOZ.asi for RoF2)
**Target:** standalone single-TU ASI, e.g. `C:/BOZ-Mkt-RoF2/src/dllmain_mkt.cpp` → `BOZMkt.asi`
**Cross-ref:** the offset constants below are also enumerated in `offset-inventory.json` from the prior doc workflow.

This is an ordered, do-this checklist. Work top to bottom: project skeleton → offset header → copy units → fork hooks → closure check.

---

## STEP 1 — Standalone project layout

### 1a. `CMakeLists.txt` (mirror the original; change OUTPUT_NAME; drop psapi)

```cmake
cmake_minimum_required(VERSION 3.15)
project(BOZMkt CXX)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "x86/Win32 ONLY. Configure with:  cmake -A Win32 ..")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT -> zero external runtime deps (no UCRT / api-ms-win-*)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(BOZMkt SHARED
  src/dllmain_mkt.cpp
)

set_target_properties(BOZMkt PROPERTIES
  OUTPUT_NAME "BOZMkt"     # NOT "BOZ" — must not overwrite the full BOZ.asi in the client dir
  PREFIX ""
  SUFFIX ".asi"
)

target_compile_definitions(BOZMkt PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
)

# NO target_link_libraries(... psapi): psapi was pulled in ONLY for the
# anti-cheat EnumProcessModules/GetModuleBaseNameA scanner, which is dropped.
# kernel32 (VirtualAlloc/Protect/Free/Query, GetModuleHandle, CreateThread,
# Heap*, FlushInstructionCache) is auto-linked. No extra libs needed.
```

Configure: `cmake -A Win32 ..` — Win32 generator is mandatory (the FATAL_ERROR guard above enforces it).

### 1b. `dllmain_mkt.cpp` top matter

Includes only:
```cpp
#include <windows.h>     // Win32 + DWORD/BYTE typedefs + SEH
#include <cstdint>       // uint8_t / uint16_t
#include <cstdio>        // _snprintf, fopen/vfprintf (Log)
#include <cstdlib>       // atoi, strtoul
#include <cstring>       // strncmp, strchr, strcpy, memcpy, memset, strlen
#include <cstdarg>       // va_list (Log)
```

### 1c. Entry point — `DllMain` + `InitThread` (collapsed)

Mirror the original two-stage boot (DllMain at src line 6360 spins a thread and returns TRUE on the loader lock; real work in `InitThread` at line 6197):

```cpp
BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
```

`InitThread` collapses to **exactly these three steps** (everything else in the original 6197–~6300 — byte-patch loop, SkillRow hook, 0x870C60 master-UI hook, cascade-menu hook, Node Fight window, anti-cheat scanner — is dropped):

```cpp
static DWORD WINAPI InitThread(LPVOID) {
    if (!ResolveBase()) { Log::Write("[BOZMkt] GetModuleHandle(NULL) failed -- abort"); return 1; }
    Log::Write("[BOZMkt] loaded -- runtime base = 0x%08X", g_baseAddr);
    InstallHook_DspChat();   // catalog feed IN  (installs the 1st of 3 hooks)
    InstallHook_PwDraw();    // per-frame tick  (installs the 2nd hook; drives TickMarketplace)
    return 0;
}
```

The **3rd hook** (`InstallMktVTableHook`) is NOT wired here — it installs **lazily** from `TickMarketplace` (src 3019–3021) on first marketplace-window-open, which runs from the PwDraw tick. No extra InitThread wiring for it.

**ASLR safety:** every offset constant is `ABS - 0x00400000`; `ResolveBase()` does `g_baseAddr = GetModuleHandleA(NULL)`; `Resolve(off) = g_baseAddr + off`. Identical to the monolith — rebase-safe, no relocation work.

---

## STEP 2 — The OFFSET HEADER

Put two groups of constants near the top (after the includes, before the rebase plumbing). All also appear in `offset-inventory.json`.

### 2a. `namespace MktOff` — copy **verbatim** from src **2298–2345**, plus the `Mkt* typedef` block **2347–2361**.
Self-contained marketplace offsets/field-strides; moves wholesale, no external split. Contents (for the closure check):
`pWnd 0x91FD08`, `CreateCats 0x2DD260`, `RenderCats 0x2DCF40`, `Visible 0x196`, `CatCount/CatArray/CatCapacity 0x280/0x284/0x288`, `NodeIntName 0x10`, `NodeCount 0x30`, `CXStrLen/CXStrText 0x08/0x14`, `NodeAlloc/NodeCtor/CXStrCtor`, `NodeVisible/Parent/ChildCnt 0x18/0x1c/0x20`, `SelNode 0x25c`, `Grid 0x384`, `UiMgr 0x11D3D08` (duplicates `Off::SidlMgrPtr` so mkt never needs `Off::` for the UI mgr), `MakeTile/TplResolve/FindChild`, `SetIcon`, `IconAnim 0x228`, `AnimFind/AnimCtor/AnimCopy`, `TileTplId 3211409`, `VtSetText 0x124`, `VtSetVis 0xd8`, `VtLayout 0xa0`, `NameColor 0x12c`, `TreeCtrl 0x3b4`, `TreeCount/Arr/SelIdx/Stride 0x1d8/0x1dc/0x1f8/0x128`, `TreeLineNode 0x10`, `TreeItemLabel 0x14`, `LeafVtable 0x5DED30`, `BranchVtable 0x5DE8B8`, `BranchLabel 0x18`.

### 2b. `namespace Off` (shared) — create a **trimmed** `Off` with ONLY these `copy_constant` symbols (do not copy the full src-23 `Off` namespace; bring just the entries the closure needs):

| Symbol | src line | value (RVA = ABS − 0x400000) | needed by |
|---|---|---|---|
| `FnDspChat` | 290 | `0x0051F1A0` | forked dsp_chat install; `g_orig_DspChat` for `MktMaybeHint` |
| `FnPlayerWndDraw` | 327 | `0x00718CF0` | forked PwDraw install |
| `FnChatSend` | 340 | `0x005369E0` | `SendServerCommand` |
| `ChatMgrPtr` | 343 | `0x00E67CCC` | `SendServerCommand` |
| `FnCreatePlayerActor` | 462 | `0x0048F4B0` | `MktRenderCloneBareRebuild` (4799) |
| `FnViewportSetModel` | 485 | `0x005768C0` | preview path |
| `GblPreviewSubject` | 486 | `0x00DD2630` | `MktShowPreview` (4951); also read by ArmorOrn fork |
| `GblCDisplay` | 487 | `0x00DD2660` | `MktRenderCloneBareRebuild` (4798) |
| `FnDisplayActorCleanup` | 493 | `0x00490A10` | `MktRenderCloneBareRebuild` (4800) |
| `FnPaperDollPreview` | 507 | `0x00576CA0` | `MktShowPreview` (4954) |
| `FnSetWeaponSlot` | 508 | `0x005923F0` | preview weapon overlay |
| `FnAppearanceApply` | 514 | `0x00596B20` | preview appearance re-apply |
| `GblEngine` | 517 | `0x015D46A4` | preview re-register actor |
| `FnArmorApply` | 523 | `0x00594E50` | ArmorOrn fork (`PreviewApply` g_orig trampoline) |

> These preview offsets live in the SHARED `Off`, NOT `MktOff` — the carve-out needs them. `FnGetItem`/`GblLocalPC` (src 524–525) are NOT needed (only the dropped ArmorOrn worn-walk uses them).

---

## STEP 3 — Copy the marketplace units (in dependency order)

Copy each line range **verbatim**. The list is already in build order: rebase/log/guards first, then `MktOff` typedefs (Step 2a), then the Mkt code. **Two non-marketplace islands sit inside the mkt line span and MUST be skipped:** `ParseBozNode` (3138–3156) and everything from `SetListRowColor` (3187) through Node-Fight (3678). Also do NOT copy `TickCycleHeader` (2264–2282) or the Node-Fight `SendServerCommand` fwd-decl at 2252.

### 3a. Shared plumbing / `copy_helper` (bring these first — every Mkt unit depends on them)

| Unit | src lines | notes |
|---|---|---|
| `g_baseAddr` + `Resolve` + `ResolveBase` | 564–575 | rebase core; init before any Resolve |
| `namespace Log` (`PATH` + `Write`) | 528–541 | repoint `PATH` to e.g. `C:\\BOZ-Client-RoF2\\bozmkt.log` |
| `IsReadable` | 578–586 | used by `SendServerCommand` + both forked installers |
| `CopyField` | 3125–3134 | used by `ParseBozMkt` + `[BOZ_FUNDS]` branch |
| `FnChatSend_t` typedef + `SendServerCommand` | 4237–4264 | pulls `Off::ChatMgrPtr` + `Off::FnChatSend` |
| `FnCreateActor_t` typedef | 1800–1802 | typedef ONLY — do NOT bring `g_orig_CreateActor`/`MyHook_CreateActor` |

### 3b. Marketplace units — copy these ranges in order

| # | Unit | src lines |
|---|---|---|
| 1 | Marketplace banner comment | 2284–2297 |
| 2 | `MktOff` namespace | 2298–2345 |
| 3 | `Mkt*` typedefs | 2347–2361 |
| 4 | `MktRenameCategory` | 2366–2375 |
| 5 | `MktMakeNode` | 2381–2394 |
| 6 | `MktAddBranch` | 2399–2413 |
| 7 | `MktAddGearBranch` fwd decl | 2418 |
| 8 | `MktPopulate` | 2419–2553 |
| 9 | pointer-safety fwd decls | 2555–2558 |
| 10 | `MktFindChild` | 2563–2568 |
| 11 | `MktSetChildText` | 2571–2578 |
| 12 | `MktSetWidgetText` | 2581–2587 |
| 13 | `MktHideChild` | 2590–2595 |
| 14 | `MktReadable` / `MktExecutable` / `MktTileLooksValid` | 2603–2627 |
| 15 | `MktSafeSetVisible` / `MktSafeHide` / `MktSafeRelayout` | 2631–2646 |
| 16 | `MktHideAllTiles` | 2650–2660 |
| 17 | `struct MktItem` | 2668–2681 |
| 18 | `g_mktItems[4000]` / `g_mktItemCount` / `g_mktCollecting` | 2682–2689 |
| 19 | `g_mktView` / `g_mktViewN` | 2691–2693 |
| 20 | `MktAddGearBranch` (definition) | 2702–2717 |
| 21 | `g_mktPlayerLevel` | 2719–2721 |
| 22 | `g_mktSouls/Conquest/Raid/FundsValid` | 2723–2729 |
| 23 | WndNotif/buy/selection globals (`g_origMktWndNotif`, `g_mktVtableCopy`, btn ptrs, `MKT_MAX_TILES`, …) | 2731–2750 |
| 24 | `MktStrip` | 2754–2758 |
| 25 | `MktLookupCategory` | 2764–2789 |
| 26 | `g_mktFilledCat` / `g_mktFilledN` / `g_mktDone` | 2794–2799 |
| 27 | `g_mktIconAnim` + `MktItemIconAnim` | 2812–2834 |
| 28 | `MktFillOneTile` | 2836–2871 |
| 29 | `MktTickTiles` | 2881–2943 |
| 30 | `MktGridIndexOf` | 2949–2961 |
| 31 | tick fwd decls + populate-state globals (`g_mktPopulated/Requested/NeedRepop/LastSel`) | 2966–2971 |
| 32 | `TickMarketplace` | 2972–3121 |
| 33 | `ParseBozMkt` | 3160–3185 |
| 34 | 3D-preview section header | 4742–4755 |
| 35 | preview typedefs (`MktPaperDollFn`, `MktSetWeaponFn`, `MktApplyFn`, `MktSetPreviewFn`, `MktActorReapFn`) | 4756–4761 |
| 36 | preview state globals (`g_mktPreviewLastId/Busy/Enabled`) | 4763–4774 |
| 37 | `MktRenderCloneBareRebuild` | 4796–4853 |
| 38 | `MktRenderClone` | 4873–4910 |
| 39 | `g_mktRobeHintShown` + `MktMaybeHint` | 4919–4942 |
| 40 | `MktShowPreview` | 4944–4964 |
| 41 | `MyHook_MktWndNotification` | 4975–5037 |
| 42 | `InstallMktVTableHook` | 5039–5066 |

> Also bring the `TickMarketplace` fwd-decl (2020) and the `SendServerCommand`+`InstallMktVTableHook` fwd-decls (2966–2967) — relocate them above their definitions in the new TU as needed.

---

## STEP 4 — FORK the three shared hooks (keep mkt branch, drop the rest)

### 4a. `MyHook_PwDraw` + installer — `forked_hook`
- **Bring:** `FnPwDraw_t` typedef + `g_orig_PwDraw` + `g_hookPwDraw_installed` (2010–2012); `InstallHook_PwDraw` (2057–2106, verbatim — its prologue check `56 8B F1 0F B6 86 80 02 00 00` and 10-byte steal are unchanged); a forked `MyHook_PwDraw` body.
- **Forked body — KEEP:** call `g_orig_PwDraw(this_ecx)` (2024–2025), then `TickMarketplace();` (line **2047**), then `return ret;`.
- **DROP:** the melee MP-bar `__try` block (2027–2039, offsets +0x22C/+0x238/+0x244), `TickCycleHeader();` (2044), `AcFlushPendingFromMainThread();` (2052). Result is ~10 lines.

### 4b. `MyHook_DspChat` + installer — `forked_hook`
- **Bring:** `FnDspChat_t` typedef + `g_orig_DspChat` + `g_hookDspChatInstalled` (3590–3592); `g_dspChatThis` (3600, read by `MktMaybeHint`); `InstallHook_DspChat` (3909–3964, verbatim — prologue `64 A1 00 00 00 00`, 6-byte steal); a forked `MyHook_DspChat` body.
- **Forked body — KEEP:** entry `if (!g_dspChatThis) g_dspChatThis = this_ecx;` + the `[B`/`O`/`Z`/`_` prefix gate (3681–3683); the **five** marketplace branches **3810–3843** (`[BOZ_MKT_START]`, `[BOZ_MKT_LEVEL]`, `[BOZ_MKT_END]`, `[BOZ_MKT]→ParseBozMkt`, `[BOZ_FUNDS]→CopyField`); the tail `if (g_orig_DspChat) g_orig_DspChat(this_ecx,text,color,echo,brackets);` (3904–3906).
- **DROP:** all other `[BOZ_*]` branches — `SPELL_UNLOCK`, `ARMORORN`, `MAXMANA`, `SKILL_GRANTS`, `SKILL_MIRROR`, `NODE_ACCESS`, `OPEN_WINDOW`, `NODES`, `NODE`, `CYCLE`, `SOUL_BANK`, `SLOTS` (≈3684–3902, minus the 3810–3843 mkt span).

### 4c. `MyHook_MktWndNotification` / `InstallMktVTableHook` — already 100% marketplace
- Travels intact (Step 3b #41/#42) with `g_origMktWndNotif` / `g_mktVtableCopy` / `MktWndNotifyFn` (from the 2731–2750 globals). Per-instance vtable[34] swap on the runtime-discovered `CMarketplaceWnd` (`*Resolve(MktOff::pWnd)`, gated on +0x196==1); not a code patch. No branch trimming.

### 4d. ArmorOrn — `fork_hook` (marketplace's ONLY cross-unit CODE call)
`MktRenderCloneBareRebuild`/`MktRenderClone` call `ArmorOrn::PreviewApply` (defined 6144–6149) through `ArmorOrn::g_orig`. Fork a **minimal** `ArmorOrn`:
- **Bring:** the `FnArmorApply_t` typedef + `g_orig`/`g_installed` globals (6006–6014), `PreviewApply` (6144–6149), and `Install` (6151–6193, verbatim — prologue `6A FF 68`, 7-byte steal). Wrap them in `namespace ArmorOrn`.
- **DROP:** the worn-item-walk `MyHook` body, `RefreshWornSlot` (6119–6133), `LookForRenderSlot`, `WornToRender`, the `kWornSlot` map, `FnGetItem_t`/`g_GetItem` (6010/6013), and all ITEMDEF_* offsets — none are reached by the preview path.
- **Install:** call `ArmorOrn::Install()` from `InitThread` (add a 4th line) so `g_orig` (the un-hooked `FnArmorApply` trampoline) resolves before the first preview. `PreviewApply` uses `g_orig`, so the hook need not actually intercept anything — but `Install()` is the simplest way to build the trampoline; keep it.
  *(Alternative `write_fresh`: skip the hook entirely and write a tiny wrapper that resolves `Off::FnArmorApply` and calls it `__thiscall(spawn, slot, material, 0,0,0,0, color, 1)` directly. Either satisfies the closure.)*

---

## STEP 5 — Closure check (every external symbol accounted for)

The union {Mkt units (3b)} + {`copy_constant` Off:: (2b) + MktOff (2a)} + {`copy_helper` (3a)} + {forked-hook mkt-branches (4)} is **closed**. Per-symbol:

**Satisfied by a COPY (helper):**
- `Resolve`/`ResolveBase`/`g_baseAddr` ← 3a; `Log::Write` ← 3a; `IsReadable` ← 3a; `CopyField` ← 3a; `SendServerCommand`+`FnChatSend_t` ← 3a; `FnCreateActor_t` typedef ← 3a.

**Satisfied by a COPY (constant):** all of `MktOff::*` ← 2a; `Off::{FnDspChat, FnPlayerWndDraw, FnChatSend, ChatMgrPtr, FnCreatePlayerActor, FnViewportSetModel, GblPreviewSubject, GblCDisplay, FnDisplayActorCleanup, FnPaperDollPreview, FnSetWeaponSlot, FnAppearanceApply, GblEngine, FnArmorApply}` ← 2b.

**Satisfied by FORK:**
- `g_orig_PwDraw`/`g_hookPwDraw_installed`/`InstallHook_PwDraw` ← 4a.
- `g_orig_DspChat`/`g_hookDspChatInstalled`/`g_dspChatThis`/`InstallHook_DspChat` ← 4b. (`MktMaybeHint`'s reads of `g_orig_DspChat`+`g_dspChatThis` resolve here.)
- `ArmorOrn::PreviewApply`/`ArmorOrn::g_orig`/`ArmorOrn::Install` ← 4d.

**Satisfied by WINAPI / CRT (header-only, no copy):**
- Win32: `VirtualAlloc`/`VirtualProtect`/`VirtualFree`/`VirtualQuery`, `MEMORY_BASIC_INFORMATION`, `FlushInstructionCache`, `GetCurrentProcess`, `GetModuleHandleA`, `GetLastError`, `HeapAlloc`/`HeapFree`/`GetProcessHeap`, `GetTickCount`, `CreateThread`, `lstrcmpA`/`lstrcpynA`, `DWORD`/`BYTE` typedefs — `<windows.h>`.
- SEH `__try`/`__except`/`__finally` + `EXCEPTION_EXECUTE_HANDLER` — MSVC intrinsic (POD-only locals already enforced in those scopes).
- CRT: `_snprintf`, `strncmp`, `strchr`, `strtoul`, `atoi`, `strcpy`, `strlen`, `memcpy`, `memset`, `fopen`/`vfprintf`/`fputc`/`fclose`, `va_*` — `<cstdio>`/`<cstdlib>/<cstring>/<cstdarg>`.
- `uint8_t`/`uint16_t` — `<cstdint>`.

**No `psapi`, no `EnumProcessModules`, no anti-cheat, no Node-Fight, no byte-patch writers** are referenced by any copied unit → the `target_link_libraries(... psapi)` line is correctly omitted (kernel32 implicit).

**Result:** no dangling reference. The single TU compiles and links against kernel32 + static CRT only, emitting `BOZMkt.asi`.
---

## CORRECTIONS (main-thread, verified against source 2026-06-19)

The adversarial verify flagged the ArmorOrn handling; resolved by reading the real code:

**C1 — DROP the ArmorOrn fork (Install/MyHook). Slim ArmorOrn to ONLY `PreviewApply`.**
The marketplace preview calls `ArmorOrn::PreviewApply` (src 4808/4838) which calls `g_orig`
(the un-hooked FUN_00594E50 trampoline). The standalone does NOT hook FUN_00594E50 (that's the
separate §88 live-player armor-ornament feature), so rewire `PreviewApply` to resolve
`FUN_00594E50` DIRECTLY from `Off::FnArmorApply` (identical effect when the fn is unhooked):
```cpp
namespace ArmorOrn {
    typedef void (__thiscall *FnArmorApply_t)(void*, unsigned, unsigned, int,int,int, unsigned, int,int);
    static FnArmorApply_t g_armorApply = nullptr;
    void PreviewApply(void* spawn, unsigned renderSlot, unsigned material, unsigned color) {
        if (!g_armorApply) g_armorApply = (FnArmorApply_t)Resolve(Off::FnArmorApply);
        if (!g_armorApply || !spawn || renderSlot > 6) return;
        __try { g_armorApply(spawn, renderSlot, material, 0,0,0,0, (int)color, 1); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}
```
  DROP entirely: LookForRenderSlot, MyHook, WornToRender, RefreshWornSlot, Install, g_GetItem,
  g_installed, kWornSlot[], ITEMDEF_*/ITEM_AUG_*/ARMOR_ORNAMENT_AUGTYPE consts.
  Keep the fwd-decl (src 3674-3676): `namespace ArmorOrn { void PreviewApply(void*,unsigned,unsigned,unsigned); }`.

**C2 — DROP 4 unreferenced preview offsets** from the Off header: `FnViewportSetModel` (485),
`FnSetWeaponSlot` (508), `FnAppearanceApply` (514), `GblEngine` (517) — superseded dead-ends;
the shipped preview (MktRenderCloneBareRebuild/MktShowPreview) uses only FnCreatePlayerActor,
FnDisplayActorCleanup, FnPaperDollPreview, GblCDisplay, GblPreviewSubject, NameColor, + FnArmorApply.

**C3 — DROP `Off::FnGetItem` and `Off::GblLocalPC`** (only the dropped LookForRenderSlot used them).

**C4 — dsp_chat fork keeps ONLY** the `[BOZ_MKT_START]/[BOZ_MKT]/[BOZ_MKT_END]/[BOZ_MKT_LEVEL]/[BOZ_FUNDS]`
branches; DROP `[BOZ_ARMORORN]` (calls the dropped RefreshWornSlot) and any other [BOZ_*] tags.

FINAL Off:: copy_constant set (10): FnDspChat, FnPlayerWndDraw, FnChatSend, ChatMgrPtr,
FnCreatePlayerActor, GblPreviewSubject, GblCDisplay, FnDisplayActorCleanup, FnPaperDollPreview, FnArmorApply.
