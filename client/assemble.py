# Assembles src/marketplace_asi.cpp by extracting verbatim Mkt unit ranges from
# the monolithic BOZ.asi source and splicing in hand-written glue (top matter,
# slimmed ArmorOrn, forked hook bodies, InitThread/DllMain). Per EXTRACTION_MANIFEST.md.
import io

SRC = r"C:\BOZ-Client-RoF2\src\dllmain_rof2.cpp"
OUT = r"C:\boz-marketplace-release\client\src\marketplace_asi.cpp"

lines = open(SRC, encoding="utf-8", errors="replace").read().split("\n")
def R(a, b):  # 1-indexed inclusive
    return "\n".join(lines[a-1:b])

parts = []
def add(label, text):
    parts.append("\n// ==== " + label + " ====\n" + text)

# 1. Top matter
parts.append(r'''// EQ Marketplace ASI -- standalone client mod, carved from BOZ.asi (RoF2).
// In-game shop window (categories / tiles / buy / funds) + 3D item preview.
// Single translation unit. Client-build offsets + rebase plumbing: offsets.h.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include "offsets.h"
''')

# 2. Shared helpers (verbatim)
add("Log (verbatim 528-541)", R(528, 541))
add("IsReadable (verbatim 578-586)", R(578, 586))
add("FnCreateActor_t typedef (verbatim 1800-1802)", R(1800, 1802))
add("CopyField (verbatim 3125-3134)", R(3125, 3134))
add("SendServerCommand + FnChatSend_t (verbatim 4237-4264)", R(4237, 4264))

# 3. Slimmed ArmorOrn (hand; correction C1)
add("ArmorOrn slim -- preview repaint only", r'''namespace ArmorOrn {
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
}''')

# 4. Forked-hook declarations
add("PwDraw hook decls (verbatim 2010-2012)", R(2010, 2012))
add("dsp_chat hook decls (hand)", r'''typedef void (__thiscall *FnDspChat_t)(void* this_, char* text, int color, int echo, char brackets);
static FnDspChat_t g_orig_DspChat = nullptr;
static bool        g_hookDspChatInstalled = false;
static void*       g_dspChatThis = nullptr;   // CEverQuest singleton, read by MktMaybeHint''')

# 5. Marketplace units, in source order (MktOff + Mkt typedefs are in offsets.h -> skipped)
mkt = [
    (2284,2297,"banner"), (2366,2375,"MktRenameCategory"), (2381,2394,"MktMakeNode"),
    (2399,2413,"MktAddBranch"), (2418,2418,"MktAddGearBranch fwd"), (2419,2553,"MktPopulate"),
    (2555,2558,"ptr-safety fwd decls"), (2563,2568,"MktFindChild"), (2571,2578,"MktSetChildText"),
    (2581,2587,"MktSetWidgetText"), (2590,2595,"MktHideChild"),
    (2603,2627,"MktReadable/Executable/TileLooksValid"), (2631,2646,"MktSafe*"),
    (2650,2660,"MktHideAllTiles"), (2668,2681,"struct MktItem"), (2682,2689,"g_mktItems"),
    (2691,2693,"g_mktView"), (2702,2717,"MktAddGearBranch def"), (2719,2721,"g_mktPlayerLevel"),
    (2723,2729,"g_mktSouls etc"), (2731,2750,"WndNotif/buy/selection globals"), (2754,2758,"MktStrip"),
    (2764,2789,"MktLookupCategory"), (2794,2799,"g_mktFilledCat etc"),
    (2812,2834,"g_mktIconAnim+MktItemIconAnim"), (2836,2871,"MktFillOneTile"),
    (2881,2943,"MktTickTiles"), (2949,2961,"MktGridIndexOf"),
    (2966,2971,"tick fwd decls + populate-state globals"), (2972,3121,"TickMarketplace"),
    (3160,3185,"ParseBozMkt"), (4742,4755,"preview section header"), (4756,4761,"preview typedefs"),
    (4763,4774,"preview state globals"), (4796,4853,"MktRenderCloneBareRebuild"),
    (4873,4910,"MktRenderClone"), (4919,4942,"g_mktRobeHintShown+MktMaybeHint"),
    (4944,4964,"MktShowPreview"), (4975,5037,"MyHook_MktWndNotification"),
    (5039,5066,"InstallMktVTableHook"),
]
for a,b,name in mkt:
    add("%s (verbatim %d-%d)" % (name,a,b), R(a,b))

# 6. Forked PwDraw hook + installer
add("MyHook_PwDraw (forked: keep g_orig + TickMarketplace)", r'''static int __fastcall MyHook_PwDraw(void* this_ecx, void* /*edx_dummy*/) {
    int ret = 0;
    if (g_orig_PwDraw) ret = g_orig_PwDraw(this_ecx);
    TickMarketplace();   // inject categories/tiles/funds when the window is open
    return ret;
}''')
add("InstallHook_PwDraw (verbatim 2057-2106)", R(2057, 2106))

# 7. Forked dsp_chat hook + installer (keep only the 5 [BOZ_MKT*]/[BOZ_FUNDS] branches)
add("MyHook_DspChat (forked: marketplace feed branches only)", r'''static void __fastcall MyHook_DspChat(void* this_ecx, void* /*edx_dummy*/,
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
            char a[16], b[16], cc[16];
            p = CopyField(p, a,  sizeof(a));
            p = CopyField(p, b,  sizeof(b));
            p = CopyField(p, cc, sizeof(cc));
            g_mktSouls      = atoi(a);
            g_mktConquest   = atoi(b);
            g_mktRaid       = atoi(cc);
            g_mktFundsValid = true;
            return;
        }
    }
    if (g_orig_DspChat) g_orig_DspChat(this_ecx, text, color, echo, brackets);
}''')
add("InstallHook_DspChat (verbatim 3909-3964)", R(3909, 3964))

# 8. Entry point
add("InitThread + DllMain", r'''static DWORD WINAPI InitThread(LPVOID) {
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
}''')

out = "\n".join(parts) + "\n"
open(OUT, "w", encoding="utf-8", newline="\n").write(out)
print("wrote", len(out), "bytes,", out.count("\n"), "lines ->", OUT)
