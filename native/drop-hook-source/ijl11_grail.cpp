/*
 * ijl11.dll — DropIdentified + Grail Drop Logger for SoE / Project Diablo 2
 *
 * Build (MSYS2 MINGW64, 32-bit):
 *   cd /path/to/this/folder
 *   /mingw32/bin/g++ -O2 -m32 -std=c++14 -shared -o ijl11.dll ijl11.cpp -lkernel32 -luser32 -lshlwapi
 *
 * Drop into C:\Program Files (x86)\Diablo II\ProjectD2\
 * Rename the original ijl11.dll to ijl11_orig.dll first.
 *
 * What it does:
 *   1. DropIdentified: intercepts SetItemFlag in D2Common.dll so magic/rare/set/unique
 *      items drop already identified (controlled by DropIdentified.ini).
 *   2. Grail Logger: when a unique or set item drops, looks up its name via
 *      D2Common.sgptDataTables -> UniqueItemsTxt / SetItemsTxt -> GetStringById
 *      and appends "ItemName|quality\n" to C:\grail_drops.log.
 *      The SoE Companion web tool can import this log to auto-check grail items.
 *
 * All offsets confirmed against SoE's actual D2Common.dll and D2Lang.dll (May 2026).
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <string>
#include <vector>

// ── Proxy exports ─────────────────────────────────────────────────────────────
static HMODULE g_orig = nullptr;

// Intel JPEG Library exports used by Diablo II.
// Keep the public names/ordinals aligned with the live proxy via ijl11.def.
typedef int         (__stdcall *pfnijlInit)(void*);
typedef int         (__stdcall *pfnijlFree)(void*);
typedef int         (__stdcall *pfnijlRead)(void*, int);
typedef int         (__stdcall *pfnijlWrite)(void*, int);
typedef const char* (__stdcall *pfnijlErrorStr)(int);
typedef void*       (__stdcall *pfnijlGetLibVersion)();

static pfnijlInit          g_ijlInit          = nullptr;
static pfnijlFree          g_ijlFree          = nullptr;
static pfnijlRead          g_ijlRead          = nullptr;
static pfnijlWrite         g_ijlWrite         = nullptr;
static pfnijlErrorStr      g_ijlErrorStr      = nullptr;
static pfnijlGetLibVersion g_ijlGetLibVersion = nullptr;

extern "C" int __stdcall ijlFree(void* props)
{
    return g_ijlFree ? g_ijlFree(props) : 0;
}

extern "C" const char* __stdcall ijlGetErrorString(int code)
{
    return g_ijlErrorStr ? g_ijlErrorStr(code) : "ijl11 proxy: original ijlErrorStr unavailable";
}

extern "C" void* __stdcall ijlGetLibVersion()
{
    return g_ijlGetLibVersion ? g_ijlGetLibVersion() : nullptr;
}

extern "C" int __stdcall ijlInit(void* props)
{
    return g_ijlInit ? g_ijlInit(props) : 0;
}

extern "C" int __stdcall ijlRead(void* props, int ioType)
{
    return g_ijlRead ? g_ijlRead(props, ioType) : 0;
}

extern "C" int __stdcall ijlWrite(void* props, int ioType)
{
    return g_ijlWrite ? g_ijlWrite(props, ioType) : 0;
}

// ── Config ────────────────────────────────────────────────────────────────────
static bool g_dropIdMagic  = true;
static bool g_dropIdRare   = true;
static bool g_dropIdSet    = true;
static bool g_dropIdUnique = true;
static bool g_dropIdSmallCharm = false;
static bool g_dropIdLargeCharm = false;
static bool g_dropIdGrandCharm = false;
static void LoadConfig()
{
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) slash[1] = '\0';
    strcat(path, "DropIdentified.ini");

    g_dropIdMagic  = GetPrivateProfileIntA("DropIdentified", "Magic",  1, path) != 0;
    g_dropIdRare   = GetPrivateProfileIntA("DropIdentified", "Rare",   1, path) != 0;
    g_dropIdSet    = GetPrivateProfileIntA("DropIdentified", "Set",    1, path) != 0;
    g_dropIdUnique = GetPrivateProfileIntA("DropIdentified", "Unique", 1, path) != 0;
    g_dropIdSmallCharm = GetPrivateProfileIntA("DropIdentified", "SmallCharm", 0, path) != 0;
    g_dropIdLargeCharm = GetPrivateProfileIntA("DropIdentified", "LargeCharm", 0, path) != 0;
    g_dropIdGrandCharm = GetPrivateProfileIntA("DropIdentified", "GrandCharm", 0, path) != 0;
}

// ── D2 type/offset definitions ────────────────────────────────────────────────
// UnitAny offsets
static const uintptr_t UNIT_TYPE     = 0x00;
static const uintptr_t UNIT_CLASS    = 0x04;
static const uintptr_t UNIT_ID       = 0x0C;
static const uintptr_t UNIT_MODE     = 0x10;
static const uintptr_t UNIT_ITEMDATA = 0x14;

// ItemData offsets
static const uintptr_t ITEM_QUALITY   = 0x00;
static const uintptr_t ITEM_SEED      = 0x14;
static const uintptr_t ITEM_FLAGS     = 0x18;
static const uintptr_t ITEM_FILEINDEX = 0x2C;

// Item quality values
static const uint32_t QUALITY_MAGIC  = 4;
static const uint32_t QUALITY_SET    = 5;
static const uint32_t QUALITY_RARE   = 6;
static const uint32_t QUALITY_UNIQUE = 7;

// IFLAG_IDENTIFIED bit
static const uint32_t IFLAG_IDENTIFIED = 0x10;
static const uint32_t IFLAG_ETHEREAL   = 0x00400000;
static const uint32_t IFLAG_RUNEWORD   = 0x04000000;

// D2Common data table offsets (D2MOO struct — unchanged between MXL/PD2/SoE)
static const uintptr_t SGPT_DATA_TABLES_RVA      = 0x99E1C;  // confirmed SoE D2Common
static const uintptr_t ITEMS_TXT_COUNT_RVA        = 0x9FB94;
static const uintptr_t ITEMS_TXT_PTR_RVA          = 0x9FB98;
static const uintptr_t ITEMS_TXT_RECORD_SIZE      = 0x1A8;
static const uintptr_t ITEMS_TXT_CODE             = 0x00;
static const uintptr_t DT_UNIQUE_ITEMS_TXT_PTR   = 0xC24;
static const uintptr_t DT_SET_ITEMS_TXT_PTR      = 0xC18;

// UniqueItems.txt record
static const uintptr_t UNI_RECORD_SIZE = 0x14C;
static const uintptr_t UNI_NAME_ID     = 0x22;   // word (wTblIndex)

// SetItems.txt record
static const uintptr_t SET_RECORD_SIZE = 0x1B8;
static const uintptr_t SET_NAME_ID     = 0x24;   // word (wStringId)

// D2Lang GetStringById: ordinal 10003, RVA 0x9450 (confirmed same in SoE D2Lang)
static const uintptr_t D2LANG_GETSTRING_RVA = 0x9450;
static const uintptr_t D2CLIENT_PLAYER_UNIT_RVA = 0x11BBFC;

// Sentinel NAME_ID that means "lookup failed at DLL load"
static const uint16_t BAD_NAME_ID = 5383;

// ── Runtime pointers ──────────────────────────────────────────────────────────
// SetItemFlag IAT slot in D2Game.dll (found at startup via previous analysis)
static uintptr_t* g_pIATSlot       = nullptr;
static uintptr_t  g_origSetItemFlag = 0;
static bool       g_hooked         = false;
static volatile bool g_shutdown    = false;

static CRITICAL_SECTION g_hookDropEventLock;
static bool             g_hookDropEventLockReady = false;
static HANDLE           g_hookDropEventReady = nullptr;
static HANDLE           g_hookDropEventThread = nullptr;
static std::vector<std::string> g_hookDropEventQueue;
static const size_t HOOK_DROP_EVENT_QUEUE_LIMIT = 4096;

static uint32_t ItemCodeForUnit(uintptr_t pUnit)
{
    HMODULE hD2Common = GetModuleHandleA("D2Common.dll");
    if (!hD2Common || IsBadReadPtr((void*)pUnit, UNIT_CLASS + 4)) return 0;

    uint32_t itemClass = *(uint32_t*)(pUnit + UNIT_CLASS);
    uint32_t itemCount = *(uint32_t*)((uintptr_t)hD2Common + ITEMS_TXT_COUNT_RVA);
    uintptr_t pItemsTxt = *(uintptr_t*)((uintptr_t)hD2Common + ITEMS_TXT_PTR_RVA);
    if (!pItemsTxt || itemClass >= itemCount) return 0;

    uintptr_t record = pItemsTxt + (uintptr_t)itemClass * ITEMS_TXT_RECORD_SIZE;
    if (IsBadReadPtr((void*)record, ITEMS_TXT_CODE + 4)) return 0;
    return *(uint32_t*)(record + ITEMS_TXT_CODE);
}

static bool IsConfiguredCharm(uintptr_t pUnit)
{
    uint32_t code = ItemCodeForUnit(pUnit);
    // Item codes are little-endian dwords: "cm1", "cm2", "cm3".
    if (code == 0x00316D63) return g_dropIdSmallCharm;
    if (code == 0x00326D63) return g_dropIdLargeCharm;
    if (code == 0x00336D63) return g_dropIdGrandCharm;
    return false;
}

#ifdef SOE_MATERIALS_TRACE
#ifdef SOE_MATERIALS_PASSIVE_TRACE
static const bool kMaterialsPassiveTrace = true;
#else
static const bool kMaterialsPassiveTrace = false;
#endif
extern "C" uintptr_t g_materialsTraceE80Trampoline = 0;
extern "C" uintptr_t g_materialsTraceF70Trampoline = 0;
extern "C" uintptr_t g_materialsTraceE20Trampoline = 0;
extern "C" uintptr_t g_materialsTraceDB0Trampoline = 0;
extern "C" uintptr_t g_materialsTraceEE0Trampoline = 0;
extern "C" uintptr_t g_materialsTraceFF0Trampoline = 0;
extern "C" uintptr_t g_materialsTrace3060Trampoline = 0;
extern "C" uintptr_t g_materialsNativeD560Trampoline = 0;
extern "C" uintptr_t g_materialsNativeE7B0Trampoline = 0;
extern "C" uintptr_t g_materialsNativeE860Trampoline = 0;
extern "C" uintptr_t g_materialsNativeE8C0Trampoline = 0;
extern "C" uintptr_t g_materialsNativeE940Trampoline = 0;
extern "C" uintptr_t g_materialsNativeF3A0Trampoline = 0;
extern "C" uintptr_t g_materialsNativeF5F0Trampoline = 0;
extern "C" uintptr_t g_materialsStorageB910Trampoline = 0;
extern "C" uintptr_t g_materialsStorageB9E0Trampoline = 0;
extern "C" uintptr_t g_materialsStorageBAD0Trampoline = 0;
extern "C" uintptr_t g_materialsStorageD000Trampoline = 0;
extern "C" uintptr_t g_materialsStorageD110Trampoline = 0;
extern "C" uintptr_t g_materialsStorageD1D0Trampoline = 0;
extern "C" uintptr_t g_materialsTraceUiTrampoline = 0;
extern "C" uintptr_t g_materialsTraceTransitionTrampoline = 0;
extern "C" uintptr_t g_materialsTraceDrawTrampoline = 0;
extern "C" uintptr_t g_materialsGridRenderTrampoline = 0;
extern "C" uintptr_t g_materialsPageDrawTrampoline = 0;
extern "C" uintptr_t g_materialsContentDrawTarget = 0;
extern "C" uintptr_t g_materialsObjectDrawTrampoline = 0;
extern "C" uintptr_t g_materialsD2ClientDrawEntryTrampoline = 0;
extern "C" uintptr_t g_materialsD2ClientSpriteDrawTrampoline = 0;
extern "C" uintptr_t g_materialsPage2CloneDrawSpriteFn = 0;
extern "C" uintptr_t g_materialsPage2CloneHoverFlagAddr = 0;
extern "C" uintptr_t g_materialsPage2CloneReturn = 0;
static uintptr_t g_materialsPage2CloneTrampoline = 0;
extern "C" uintptr_t g_materialsActiveClickTrampoline = 0;
extern "C" uintptr_t g_materialsActiveClickBlockedReturn = 0;
extern "C" uintptr_t g_materialsHoverLookupTrampoline = 0;
extern "C" uintptr_t g_materialsDescriptorLookupTrampoline = 0;
extern "C" uintptr_t g_materialsDescriptorHasSlotTrampoline = 0;
extern "C" uintptr_t g_materialsDescriptorTextTrampoline = 0;
extern "C" uintptr_t g_materialsMsg1450Trampoline = 0;
extern "C" uintptr_t g_materialsMsg1560Trampoline = 0;
static uint8_t   g_materialsTraceE80Original[8] = {};
static uint8_t   g_materialsTraceF70Original[8] = {};
static uint8_t   g_materialsTraceE20Original[8] = {};
static uint8_t   g_materialsTraceDB0Original[8] = {};
static uint8_t   g_materialsTraceEE0Original[8] = {};
static uint8_t   g_materialsTraceFF0Original[8] = {};
static uint8_t   g_materialsTrace3060Original[8] = {};
static uint8_t   g_materialsNativeD560Original[8] = {};
static uint8_t   g_materialsNativeE7B0Original[8] = {};
static uint8_t   g_materialsNativeE860Original[8] = {};
static uint8_t   g_materialsNativeE8C0Original[9] = {};
static uint8_t   g_materialsNativeE940Original[8] = {};
static uint8_t   g_materialsNativeF3A0Original[9] = {};
static uint8_t   g_materialsNativeF5F0Original[5] = {};
static uint8_t   g_materialsStorageB910Original[6] = {};
static uint8_t   g_materialsStorageB9E0Original[6] = {};
static uint8_t   g_materialsStorageBAD0Original[6] = {};
static uint8_t   g_materialsStorageD000Original[6] = {};
static uint8_t   g_materialsStorageD110Original[6] = {};
static uint8_t   g_materialsStorageD1D0Original[5] = {};
static uint8_t   g_materialsTraceUiOriginal[7] = {};
static uint8_t   g_materialsTraceTransitionOriginal[5] = {};
static uint8_t   g_materialsTraceDrawOriginal[9] = {};
static uint8_t   g_materialsGridRenderOriginal[6] = {};
static uint8_t   g_materialsPageDrawOriginal[6] = {};
static uint8_t   g_materialsContentDrawCallOriginal[5] = {};
static uint8_t   g_materialsObjectDrawOriginal[7] = {};
static uint8_t   g_materialsD2ClientDrawEntryOriginal[5] = {};
static uint8_t   g_materialsD2ClientSpriteDrawOriginal[5] = {};
static uint8_t   g_materialsPage2CloneOriginal[7] = {};
static uint8_t   g_materialsActiveClickOriginal[6] = {};
static uint8_t   g_materialsHoverLookupOriginal[6] = {};
static uint8_t   g_materialsDescriptorLookupOriginal[5] = {};
static uint8_t   g_materialsDescriptorHasSlotOriginal[6] = {};
static uint8_t   g_materialsDescriptorTextOriginal[6] = {};
static uint8_t   g_materialsMsg1450Original[9] = {};
static uint8_t   g_materialsMsg1560Original[9] = {};
static uintptr_t g_materialsTraceE80PatchAddr = 0;
static uintptr_t g_materialsTraceF70PatchAddr = 0;
static uintptr_t g_materialsTraceE20PatchAddr = 0;
static uintptr_t g_materialsTraceDB0PatchAddr = 0;
static uintptr_t g_materialsTraceEE0PatchAddr = 0;
static uintptr_t g_materialsTraceFF0PatchAddr = 0;
static uintptr_t g_materialsTrace3060PatchAddr = 0;
static uintptr_t g_materialsNativeD560PatchAddr = 0;
static uintptr_t g_materialsNativeE7B0PatchAddr = 0;
static uintptr_t g_materialsNativeE860PatchAddr = 0;
static uintptr_t g_materialsNativeE8C0PatchAddr = 0;
static uintptr_t g_materialsNativeE940PatchAddr = 0;
static uintptr_t g_materialsNativeF3A0PatchAddr = 0;
static uintptr_t g_materialsNativeF5F0PatchAddr = 0;
static uintptr_t g_materialsStorageB910PatchAddr = 0;
static uintptr_t g_materialsStorageB9E0PatchAddr = 0;
static uintptr_t g_materialsStorageBAD0PatchAddr = 0;
static uintptr_t g_materialsStorageD000PatchAddr = 0;
static uintptr_t g_materialsStorageD110PatchAddr = 0;
static uintptr_t g_materialsStorageD1D0PatchAddr = 0;
static uintptr_t g_materialsTraceUiPatchAddr = 0;
static uintptr_t g_materialsTraceTransitionPatchAddr = 0;
static uintptr_t g_materialsTraceDrawPatchAddr = 0;
static uintptr_t g_materialsGridRenderPatchAddr = 0;
static uintptr_t g_materialsPageDrawPatchAddr = 0;
static uintptr_t g_materialsContentDrawCallAddr = 0;
static uintptr_t g_materialsObjectDrawPatchAddr = 0;
static uintptr_t g_materialsD2ClientDrawEntryPatchAddr = 0;
static uintptr_t g_materialsD2ClientSpriteDrawPatchAddr = 0;
static uintptr_t g_materialsPage2ClonePatchAddr = 0;
static uintptr_t g_materialsActiveClickPatchAddr = 0;
static uintptr_t g_materialsHoverLookupPatchAddr = 0;
static uintptr_t g_materialsDescriptorLookupPatchAddr = 0;
static uintptr_t g_materialsDescriptorHasSlotPatchAddr = 0;
static uintptr_t g_materialsDescriptorTextPatchAddr = 0;
static uintptr_t g_materialsMsg1450PatchAddr = 0;
static uintptr_t g_materialsMsg1560PatchAddr = 0;
static bool      g_materialsTraceE80Hooked = false;
static bool      g_materialsTraceF70Hooked = false;
static bool      g_materialsTraceE20Hooked = false;
static bool      g_materialsTraceDB0Hooked = false;
static bool      g_materialsTraceEE0Hooked = false;
static bool      g_materialsTraceFF0Hooked = false;
static bool      g_materialsTrace3060Hooked = false;
static bool      g_materialsNativeD560Hooked = false;
static bool      g_materialsNativeE7B0Hooked = false;
static bool      g_materialsNativeE860Hooked = false;
static bool      g_materialsNativeE8C0Hooked = false;
static bool      g_materialsNativeE940Hooked = false;
static bool      g_materialsNativeF3A0Hooked = false;
static bool      g_materialsNativeF5F0Hooked = false;
static bool      g_materialsStorageB910Hooked = false;
static bool      g_materialsStorageB9E0Hooked = false;
static bool      g_materialsStorageBAD0Hooked = false;
static bool      g_materialsStorageD000Hooked = false;
static bool      g_materialsStorageD110Hooked = false;
static bool      g_materialsStorageD1D0Hooked = false;
static bool      g_materialsTraceUiHooked = false;
static bool      g_materialsTraceTransitionHooked = false;
static bool      g_materialsTraceDrawHooked = false;
static bool      g_materialsGridRenderHooked = false;
static bool      g_materialsPageDrawHooked = false;
static bool      g_materialsContentDrawCallHooked = false;
static bool      g_materialsObjectDrawHooked = false;
static bool      g_materialsD2ClientDrawEntryHooked = false;
static bool      g_materialsD2ClientSpriteDrawHooked = false;
static bool      g_materialsPage2CloneHooked = false;
static bool      g_materialsActiveClickHooked = false;
static bool      g_materialsHoverLookupHooked = false;
static bool      g_materialsDescriptorLookupHooked = false;
static bool      g_materialsDescriptorHasSlotHooked = false;
static bool      g_materialsDescriptorTextHooked = false;
static bool      g_materialsMsg1450Hooked = false;
static bool      g_materialsMsg1560Hooked = false;
#ifdef SOE_MATERIALS_PATCH_GATE_SKIP
static uintptr_t g_materialsGateSkipPatchAddr = 0;
static uint8_t   g_materialsGateSkipOriginal[2] = {};
static bool      g_materialsGateSkipHooked = false;
#endif
static uintptr_t* g_materialsD2Common11139Slot = nullptr;
static uintptr_t  g_materialsOrigD2Common11139 = 0;
static bool       g_materialsD2Common11139Hooked = false;
static volatile LONG g_materialsIteratorLogCount = 0;
static volatile LONG g_materialsIteratorPage2LogCount = 0;
static volatile LONG g_materialsIteratorPage2BlankCount = 0;
static volatile LONG g_materialsIteratorStackLogCount = 0;
static volatile LONG g_materialsIteratorBridgeLogCount = 0;
static volatile LONG g_materialsD2ClientDrawEntryLogCount = 0;
static volatile LONG g_materialsD2ClientDrawEntryPage2LogCount = 0;
static volatile LONG g_materialsNativeLogCount = 0;
static volatile LONG g_materialsStorageLogCount = 0;
static volatile LONG g_materialsMessageLogCount = 0;
static volatile LONG g_materialsDescriptorLookupTraceCount = 0;
static volatile LONG g_materialsDescriptorHelperTraceCount = 0;
static volatile LONG g_materialsGridRenderLogCount = 0;
static volatile LONG g_materialsPageDrawLogCount = 0;
static volatile LONG g_materialsObjectDrawLogCount = 0;
static volatile LONG g_materialsSpriteDrawLogCount = 0;
static volatile LONG g_materialsSpriteDrawSkipCount = 0;
static volatile LONG g_materialsPage2RulesLogCount = 0;
static volatile LONG g_materialsPage2RulesWriteCount = 0;
static volatile LONG g_materialsPage2NativeRendererSkipLogCount = 0;
static volatile LONG g_materialsD2ClientDrawWrapCount = 0;
static volatile LONG g_materialsD2ClientDrawWrapMissCount = 0;
#if defined(SOE_MATERIALS_WRAP_PAGE2_MODE4_EMPTY_CONTAINER) || defined(SOE_MATERIALS_WRAP_PAGE2_MODE0_EMPTY_CONTAINER)
static volatile LONG g_materialsD2ClientDrawWrapActive = 0;
static volatile uintptr_t g_materialsD2ClientDrawWrapObject = 0;
static volatile uintptr_t g_materialsD2ClientDrawWrapOriginalContainer = 0;
static uint8_t g_materialsPage2EmptyContainer[0x80] = {};
#endif
static volatile LONG g_materialsClickProbeLogCount = 0;
static volatile LONG g_materialsCatalogDumpCount = 0;
static volatile LONG g_materialsIteratorSweepDumpCount = 0;
static volatile LONG g_materialsNativeSwitchInProgress = 0;
static volatile LONG g_materialsPage2RenderContainer = 0;
static volatile LONG g_materialsPage2Active = 0;
static volatile LONG g_materialsDescriptorDumped = 0;
static volatile LONG g_materialsLastUiPanel = 0;
static volatile LONG g_materialsLastUiExtra = 0;
static volatile LONG g_materialsAllowNextTransition6 = 0;
static volatile LONG g_materialsLeftMaterials = 1;
static volatile LONG g_materialsPendingExternalEntry = 0;
static volatile LONG g_materialsSawExternalPage = 1;
static volatile LONG g_materialsExternalArmed = 1;
static volatile LONG g_materialsRealEntryClickSeen = 0;
static volatile LONG g_materialsSuppressNextSelfToggle = 0;
static volatile LONG g_materialsSuppressSelfToggleUntilTick = 0;
static volatile LONG g_materialsForceRealEntry = 0;
static volatile LONG g_materialsForceRealDraws = 0;
static volatile LONG g_materialsLastDrawMode = -1;
static volatile LONG g_materialsPage2ButtonDown = 0;
static volatile LONG g_materialsContentArgDumpedNormal = 0;
static volatile LONG g_materialsContentArgDumpedPage2 = 0;
#ifdef SOE_MATERIALS_ITERATOR_PAGE2_BURST_BLANK
static volatile LONG g_materialsPage2Mode4BurstUntilTick = 0;
static volatile uintptr_t g_materialsPage2Mode4BurstContainer = 0;
#endif
static volatile uintptr_t g_materialsLastUiWidget = 0;
static uintptr_t g_materialsLastSnapshotObject = 0;
static uintptr_t g_materialsLastSnapshotContainer = 0;
static uint32_t  g_materialsLastSnapshotPanel = 0xFFFFFFFFu;
static uint32_t  g_materialsLastSnapshotExtra = 0xFFFFFFFFu;
static uint32_t  g_materialsLastSnapshotMode = 0xFFFFFFFFu;
static uint32_t  g_materialsLastSnapshotSkip = 0xFFFFFFFFu;
static uint8_t   g_materialsDescriptorSnapshot[2160] = {};
static uintptr_t g_materialsDescriptorSnapshotTable = 0;
static bool      g_materialsDescriptorSnapshotReady = false;
static uint32_t  g_materialsSavedMaterialKey = 0;
static bool      g_materialsSavedMaterialKeyReady = false;
static bool      g_materialsCatalogSnapshotReady = false;
static uintptr_t g_materialsCatalogCountPtr = 0;
static uintptr_t g_materialsCatalogIndexPtr = 0;
static uintptr_t g_materialsCatalogCurrentPtr = 0;
static uintptr_t g_materialsCatalogHoverPtr = 0;
static uint32_t  g_materialsCatalogSavedCount = 0;
static uint32_t  g_materialsCatalogSavedIndex = 0;
static uint32_t  g_materialsCatalogSavedCurrent = 0;
static uint32_t  g_materialsCatalogSavedHover = 0;

struct MaterialsWindowEnumData {
    DWORD pid;
    HWND hwnd;
};

static HWND g_soePage2OverlayHwnd = nullptr;
static ATOM g_soePage2OverlayClass = 0;
static volatile LONG g_soePage2OverlayDrawn = 0;

struct SoEPage2Slot {
    uint32_t itemId;
    uint32_t count;
};

static const int SOE_PAGE2_SLOT_COUNT = 80;
static SoEPage2Slot g_soePage2Slots[SOE_PAGE2_SLOT_COUNT] = {};
static bool g_soePage2StorageLoaded = false;

static void AppendMaterialsTraceLine(const char* fmt, ...);

static BOOL CALLBACK MaterialsEnumWindowProc(HWND hwnd, LPARAM lParam)
{
    auto* data = (MaterialsWindowEnumData*)lParam;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != data->pid || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) {
        return TRUE;
    }

    char title[128] = {};
    char cls[128] = {};
    GetWindowTextA(hwnd, title, sizeof(title));
    GetClassNameA(hwnd, cls, sizeof(cls));
    if (strstr(title, "Diablo II") || strstr(title, "Project Diablo") || strstr(cls, "Diablo")) {
        data->hwnd = hwnd;
        return FALSE;
    }

    return TRUE;
}

static HWND FindMaterialsGameWindow()
{
    MaterialsWindowEnumData data = {};
    data.pid = GetCurrentProcessId();
    EnumWindows(MaterialsEnumWindowProc, (LPARAM)&data);
    if (!data.hwnd) {
        HWND fg = GetForegroundWindow();
        DWORD pid = 0;
        if (fg) GetWindowThreadProcessId(fg, &pid);
        if (fg && pid == data.pid) data.hwnd = fg;
    }
    if (!data.hwnd) {
        HWND active = GetActiveWindow();
        DWORD pid = 0;
        if (active) GetWindowThreadProcessId(active, &pid);
        if (active && pid == data.pid) data.hwnd = active;
    }
    return data.hwnd;
}

static LRESULT CALLBACK SoEPage2OverlayProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_ERASEBKGND) return 1;
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static HWND EnsureSoEPage2OverlayWindow(HWND parent)
{
    if (!parent) return nullptr;
    if (g_soePage2OverlayHwnd && IsWindow(g_soePage2OverlayHwnd)) return g_soePage2OverlayHwnd;

    HINSTANCE inst = (HINSTANCE)GetModuleHandleA(nullptr);
    if (!g_soePage2OverlayClass) {
        WNDCLASSEXA wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = SoEPage2OverlayProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = "SoECompanionMaterialsPage2Overlay";
        g_soePage2OverlayClass = RegisterClassExA(&wc);
        AppendMaterialsTraceLine("page2-overlay class atom=%u gle=%lu", (unsigned)g_soePage2OverlayClass, GetLastError());
    }
    if (!g_soePage2OverlayClass) {
        AppendMaterialsTraceLine("page2-overlay class failed gle=%lu", GetLastError());
        return nullptr;
    }

    g_soePage2OverlayHwnd = CreateWindowExA(
        WS_EX_TRANSPARENT,
        "SoECompanionMaterialsPage2Overlay",
        "SoE Materials Page 2",
        WS_CHILD | WS_VISIBLE,
        0, 0, 100, 100,
        parent,
        nullptr,
        inst,
        nullptr);
    if (g_soePage2OverlayHwnd) {
        AppendMaterialsTraceLine("page2-overlay child created parent=%p hwnd=%p gle=%lu", (void*)parent, (void*)g_soePage2OverlayHwnd, GetLastError());
    } else {
        AppendMaterialsTraceLine("page2-overlay create failed gle=%lu", GetLastError());
    }
    return g_soePage2OverlayHwnd;
}

static void HideSoEPage2Overlay()
{
    if (g_soePage2OverlayHwnd && IsWindow(g_soePage2OverlayHwnd)) {
        ShowWindow(g_soePage2OverlayHwnd, SW_HIDE);
    }
    InterlockedExchange(&g_soePage2OverlayDrawn, 0);
}

static void GetSoEPage2StoragePath(char* out, size_t outSize)
{
    if (!out || outSize == 0) return;
    out[0] = '\0';
    char dir[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableA("APPDATA", dir, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        _snprintf(out, outSize, "%s\\com.soecompanion.app", dir);
        CreateDirectoryA(out, nullptr);
        strncat(out, "\\materials_page2.bin", outSize - strlen(out) - 1);
    } else {
        GetTempPathA((DWORD)outSize, out);
        strncat(out, "soe_materials_page2.bin", outSize - strlen(out) - 1);
    }
}

static void LoadSoEPage2Storage()
{
    if (g_soePage2StorageLoaded) return;
    g_soePage2StorageLoaded = true;
    memset(g_soePage2Slots, 0, sizeof(g_soePage2Slots));

    char path[MAX_PATH] = {};
    GetSoEPage2StoragePath(path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (!f) {
        AppendMaterialsTraceLine("page2-storage new path=%s slots=%d", path, SOE_PAGE2_SLOT_COUNT);
        return;
    }
    size_t n = fread(g_soePage2Slots, sizeof(SoEPage2Slot), SOE_PAGE2_SLOT_COUNT, f);
    fclose(f);
    AppendMaterialsTraceLine("page2-storage loaded path=%s slots=%u", path, (unsigned)n);
}

static void SaveSoEPage2Storage()
{
    char path[MAX_PATH] = {};
    GetSoEPage2StoragePath(path, sizeof(path));
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fwrite(g_soePage2Slots, sizeof(SoEPage2Slot), SOE_PAGE2_SLOT_COUNT, f);
    fclose(f);
}

static uint32_t SafeReadDwordTrace(uintptr_t addr)
{
    if (!addr || IsBadReadPtr((void*)addr, sizeof(uint32_t))) return 0xFFFFFFFFu;
    return *(uint32_t*)addr;
}

static uint8_t SafeReadByteTrace(uintptr_t addr)
{
    if (!addr || IsBadReadPtr((void*)addr, sizeof(uint8_t))) return 0;
    return *(uint8_t*)addr;
}

static uintptr_t SafeReadPtrTrace(uintptr_t addr)
{
    if (!addr || IsBadReadPtr((void*)addr, sizeof(uintptr_t))) return 0;
    return *(uintptr_t*)addr;
}

static void DescribeAddressTrace(uintptr_t addr, char* out, size_t outSize)
{
    if (!out || outSize == 0) return;
    out[0] = '\0';
    MEMORY_BASIC_INFORMATION mbi = {};
    if (!addr || !VirtualQuery((void*)addr, &mbi, sizeof(mbi))) {
        _snprintf(out, outSize, "addr=%p module=<unknown>", (void*)addr);
        return;
    }
    char path[MAX_PATH] = {};
    HMODULE module = (HMODULE)mbi.AllocationBase;
    GetModuleFileNameA(module, path, sizeof(path));
    const char* name = strrchr(path, '\\');
    name = name ? name + 1 : path;
    _snprintf(out, outSize, "addr=%p module=%s base=%p rva=%08x",
        (void*)addr,
        name && *name ? name : "<unknown>",
        (void*)module,
        (unsigned)(addr - (uintptr_t)module));
}

static void AppendMaterialsTraceLine(const char* fmt, ...)
{
    char logPath[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableA("APPDATA", logPath, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        strncat(logPath, "\\com.soecompanion.app", MAX_PATH - strlen(logPath) - 1);
        CreateDirectoryA(logPath, nullptr);
        strncat(logPath, "\\soe_materials_trace.log", MAX_PATH - strlen(logPath) - 1);
    } else {
        GetTempPathA(MAX_PATH, logPath);
        strncat(logPath, "soe_materials_trace.log", MAX_PATH - strlen(logPath) - 1);
    }
    FILE* f = fopen(logPath, "a");
    if (!f) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "%02d:%02d:%02d.%03d ",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fprintf(f, "\n");
    fclose(f);
}

static void DumpMaterialsPointerBlock(const char* label, const char* field, uintptr_t ptr)
{
    if (!ptr || IsBadReadPtr((void*)ptr, 0x80)) {
        AppendMaterialsTraceLine("ptrblock %s %s=%p unreadable", label ? label : "?", field ? field : "?", (void*)ptr);
        return;
    }

    AppendMaterialsTraceLine(
        "ptrblock %s %s=%p dwords00-7c=%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x",
        label ? label : "?",
        field ? field : "?",
        (void*)ptr,
        SafeReadDwordTrace(ptr + 0x00), SafeReadDwordTrace(ptr + 0x04), SafeReadDwordTrace(ptr + 0x08), SafeReadDwordTrace(ptr + 0x0c),
        SafeReadDwordTrace(ptr + 0x10), SafeReadDwordTrace(ptr + 0x14), SafeReadDwordTrace(ptr + 0x18), SafeReadDwordTrace(ptr + 0x1c),
        SafeReadDwordTrace(ptr + 0x20), SafeReadDwordTrace(ptr + 0x24), SafeReadDwordTrace(ptr + 0x28), SafeReadDwordTrace(ptr + 0x2c),
        SafeReadDwordTrace(ptr + 0x30), SafeReadDwordTrace(ptr + 0x34), SafeReadDwordTrace(ptr + 0x38), SafeReadDwordTrace(ptr + 0x3c),
        SafeReadDwordTrace(ptr + 0x40), SafeReadDwordTrace(ptr + 0x44), SafeReadDwordTrace(ptr + 0x48), SafeReadDwordTrace(ptr + 0x4c),
        SafeReadDwordTrace(ptr + 0x50), SafeReadDwordTrace(ptr + 0x54), SafeReadDwordTrace(ptr + 0x58), SafeReadDwordTrace(ptr + 0x5c),
        SafeReadDwordTrace(ptr + 0x60), SafeReadDwordTrace(ptr + 0x64), SafeReadDwordTrace(ptr + 0x68), SafeReadDwordTrace(ptr + 0x6c),
        SafeReadDwordTrace(ptr + 0x70), SafeReadDwordTrace(ptr + 0x74), SafeReadDwordTrace(ptr + 0x78), SafeReadDwordTrace(ptr + 0x7c));
}

static void ProbeMaterialsNativeGridIds(const char* label, uintptr_t base, uintptr_t container)
{
#ifdef SOE_MATERIALS_PROBE_GRID_IDS
    if (!base || !container || IsBadReadPtr((void*)container, 0x20)) return;

    typedef uintptr_t (__stdcall *FnGridLookup)(uint32_t, uintptr_t, uintptr_t);
    FnGridLookup gridLookup = (FnGridLookup)(base + 0x23f6d0);
    for (uint32_t gridId = 0; gridId <= 16; ++gridId) {
        uint8_t out[0x30] = {};
        uintptr_t grid = gridLookup(gridId, container, (uintptr_t)out);
        AppendMaterialsTraceLine(
            "grid-probe %s id=%u container=%p grid=%p grid00=%08x grid08=%02x grid09=%02x grid0c=%p out=%08x %08x %08x %08x %08x %08x",
            label ? label : "?",
            (unsigned)gridId,
            (void*)container,
            (void*)grid,
            SafeReadDwordTrace(grid + 0x00),
            SafeReadByteTrace(grid + 0x08),
            SafeReadByteTrace(grid + 0x09),
            (void*)SafeReadPtrTrace(grid + 0x0c),
            SafeReadDwordTrace((uintptr_t)out + 0x00),
            SafeReadDwordTrace((uintptr_t)out + 0x04),
            SafeReadDwordTrace((uintptr_t)out + 0x08),
            SafeReadDwordTrace((uintptr_t)out + 0x0c),
            SafeReadDwordTrace((uintptr_t)out + 0x10),
            SafeReadDwordTrace((uintptr_t)out + 0x14));
    }
#else
    (void)label;
    (void)base;
    (void)container;
#endif
}

static void ProbeMaterialsPacketGrid(
    const char* label,
    uintptr_t base,
    uintptr_t container,
    uint32_t packetSelector,
    uintptr_t packet)
{
#ifdef SOE_MATERIALS_PROBE_GRID_IDS
    if (!base || !container || IsBadReadPtr((void*)container, 0x20)) return;

    typedef uintptr_t (__stdcall *FnGridLookup)(uint32_t, uintptr_t, uintptr_t);
    FnGridLookup gridLookup = (FnGridLookup)(base + 0x23f6d0);
    uint32_t gridId = packetSelector + 2u;
    uint8_t out[0x30] = {};
    uintptr_t grid = gridLookup(gridId, container, (uintptr_t)out);
    AppendMaterialsTraceLine(
        "msg-grid %s selector=%u gridId=%u packet=%p container=%p grid=%p gridDims=%ux%u grid0c=%p clear=%02x flags=%02x pos=%u,%u out=%08x %08x %08x %08x %08x %08x",
        label ? label : "?",
        (unsigned)packetSelector,
        (unsigned)gridId,
        (void*)packet,
        (void*)container,
        (void*)grid,
        (unsigned)SafeReadByteTrace(grid + 0x08),
        (unsigned)SafeReadByteTrace(grid + 0x09),
        (void*)SafeReadPtrTrace(grid + 0x0c),
        (unsigned)SafeReadByteTrace(packet + 0x09),
        (unsigned)SafeReadByteTrace(packet + 0x0a),
        (unsigned)SafeReadByteTrace(packet + 0x07),
        (unsigned)SafeReadByteTrace(packet + 0x08),
        SafeReadDwordTrace((uintptr_t)out + 0x00),
        SafeReadDwordTrace((uintptr_t)out + 0x04),
        SafeReadDwordTrace((uintptr_t)out + 0x08),
        SafeReadDwordTrace((uintptr_t)out + 0x0c),
        SafeReadDwordTrace((uintptr_t)out + 0x10),
        SafeReadDwordTrace((uintptr_t)out + 0x14));
#else
    (void)label;
    (void)base;
    (void)container;
    (void)packetSelector;
    (void)packet;
#endif
}

static void DumpMaterialsHcBlock(const char* label, uintptr_t unit)
{
    if (!unit || IsBadReadPtr((void*)unit, 0x18)) {
        AppendMaterialsTraceLine("hcblock %s unit=%p unreadable", label ? label : "?", (void*)unit);
        return;
    }

    uintptr_t data = SafeReadPtrTrace(unit + 0x14);
    uintptr_t hc = data ? data + 0x512 : 0;
    if (!hc || IsBadReadPtr((void*)hc, 0x80)) {
        AppendMaterialsTraceLine(
            "hcblock %s unit=%p data=%p hc=%p unreadable",
            label ? label : "?",
            (void*)unit,
            (void*)data,
            (void*)hc);
        return;
    }

    char line[1600] = {};
    size_t used = 0;
    for (int i = 0; i < 16; ++i) {
        uintptr_t p = hc + (uintptr_t)i * 8;
        int wrote = _snprintf(
            line + used,
            sizeof(line) - used,
            "%s%02d:{dw0=%08x dw4=%08x w0=%04x w2=%04x}",
            used ? " | " : "",
            i,
            SafeReadDwordTrace(p + 0),
            SafeReadDwordTrace(p + 4),
            SafeReadDwordTrace(p + 0) & 0xffffu,
            (SafeReadDwordTrace(p + 0) >> 16) & 0xffffu);
        if (wrote <= 0) break;
        used += (size_t)wrote;
        if (used >= sizeof(line) - 1) break;
    }

    AppendMaterialsTraceLine(
        "hcblock %s unit=%p data=%p hc=%p records %s",
        label ? label : "?",
        (void*)unit,
        (void*)data,
        (void*)hc,
        line);
}

static uint16_t FindMaterialsDescriptorSlotTrace(uintptr_t table, uint32_t itemId, uint32_t key)
{
    if (!table || IsBadReadPtr((void*)table, 216 * 10)) return 0xffff;
    for (int i = 0; i < 216; ++i) {
        uintptr_t entry = table + (uintptr_t)i * 10;
        if (SafeReadDwordTrace(entry + 0) == itemId && SafeReadDwordTrace(entry + 6) == key) {
            return (uint16_t)(SafeReadDwordTrace(entry + 4) & 0xffffu);
        }
    }
    return 0xffff;
}

static void DumpMaterialsCatalogState(const char* label, uintptr_t base)
{
    if (!base) return;

    LONG dumpNo = InterlockedIncrement(&g_materialsCatalogDumpCount);
    if (dumpNo > 10) return;

    uintptr_t countPtr = SafeReadPtrTrace(base + 0x410314);
    uintptr_t indexBasePtr = SafeReadPtrTrace(base + 0x4102f8);
    uintptr_t currentPtr = SafeReadPtrTrace(base + 0x41010c);
    uintptr_t hoverPtr = SafeReadPtrTrace(base + 0x41020c);
    uintptr_t activePtr = SafeReadPtrTrace(base + 0x410998);
    uintptr_t table = SafeReadPtrTrace(base + 0x41076c);
    uint32_t count = SafeReadDwordTrace(countPtr);
    uint32_t indexBase = SafeReadDwordTrace(indexBasePtr);
    uint32_t current = SafeReadDwordTrace(currentPtr);
    uint32_t hover = SafeReadDwordTrace(hoverPtr);
    uint32_t active = SafeReadDwordTrace(activePtr);

    AppendMaterialsTraceLine(
        "catalog %s #%ld controls countPtr=%p count=%u indexPtr=%p indexBase=%u currentPtr=%p current=%u hoverPtr=%p hover=%u activePtr=%p active=%u table=%p page2=%ld panel=%08x extra=%08x mat=%08x",
        label ? label : "?",
        dumpNo,
        (void*)countPtr,
        count,
        (void*)indexBasePtr,
        indexBase,
        (void*)currentPtr,
        current,
        (void*)hoverPtr,
        hover,
        (void*)activePtr,
        active,
        (void*)table,
        (long)g_materialsPage2Active,
        SafeReadDwordTrace(SafeReadPtrTrace(base + 0x410688)),
        SafeReadDwordTrace(SafeReadPtrTrace(base + 0x410a74)),
        SafeReadDwordTrace(base + 0x30dca8));

    for (uint32_t start = 0; start < 128; start += 8) {
        char line[1400] = {};
        size_t used = 0;
        for (uint32_t i = 0; i < 8; ++i) {
            uint32_t idx = start + i;
            uintptr_t entry = base + 0x2d4668 + (uintptr_t)idx * 10;
            uint32_t itemId = SafeReadDwordTrace(entry + 0);
            uint32_t nameId = SafeReadDwordTrace(entry + 4) & 0xffffu;
            uint32_t flags = SafeReadDwordTrace(entry + 8) & 0xffffu;
            uint16_t slot0 = FindMaterialsDescriptorSlotTrace(table, itemId, 0);
            uint16_t slot1 = FindMaterialsDescriptorSlotTrace(table, itemId, 1);
            int wrote = _snprintf(
                line + used,
                sizeof(line) - used,
                "%s%u:%u/%u/%u/k0=%04x/k1=%04x",
                used ? " | " : "",
                idx,
                itemId,
                nameId,
                flags,
                slot0,
                slot1);
            if (wrote <= 0) break;
            used += (size_t)wrote;
            if (used >= sizeof(line) - 1) break;
        }
        AppendMaterialsTraceLine("catalog %s rows %u-%u %s", label ? label : "?", start, start + 7, line);
    }

    char found[1200] = {};
    size_t used = 0;
    for (uint32_t idx = 0; idx < 512; ++idx) {
        uintptr_t entry = base + 0x2d4668 + (uintptr_t)idx * 10;
        uint32_t itemId = SafeReadDwordTrace(entry + 0);
        if (itemId < 60 || itemId > 107) continue;
        uint32_t nameId = SafeReadDwordTrace(entry + 4) & 0xffffu;
        int wrote = _snprintf(
            found + used,
            sizeof(found) - used,
            "%sidx%u=item%u/name%u",
            used ? "," : "",
            idx,
            itemId,
            nameId);
        if (wrote <= 0) break;
        used += (size_t)wrote;
        if (used >= sizeof(found) - 1) break;
    }
    AppendMaterialsTraceLine("catalog %s item60-107 indices [%s]", label ? label : "?", used ? found : "-");
}

static void DumpMaterialsIteratorSweep(const char* label, uintptr_t base)
{
    if (!base || !g_materialsOrigD2Common11139) return;

    LONG dumpNo = InterlockedIncrement(&g_materialsIteratorSweepDumpCount);
    if (dumpNo > 8) return;

    uintptr_t objectSlot = SafeReadPtrTrace(base + 0x410224);
    uintptr_t object = SafeReadPtrTrace(objectSlot);
    uintptr_t container = object ? SafeReadPtrTrace(object + 0x60) : 0;
    uintptr_t table = SafeReadPtrTrace(base + 0x41076c);
    if (!container || IsBadReadPtr((void*)container, 0x20)) {
        AppendMaterialsTraceLine(
            "iterator-sweep %s #%ld no-container objectSlot=%p object=%p container=%p",
            label ? label : "?",
            dumpNo,
            (void*)objectSlot,
            (void*)object,
            (void*)container);
        return;
    }

    typedef uintptr_t (__stdcall *Fn)(uintptr_t, uint32_t);
    Fn orig = (Fn)g_materialsOrigD2Common11139;

    AppendMaterialsTraceLine(
        "iterator-sweep %s #%ld objectSlot=%p object=%p container=%p count=%u indexBase=%u page2=%ld panel=%08x extra=%08x mat=%08x",
        label ? label : "?",
        dumpNo,
        (void*)objectSlot,
        (void*)object,
        (void*)container,
        SafeReadDwordTrace(SafeReadPtrTrace(base + 0x410314)),
        SafeReadDwordTrace(SafeReadPtrTrace(base + 0x4102f8)),
        (long)g_materialsPage2Active,
        SafeReadDwordTrace(SafeReadPtrTrace(base + 0x410688)),
        SafeReadDwordTrace(SafeReadPtrTrace(base + 0x410a74)),
        SafeReadDwordTrace(base + 0x30dca8));

    for (uint32_t start = 0; start < 128; start += 8) {
        char line[1800] = {};
        size_t used = 0;
        bool anyResult = false;
        for (uint32_t i = 0; i < 8; ++i) {
            uint32_t idx = start + i;
            uintptr_t catalog = base + 0x2d4668 + (uintptr_t)idx * 10;
            uint32_t catalogItem = SafeReadDwordTrace(catalog + 0);
            uint32_t catalogName = SafeReadDwordTrace(catalog + 4) & 0xffffu;
            uint16_t slot0 = FindMaterialsDescriptorSlotTrace(table, catalogItem, 0);
            uint16_t slot1 = FindMaterialsDescriptorSlotTrace(table, catalogItem, 1);
            uintptr_t result = orig(container, idx);
            if (result) anyResult = true;
            int wrote = _snprintf(
                line + used,
                sizeof(line) - used,
                "%s%u(cat=%u/%u k0=%04x k1=%04x)->%p[%08x,%08x,%08x,%08x,%08x,%08x,%08x]",
                used ? " | " : "",
                idx,
                catalogItem,
                catalogName,
                slot0,
                slot1,
                (void*)result,
                SafeReadDwordTrace(result + 0x00),
                SafeReadDwordTrace(result + 0x04),
                SafeReadDwordTrace(result + 0x08),
                SafeReadDwordTrace(result + 0x0c),
                SafeReadDwordTrace(result + 0x10),
                SafeReadDwordTrace(result + 0x14),
                SafeReadDwordTrace(result + 0x2c));
            if (wrote <= 0) break;
            used += (size_t)wrote;
            if (used >= sizeof(line) - 1) break;
        }
        if (anyResult || (start >= 56 && start <= 120)) {
            AppendMaterialsTraceLine(
                "iterator-sweep %s #%ld rows %u-%u %s",
                label ? label : "?",
                dumpNo,
                start,
                start + 7,
                line);
        }
    }
}

static void DumpMaterialsObjectSlotNeighborhood(const char* label, uintptr_t slotPtr, uintptr_t currentObject)
{
    if (!slotPtr || IsBadReadPtr((void*)slotPtr, sizeof(uintptr_t))) {
        AppendMaterialsTraceLine("objslot %s slot=%p unreadable", label ? label : "?", (void*)slotPtr);
        return;
    }

    for (int rel = -0x80; rel <= 0x80; rel += 4) {
        uintptr_t addr = slotPtr + rel;
        uintptr_t value = SafeReadPtrTrace(addr);
        bool objectLike = value && !IsBadReadPtr((void*)value, 0x68);
        if (!objectLike && value == 0 && rel != 0) continue;

        uintptr_t container = objectLike ? SafeReadPtrTrace(value + 0x60) : 0;
        uintptr_t head = container ? SafeReadPtrTrace(container + 0x0c) : 0;
        AppendMaterialsTraceLine(
            "objslot %s rel=%+04x addr=%p value=%p current=%d obj=%08x %08x %08x %08x %08x %08x cont=%p cont00=%08x cont0c=%08x cont14=%08x head=%p",
            label ? label : "?",
            rel,
            (void*)addr,
            (void*)value,
            value == currentObject ? 1 : 0,
            objectLike ? SafeReadDwordTrace(value + 0x00) : 0xffffffffu,
            objectLike ? SafeReadDwordTrace(value + 0x04) : 0xffffffffu,
            objectLike ? SafeReadDwordTrace(value + 0x08) : 0xffffffffu,
            objectLike ? SafeReadDwordTrace(value + 0x0c) : 0xffffffffu,
            objectLike ? SafeReadDwordTrace(value + 0x10) : 0xffffffffu,
            objectLike ? SafeReadDwordTrace(value + 0x14) : 0xffffffffu,
            (void*)container,
            container ? SafeReadDwordTrace(container + 0x00) : 0xffffffffu,
            container ? SafeReadDwordTrace(container + 0x0c) : 0xffffffffu,
            container ? SafeReadDwordTrace(container + 0x14) : 0xffffffffu,
            (void*)head);
    }
}

static void DumpMaterialsStateSnapshot(const char* label, uintptr_t base, uintptr_t objectPtr, int skipDraw, bool force)
{
    if (!base) return;

    uintptr_t ptr410224 = SafeReadPtrTrace(base + 0x410224);
    uintptr_t object = objectPtr ? objectPtr : SafeReadPtrTrace(ptr410224);
    uintptr_t container = object ? SafeReadPtrTrace(object + 0x60) : 0;
    uintptr_t ptr410688 = SafeReadPtrTrace(base + 0x410688);
    uintptr_t ptr410a74 = SafeReadPtrTrace(base + 0x410a74);
    uintptr_t ptr410998 = SafeReadPtrTrace(base + 0x410998);
    uintptr_t modePtr = SafeReadPtrTrace(base + 0x4101e4);
    uintptr_t table = SafeReadPtrTrace(base + 0x41076c);
    uintptr_t resolvedDraw = SafeReadPtrTrace(base + 0x4186c4);

    uint32_t panel = SafeReadDwordTrace(ptr410688);
    uint32_t extra = SafeReadDwordTrace(ptr410a74);
    uint32_t mode = SafeReadDwordTrace(modePtr);
    uint32_t activeIndex = SafeReadDwordTrace(ptr410998);
    uint32_t activeItemId = 0xFFFFFFFFu;
    if (activeIndex != 0xFFFFFFFFu && activeIndex < 0x10000u) {
        activeItemId = SafeReadDwordTrace(base + 0x2d4668 + (uintptr_t)activeIndex * 10);
    }

    if (!force &&
        object == g_materialsLastSnapshotObject &&
        container == g_materialsLastSnapshotContainer &&
        panel == g_materialsLastSnapshotPanel &&
        extra == g_materialsLastSnapshotExtra &&
        mode == g_materialsLastSnapshotMode &&
        (uint32_t)skipDraw == g_materialsLastSnapshotSkip) {
        return;
    }

    g_materialsLastSnapshotObject = object;
    g_materialsLastSnapshotContainer = container;
    g_materialsLastSnapshotPanel = panel;
    g_materialsLastSnapshotExtra = extra;
    g_materialsLastSnapshotMode = mode;
    g_materialsLastSnapshotSkip = (uint32_t)skipDraw;

    AppendMaterialsTraceLine(
        "state %s skip=%d panel=%08x extra=%08x page2=%ld left=%ld pending=%ld external=%ld armed=%ld force=%ld forceDraws=%ld mode=%u modePtr=%p objSlot=%p object=%p container60=%p table=%p draw=%p stash=%08x mat=%08x hover=%08x active=%08x activeItem=%08x",
        label ? label : "?",
        skipDraw,
        panel,
        extra,
        (long)g_materialsPage2Active,
        (long)g_materialsLeftMaterials,
        (long)g_materialsPendingExternalEntry,
        (long)g_materialsSawExternalPage,
        (long)g_materialsExternalArmed,
        (long)g_materialsForceRealEntry,
        (long)g_materialsForceRealDraws,
        mode,
        (void*)modePtr,
        (void*)ptr410224,
        (void*)object,
        (void*)container,
        (void*)table,
        (void*)resolvedDraw,
        SafeReadDwordTrace(base + 0x2d4b24),
        SafeReadDwordTrace(base + 0x30dca8),
        SafeReadDwordTrace(base + 0x30de48),
        activeIndex,
        activeItemId);

    if (object && !IsBadReadPtr((void*)object, 0x80)) {
        AppendMaterialsTraceLine(
            "state %s object=%p dwords00-7c=%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x",
            label ? label : "?",
            (void*)object,
            SafeReadDwordTrace(object + 0x00), SafeReadDwordTrace(object + 0x04), SafeReadDwordTrace(object + 0x08), SafeReadDwordTrace(object + 0x0c),
            SafeReadDwordTrace(object + 0x10), SafeReadDwordTrace(object + 0x14), SafeReadDwordTrace(object + 0x18), SafeReadDwordTrace(object + 0x1c),
            SafeReadDwordTrace(object + 0x20), SafeReadDwordTrace(object + 0x24), SafeReadDwordTrace(object + 0x28), SafeReadDwordTrace(object + 0x2c),
            SafeReadDwordTrace(object + 0x30), SafeReadDwordTrace(object + 0x34), SafeReadDwordTrace(object + 0x38), SafeReadDwordTrace(object + 0x3c),
            SafeReadDwordTrace(object + 0x40), SafeReadDwordTrace(object + 0x44), SafeReadDwordTrace(object + 0x48), SafeReadDwordTrace(object + 0x4c),
            SafeReadDwordTrace(object + 0x50), SafeReadDwordTrace(object + 0x54), SafeReadDwordTrace(object + 0x58), SafeReadDwordTrace(object + 0x5c),
            SafeReadDwordTrace(object + 0x60), SafeReadDwordTrace(object + 0x64), SafeReadDwordTrace(object + 0x68), SafeReadDwordTrace(object + 0x6c),
            SafeReadDwordTrace(object + 0x70), SafeReadDwordTrace(object + 0x74), SafeReadDwordTrace(object + 0x78), SafeReadDwordTrace(object + 0x7c));

        if (force || g_materialsPage2Active) {
            DumpMaterialsObjectSlotNeighborhood(label, ptr410224, object);
            DumpMaterialsPointerBlock(label, "object+1c", SafeReadPtrTrace(object + 0x1c));
            DumpMaterialsPointerBlock(label, "object+2c", SafeReadPtrTrace(object + 0x2c));
            DumpMaterialsPointerBlock(label, "object+54", SafeReadPtrTrace(object + 0x54));
            DumpMaterialsPointerBlock(label, "object+5c", SafeReadPtrTrace(object + 0x5c));
            DumpMaterialsPointerBlock(label, "object+60", SafeReadPtrTrace(object + 0x60));
            DumpMaterialsPointerBlock(label, "object+64", SafeReadPtrTrace(object + 0x64));
        }
    }

    if (container && !IsBadReadPtr((void*)container, 0x80)) {
        AppendMaterialsTraceLine(
            "state %s container=%p dwords00-7c=%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x",
            label ? label : "?",
            (void*)container,
            SafeReadDwordTrace(container + 0x00), SafeReadDwordTrace(container + 0x04), SafeReadDwordTrace(container + 0x08), SafeReadDwordTrace(container + 0x0c),
            SafeReadDwordTrace(container + 0x10), SafeReadDwordTrace(container + 0x14), SafeReadDwordTrace(container + 0x18), SafeReadDwordTrace(container + 0x1c),
            SafeReadDwordTrace(container + 0x20), SafeReadDwordTrace(container + 0x24), SafeReadDwordTrace(container + 0x28), SafeReadDwordTrace(container + 0x2c),
            SafeReadDwordTrace(container + 0x30), SafeReadDwordTrace(container + 0x34), SafeReadDwordTrace(container + 0x38), SafeReadDwordTrace(container + 0x3c),
            SafeReadDwordTrace(container + 0x40), SafeReadDwordTrace(container + 0x44), SafeReadDwordTrace(container + 0x48), SafeReadDwordTrace(container + 0x4c),
            SafeReadDwordTrace(container + 0x50), SafeReadDwordTrace(container + 0x54), SafeReadDwordTrace(container + 0x58), SafeReadDwordTrace(container + 0x5c),
            SafeReadDwordTrace(container + 0x60), SafeReadDwordTrace(container + 0x64), SafeReadDwordTrace(container + 0x68), SafeReadDwordTrace(container + 0x6c),
            SafeReadDwordTrace(container + 0x70), SafeReadDwordTrace(container + 0x74), SafeReadDwordTrace(container + 0x78), SafeReadDwordTrace(container + 0x7c));

        DumpMaterialsPointerBlock(label, "container+10", SafeReadPtrTrace(container + 0x10));
        DumpMaterialsPointerBlock(label, "container+14", SafeReadPtrTrace(container + 0x14));
        DumpMaterialsPointerBlock(label, "container+40", SafeReadPtrTrace(container + 0x40));
        DumpMaterialsPointerBlock(label, "container+44", SafeReadPtrTrace(container + 0x44));
    }
}

static bool PatchMaterialsJump(uintptr_t patchAddr, void* hook, uint8_t* original, size_t patchLen, uintptr_t* trampolineOut)
{
    if (!patchAddr || !hook || !original || !trampolineOut || patchLen < 5) return false;

    uint8_t* gateway = (uint8_t*)VirtualAlloc(nullptr, patchLen + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!gateway) return false;

    DWORD oldProt = 0;
    if (!VirtualProtect((void*)patchAddr, patchLen, PAGE_EXECUTE_READWRITE, &oldProt)) {
        VirtualFree(gateway, 0, MEM_RELEASE);
        return false;
    }

    memcpy(original, (void*)patchAddr, patchLen);
    memcpy(gateway, original, patchLen);
    gateway[patchLen] = 0xE9;
    *(int32_t*)(gateway + patchLen + 1) = (int32_t)((patchAddr + patchLen) - ((uintptr_t)gateway + patchLen + 5));
    *trampolineOut = (uintptr_t)gateway;

    uint8_t patch[16] = {};
    patch[0] = 0xE9;
    *(int32_t*)&patch[1] = (int32_t)((uintptr_t)hook - (patchAddr + 5));
    for (size_t i = 5; i < patchLen; ++i) patch[i] = 0x90;
    memcpy((void*)patchAddr, patch, patchLen);

    FlushInstructionCache(GetCurrentProcess(), (void*)patchAddr, patchLen);
    FlushInstructionCache(GetCurrentProcess(), gateway, patchLen + 5);
    VirtualProtect((void*)patchAddr, patchLen, oldProt, &oldProt);
    return true;
}

static void RestoreMaterialsJump(uintptr_t patchAddr, const uint8_t* original, size_t patchLen, uintptr_t* trampoline)
{
    if (patchAddr && original && patchLen) {
        DWORD oldProt = 0;
        if (VirtualProtect((void*)patchAddr, patchLen, PAGE_EXECUTE_READWRITE, &oldProt)) {
            memcpy((void*)patchAddr, original, patchLen);
            FlushInstructionCache(GetCurrentProcess(), (void*)patchAddr, patchLen);
            VirtualProtect((void*)patchAddr, patchLen, oldProt, &oldProt);
        }
    }
    if (trampoline && *trampoline) {
        VirtualFree((void*)*trampoline, 0, MEM_RELEASE);
        *trampoline = 0;
    }
}

static bool PatchMaterialsCall(uintptr_t patchAddr, void* hook, uint8_t* original)
{
    if (!patchAddr || !hook || !original) return false;
    DWORD oldProt = 0;
    if (!VirtualProtect((void*)patchAddr, 5, PAGE_EXECUTE_READWRITE, &oldProt)) return false;

    memcpy(original, (void*)patchAddr, 5);
    uint8_t patch[5] = {};
    patch[0] = 0xE8;
    *(int32_t*)&patch[1] = (int32_t)((uintptr_t)hook - (patchAddr + 5));
    memcpy((void*)patchAddr, patch, sizeof(patch));

    FlushInstructionCache(GetCurrentProcess(), (void*)patchAddr, 5);
    VirtualProtect((void*)patchAddr, 5, oldProt, &oldProt);
    return true;
}

static void RestoreMaterialsBytes(uintptr_t patchAddr, const uint8_t* original, size_t patchLen)
{
    if (!patchAddr || !original || !patchLen) return;
    DWORD oldProt = 0;
    if (!VirtualProtect((void*)patchAddr, patchLen, PAGE_EXECUTE_READWRITE, &oldProt)) return;
    memcpy((void*)patchAddr, original, patchLen);
    FlushInstructionCache(GetCurrentProcess(), (void*)patchAddr, patchLen);
    VirtualProtect((void*)patchAddr, patchLen, oldProt, &oldProt);
}

extern "C" void __stdcall LogMaterialsTabHit(uint32_t rva, uintptr_t widget)
{
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    if (!base) {
        AppendMaterialsTraceLine("tab rva=%08x widget=%p project=missing", (unsigned)rva, (void*)widget);
        return;
    }

    uintptr_t ptr410a2c = SafeReadPtrTrace(base + 0x410a2c);
    uintptr_t ptr410a74 = SafeReadPtrTrace(base + 0x410a74);
    uintptr_t ptr410688 = SafeReadPtrTrace(base + 0x410688);
    uintptr_t ptr410998 = SafeReadPtrTrace(base + 0x410998);
    uint32_t activeIndex = SafeReadDwordTrace(ptr410998);
    uint32_t activeItemId = 0xFFFFFFFFu;
    if (activeIndex != 0xFFFFFFFFu && activeIndex < 0x10000u) {
        activeItemId = SafeReadDwordTrace(base + 0x2d4668 + (uintptr_t)activeIndex * 10);
    }

    AppendMaterialsTraceLine(
        "tab rva=%08x widget=%p w0c=%08x w18=%08x w1c=%08x busyPtr=%p busy=%08x stash=%08x mat=%08x hover=%08x activePtr=%p active=%08x activeItem=%08x panelPtr=%p panel=%08x extraPtr=%p extra=%08x",
        (unsigned)rva,
        (void*)widget,
        SafeReadDwordTrace(widget + 0x0c),
        SafeReadDwordTrace(widget + 0x18),
        SafeReadDwordTrace(widget + 0x1c),
        (void*)ptr410a2c,
        SafeReadDwordTrace(ptr410a2c),
        SafeReadDwordTrace(base + 0x2d4b24),
        SafeReadDwordTrace(base + 0x30dca8),
        SafeReadDwordTrace(base + 0x30de48),
        (void*)ptr410998,
        activeIndex,
        activeItemId,
        (void*)ptr410688,
        SafeReadDwordTrace(ptr410688),
        (void*)ptr410a74,
        SafeReadDwordTrace(ptr410a74));
    DumpMaterialsStateSnapshot("tab", base, 0, 0, true);
}

static void DumpMaterialsDescriptorTable(uintptr_t base);
static void DumpMaterialsCatalogState(const char* label, uintptr_t base);
static void DumpMaterialsStateSnapshot(const char* label, uintptr_t base, uintptr_t objectPtr, int skipDraw, bool force);
static void ApplyMaterialsPage2DescriptorView(uintptr_t base);
static void RestoreMaterialsDescriptorView();
static void RestoreMaterialsCatalogWindow();
static void ApplyMaterialsPage2CatalogWindow(uintptr_t base, const char* reason);
static void DrawSoEPage2Scaffold(uintptr_t base);
static void GetSoEPage2BottomButtonRect(int baseX, int baseY, int* left, int* top, int* right, int* bottom);
static void DrawSoEPage2ButtonOverlay(uintptr_t base, const char* reason);
static void DrawSoEPage2MaterialMask(uintptr_t base, const char* reason);
static void TryNativeMaterialsPageSwitch(uint32_t page, const char* reason);
static void TryNativeStashPageStep(const char* reason);
static void TryNativeStashSelectPage2(const char* reason);
static void TryNativeStashCoordSelectPage2(const char* reason);
static void TryNativePageListSelectorPage2(const char* reason);
static void TryNativeStashPacketPage2(const char* reason);
static void SetMaterialsDescriptorPage2(uintptr_t base, bool active, const char* reason);
static bool IsMaterialsPage2ObjectActive(uintptr_t base, uint32_t* outPage);
static bool WriteMaterialsDword(uintptr_t addr, uint32_t value);
static void ApplyMaterialsPage2KeyedDescriptorPage(uintptr_t base, uint32_t key, const char* reason);
static void MaintainMaterialsPage2NativeKey(uintptr_t base, const char* reason);
static void SummarizeMaterialsDescriptorTable(uintptr_t base, char* out, size_t outSize);
static bool TryHandleMaterialsPage2ButtonClick(uintptr_t base, uint32_t panel, uint32_t extra, const char* reason);
extern "C" int __stdcall ShouldBlockMaterialsHover(uintptr_t outPtr);
extern "C" void __stdcall LogMaterialsContentDrawArg(uintptr_t objectPtr, int skipDraw);
extern "C" void __stdcall DrawMaterialsPage2Content();
extern "C" void __stdcall DrawMaterialsPage2ButtonOverlay();
extern "C" uintptr_t __stdcall HookD2Common11139(uintptr_t container, uint32_t index);
#if defined(SOE_MATERIALS_PAGE2_MATERIAL_RULES) && defined(SOE_MATERIALS_ITERATOR_PAGE2_BRIDGE_UNITS)
static void SoECodeToText(uint32_t code, char out[5]);
static uintptr_t FindSoEPage2BridgeUnit(uintptr_t base, uintptr_t container, uint32_t ordinal);
#endif
extern "C" void __stdcall LogMaterialsD2ClientDrawEntry(uintptr_t savedEsp);
static void TryInstallMaterialsIteratorHook();
extern "C" void __stdcall LogMaterialsNativeCall(
    uint32_t rva,
    uintptr_t eax,
    uintptr_t ecx,
    uintptr_t edx,
    uintptr_t ebx,
    uintptr_t savedEspAfterFlags);
extern "C" void __stdcall LogMaterialsStorageCall(
    uint32_t rva,
    uintptr_t eax,
    uintptr_t ecx,
    uintptr_t edx,
    uintptr_t ebx,
    uintptr_t savedEspAfterFlags);
extern "C" void __stdcall LogMaterialsDescriptorLookup(uint32_t itemId, uint32_t key);
extern "C" uint32_t __stdcall RewriteMaterialsDescriptorLookupKey(uint32_t rva, uint32_t itemId, uint32_t key);
extern "C" void __stdcall LogMaterialsDescriptorHelper(uint32_t rva, uint32_t itemId, uint32_t key);
extern "C" void __stdcall LogMaterialsGridRender(uintptr_t objectPtr, uintptr_t savedEspAfterFlags);
extern "C" void __stdcall LogMaterialsPageDraw(uintptr_t savedEspAfterFlags);
extern "C" void __stdcall LogMaterialsObjectDraw(uintptr_t objectPtr, uintptr_t savedEspAfterFlags);
extern "C" void __stdcall LogMaterialsMessageCall(uint32_t rva, uintptr_t ecxValue, uintptr_t originalEsp);


extern "C" void __stdcall LogMaterialsUiRefresh(uintptr_t retAddr, uintptr_t widget)
{
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    uintptr_t retRva = base && retAddr >= base ? retAddr - base : retAddr;
    uintptr_t ptr410998 = base ? SafeReadPtrTrace(base + 0x410998) : 0;
    uintptr_t ptr410a74 = base ? SafeReadPtrTrace(base + 0x410a74) : 0;
    uintptr_t ptr410688 = base ? SafeReadPtrTrace(base + 0x410688) : 0;
    uint32_t activeIndex = SafeReadDwordTrace(ptr410998);
    uint32_t activeItemId = 0xFFFFFFFFu;
    if (base && activeIndex != 0xFFFFFFFFu && activeIndex < 0x10000u) {
        activeItemId = SafeReadDwordTrace(base + 0x2d4668 + (uintptr_t)activeIndex * 10);
    }
    uint32_t panel = SafeReadDwordTrace(ptr410688);
    uint32_t extra = SafeReadDwordTrace(ptr410a74);
#ifdef SOE_MATERIALS_GLOBAL_UI_PAGE2_SWITCH
    TryHandleMaterialsPage2ButtonClick(base, panel, extra, "ui-refresh-global");
#endif
    if (panel == 0x0c && extra == 2 && widget) {
        g_materialsLastUiWidget = widget;
    }
    DumpMaterialsStateSnapshot("ui-refresh", base, 0, 0, false);

    if (base && panel == 0x0c && extra == 2 && InterlockedCompareExchange(&g_materialsDescriptorDumped, 1, 0) == 0) {
        DumpMaterialsDescriptorTable(base);
        DumpMaterialsCatalogState("ui-materials-first", base);
        DumpMaterialsIteratorSweep("ui-materials-first", base);
    }
#ifdef SOE_MATERIALS_DESCRIPTOR_PAGE
    if (!kMaterialsPassiveTrace) {
        if (!(panel == 0x0c && extra == 2)) {
            if (g_materialsPage2Active || g_materialsDescriptorSnapshotReady) {
#ifdef SOE_MATERIALS_PAGE2_OWN_STATE
                if (g_materialsPage2Active) {
                    RestoreMaterialsDescriptorView();
#ifdef SOE_MATERIALS_PAGE2_SET_MATERIAL_KEY
                    if (g_materialsSavedMaterialKeyReady && base) {
                        WriteMaterialsDword(base + 0x30dca8, g_materialsSavedMaterialKey);
                        AppendMaterialsTraceLine(
                            "own-page2 restore-material-key saved=%08x now=%08x reason=ui-left-materials",
                            (unsigned)g_materialsSavedMaterialKey,
                            (unsigned)SafeReadDwordTrace(base + 0x30dca8));
                        g_materialsSavedMaterialKeyReady = false;
                    }
#endif
                    AppendMaterialsTraceLine(
                        "own-page2 ui-left keep-active panel=%08x extra=%08x snapshot=%ld",
                        (unsigned)panel,
                        (unsigned)extra,
                        (long)g_materialsDescriptorSnapshotReady);
                } else
#endif
#ifdef SOE_MATERIALS_KEEP_PAGE2_ON_UI_LEFT
                if (g_materialsPage2Active) {
                    AppendMaterialsTraceLine(
                        "ui-left-materials keep-page2 panel=%08x extra=%08x snapshot=%ld",
                        (unsigned)panel,
                        (unsigned)extra,
                        (long)g_materialsDescriptorSnapshotReady);
                } else
#endif
                SetMaterialsDescriptorPage2(base, false, "ui-left-materials");
            }
#ifndef SOE_MATERIALS_KEEP_PAGE2_ON_UI_LEFT
#ifndef SOE_MATERIALS_PAGE2_OWN_STATE
            InterlockedExchange(&g_materialsLeftMaterials, 1);
            InterlockedExchange(&g_materialsPendingExternalEntry, 1);
            InterlockedExchange(&g_materialsSawExternalPage, 1);
            InterlockedExchange(&g_materialsExternalArmed, 1);
#else
            if (!g_materialsPage2Active) {
                InterlockedExchange(&g_materialsLeftMaterials, 1);
                InterlockedExchange(&g_materialsPendingExternalEntry, 1);
                InterlockedExchange(&g_materialsSawExternalPage, 1);
                InterlockedExchange(&g_materialsExternalArmed, 1);
            } else {
                InterlockedExchange(&g_materialsLeftMaterials, 0);
                InterlockedExchange(&g_materialsPendingExternalEntry, 0);
                InterlockedExchange(&g_materialsSawExternalPage, 0);
                InterlockedExchange(&g_materialsExternalArmed, 0);
            }
#endif
#else
            if (!g_materialsPage2Active) {
                InterlockedExchange(&g_materialsLeftMaterials, 1);
                InterlockedExchange(&g_materialsPendingExternalEntry, 1);
                InterlockedExchange(&g_materialsSawExternalPage, 1);
                InterlockedExchange(&g_materialsExternalArmed, 1);
            }
#endif
        } else if (g_materialsPage2Active && !g_materialsDescriptorSnapshotReady) {
            SetMaterialsDescriptorPage2(base, true, "ui-refresh-reapply-page2");
        }
    }
#else
    if (!kMaterialsPassiveTrace && !(panel == 0x0c && extra == 2)) {
#ifdef SOE_MATERIALS_GLOBAL_UI_PAGE2_LATCH
        if (g_materialsPage2Active) {
            AppendMaterialsTraceLine(
                "global-page2-latch ui-refresh clear-active panel=%08x extra=%08x mat=%08x",
                (unsigned)panel,
                (unsigned)extra,
                base ? SafeReadDwordTrace(base + 0x30dca8) : 0xffffffffu);
        }
#endif
        InterlockedExchange(&g_materialsAllowNextTransition6, 0);
        InterlockedExchange(&g_materialsPage2Active, 0);
        InterlockedExchange(&g_materialsPage2RenderContainer, 0);
        HideSoEPage2Overlay();
        InterlockedExchange(&g_materialsLeftMaterials, 1);
        InterlockedExchange(&g_materialsPendingExternalEntry, 1);
        InterlockedExchange(&g_materialsSawExternalPage, 1);
        InterlockedExchange(&g_materialsExternalArmed, 1);
        InterlockedExchange(&g_materialsRealEntryClickSeen, 0);
        InterlockedExchange(&g_materialsSuppressNextSelfToggle, 0);
        InterlockedExchange(&g_materialsSuppressSelfToggleUntilTick, 0);
        InterlockedExchange(&g_materialsForceRealEntry, 1);
        InterlockedExchange(&g_materialsForceRealDraws, 240);
        if (g_materialsDescriptorSnapshotReady) {
            RestoreMaterialsDescriptorView();
        }
    } else if (!kMaterialsPassiveTrace) {
        if (!g_materialsPage2Active && g_materialsDescriptorSnapshotReady) {
            RestoreMaterialsDescriptorView();
        }
        if (g_materialsPendingExternalEntry) {
            InterlockedExchange(&g_materialsLeftMaterials, 0);
        }
    }
#endif

    AppendMaterialsTraceLine(
        "ui-refresh ret=%p rva=%08x widget=%p w08=%08x w0a=%08x w0c=%08x w18=%08x w1c=%08x stash=%08x mat=%08x active=%08x activeItem=%08x panel=%08x extra=%08x left=%ld pending=%ld external=%ld armed=%ld clickSeen=%ld suppress=%ld cooldown=%ld force=%ld forceDraws=%ld",
        (void*)retAddr,
        (unsigned)retRva,
        (void*)widget,
        SafeReadDwordTrace(widget + 0x08),
        SafeReadDwordTrace(widget + 0x0a),
        SafeReadDwordTrace(widget + 0x0c),
        SafeReadDwordTrace(widget + 0x18),
        SafeReadDwordTrace(widget + 0x1c),
        base ? SafeReadDwordTrace(base + 0x2d4b24) : 0xFFFFFFFFu,
        base ? SafeReadDwordTrace(base + 0x30dca8) : 0xFFFFFFFFu,
        activeIndex,
        activeItemId,
        panel,
        extra,
        (long)g_materialsLeftMaterials,
        (long)g_materialsPendingExternalEntry,
        (long)g_materialsSawExternalPage,
        (long)g_materialsExternalArmed,
        (long)g_materialsRealEntryClickSeen,
        (long)g_materialsSuppressNextSelfToggle,
        (long)g_materialsSuppressSelfToggleUntilTick,
        (long)g_materialsForceRealEntry,
        (long)g_materialsForceRealDraws);

    InterlockedExchange(&g_materialsLastUiPanel, (LONG)panel);
    InterlockedExchange(&g_materialsLastUiExtra, (LONG)extra);
}

extern "C" int __stdcall HandleMaterialsTransition(uintptr_t originalEsp)
{
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    uintptr_t retAddr = SafeReadPtrTrace(originalEsp);
    uintptr_t retRva = base && retAddr >= base ? retAddr - base : retAddr;
    uintptr_t ptr410a74 = base ? SafeReadPtrTrace(base + 0x410a74) : 0;
    uintptr_t ptr410688 = base ? SafeReadPtrTrace(base + 0x410688) : 0;
    uint32_t a1 = SafeReadDwordTrace(originalEsp + 0x04);
    uint32_t a2 = SafeReadDwordTrace(originalEsp + 0x08);
    uint32_t a3 = SafeReadDwordTrace(originalEsp + 0x0c);
    uint32_t a4 = SafeReadDwordTrace(originalEsp + 0x10);
    uint32_t a5 = SafeReadDwordTrace(originalEsp + 0x14);
    uint32_t panel = SafeReadDwordTrace(ptr410688);
    uint32_t extra = SafeReadDwordTrace(ptr410a74);
    LONG priorPanel = g_materialsLastUiPanel;
    LONG priorExtra = g_materialsLastUiExtra;
    LONG leftMaterials = g_materialsLeftMaterials;
    LONG pendingExternalEntry = g_materialsPendingExternalEntry;
    LONG sawExternalPage = g_materialsSawExternalPage;
    LONG externalArmed = g_materialsExternalArmed;
    LONG suppressSelfToggle = g_materialsSuppressNextSelfToggle;
    LONG forceRealEntry = g_materialsForceRealEntry;
    LONG nativeSwitch = g_materialsNativeSwitchInProgress;
    LONG cooldownUntil = g_materialsSuppressSelfToggleUntilTick;
    DWORD nowTick = GetTickCount();
    bool toggleCooldown = cooldownUntil != 0 && (LONG)(cooldownUntil - (LONG)nowTick) > 0;
    bool activeMaterialsBackClick = (forceRealEntry == 0 && !toggleCooldown && suppressSelfToggle == 0 && externalArmed == 0 && sawExternalPage == 0 && leftMaterials == 0 && pendingExternalEntry == 0 && retRva == 0x193b96 && a1 == 1 && panel == 0x0c && extra == 2 && priorPanel == 0x0c && priorExtra == 2);
    bool enteringMaterials = (retRva == 0x193b96 && a1 == 1 && panel == 0x0c && extra == 2 && !activeMaterialsBackClick);
    int blockTransition = 0;

#ifdef SOE_MATERIALS_DESCRIPTOR_PAGE
    if (!kMaterialsPassiveTrace) {
        if (!nativeSwitch && g_materialsPage2Active && retRva == 0x193b96 && a1 == 1 && panel == 0x0c && extra == 2) {
#ifdef SOE_MATERIALS_KEEP_PAGE2_ON_ACTIVE_CLICK
            AppendMaterialsTraceLine(
                "transition-page2-keep-all ret=%08x a1=%u panel=%08x extra=%08x",
                (unsigned)retRva,
                (unsigned)a1,
                (unsigned)panel,
                (unsigned)extra);
#else
            SetMaterialsDescriptorPage2(base, false, "materials-button-page1");
            InterlockedExchange(&g_materialsSuppressNextSelfToggle, 1);
            InterlockedExchange(&g_materialsSuppressSelfToggleUntilTick, (LONG)(GetTickCount() + 500));
#endif
        } else if ((a1 == 1 || a1 == 2 || a1 == 6) && !(panel == 0x0c && extra == 2)) {
            if (g_materialsPage2Active || g_materialsDescriptorSnapshotReady) {
#ifdef SOE_MATERIALS_KEEP_PAGE2_ON_TRANSITION_LEFT
                if (g_materialsPage2Active) {
                    AppendMaterialsTraceLine(
                        "transition-left-materials keep-page2 ret=%08x a1=%u panel=%08x extra=%08x",
                        (unsigned)retRva,
                        (unsigned)a1,
                        (unsigned)panel,
                        (unsigned)extra);
                } else
#endif
                SetMaterialsDescriptorPage2(base, false, "transition-left-materials");
            }
        }
        blockTransition = 0;
    }
#else
    if (!kMaterialsPassiveTrace && retRva == 0x231471 && a1 == 6 && panel == 0x0c && extra == 2) {
        InterlockedExchange(&g_materialsAllowNextTransition6, 0);
        if (InterlockedExchange(&g_materialsPage2Active, 0) != 0) {
            InterlockedExchange(&g_materialsPage2RenderContainer, 0);
            HideSoEPage2Overlay();
            RestoreMaterialsDescriptorView();
        }
    } else if (!kMaterialsPassiveTrace && activeMaterialsBackClick) {
        InterlockedExchange(&g_materialsAllowNextTransition6, 0);
        InterlockedExchange(&g_materialsPage2Active, 0);
        InterlockedExchange(&g_materialsPage2RenderContainer, 0);
        HideSoEPage2Overlay();
        RestoreMaterialsDescriptorView();
        // Let ProjectDiablo receive the real tab transition. Earlier builds
        // swallowed this click after our page2 latch was active, leaving the
        // stash UI in a half-cleared state until another tab was selected.
        blockTransition = 0;
    } else if (!kMaterialsPassiveTrace && enteringMaterials && !nativeSwitch) {
        if (base) {
            uintptr_t flagAddr = base + 0x30dca8;
            DWORD oldProt = 0;
            if (VirtualProtect((void*)flagAddr, sizeof(uint32_t), PAGE_READWRITE, &oldProt)) {
                *(uint32_t*)flagAddr = 0;
                VirtualProtect((void*)flagAddr, sizeof(uint32_t), oldProt, &oldProt);
            }
        }
        InterlockedExchange(&g_materialsPendingExternalEntry, 1);
        InterlockedExchange(&g_materialsRealEntryClickSeen, 1);
        InterlockedExchange(&g_materialsAllowNextTransition6, 1);
        if (forceRealEntry) {
            InterlockedExchange(&g_materialsPage2Active, 0);
            InterlockedExchange(&g_materialsPage2RenderContainer, 0);
            HideSoEPage2Overlay();
            RestoreMaterialsDescriptorView();
        }
    } else if (!kMaterialsPassiveTrace && !nativeSwitch && (a1 == 2 || a1 == 1)) {
        InterlockedExchange(&g_materialsAllowNextTransition6, 0);
        InterlockedExchange(&g_materialsPage2Active, 0);
        InterlockedExchange(&g_materialsPage2RenderContainer, 0);
        HideSoEPage2Overlay();
        InterlockedExchange(&g_materialsLeftMaterials, 1);
        InterlockedExchange(&g_materialsPendingExternalEntry, 1);
        InterlockedExchange(&g_materialsSawExternalPage, 1);
        InterlockedExchange(&g_materialsExternalArmed, 1);
        InterlockedExchange(&g_materialsRealEntryClickSeen, 0);
        InterlockedExchange(&g_materialsSuppressNextSelfToggle, 0);
        InterlockedExchange(&g_materialsSuppressSelfToggleUntilTick, 0);
        InterlockedExchange(&g_materialsForceRealEntry, 1);
        InterlockedExchange(&g_materialsForceRealDraws, 240);
        RestoreMaterialsDescriptorView();
    }
#endif

    AppendMaterialsTraceLine(
        "transition esp=%p ret=%p rva=%08x args=%08x,%08x,%08x,%08x,%08x stash=%08x mat=%08x panel=%08x extra=%08x prior=%08lx/%08lx nativeSwitch=%ld page2=%ld allow6=%ld left=%ld pending=%ld external=%ld armed=%ld clickSeen=%ld suppress=%ld cooldown=%ld force=%ld block=%d",
        (void*)originalEsp,
        (void*)retAddr,
        (unsigned)retRva,
        (unsigned)a1,
        (unsigned)a2,
        (unsigned)a3,
        (unsigned)a4,
        (unsigned)a5,
        base ? SafeReadDwordTrace(base + 0x2d4b24) : 0xFFFFFFFFu,
        base ? SafeReadDwordTrace(base + 0x30dca8) : 0xFFFFFFFFu,
        panel,
        extra,
        (long)priorPanel,
        (long)priorExtra,
        (long)nativeSwitch,
        (long)g_materialsPage2Active,
        (long)g_materialsAllowNextTransition6,
        (long)g_materialsLeftMaterials,
        (long)g_materialsPendingExternalEntry,
        (long)g_materialsSawExternalPage,
        (long)g_materialsExternalArmed,
        (long)g_materialsRealEntryClickSeen,
        (long)g_materialsSuppressNextSelfToggle,
        (long)g_materialsSuppressSelfToggleUntilTick,
        (long)g_materialsForceRealEntry,
        blockTransition);

    return blockTransition;
}

extern "C" int __stdcall HandleMaterialsActiveClickEarly()
{
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    uintptr_t ptr410a74 = base ? SafeReadPtrTrace(base + 0x410a74) : 0;
    uintptr_t ptr410688 = base ? SafeReadPtrTrace(base + 0x410688) : 0;
    uint32_t panel = SafeReadDwordTrace(ptr410688);
    uint32_t extra = SafeReadDwordTrace(ptr410a74);
    LONG priorPanel = g_materialsLastUiPanel;
    LONG priorExtra = g_materialsLastUiExtra;
    LONG leftMaterials = g_materialsLeftMaterials;
    LONG pendingExternalEntry = g_materialsPendingExternalEntry;
    LONG sawExternalPage = g_materialsSawExternalPage;
    LONG externalArmed = g_materialsExternalArmed;
    LONG suppressSelfToggle = g_materialsSuppressNextSelfToggle;
    LONG forceRealEntry = g_materialsForceRealEntry;
    LONG page2Active = g_materialsPage2Active;
    LONG cooldownUntil = g_materialsSuppressSelfToggleUntilTick;
    DWORD nowTick = GetTickCount();
    bool toggleCooldown = cooldownUntil != 0 && (LONG)(cooldownUntil - (LONG)nowTick) > 0;
    bool activeMaterialsClick = (page2Active != 0 && forceRealEntry == 0 && !toggleCooldown && suppressSelfToggle == 0 && externalArmed == 0 && sawExternalPage == 0 && leftMaterials == 0 && pendingExternalEntry == 0 && panel == 0x0c && extra == 2 && priorPanel == 0x0c && priorExtra == 2);
    int blockClick = 0;

    if (kMaterialsPassiveTrace) {
        AppendMaterialsTraceLine(
            "active-click-early passive panel=%08x extra=%08x prior=%08lx/%08lx leftBefore=%ld pendingBefore=%ld externalBefore=%ld armedBefore=%ld clickSeenBefore=%ld suppressBefore=%ld cooldownBefore=%ld forceBefore=%ld page2=%ld allow6=%ld block=0",
            panel,
            extra,
            (long)priorPanel,
            (long)priorExtra,
            (long)leftMaterials,
            (long)pendingExternalEntry,
            (long)sawExternalPage,
            (long)externalArmed,
            (long)g_materialsRealEntryClickSeen,
            (long)suppressSelfToggle,
            (long)cooldownUntil,
            (long)forceRealEntry,
            (long)page2Active,
            (long)g_materialsAllowNextTransition6);
        return 0;
    }

#ifdef SOE_MATERIALS_DESCRIPTOR_PAGE
    if (page2Active != 0 && panel == 0x0c && extra == 2 && priorPanel == 0x0c && priorExtra == 2) {
#ifdef SOE_MATERIALS_KEEP_PAGE2_ON_ACTIVE_CLICK
        uintptr_t ptrMouseX = base ? SafeReadPtrTrace(base + 0x41016c) : 0;
        uintptr_t ptrMouseY = base ? SafeReadPtrTrace(base + 0x4101f8) : 0;
        int mouseX = (int)SafeReadDwordTrace(ptrMouseX);
        int mouseY = (int)SafeReadDwordTrace(ptrMouseY);
        AppendMaterialsTraceLine(
            "active-click-page2-keep-all mouse=%d,%d panel=%08x extra=%08x prior=%08lx/%08lx",
            mouseX,
            mouseY,
            (unsigned)panel,
            (unsigned)extra,
            (long)priorPanel,
            (long)priorExtra);
#else
        uintptr_t ptrMouseX = base ? SafeReadPtrTrace(base + 0x41016c) : 0;
        uintptr_t ptrMouseY = base ? SafeReadPtrTrace(base + 0x4101f8) : 0;
        uintptr_t ptrBaseX = base ? SafeReadPtrTrace(base + 0x4100a0) : 0;
        uintptr_t ptrBaseY1 = base ? SafeReadPtrTrace(base + 0x4101ec) : 0;
        uintptr_t ptrBaseY2 = base ? SafeReadPtrTrace(base + 0x4101a8) : 0;
        int mouseX = (int)SafeReadDwordTrace(ptrMouseX);
        int mouseY = (int)SafeReadDwordTrace(ptrMouseY);
        int baseX = (int)SafeReadDwordTrace(ptrBaseX);
        int baseY = (int)SafeReadDwordTrace(ptrBaseY1) + (int)SafeReadDwordTrace(ptrBaseY2);
        if (baseX <= 0 || baseY <= 0) {
            baseX = 214;
            baseY = 528;
        }
        int gridLeft = baseX + 160;
        int gridTop = baseY - 443;
        int gridRight = gridLeft + 13 * 42 + 24;
        int gridBottom = gridTop + 18 * 42;
        bool insidePage2Grid = (mouseX >= gridLeft && mouseX <= gridRight && mouseY >= gridTop && mouseY <= gridBottom);
        if (insidePage2Grid) {
            AppendMaterialsTraceLine(
                "active-click-page2-grid-keep mouse=%d,%d rect=%d,%d,%d,%d",
                mouseX,
                mouseY,
                gridLeft,
                gridTop,
                gridRight,
                gridBottom);
        } else {
            SetMaterialsDescriptorPage2(base, false, "active-click-page1");
            InterlockedExchange(&g_materialsSuppressNextSelfToggle, 1);
            InterlockedExchange(&g_materialsSuppressSelfToggleUntilTick, (LONG)(GetTickCount() + 500));
        }
#endif
        blockClick = 0;
    } else {
        InterlockedExchange(&g_materialsPendingExternalEntry, 1);
        InterlockedExchange(&g_materialsRealEntryClickSeen, 1);
        if (g_materialsPage2Active || g_materialsDescriptorSnapshotReady) {
            SetMaterialsDescriptorPage2(base, false, "active-click-other");
        }
    }
#else
    if (page2Active != 0 && (toggleCooldown || suppressSelfToggle) && panel == 0x0c && extra == 2 && priorPanel == 0x0c && priorExtra == 2) {
        InterlockedExchange(&g_materialsSuppressNextSelfToggle, 0);
        InterlockedExchange(&g_materialsPage2Active, 0);
        InterlockedExchange(&g_materialsPage2RenderContainer, 0);
        HideSoEPage2Overlay();
        InterlockedExchange(&g_materialsAllowNextTransition6, 0);
        RestoreMaterialsDescriptorView();
        blockClick = 0;
    } else if (activeMaterialsClick) {
        InterlockedExchange(&g_materialsAllowNextTransition6, 0);
        InterlockedExchange(&g_materialsPage2Active, 0);
        InterlockedExchange(&g_materialsPage2RenderContainer, 0);
        HideSoEPage2Overlay();
        RestoreMaterialsDescriptorView();
        blockClick = 0;
    } else {
        InterlockedExchange(&g_materialsAllowNextTransition6, 0);
        InterlockedExchange(&g_materialsPendingExternalEntry, 1);
        InterlockedExchange(&g_materialsRealEntryClickSeen, 1);
        if (InterlockedExchange(&g_materialsPage2Active, 0) != 0) {
            InterlockedExchange(&g_materialsPage2RenderContainer, 0);
            HideSoEPage2Overlay();
            RestoreMaterialsDescriptorView();
        }
    }
#endif

    AppendMaterialsTraceLine(
        "active-click-early panel=%08x extra=%08x prior=%08lx/%08lx leftBefore=%ld pendingBefore=%ld externalBefore=%ld armedBefore=%ld clickSeenBefore=%ld suppressBefore=%ld cooldownBefore=%ld forceBefore=%ld page2=%ld allow6=%ld left=%ld pending=%ld external=%ld armed=%ld clickSeen=%ld suppress=%ld cooldown=%ld force=%ld block=%d",
        panel,
        extra,
        (long)priorPanel,
        (long)priorExtra,
        (long)leftMaterials,
        (long)pendingExternalEntry,
        (long)sawExternalPage,
        (long)externalArmed,
        (long)g_materialsRealEntryClickSeen,
        (long)suppressSelfToggle,
        (long)cooldownUntil,
        (long)forceRealEntry,
        (long)g_materialsPage2Active,
        (long)g_materialsAllowNextTransition6,
        (long)g_materialsLeftMaterials,
        (long)g_materialsPendingExternalEntry,
        (long)g_materialsSawExternalPage,
        (long)g_materialsExternalArmed,
        (long)g_materialsRealEntryClickSeen,
        (long)g_materialsSuppressNextSelfToggle,
        (long)g_materialsSuppressSelfToggleUntilTick,
        (long)g_materialsForceRealEntry,
        blockClick);

    return blockClick;
}

static bool TryHandleMaterialsPage2ButtonClick(uintptr_t base, uint32_t panel, uint32_t extra, const char* reason)
{
    if (!base) return false;
    uint32_t stashOpen = SafeReadDwordTrace(base + 0x2d4b24);
    if (!stashOpen) {
        InterlockedExchange(&g_materialsPage2ButtonDown, 0);
        return false;
    }

    bool mouseDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    LONG wasDown = g_materialsPage2ButtonDown;
    if (!mouseDown) {
        InterlockedExchange(&g_materialsPage2ButtonDown, 0);
        return false;
    }
    if (wasDown) return false;
    InterlockedExchange(&g_materialsPage2ButtonDown, 1);

    uintptr_t ptrMouseX = SafeReadPtrTrace(base + 0x41016c);
    uintptr_t ptrMouseY = SafeReadPtrTrace(base + 0x4101f8);
    uintptr_t ptrBaseX = SafeReadPtrTrace(base + 0x4100a0);
    uintptr_t ptrBaseY1 = SafeReadPtrTrace(base + 0x4101ec);
    uintptr_t ptrBaseY2 = SafeReadPtrTrace(base + 0x4101a8);
    uint32_t mouseX = SafeReadDwordTrace(ptrMouseX);
    uint32_t mouseY = SafeReadDwordTrace(ptrMouseY);
    uint32_t baseX = SafeReadDwordTrace(ptrBaseX);
    uint32_t baseY1 = SafeReadDwordTrace(ptrBaseY1);
    uint32_t baseY2 = SafeReadDwordTrace(ptrBaseY2);
    int hitLeft = 0;
    int hitTop = 0;
    int hitRight = 0;
    int hitBottom = 0;
    GetSoEPage2BottomButtonRect((int)baseX, (int)baseY1 + (int)baseY2, &hitLeft, &hitTop, &hitRight, &hitBottom);
    bool inMaterialsPage2Button = (
        (int)mouseX >= hitLeft && (int)mouseX <= hitRight &&
        (int)mouseY >= hitTop && (int)mouseY <= hitBottom);
#ifdef SOE_MATERIALS_PAGE2_ENABLE_TOP_HITBOX
    int stashTopLeft = (int)baseX + 190;
    int stashTopTop = (int)baseY1 + (int)baseY2 - 350;
    int stashTopRight = (int)baseX + 390;
    int stashTopBottom = (int)baseY1 + (int)baseY2 - 200;
    bool inStashTopPage2Button = (
        (int)mouseX >= stashTopLeft && (int)mouseX <= stashTopRight &&
        (int)mouseY >= stashTopTop && (int)mouseY <= stashTopBottom);
#else
    int stashTopLeft = 0;
    int stashTopTop = 0;
    int stashTopRight = 0;
    int stashTopBottom = 0;
    bool inStashTopPage2Button = false;
#endif
    bool inPage2Button = inMaterialsPage2Button || inStashTopPage2Button;

    LONG clickProbe = InterlockedIncrement(&g_materialsClickProbeLogCount);
    if (clickProbe <= 120) {
        AppendMaterialsTraceLine(
            "page2-button-probe source=%s mouse=%u,%u base=%u,%u,%u matRect=%d,%d,%d,%d topRect=%d,%d,%d,%d inMat=%d inTop=%d inPage2=%d panel=%08x extra=%08x stash=%08x mat=%08x page2=%ld",
            reason ? reason : "?",
            mouseX,
            mouseY,
            baseX,
            baseY1,
            baseY2,
            hitLeft,
            hitTop,
            hitRight,
            hitBottom,
            stashTopLeft,
            stashTopTop,
            stashTopRight,
            stashTopBottom,
            inMaterialsPage2Button ? 1 : 0,
            inStashTopPage2Button ? 1 : 0,
            inPage2Button ? 1 : 0,
            panel,
            extra,
            stashOpen,
            SafeReadDwordTrace(base + 0x30dca8),
            (long)g_materialsPage2Active);
    }
    if (!inPage2Button) return false;

    AppendMaterialsTraceLine(
        "page2-button-global-hit source=%s panel=%08x extra=%08x page2=%ld",
        reason ? reason : "?",
        panel,
        extra,
        (long)g_materialsPage2Active);
    DumpMaterialsStateSnapshot("page2-button-global-hit", base, 0, 0, true);
    {
        uintptr_t playerSlot = SafeReadPtrTrace(base + 0x4159bc);
        uintptr_t player = SafeReadPtrTrace(playerSlot);
        uintptr_t playerContainer = player ? SafeReadPtrTrace(player + 0x60) : 0;
        uintptr_t objectSlot = SafeReadPtrTrace(base + 0x410224);
        uintptr_t currentObject = SafeReadPtrTrace(objectSlot);
        uintptr_t currentContainer = currentObject ? SafeReadPtrTrace(currentObject + 0x60) : 0;
        ProbeMaterialsNativeGridIds("page2-player", base, playerContainer);
        if (currentContainer != playerContainer) {
            ProbeMaterialsNativeGridIds("page2-current", base, currentContainer);
        }
    }
    InterlockedExchange(&g_materialsStorageLogCount, 0);
    InterlockedExchange(&g_materialsMessageLogCount, 0);
    InterlockedExchange(&g_materialsNativeLogCount, 0);
    InterlockedExchange(&g_materialsObjectDrawLogCount, 0);
    InterlockedExchange(&g_materialsPageDrawLogCount, 0);
    InterlockedExchange(&g_materialsGridRenderLogCount, 0);
    InterlockedExchange(&g_materialsDescriptorLookupTraceCount, 0);
    InterlockedExchange(&g_materialsDescriptorHelperTraceCount, 0);
    InterlockedExchange(&g_materialsIteratorLogCount, 0);
    InterlockedExchange(&g_materialsIteratorPage2LogCount, 0);
    InterlockedExchange(&g_materialsIteratorPage2BlankCount, 0);
    InterlockedExchange(&g_materialsIteratorStackLogCount, 0);
    InterlockedExchange(&g_materialsIteratorBridgeLogCount, 0);
    InterlockedExchange(&g_materialsD2ClientDrawEntryLogCount, 0);
    InterlockedExchange(&g_materialsD2ClientDrawEntryPage2LogCount, 0);

    if (!kMaterialsPassiveTrace) {
#ifdef SOE_MATERIALS_PAGE2_USE_STASH_NEXT_HANDLER
        TryNativeStashPageStep(reason ? reason : "page2-button-global");
        InterlockedExchange(&g_materialsPage2Active, 0);
        InterlockedExchange(&g_materialsPage2RenderContainer, 0);
        return true;
#elif defined(SOE_MATERIALS_PAGE2_USE_STASH_SELECT_HANDLER)
        TryNativeStashSelectPage2(reason ? reason : "page2-button-global");
        InterlockedExchange(&g_materialsPage2Active, 0);
        InterlockedExchange(&g_materialsPage2RenderContainer, 0);
        return true;
#elif defined(SOE_MATERIALS_PAGE2_USE_STASH_COORD_SELECT_HANDLER)
        TryNativeStashCoordSelectPage2(reason ? reason : "page2-button-global");
        InterlockedExchange(&g_materialsPage2Active, 0);
        InterlockedExchange(&g_materialsPage2RenderContainer, 0);
        return true;
#elif defined(SOE_MATERIALS_PAGE2_USE_STASH_PACKET_PAGE)
        TryNativeStashPacketPage2(reason ? reason : "page2-button-global");
#ifndef SOE_MATERIALS_PAGE2_DESCRIPTOR_AFTER_STASH_PACKET
        InterlockedExchange(&g_materialsPage2Active, 0);
        InterlockedExchange(&g_materialsPage2RenderContainer, 0);
#endif
        return true;
#elif defined(SOE_MATERIALS_PAGE2_USE_PAGE_LIST_SELECTOR)
        TryNativePageListSelectorPage2(reason ? reason : "page2-button-global");
        InterlockedExchange(&g_materialsPage2Active, 0);
        InterlockedExchange(&g_materialsPage2RenderContainer, 0);
        return true;
#else
#ifdef SOE_MATERIALS_DESCRIPTOR_PAGE
#ifdef SOE_MATERIALS_PAGE2_DESCRIPTOR_ENTER_NATIVE
        if (!(panel == 0x0c && extra == 2)) {
            TryNativeMaterialsPageSwitch(1, reason ? reason : "page2-button-global-descriptor-enter");
        }
#endif
        SetMaterialsDescriptorPage2(base, true, reason ? reason : "page2-button-global");
#elif defined(SOE_MATERIALS_GLOBAL_UI_PAGE2_LATCH)
#ifdef SOE_MATERIALS_PAGE2_ENTER_NATIVE_FIRST
#ifdef SOE_MATERIALS_PAGE2_NATIVE_PAGE
        TryNativeMaterialsPageSwitch((uint32_t)SOE_MATERIALS_PAGE2_NATIVE_PAGE, reason ? reason : "page2-button-global");
#else
        TryNativeMaterialsPageSwitch(1, reason ? reason : "page2-button-global");
#endif
#endif
#ifdef SOE_MATERIALS_PAGE2_FORCE_NATIVE_KEY
        {
            uint32_t keyBefore = SafeReadDwordTrace(base + 0x30dca8);
            ApplyMaterialsPage2KeyedDescriptorPage(
                base,
                (uint32_t)SOE_MATERIALS_PAGE2_FORCE_NATIVE_KEY,
                reason ? reason : "page2-button-global");
#ifdef SOE_MATERIALS_PAGE2_KEYED_CATALOG_WINDOW
            ApplyMaterialsPage2CatalogWindow(base, reason ? reason : "page2-button-global");
#endif
            WriteMaterialsDword(base + 0x30dca8, (uint32_t)SOE_MATERIALS_PAGE2_FORCE_NATIVE_KEY);
            WriteMaterialsDword(base + 0x30dc84, 1);
            AppendMaterialsTraceLine(
                "page2-button-force-native-key source=%s before=%08x after=%08x dirty=%08x",
                reason ? reason : "?",
                (unsigned)keyBefore,
                (unsigned)SafeReadDwordTrace(base + 0x30dca8),
                (unsigned)SafeReadDwordTrace(base + 0x30dc84));
        }
#endif
        InterlockedExchange(&g_materialsPage2Active, 1);
        InterlockedExchange(&g_materialsPage2RenderContainer, 0);
        InterlockedExchange(&g_materialsLeftMaterials, 0);
        InterlockedExchange(&g_materialsPendingExternalEntry, 0);
        InterlockedExchange(&g_materialsSawExternalPage, 0);
        InterlockedExchange(&g_materialsExternalArmed, 0);
        InterlockedExchange(&g_materialsForceRealEntry, 0);
        InterlockedExchange(&g_materialsForceRealDraws, 0);
        AppendMaterialsTraceLine(
            "page2-button-latch-only source=%s panel=%08x extra=%08x mat=%08x",
            reason ? reason : "?",
            panel,
            extra,
            SafeReadDwordTrace(base + 0x30dca8));
#elif defined(SOE_MATERIALS_GLOBAL_UI_PAGE2_SWITCH)
        TryNativeMaterialsPageSwitch(1, reason ? reason : "page2-button-global");
#else
        TryNativeMaterialsPageSwitch(1, reason ? reason : "page2-button-global");
#endif
#endif
    }
    return true;
}

static bool IsMaterialsPage2ObjectActive(uintptr_t base, uint32_t* outPage)
{
    if (outPage) *outPage = 0xffffffffu;
    if (!base || !SafeReadDwordTrace(base + 0x2d4b24)) return false;

    uintptr_t ptrPanel = SafeReadPtrTrace(base + 0x410688);
    uintptr_t ptrExtra = SafeReadPtrTrace(base + 0x410a74);
    if (SafeReadDwordTrace(ptrPanel) != 0x0c || SafeReadDwordTrace(ptrExtra) != 0x02) {
        return false;
    }

    uintptr_t objectSlot = SafeReadPtrTrace(base + 0x410224);
    uintptr_t currentObject = SafeReadPtrTrace(objectSlot);
    uintptr_t objectData = currentObject ? SafeReadPtrTrace(currentObject + 0x14) : 0;
    uint32_t objectPage = objectData ? SafeReadDwordTrace(objectData + 0x1b4) : 0xffffffffu;
    if (outPage) *outPage = objectPage;

#ifdef SOE_MATERIALS_PAGE2_STASH_CLIENT_INDEX
    const uint32_t page2Index = (uint32_t)SOE_MATERIALS_PAGE2_STASH_CLIENT_INDEX;
#else
    const uint32_t page2Index = 11u;
#endif
    return objectPage == page2Index;
}

extern "C" int __stdcall ShouldSkipMaterialsDraw()
{
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    uintptr_t ptr410a74 = base ? SafeReadPtrTrace(base + 0x410a74) : 0;
    uintptr_t ptr410688 = base ? SafeReadPtrTrace(base + 0x410688) : 0;
    uint32_t panel = SafeReadDwordTrace(ptr410688);
    uint32_t extra = SafeReadDwordTrace(ptr410a74);
    uint32_t stashOpen = base ? SafeReadDwordTrace(base + 0x2d4b24) : 0;
    bool isMaterialsPanel = (panel == 0x0c && (extra == 1 || extra == 2));
    bool page2ButtonEligible = isMaterialsPanel;
#ifdef SOE_MATERIALS_PAGE2_OWN_STATE
    page2ButtonEligible = (stashOpen != 0);
#endif
    if (page2ButtonEligible) {
        bool mouseDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        LONG wasDown = g_materialsPage2ButtonDown;
        if (!mouseDown) {
            InterlockedExchange(&g_materialsPage2ButtonDown, 0);
        } else if (!wasDown) {
            InterlockedExchange(&g_materialsPage2ButtonDown, 1);

            uintptr_t ptrMouseX = base ? SafeReadPtrTrace(base + 0x41016c) : 0;
            uintptr_t ptrMouseY = base ? SafeReadPtrTrace(base + 0x4101f8) : 0;
            uintptr_t ptrBaseX = base ? SafeReadPtrTrace(base + 0x4100a0) : 0;
            uintptr_t ptrBaseY1 = base ? SafeReadPtrTrace(base + 0x4101ec) : 0;
            uintptr_t ptrBaseY2 = base ? SafeReadPtrTrace(base + 0x4101a8) : 0;
            uint32_t mouseX = SafeReadDwordTrace(ptrMouseX);
            uint32_t mouseY = SafeReadDwordTrace(ptrMouseY);
            uint32_t baseX = SafeReadDwordTrace(ptrBaseX);
            uint32_t baseY1 = SafeReadDwordTrace(ptrBaseY1);
            uint32_t baseY2 = SafeReadDwordTrace(ptrBaseY2);
            int hitLeft = 0;
            int hitTop = 0;
            int hitRight = 0;
            int hitBottom = 0;
            GetSoEPage2BottomButtonRect((int)baseX, (int)baseY1 + (int)baseY2, &hitLeft, &hitTop, &hitRight, &hitBottom);
            bool inPage2Button = (
                (int)mouseX >= hitLeft && (int)mouseX <= hitRight &&
                (int)mouseY >= hitTop && (int)mouseY <= hitBottom);
            LONG clickProbe = InterlockedIncrement(&g_materialsClickProbeLogCount);
            if (clickProbe <= 80) {
                POINT cursor = {};
                GetCursorPos(&cursor);
                AppendMaterialsTraceLine(
                    "materials-click-probe #%ld mouse=%u,%u cursor=%ld,%ld base=%u,%u,%u rect=%d,%d,%d,%d inPage2=%d panel=%08x extra=%08x page2=%ld passive=%d",
                    (long)clickProbe,
                    mouseX,
                    mouseY,
                    (long)cursor.x,
                    (long)cursor.y,
                    baseX,
                    baseY1,
                    baseY2,
                    hitLeft,
                    hitTop,
                    hitRight,
                    hitBottom,
                    inPage2Button ? 1 : 0,
                    panel,
                    extra,
                    (long)g_materialsPage2Active,
                    kMaterialsPassiveTrace ? 1 : 0);
            }
            if (inPage2Button) {
                AppendMaterialsTraceLine(
                    "page2-button %s mouse=%u,%u rect=%d,%d,%d,%d",
                    kMaterialsPassiveTrace ? "passive-trace" : "native-switch-request",
                    mouseX,
                    mouseY,
                    hitLeft,
                    hitTop,
                    hitRight,
                    hitBottom);
                DumpMaterialsStateSnapshot("page2-button-native-request", base, 0, 0, true);
                DumpMaterialsCatalogState("page2-button", base);
                DumpMaterialsIteratorSweep("page2-button", base);
                InterlockedExchange(&g_materialsStorageLogCount, 0);
                InterlockedExchange(&g_materialsMessageLogCount, 0);
                InterlockedExchange(&g_materialsNativeLogCount, 0);
                InterlockedExchange(&g_materialsObjectDrawLogCount, 0);
                InterlockedExchange(&g_materialsPageDrawLogCount, 0);
                InterlockedExchange(&g_materialsGridRenderLogCount, 0);
                InterlockedExchange(&g_materialsDescriptorLookupTraceCount, 0);
                InterlockedExchange(&g_materialsDescriptorHelperTraceCount, 0);
                InterlockedExchange(&g_materialsIteratorLogCount, 0);
                InterlockedExchange(&g_materialsIteratorPage2LogCount, 0);
                InterlockedExchange(&g_materialsIteratorPage2BlankCount, 0);
                InterlockedExchange(&g_materialsIteratorStackLogCount, 0);
                InterlockedExchange(&g_materialsIteratorBridgeLogCount, 0);
                InterlockedExchange(&g_materialsD2ClientDrawEntryLogCount, 0);
                InterlockedExchange(&g_materialsD2ClientDrawEntryPage2LogCount, 0);
                if (!kMaterialsPassiveTrace) {
#ifdef SOE_MATERIALS_PAGE2_USE_STASH_NEXT_HANDLER
                    TryNativeStashPageStep("page2-button");
                    InterlockedExchange(&g_materialsPage2Active, 0);
                    InterlockedExchange(&g_materialsPage2RenderContainer, 0);
#elif defined(SOE_MATERIALS_PAGE2_USE_STASH_SELECT_HANDLER)
                    TryNativeStashSelectPage2("page2-button");
                    InterlockedExchange(&g_materialsPage2Active, 0);
                    InterlockedExchange(&g_materialsPage2RenderContainer, 0);
#elif defined(SOE_MATERIALS_PAGE2_USE_STASH_COORD_SELECT_HANDLER)
                    TryNativeStashCoordSelectPage2("page2-button");
                    InterlockedExchange(&g_materialsPage2Active, 0);
                    InterlockedExchange(&g_materialsPage2RenderContainer, 0);
#elif defined(SOE_MATERIALS_PAGE2_USE_STASH_PACKET_PAGE)
                    TryNativeStashPacketPage2("page2-button");
#ifndef SOE_MATERIALS_PAGE2_DESCRIPTOR_AFTER_STASH_PACKET
                    InterlockedExchange(&g_materialsPage2Active, 0);
                    InterlockedExchange(&g_materialsPage2RenderContainer, 0);
#endif
#elif defined(SOE_MATERIALS_PAGE2_USE_PAGE_LIST_SELECTOR)
                    TryNativePageListSelectorPage2("page2-button");
                    InterlockedExchange(&g_materialsPage2Active, 0);
                    InterlockedExchange(&g_materialsPage2RenderContainer, 0);
#else
#ifdef SOE_MATERIALS_DESCRIPTOR_PAGE
                    SetMaterialsDescriptorPage2(base, true, "page2-button");
#ifdef SOE_MATERIALS_PAGE2_OWN_STATE
                    InterlockedExchange(&g_materialsLeftMaterials, 0);
                    InterlockedExchange(&g_materialsPendingExternalEntry, 0);
                    InterlockedExchange(&g_materialsSawExternalPage, 0);
                    InterlockedExchange(&g_materialsExternalArmed, 0);
#endif
#else
#ifdef SOE_MATERIALS_PAGE2_NATIVE_PAGE
                    TryNativeMaterialsPageSwitch((uint32_t)SOE_MATERIALS_PAGE2_NATIVE_PAGE, "page2-button");
#else
                    TryNativeMaterialsPageSwitch(1, "page2-button");
#endif
                    MaintainMaterialsPage2NativeKey(base, "active-click-page2-button");
                    InterlockedExchange(&g_materialsPage2Active, 1);
                    InterlockedExchange(&g_materialsLeftMaterials, 0);
                    InterlockedExchange(&g_materialsPendingExternalEntry, 0);
                    InterlockedExchange(&g_materialsSawExternalPage, 0);
                    InterlockedExchange(&g_materialsExternalArmed, 0);
                    AppendMaterialsTraceLine(
                        "active-click-page2-latch source=page2-button panel=%08x extra=%08x mat=%08x",
                        panel,
                        extra,
                        base ? SafeReadDwordTrace(base + 0x30dca8) : 0xffffffffu);
#endif
#endif
                }
            }
        }
    }
    if (kMaterialsPassiveTrace) {
        InterlockedExchange(&g_materialsLastDrawMode, 0);
        return 0;
    }
    LONG page2 = g_materialsPage2Active;
    LONG force = g_materialsForceRealEntry;
    LONG forceDraws = g_materialsForceRealDraws;
    LONG pendingExternalEntry = g_materialsPendingExternalEntry;
    LONG externalArmed = g_materialsExternalArmed;
    LONG clickSeen = g_materialsRealEntryClickSeen;
    uint32_t page2ObjectPage = 0xffffffffu;
    bool page2ObjectActive = IsMaterialsPage2ObjectActive(base, &page2ObjectPage);
#ifdef SOE_MATERIALS_PAGE2_NATIVE_RENDERER_PATH
    if (page2ObjectActive && !page2) {
        InterlockedExchange(&g_materialsPage2Active, 1);
        page2 = 1;
#ifdef SOE_MATERIALS_DESCRIPTOR_PAGE
        if (!g_materialsDescriptorSnapshotReady) {
            SetMaterialsDescriptorPage2(base, true, "native-renderer-detected-page2");
        }
#endif
        LONG n = InterlockedIncrement(&g_materialsPage2NativeRendererSkipLogCount);
        if (n <= 40) {
            AppendMaterialsTraceLine(
                "native-renderer page2-detected #%ld panel=%08x extra=%08x objectPage=%08x",
                (long)n,
                panel,
                extra,
                (unsigned)page2ObjectPage);
        }
    } else if (page2 && !page2ObjectActive) {
        InterlockedExchange(&g_materialsPage2Active, 0);
        InterlockedExchange(&g_materialsPage2RenderContainer, 0);
        HideSoEPage2Overlay();
        RestoreMaterialsDescriptorView();
        page2 = 0;
    }
#endif
    if (!isMaterialsPanel) {
        if (page2) {
#if defined(SOE_MATERIALS_PAGE2_OWN_STATE) || defined(SOE_MATERIALS_GLOBAL_UI_PAGE2_LATCH)
            InterlockedExchange(&g_materialsPage2RenderContainer, 0);
            RestoreMaterialsDescriptorView();
#else
            InterlockedExchange(&g_materialsPage2Active, 0);
            InterlockedExchange(&g_materialsPage2RenderContainer, 0);
            HideSoEPage2Overlay();
            RestoreMaterialsDescriptorView();
#endif
        }
        InterlockedExchange(&g_materialsLastDrawMode, 0);
        return 0;
    }
    if ((pendingExternalEntry || externalArmed) && !clickSeen) {
        if (page2) {
            InterlockedExchange(&g_materialsPage2Active, 0);
            InterlockedExchange(&g_materialsPage2RenderContainer, 0);
            HideSoEPage2Overlay();
            RestoreMaterialsDescriptorView();
        }
        if (InterlockedExchange(&g_materialsLastDrawMode, 3) != 3) {
            AppendMaterialsTraceLine(
                "content-draw pre-entry panel=%08x extra=%08x page2Before=%ld left=%ld pending=%ld external=%ld armed=%ld clickSeen=%ld force=%ld forceDraws=%ld",
                panel,
                extra,
                (long)page2,
                (long)g_materialsLeftMaterials,
                (long)pendingExternalEntry,
                (long)g_materialsSawExternalPage,
                (long)externalArmed,
                (long)clickSeen,
                (long)force,
                (long)forceDraws);
        }
        return 0;
    }
    if (force || forceDraws > 0 || pendingExternalEntry || externalArmed) {
        if (page2) {
            InterlockedExchange(&g_materialsPage2Active, 0);
            InterlockedExchange(&g_materialsPage2RenderContainer, 0);
            HideSoEPage2Overlay();
            RestoreMaterialsDescriptorView();
        }
        InterlockedExchange(&g_materialsPendingExternalEntry, 0);
        InterlockedExchange(&g_materialsExternalArmed, 0);
        InterlockedExchange(&g_materialsRealEntryClickSeen, 0);
        InterlockedExchange(&g_materialsSuppressNextSelfToggle, 1);
        InterlockedExchange(&g_materialsSuppressSelfToggleUntilTick, (LONG)(GetTickCount() + 1500));
        InterlockedExchange(&g_materialsSawExternalPage, 0);
        InterlockedExchange(&g_materialsLeftMaterials, 0);
        InterlockedExchange(&g_materialsForceRealEntry, 0);
        InterlockedExchange(&g_materialsForceRealDraws, 0);
        if (InterlockedExchange(&g_materialsLastDrawMode, 2) != 2) {
            AppendMaterialsTraceLine(
                "content-draw real-entry panel=%08x extra=%08x page2Before=%ld left=%ld pending=%ld external=%ld armed=%ld clickSeen=%ld suppress=%ld cooldown=%ld force=%ld forceDraws=%ld",
                panel,
                extra,
                (long)page2,
                (long)g_materialsLeftMaterials,
                (long)pendingExternalEntry,
                (long)g_materialsSawExternalPage,
                (long)externalArmed,
                (long)clickSeen,
                (long)g_materialsSuppressNextSelfToggle,
                (long)g_materialsSuppressSelfToggleUntilTick,
                (long)force,
                (long)forceDraws);
        }
        return 0;
    }
    if (page2) {
#ifdef SOE_MATERIALS_DESCRIPTOR_PAGE
        if (!g_materialsDescriptorSnapshotReady) {
            SetMaterialsDescriptorPage2(base, true, "draw-reapply-descriptor-page");
        }
#endif
#ifdef SOE_MATERIALS_CATALOG_PAGE
        ApplyMaterialsPage2CatalogWindow(base, "draw-page2");
#endif
#ifdef SOE_MATERIALS_PAGE2_KEYED_CATALOG_WINDOW
        ApplyMaterialsPage2CatalogWindow(base, "draw-page2-keyed");
#endif
#ifdef SOE_MATERIALS_PAGE2_SCAFFOLD
        DrawSoEPage2Scaffold(base);
#endif
        if (InterlockedExchange(&g_materialsLastDrawMode, 1) != 1) {
            AppendMaterialsTraceLine(
                "content-draw page2-native-descriptor panel=%08x extra=%08x page2=%ld objectPage=%08x nativeObject=%d left=%ld pending=%ld external=%ld armed=%ld",
                panel,
                extra,
                (long)page2,
                (unsigned)page2ObjectPage,
                page2ObjectActive ? 1 : 0,
                (long)g_materialsLeftMaterials,
                (long)g_materialsPendingExternalEntry,
                (long)g_materialsSawExternalPage,
                (long)g_materialsExternalArmed);
        }
#ifdef SOE_MATERIALS_PAGE2_NATIVE_RENDERER_PATH
        if (page2ObjectActive) {
            LONG n = InterlockedIncrement(&g_materialsPage2NativeRendererSkipLogCount);
            if (n <= 80) {
                AppendMaterialsTraceLine(
                    "native-renderer content-skip #%ld panel=%08x extra=%08x objectPage=%08x",
                    (long)n,
                    panel,
                    extra,
                    (unsigned)page2ObjectPage);
            }
            return 1;
        }
#endif
        return 0;
    }
    InterlockedExchange(&g_materialsLastDrawMode, 0);
    return 0;
}

extern "C" int __stdcall ShouldBlockMaterialsHover(uintptr_t outPtr)
{
    return 0;

    if (!g_materialsPage2Active) return 0;

    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    if (!base) return 0;

    uintptr_t ptr410a74 = SafeReadPtrTrace(base + 0x410a74);
    uintptr_t ptr410688 = SafeReadPtrTrace(base + 0x410688);
    uint32_t panel = SafeReadDwordTrace(ptr410688);
    uint32_t extra = SafeReadDwordTrace(ptr410a74);
    if (!(panel == 0x0c && extra == 2)) return 0;

    uintptr_t ptrMouseX = SafeReadPtrTrace(base + 0x41016c);
    uintptr_t ptrMouseY = SafeReadPtrTrace(base + 0x4101f8);
    uintptr_t ptrBaseX = SafeReadPtrTrace(base + 0x4100a0);
    uintptr_t ptrBaseY1 = SafeReadPtrTrace(base + 0x4101ec);
    uintptr_t ptrBaseY2 = SafeReadPtrTrace(base + 0x4101a8);
    int mouseX = (int)SafeReadDwordTrace(ptrMouseX);
    int mouseY = (int)SafeReadDwordTrace(ptrMouseY);
    int baseX = (int)SafeReadDwordTrace(ptrBaseX);
    int baseY = (int)SafeReadDwordTrace(ptrBaseY1) + (int)SafeReadDwordTrace(ptrBaseY2);
    if (baseX <= 0 || baseY <= 0) {
        baseX = 214;
        baseY = 528;
    }

    int cell = 42;
    int cols = 13;
    int rows = 18;
    int left = baseX - 180;
    int top = baseY - 443;
    int width = cols * cell;
    int height = rows * cell;
    static LONG hoverProbeCount = 0;
    LONG probeIndex = InterlockedIncrement(&hoverProbeCount);
    if (probeIndex <= 80) {
        AppendMaterialsTraceLine(
            "page2-hover-probe out=%p mouse=%d,%d rect=%d,%d,%d,%d panel=%08x extra=%08x",
            (void*)outPtr,
            mouseX,
            mouseY,
            left,
            top,
            left + width,
            top + height,
            panel,
            extra);
    }

    bool inside = mouseX >= left && mouseX <= left + width && mouseY >= top && mouseY <= top + height;
    if (!inside) return 0;

    static DWORD lastHoverLogTick = 0;
    DWORD now = GetTickCount();
    if (now - lastHoverLogTick > 500) {
        lastHoverLogTick = now;
        AppendMaterialsTraceLine(
            "page2-hover-block out=%p mouse=%d,%d rect=%d,%d,%d,%d",
            (void*)outPtr,
            mouseX,
            mouseY,
            left,
            top,
            left + width,
            top + height);
    }

    if (outPtr && !IsBadWritePtr((void*)outPtr, sizeof(uint32_t))) {
        *(uint32_t*)outPtr = 0;
    }
    *(uint32_t*)(base + 0x30dcac) = 0;
    *(uint32_t*)(base + 0x30dca4) = 0;
    return 1;
}

extern "C" void __stdcall LogMaterialsContentDrawArg(uintptr_t objectPtr, int skipDraw)
{
    TryInstallMaterialsIteratorHook();
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    bool page2 = g_materialsPage2Active != 0;
    const char* label = skipDraw ? "page2-skip" : (page2 ? "page2-native" : "normal");
    DumpMaterialsStateSnapshot(skipDraw || page2 ? "content-page2" : "content-normal", base, objectPtr, skipDraw || page2, false);
    LONG* dumped = (skipDraw || page2) ? (LONG*)&g_materialsContentArgDumpedPage2 : (LONG*)&g_materialsContentArgDumpedNormal;
    if (InterlockedCompareExchange(dumped, 1, 0) != 0) return;

    uintptr_t modePtr = base ? SafeReadPtrTrace(base + 0x4101e4) : 0;
    uintptr_t resolvedDraw = base ? SafeReadPtrTrace(base + 0x4186c4) : 0;
    uint32_t mode = SafeReadDwordTrace(modePtr);
    char resolvedDesc[256] = {};
    DescribeAddressTrace(resolvedDraw, resolvedDesc, sizeof(resolvedDesc));

    uintptr_t container = SafeReadPtrTrace(objectPtr + 0x60);
    AppendMaterialsTraceLine(
        "content-arg %s object=%p mode=%u modePtr=%p resolvedDraw=%p %s container60=%p obj[00..7c]=%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x",
        label,
        (void*)objectPtr,
        (unsigned)mode,
        (void*)modePtr,
        (void*)resolvedDraw,
        resolvedDesc,
        (void*)container,
        SafeReadDwordTrace(objectPtr + 0x00),
        SafeReadDwordTrace(objectPtr + 0x04),
        SafeReadDwordTrace(objectPtr + 0x08),
        SafeReadDwordTrace(objectPtr + 0x0c),
        SafeReadDwordTrace(objectPtr + 0x10),
        SafeReadDwordTrace(objectPtr + 0x14),
        SafeReadDwordTrace(objectPtr + 0x18),
        SafeReadDwordTrace(objectPtr + 0x1c),
        SafeReadDwordTrace(objectPtr + 0x20),
        SafeReadDwordTrace(objectPtr + 0x24),
        SafeReadDwordTrace(objectPtr + 0x28),
        SafeReadDwordTrace(objectPtr + 0x2c),
        SafeReadDwordTrace(objectPtr + 0x30),
        SafeReadDwordTrace(objectPtr + 0x34),
        SafeReadDwordTrace(objectPtr + 0x38),
        SafeReadDwordTrace(objectPtr + 0x3c),
        SafeReadDwordTrace(objectPtr + 0x40),
        SafeReadDwordTrace(objectPtr + 0x44),
        SafeReadDwordTrace(objectPtr + 0x48),
        SafeReadDwordTrace(objectPtr + 0x4c),
        SafeReadDwordTrace(objectPtr + 0x50),
        SafeReadDwordTrace(objectPtr + 0x54),
        SafeReadDwordTrace(objectPtr + 0x58),
        SafeReadDwordTrace(objectPtr + 0x5c),
        SafeReadDwordTrace(objectPtr + 0x60),
        SafeReadDwordTrace(objectPtr + 0x64),
        SafeReadDwordTrace(objectPtr + 0x68),
        SafeReadDwordTrace(objectPtr + 0x6c),
        SafeReadDwordTrace(objectPtr + 0x70),
        SafeReadDwordTrace(objectPtr + 0x74),
        SafeReadDwordTrace(objectPtr + 0x78),
        SafeReadDwordTrace(objectPtr + 0x7c));

    if (container && !IsBadReadPtr((void*)container, 0x80)) {
        AppendMaterialsTraceLine(
            "content-arg %s container60=%p dwords[00..7c]=%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x",
            label,
            (void*)container,
            SafeReadDwordTrace(container + 0x00),
            SafeReadDwordTrace(container + 0x04),
            SafeReadDwordTrace(container + 0x08),
            SafeReadDwordTrace(container + 0x0c),
            SafeReadDwordTrace(container + 0x10),
            SafeReadDwordTrace(container + 0x14),
            SafeReadDwordTrace(container + 0x18),
            SafeReadDwordTrace(container + 0x1c),
            SafeReadDwordTrace(container + 0x20),
            SafeReadDwordTrace(container + 0x24),
            SafeReadDwordTrace(container + 0x28),
            SafeReadDwordTrace(container + 0x2c),
            SafeReadDwordTrace(container + 0x30),
            SafeReadDwordTrace(container + 0x34),
            SafeReadDwordTrace(container + 0x38),
            SafeReadDwordTrace(container + 0x3c),
            SafeReadDwordTrace(container + 0x40),
            SafeReadDwordTrace(container + 0x44),
            SafeReadDwordTrace(container + 0x48),
            SafeReadDwordTrace(container + 0x4c),
            SafeReadDwordTrace(container + 0x50),
            SafeReadDwordTrace(container + 0x54),
            SafeReadDwordTrace(container + 0x58),
            SafeReadDwordTrace(container + 0x5c),
            SafeReadDwordTrace(container + 0x60),
            SafeReadDwordTrace(container + 0x64),
            SafeReadDwordTrace(container + 0x68),
            SafeReadDwordTrace(container + 0x6c),
            SafeReadDwordTrace(container + 0x70),
            SafeReadDwordTrace(container + 0x74),
            SafeReadDwordTrace(container + 0x78),
            SafeReadDwordTrace(container + 0x7c));
    }

    for (int i = 0; i < 8; ++i) {
        uintptr_t child = SafeReadPtrTrace(objectPtr + (uintptr_t)i * 4);
        if (child && child != 0xFFFFFFFFu && !IsBadReadPtr((void*)child, 0x20)) {
            AppendMaterialsTraceLine(
                "content-arg %s child[%02d]=%p dwords=%08x %08x %08x %08x %08x %08x %08x %08x",
                label,
                i,
                (void*)child,
                SafeReadDwordTrace(child + 0x00),
                SafeReadDwordTrace(child + 0x04),
                SafeReadDwordTrace(child + 0x08),
                SafeReadDwordTrace(child + 0x0c),
                SafeReadDwordTrace(child + 0x10),
                SafeReadDwordTrace(child + 0x14),
                SafeReadDwordTrace(child + 0x18),
                SafeReadDwordTrace(child + 0x1c));
        }
    }
}

extern "C" void __stdcall DrawMaterialsPage2Content()
{
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    if (!base || !g_materialsContentDrawTarget) return;

    uintptr_t contentArgPtr = SafeReadPtrTrace(base + 0x410224);
    uintptr_t contentArg = SafeReadPtrTrace(contentArgPtr);
    uintptr_t modePtr = SafeReadPtrTrace(base + 0x4101e4);
    if (!contentArg || !modePtr || IsBadWritePtr((void*)modePtr, sizeof(uint32_t))) return;

    uint32_t priorMode = SafeReadDwordTrace(modePtr);
    uintptr_t renderContainer = SafeReadPtrTrace(contentArg + 0x60);
    typedef void (__stdcall *DrawContentFn)(uintptr_t);
    DrawContentFn draw = (DrawContentFn)g_materialsContentDrawTarget;

    *(uint32_t*)modePtr = 4;
    if (renderContainer) {
        InterlockedExchange(&g_materialsPage2RenderContainer, (LONG)renderContainer);
    }
    AppendMaterialsTraceLine(
        "page2-direct-content object=%p container=%p modePrior=%u modeNow=4 target=%p",
        (void*)contentArg,
        (void*)renderContainer,
        (unsigned)priorMode,
        (void*)g_materialsContentDrawTarget);
    draw(contentArg);
    InterlockedExchange(&g_materialsPage2RenderContainer, 0);
    *(uint32_t*)modePtr = priorMode;
}

typedef void (__stdcall *SoEProjectDrawRectFn)(int x1, int y1, int x2, int y2, int arg5, int arg6);
typedef void (__stdcall *SoEProjectDrawSpriteFn)(void* frame, int x, int y, int arg4, int arg5, int arg6);

static void GetSoEPage2BottomButtonRect(int baseX, int baseY, int* left, int* top, int* right, int* bottom)
{
#if defined(SOE_MATERIALS_PAGE2_ORIGINAL_MATERIAL_SLOT)
    int spriteX = baseX + 0x0f;
    int spriteY = baseY - 0x15;
    if (left) *left = spriteX;
    if (top) *top = spriteY - 0x23;
    if (right) *right = spriteX + 0x20;
    if (bottom) *bottom = spriteY + 5;
#elif defined(SOE_MATERIALS_PAGE2_NATIVE_BUTTON_SPRITE) || defined(SOE_MATERIALS_PAGE2_SHIFTED_BUTTON_LAYOUT)
    int spriteX = baseX + 0x17;
    int spriteY = baseY - 0x15;
    if (left) *left = spriteX;
    if (top) *top = spriteY - 0x23;
    if (right) *right = spriteX + 0x20;
    if (bottom) *bottom = spriteY + 5;
#else
    int buttonX = baseX + 0x0f + 0x33;
    int buttonY = baseY - 0x15;
    if (left) *left = buttonX - 12;
    if (top) *top = buttonY - 0x23;
    if (right) *right = buttonX + 0x18;
    if (bottom) *bottom = buttonY + 5;
#endif
}

static void DrawSoEPage2RomanIIGlyph(SoEProjectDrawRectFn drawRect, int left, int top, int right, int bottom, bool active, bool hover)
{
    if (!drawRect || right <= left || bottom <= top) return;

    int ink = active ? 2 : (hover ? 2 : 1);
    int cx = (left + right) / 2;
    int glyphTop = top + 8;
    int glyphBottom = bottom - 7;
    if (glyphBottom <= glyphTop) return;

    drawRect(cx - 7, glyphTop - 1, cx - 2, glyphTop + 1, 0, ink);
    drawRect(cx + 2, glyphTop - 1, cx + 7, glyphTop + 1, 0, ink);
    drawRect(cx - 6, glyphTop + 1, cx - 4, glyphBottom, 0, ink);
    drawRect(cx + 4, glyphTop + 1, cx + 6, glyphBottom, 0, ink);
    drawRect(cx - 7, glyphBottom, cx - 2, glyphBottom + 2, 0, ink);
    drawRect(cx + 2, glyphBottom, cx + 7, glyphBottom + 2, 0, ink);
}

static void DrawSoEPage2TabGlyph(SoEProjectDrawRectFn drawRect, int left, int top, int right, int bottom, bool active, bool hover)
{
    if (!drawRect || right <= left || bottom <= top) return;

    int dark = 0;
    int light = active ? 2 : (hover ? 2 : 1);
    int mid = hover ? 2 : 1;

    // Draw a compact framed button instead of a filled mask. The native stash
    // art remains visible underneath, so this behaves more like a tab marker.
    drawRect(left, top, right, top + 1, 0, light);
    drawRect(left, top, left + 1, bottom, 0, light);
    drawRect(left, bottom - 1, right, bottom, 0, dark);
    drawRect(right - 1, top, right, bottom, 0, dark);
    drawRect(left + 2, top + 2, right - 2, top + 3, 0, mid);
    drawRect(left + 2, top + 2, left + 3, bottom - 2, 0, mid);
    drawRect(left + 2, bottom - 3, right - 2, bottom - 2, 0, dark);
    drawRect(right - 3, top + 2, right - 2, bottom - 2, 0, dark);

    DrawSoEPage2RomanIIGlyph(drawRect, left, top, right, bottom, active, hover);
}

static void DrawSoEPageButtonFrameOnly(SoEProjectDrawRectFn drawRect, int left, int top, int right, int bottom, bool active, bool hover)
{
    if (!drawRect || right <= left || bottom <= top) return;

    int dark = 0;
    int light = active ? 2 : (hover ? 2 : 1);
    int mid = hover ? 2 : 1;

    drawRect(left, top, right, top + 1, 0, light);
    drawRect(left, top, left + 1, bottom, 0, light);
    drawRect(left, bottom - 1, right, bottom, 0, dark);
    drawRect(right - 1, top, right, bottom, 0, dark);
    drawRect(left + 2, top + 2, right - 2, top + 3, 0, mid);
    drawRect(left + 2, top + 2, left + 3, bottom - 2, 0, mid);
    drawRect(left + 2, bottom - 3, right - 2, bottom - 2, 0, dark);
    drawRect(right - 3, top + 2, right - 2, bottom - 2, 0, dark);
}

static void DrawSoEPageButtonBackplate(SoEProjectDrawRectFn drawRect, int left, int top, int right, int bottom, bool active, bool hover)
{
    if (!drawRect || right <= left || bottom <= top) return;

    drawRect(left + 2, top + 2, right - 2, bottom - 2, 0, 0);
    DrawSoEPageButtonFrameOnly(drawRect, left, top, right, bottom, active, hover);
}

extern "C" void __stdcall DrawMaterialsPage2NativeButtonUnderlay(int page1X, int page2X, int spriteY)
{
#if defined(SOE_MATERIALS_DUPLICATE_NATIVE_MATERIAL_BUTTON)
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    if (!base) return;

    SoEProjectDrawRectFn drawRect = (SoEProjectDrawRectFn)(base + 0x1e6b60);
    if (!drawRect) return;

    int page1Left = page1X - 2;
    int page1Top = spriteY - 0x25;
    int page1Right = page1X + 0x22;
    int page1Bottom = spriteY + 7;
    DrawSoEPageButtonBackplate(drawRect, page1Left, page1Top, page1Right, page1Bottom, false, false);

    int page2Left = page2X - 2;
    int page2Top = spriteY - 0x25;
    int page2Right = page2X + 0x22;
    int page2Bottom = spriteY + 7;
    DrawSoEPageButtonBackplate(drawRect, page2Left, page2Top, page2Right, page2Bottom, false, false);
#else
    (void)page1X;
    (void)page2X;
    (void)spriteY;
#endif
}

extern "C" void __stdcall DrawMaterialsPage2NativeButtonBadge(int page2X, int spriteY)
{
#if defined(SOE_MATERIALS_DUPLICATE_NATIVE_MATERIAL_BUTTON)
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    if (!base) return;

    SoEProjectDrawRectFn drawRect = (SoEProjectDrawRectFn)(base + 0x1e6b60);
    if (!drawRect) return;

    int left = page2X + 19;
    int top = spriteY - 13;
    int right = page2X + 31;
    int bottom = spriteY + 3;
    DrawSoEPage2RomanIIGlyph(drawRect, left, top, right, bottom, false, false);
#else
    (void)page2X;
    (void)spriteY;
#endif
}

static void DrawSoEPage2ButtonOverlay(uintptr_t base, const char* reason)
{
#ifndef SOE_MATERIALS_PAGE2_VISUAL_BUTTON
    (void)base;
    (void)reason;
    return;
#else
    if (!base || !SafeReadDwordTrace(base + 0x2d4b24)) return;

    uintptr_t ptrBaseX = SafeReadPtrTrace(base + 0x4100a0);
    uintptr_t ptrBaseY1 = SafeReadPtrTrace(base + 0x4101ec);
    uintptr_t ptrBaseY2 = SafeReadPtrTrace(base + 0x4101a8);
    uintptr_t ptrMouseX = SafeReadPtrTrace(base + 0x41016c);
    uintptr_t ptrMouseY = SafeReadPtrTrace(base + 0x4101f8);

    int baseX = (int)SafeReadDwordTrace(ptrBaseX);
    int baseY = (int)SafeReadDwordTrace(ptrBaseY1) + (int)SafeReadDwordTrace(ptrBaseY2);
    int mouseX = (int)SafeReadDwordTrace(ptrMouseX);
    int mouseY = (int)SafeReadDwordTrace(ptrMouseY);
    if (baseX <= 0 || baseY <= 0) {
        baseX = 214;
        baseY = 528;
    }

    int matLeft = 0;
    int matTop = 0;
    int matRight = 0;
    int matBottom = 0;
    GetSoEPage2BottomButtonRect(baseX, baseY, &matLeft, &matTop, &matRight, &matBottom);
#if defined(SOE_MATERIALS_PAGE2_NATIVE_BUTTON_SPRITE)
    int matVisualLeft = matLeft;
    int matVisualTop = matTop;
    int matVisualRight = matRight;
    int matVisualBottom = matBottom;
#else
    int visualSize = 30;
    int matVisualLeft = matLeft + 2;
    int matVisualTop = matTop + ((matBottom - matTop) - visualSize) / 2;
    int matVisualRight = matVisualLeft + visualSize;
    int matVisualBottom = matVisualTop + visualSize;
#endif

#ifdef SOE_MATERIALS_PAGE2_ENABLE_TOP_HITBOX
    int topHitLeft = baseX + 190;
    int topHitTop = baseY - 350;
    int topHitRight = baseX + 390;
    int topHitBottom = baseY - 200;
#else
    int topHitLeft = 0;
    int topHitTop = 0;
    int topHitRight = 0;
    int topHitBottom = 0;
#endif
    int topWidth = 48;
    int topHeight = 28;
    int topLeft = ((topHitLeft + topHitRight) / 2) - (topWidth / 2);
    int topTop = topHitTop + 6;
    int topRight = topLeft + topWidth;
    int topBottom = topTop + topHeight;

    bool hoverMat = mouseX >= matLeft && mouseX <= matRight && mouseY >= matTop && mouseY <= matBottom;
#ifdef SOE_MATERIALS_PAGE2_ENABLE_TOP_HITBOX
    bool hoverTop = mouseX >= topHitLeft && mouseX <= topHitRight && mouseY >= topHitTop && mouseY <= topHitBottom;
#else
    bool hoverTop = false;
#endif

    uintptr_t objectSlot = SafeReadPtrTrace(base + 0x410224);
    uintptr_t currentObject = SafeReadPtrTrace(objectSlot);
    uintptr_t objectData = currentObject ? SafeReadPtrTrace(currentObject + 0x14) : 0;
    uint32_t objectPage = objectData ? SafeReadDwordTrace(objectData + 0x1b4) : 0xffffffffu;
    bool active = (objectPage == 0x0bu);

    SoEProjectDrawRectFn drawRect = (SoEProjectDrawRectFn)(base + 0x1e6b60);
    SoEProjectDrawSpriteFn drawSprite = (SoEProjectDrawSpriteFn)(base + 0x1e6c20);
    (void)topLeft;
    (void)topTop;
    (void)topRight;
    (void)topBottom;
    (void)hoverTop;
#if defined(SOE_MATERIALS_PAGE2_NATIVE_BUTTON_SPRITE)
    uint32_t materialButtonFrameBase = SafeReadDwordTrace(base + 0x30dd38);
    uint32_t materialButtonFrame = materialButtonFrameBase + (active ? 0x1e : 0x1c);
    if (drawSprite) {
        uint8_t frame[0x48] = {};
        *(uint32_t*)frame = materialButtonFrame;
        drawSprite(frame, matLeft, matBottom - 5, 0xffffffffu, 5, 0);
    }
    DrawSoEPage2RomanIIGlyph(drawRect, matVisualLeft, matVisualTop, matVisualRight, matVisualBottom, active, hoverMat);
#elif defined(SOE_MATERIALS_DUPLICATE_NATIVE_MATERIAL_BUTTON)
    (void)drawRect;
    (void)drawSprite;
#else
    DrawSoEPage2TabGlyph(drawRect, matVisualLeft, matVisualTop, matVisualRight, matVisualBottom, active, hoverMat);
#endif

    static DWORD lastButtonLogTick = 0;
    DWORD now = GetTickCount();
    if (now - lastButtonLogTick > 1000) {
        lastButtonLogTick = now;
        AppendMaterialsTraceLine(
            "page2-visual-button draw reason=%s active=%d objectPage=%08x mouse=%d,%d matVisual=%d,%d,%d,%d matHit=%d,%d,%d,%d topHit=%d,%d,%d,%d base=%d,%d rectFn=%p spriteFn=%p",
            reason ? reason : "?",
            active ? 1 : 0,
            (unsigned)objectPage,
            mouseX,
            mouseY,
            matVisualLeft,
            matVisualTop,
            matVisualRight,
            matVisualBottom,
            matLeft,
            matTop,
            matRight,
            matBottom,
            topHitLeft,
            topHitTop,
            topHitRight,
            topHitBottom,
            baseX,
            baseY,
            (void*)drawRect,
            (void*)drawSprite);
    }
#endif
}

extern "C" void __stdcall DrawMaterialsPage2ButtonOverlay()
{
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    DrawSoEPage2MaterialMask(base, "content-draw-overlay");
    DrawSoEPage2ButtonOverlay(base, "content-draw-overlay");
}

static void DrawSoEPage2Scaffold(uintptr_t base)
{
    LoadSoEPage2Storage();

    uintptr_t ptrBaseX = base ? SafeReadPtrTrace(base + 0x4100a0) : 0;
    uintptr_t ptrBaseY1 = base ? SafeReadPtrTrace(base + 0x4101ec) : 0;
    uintptr_t ptrBaseY2 = base ? SafeReadPtrTrace(base + 0x4101a8) : 0;
    int baseX = (int)SafeReadDwordTrace(ptrBaseX);
    int baseY = (int)SafeReadDwordTrace(ptrBaseY1) + (int)SafeReadDwordTrace(ptrBaseY2);
    if (baseX <= 0 || baseY <= 0) {
        baseX = 214;
        baseY = 528;
    }

    int cell = 42;
    int cols = 13;
    int rows = 18;
    int width = cols * cell;
    int height = rows * cell;
    int left = baseX + 180;
    int top = baseY - 443;

    typedef void (__stdcall *ProjectDrawRectFn)(int x1, int y1, int x2, int y2, int arg5, int arg6);
    ProjectDrawRectFn drawRect = (ProjectDrawRectFn)(base + 0x1e6b60);
    if (!drawRect) {
        return;
    }

#ifdef SOE_MATERIALS_PAGE2_RECT_PARAM_PROBE
    int probeLeft = left + 8;
    int probeTop = top + 8;
    for (int i = 0; i < 8; ++i) {
        int x = probeLeft + i * 28;
        drawRect(x, probeTop, x + 22, probeTop + 22, 0, i);
        drawRect(x, probeTop + 30, x + 22, probeTop + 52, i, 0);
    }

    static DWORD lastProbeLogTick = 0;
    DWORD probeNow = GetTickCount();
    if (probeNow - lastProbeLogTick > 1000) {
        lastProbeLogTick = probeNow;
        AppendMaterialsTraceLine(
            "page2-rect-param-probe pos=%d,%d base=%d,%d fn=%p",
            probeLeft,
            probeTop,
            baseX,
            baseY,
            (void*)drawRect);
    }
    return;
#endif

#ifdef SOE_MATERIALS_PAGE2_OPAQUE_MASK
    static DWORD lastMaskLogTick = 0;
    DWORD maskNow = GetTickCount();
    if (maskNow - lastMaskLogTick > 1000) {
        lastMaskLogTick = maskNow;
        AppendMaterialsTraceLine(
            "page2-opaque-mask draw pos=%d,%d size=%d,%d base=%d,%d fn=%p",
            left,
            top,
            width,
            height,
            baseX,
            baseY,
            (void*)drawRect);
    }

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int x = left + c * cell;
            int y = top + r * cell;
            drawRect(x + 1, y + 1, x + cell - 1, y + cell - 1, 0, 1);
        }
    }
    for (int c = 0; c <= cols; ++c) {
        int x = left + c * cell;
        drawRect(x, top, x + 1, top + height, 0, 0);
    }
    for (int r = 0; r <= rows; ++r) {
        int y = top + r * cell;
        drawRect(left, y, left + width, y + 1, 0, 0);
    }
    return;
#endif

    static DWORD lastLogTick = 0;
    DWORD now = GetTickCount();
    if (now - lastLogTick > 1000) {
        lastLogTick = now;
        AppendMaterialsTraceLine(
            "page2-native-grid draw pos=%d,%d size=%d,%d base=%d,%d fn=%p",
            left,
            top,
            width,
            height,
            baseX,
            baseY,
            (void*)drawRect);
    }

    uintptr_t ptrMouseX = base ? SafeReadPtrTrace(base + 0x41016c) : 0;
    uintptr_t ptrMouseY = base ? SafeReadPtrTrace(base + 0x4101f8) : 0;
    int mouseX = (int)SafeReadDwordTrace(ptrMouseX);
    int mouseY = (int)SafeReadDwordTrace(ptrMouseY);
    if (mouseX >= left && mouseX <= left + width && mouseY >= top && mouseY <= top + height) {
        // These are the item-tooltip latch globals used by the stash handlers.
        // Clearing only while the mouse is over our synthetic page prevents
        // hidden personal/shared stash items from producing tooltips.
        *(uint32_t*)(base + 0x30dcac) = 0;
        *(uint32_t*)(base + 0x30dca4) = 0;
    }

    // Reuse ProjectDiablo's native rectangle renderer, which is already used by
    // the materials UI. Fill individual cells first so the old material icons
    // do not bleed through the synthetic page.
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int x = left + c * cell;
            int y = top + r * cell;
            drawRect(x + 1, y + 1, x + cell - 1, y + cell - 1, 0, 0);
        }
    }
    for (int c = 0; c <= cols; ++c) {
        int x = left + c * cell;
        drawRect(x, top, x + 1, top + height, 0, 0);
    }
    for (int r = 0; r <= rows; ++r) {
        int y = top + r * cell;
        drawRect(left, y, left + width, y + 1, 0, 0);
    }
}

extern "C" uintptr_t __stdcall HookD2Common11139(uintptr_t container, uint32_t index)
{
    typedef uintptr_t (__stdcall *Fn)(uintptr_t, uint32_t);
    Fn orig = (Fn)g_materialsOrigD2Common11139;
    uintptr_t result = orig ? orig(container, index) : 0;

    uintptr_t retAddr = (uintptr_t)__builtin_return_address(0);
    HMODULE hD2Client = GetModuleHandleA("D2Client.dll");
    uintptr_t clientBase = (uintptr_t)hD2Client;
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    uintptr_t rva = (clientBase && retAddr >= clientBase) ? (retAddr - clientBase) : 0;
    bool fromMaterialsRenderer = (rva >= 0x953b0 && rva <= 0x95c40);
    bool page2MaterialsPanel = false;
    if (base) {
        uintptr_t ptrPanel = SafeReadPtrTrace(base + 0x410688);
        uintptr_t ptrExtra = SafeReadPtrTrace(base + 0x410a74);
        page2MaterialsPanel = (
            g_materialsPage2Active &&
            SafeReadDwordTrace(ptrPanel) == 0x0c &&
            SafeReadDwordTrace(ptrExtra) == 0x02);
    }
    uintptr_t page2Container = (uintptr_t)g_materialsPage2RenderContainer;
#if defined(SOE_MATERIALS_PAGE2_MATERIAL_RULES) && defined(SOE_MATERIALS_ITERATOR_PAGE2_BRIDGE_UNITS)
    if (fromMaterialsRenderer && page2MaterialsPanel) {
        uintptr_t indexBasePtr = base ? SafeReadPtrTrace(base + 0x4102f8) : 0;
        uint32_t indexBase = SafeReadDwordTrace(indexBasePtr);
        uint32_t ordinal = index >= indexBase ? (index - indexBase) : index;
        uintptr_t bridged = FindSoEPage2BridgeUnit(base, container, ordinal);
        if (bridged) {
            LONG bridgeNo = InterlockedIncrement(&g_materialsIteratorBridgeLogCount);
            if (bridgeNo <= 180) {
                uint32_t code = ItemCodeForUnit(bridged);
                char codeText[5] = {};
                SoECodeToText(code, codeText);
                uintptr_t bridgedData = SafeReadPtrTrace(bridged + 0x14);
                uintptr_t bridgedPath = SafeReadPtrTrace(bridged + 0x2c);
                AppendMaterialsTraceLine(
                    "iterator11139-bridge #%ld index=%u indexBase=%u ordinal=%u orig=%p bridged=%p code=%s/%08x page=%u xy=%u,%u",
                    (long)bridgeNo,
                    (unsigned)index,
                    (unsigned)indexBase,
                    (unsigned)ordinal,
                    (void*)result,
                    (void*)bridged,
                    codeText,
                    (unsigned)code,
                    (unsigned)SafeReadDwordTrace(bridgedData + 0x1b4),
                    (unsigned)SafeReadDwordTrace(bridgedPath + 0x0c),
                    (unsigned)SafeReadDwordTrace(bridgedPath + 0x10));
            }
            result = bridged;
        }
    }
#endif
    if (fromMaterialsRenderer && g_materialsPage2Active && InterlockedIncrement(&g_materialsIteratorPage2LogCount) <= 520) {
        uintptr_t objectSlot = base ? SafeReadPtrTrace(base + 0x410224) : 0;
        uintptr_t currentObject = SafeReadPtrTrace(objectSlot);
        uintptr_t currentContainer = currentObject ? SafeReadPtrTrace(currentObject + 0x60) : 0;
        uintptr_t playerSlot = base ? SafeReadPtrTrace(base + 0x4159bc) : 0;
        uintptr_t playerObject = SafeReadPtrTrace(playerSlot);
        uintptr_t playerContainer = playerObject ? SafeReadPtrTrace(playerObject + 0x60) : 0;
        uintptr_t itemData = result ? SafeReadPtrTrace(result + 0x14) : 0;
        uintptr_t result2c = result ? SafeReadPtrTrace(result + 0x2c) : 0;
        AppendMaterialsTraceLine(
            "iterator11139-page2 #%ld ret=%p rva=%08x container=%p cur60=%p player60=%p page2Container=%p index=%u result=%p itemData=%p item45=%02x result00=%08x result04=%08x result08=%08x result0c=%08x result10=%08x result14=%08x result18=%08x result1c=%08x result20=%08x result24=%08x result28=%08x result2c=%08x result30=%08x result34=%08x result38=%08x result3c=%08x result40=%08x result44=%08x result48=%08x result4c=%08x result50=%08x data=%08x %08x %08x %08x %08x %08x data2=%08x %08x %08x %08x %08x %08x node2c=%p node2cdata=%08x %08x %08x %08x %08x %08x %08x %08x %08x",
            (long)g_materialsIteratorPage2LogCount,
            (void*)retAddr,
            (unsigned)rva,
            (void*)container,
            (void*)currentContainer,
            (void*)playerContainer,
            (void*)page2Container,
            (unsigned)index,
            (void*)result,
            (void*)itemData,
            (unsigned)((SafeReadDwordTrace(itemData + 0x44) >> 8) & 0xffu),
            SafeReadDwordTrace(result + 0x00),
            SafeReadDwordTrace(result + 0x04),
            SafeReadDwordTrace(result + 0x08),
            SafeReadDwordTrace(result + 0x0c),
            SafeReadDwordTrace(result + 0x10),
            SafeReadDwordTrace(result + 0x14),
            SafeReadDwordTrace(result + 0x18),
            SafeReadDwordTrace(result + 0x1c),
            SafeReadDwordTrace(result + 0x20),
            SafeReadDwordTrace(result + 0x24),
            SafeReadDwordTrace(result + 0x28),
            SafeReadDwordTrace(result + 0x2c),
            SafeReadDwordTrace(result + 0x30),
            SafeReadDwordTrace(result + 0x34),
            SafeReadDwordTrace(result + 0x38),
            SafeReadDwordTrace(result + 0x3c),
            SafeReadDwordTrace(result + 0x40),
            SafeReadDwordTrace(result + 0x44),
            SafeReadDwordTrace(result + 0x48),
            SafeReadDwordTrace(result + 0x4c),
            SafeReadDwordTrace(result + 0x50),
            SafeReadDwordTrace(itemData + 0x00),
            SafeReadDwordTrace(itemData + 0x04),
            SafeReadDwordTrace(itemData + 0x18),
            SafeReadDwordTrace(itemData + 0x2c),
            SafeReadDwordTrace(itemData + 0x40),
            SafeReadDwordTrace(itemData + 0x44),
            SafeReadDwordTrace(itemData + 0x48),
            SafeReadDwordTrace(itemData + 0x4c),
            SafeReadDwordTrace(itemData + 0x50),
            SafeReadDwordTrace(itemData + 0x54),
            SafeReadDwordTrace(itemData + 0x58),
            SafeReadDwordTrace(itemData + 0x5c),
            (void*)result2c,
            SafeReadDwordTrace(result2c + 0x00),
            SafeReadDwordTrace(result2c + 0x04),
            SafeReadDwordTrace(result2c + 0x08),
            SafeReadDwordTrace(result2c + 0x0c),
            SafeReadDwordTrace(result2c + 0x10),
            SafeReadDwordTrace(result2c + 0x14),
            SafeReadDwordTrace(result2c + 0x18),
            SafeReadDwordTrace(result2c + 0x1c),
            SafeReadDwordTrace(result2c + 0x20));
    }
    if (fromMaterialsRenderer && g_materialsPage2Active && InterlockedIncrement(&g_materialsIteratorStackLogCount) <= 160) {
        uintptr_t espNow = 0;
        __asm__ __volatile__("movl %%esp, %0" : "=r"(espNow));

        char projectHits[320] = {};
        char clientHits[320] = {};
        size_t projectUsed = 0;
        size_t clientUsed = 0;
        const uintptr_t projectEnd = base ? (base + 0x00450000u) : 0;
        const uintptr_t clientEnd = clientBase ? (clientBase + 0x00200000u) : 0;
        for (uint32_t off = 0; off <= 0x220; off += 4) {
            uintptr_t value = SafeReadPtrTrace(espNow + off);
            if (base && value >= base && value < projectEnd && projectUsed < sizeof(projectHits) - 32) {
                int wrote = _snprintf(
                    projectHits + projectUsed,
                    sizeof(projectHits) - projectUsed,
                    "%s+%03x:%06x",
                    projectUsed ? "," : "",
                    (unsigned)off,
                    (unsigned)(value - base));
                if (wrote > 0) projectUsed += (size_t)wrote;
            }
            if (clientBase && value >= clientBase && value < clientEnd && clientUsed < sizeof(clientHits) - 32) {
                int wrote = _snprintf(
                    clientHits + clientUsed,
                    sizeof(clientHits) - clientUsed,
                    "%s+%03x:%06x",
                    clientUsed ? "," : "",
                    (unsigned)off,
                    (unsigned)(value - clientBase));
                if (wrote > 0) clientUsed += (size_t)wrote;
            }
        }

        AppendMaterialsTraceLine(
            "iterator11139-stack #%ld retRva=%08x esp=%p container=%p index=%u result=%p item=%08x pHits=[%s] cHits=[%s] s=%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x",
            (long)g_materialsIteratorStackLogCount,
            (unsigned)rva,
            (void*)espNow,
            (void*)container,
            (unsigned)index,
            (void*)result,
            SafeReadDwordTrace(result + 0x04),
            projectHits[0] ? projectHits : "-",
            clientHits[0] ? clientHits : "-",
            SafeReadDwordTrace(espNow + 0x00),
            SafeReadDwordTrace(espNow + 0x04),
            SafeReadDwordTrace(espNow + 0x08),
            SafeReadDwordTrace(espNow + 0x0c),
            SafeReadDwordTrace(espNow + 0x10),
            SafeReadDwordTrace(espNow + 0x14),
            SafeReadDwordTrace(espNow + 0x18),
            SafeReadDwordTrace(espNow + 0x1c),
            SafeReadDwordTrace(espNow + 0x20),
            SafeReadDwordTrace(espNow + 0x24),
            SafeReadDwordTrace(espNow + 0x28),
            SafeReadDwordTrace(espNow + 0x2c));
    }
#ifdef SOE_MATERIALS_ITERATOR_PAGE2_BLANK
    if (fromMaterialsRenderer && page2MaterialsPanel && index <= 10 && result && SafeReadDwordTrace(result + 0x24) == 0x0000029au) {
        LONG blankNo = InterlockedIncrement(&g_materialsIteratorPage2BlankCount);
        if (blankNo <= 80) {
            AppendMaterialsTraceLine(
                "iterator11139-page2-blank #%ld ret=%p rva=%08x container=%p index=%u result=%p item=%08x slotData=%08x",
                (long)blankNo,
                (void*)retAddr,
                (unsigned)rva,
                (void*)container,
                (unsigned)index,
                (void*)result,
                SafeReadDwordTrace(result + 0x04),
                SafeReadDwordTrace(result + 0x24));
        }
        return 0;
    }
#endif
#ifdef SOE_MATERIALS_ITERATOR_PAGE2_MODE4_BLANK
    if (fromMaterialsRenderer && page2MaterialsPanel && result && index <= 15) {
        uintptr_t modePtr = base ? SafeReadPtrTrace(base + 0x4101e4) : 0;
        uintptr_t objectSlot = base ? SafeReadPtrTrace(base + 0x410224) : 0;
        uintptr_t currentObject = SafeReadPtrTrace(objectSlot);
        uintptr_t currentContainer = currentObject ? SafeReadPtrTrace(currentObject + 0x60) : 0;
        uint32_t projectMode = SafeReadDwordTrace(modePtr);
        uint32_t itemMarker = SafeReadDwordTrace(result + 0x24);
        if (projectMode == 4 && container == currentContainer && itemMarker == 0x0000029au) {
            LONG blankNo = InterlockedIncrement(&g_materialsIteratorPage2BlankCount);
            if (blankNo <= 80) {
                AppendMaterialsTraceLine(
                    "iterator11139-page2-mode4-blank #%ld ret=%p rva=%08x container=%p current60=%p index=%u result=%p item=%08x marker=%08x mode=%u",
                    (long)blankNo,
                    (void*)retAddr,
                    (unsigned)rva,
                    (void*)container,
                    (void*)currentContainer,
                    (unsigned)index,
                    (void*)result,
                    SafeReadDwordTrace(result + 0x04),
                    itemMarker,
                    (unsigned)projectMode);
            }
            return 0;
        }
    }
#endif
#ifdef SOE_MATERIALS_ITERATOR_PAGE2_BURST_BLANK
    if (fromMaterialsRenderer && page2MaterialsPanel && result && index <= 15) {
        uintptr_t objectSlot = base ? SafeReadPtrTrace(base + 0x410224) : 0;
        uintptr_t currentObject = SafeReadPtrTrace(objectSlot);
        uintptr_t currentContainer = currentObject ? SafeReadPtrTrace(currentObject + 0x60) : 0;
        uint32_t itemMarker = SafeReadDwordTrace(result + 0x24);
        DWORD now = GetTickCount();
        DWORD until = (DWORD)g_materialsPage2Mode4BurstUntilTick;
        uintptr_t latchedContainer = (uintptr_t)g_materialsPage2Mode4BurstContainer;
        if (now <= until && container == currentContainer && container == latchedContainer && itemMarker == 0x0000029au) {
            LONG blankNo = InterlockedIncrement(&g_materialsIteratorPage2BlankCount);
            if (blankNo <= 100) {
                AppendMaterialsTraceLine(
                    "iterator11139-page2-burst-blank #%ld ret=%p rva=%08x container=%p latched=%p current60=%p index=%u result=%p item=%08x marker=%08x now=%u until=%u",
                    (long)blankNo,
                    (void*)retAddr,
                    (unsigned)rva,
                    (void*)container,
                    (void*)latchedContainer,
                    (void*)currentContainer,
                    (unsigned)index,
                    (void*)result,
                    SafeReadDwordTrace(result + 0x04),
                    itemMarker,
                    (unsigned)now,
                    (unsigned)until);
            }
            return 0;
        }
    }
#endif
#ifdef SOE_MATERIALS_ITERATOR_PAGE2_MODE0_FIRSTGRID_BLANK
    if (fromMaterialsRenderer && page2MaterialsPanel && result && index <= 10) {
        uint32_t projectMode = clientBase ? SafeReadDwordTrace(clientBase + 0x11bc48) : 0;
        uint32_t itemMarker = SafeReadDwordTrace(result + 0x24);
        uintptr_t node2c = SafeReadPtrTrace(result + 0x2c);
        uint32_t slot = SafeReadDwordTrace(node2c + 0x0c);
        uintptr_t data2Container = SafeReadPtrTrace(SafeReadPtrTrace(result + 0x14) + 0x5c);
        if (projectMode == 0 &&
            itemMarker == 0x0000029au &&
            data2Container == container &&
            slot <= 10) {
            LONG blankNo = InterlockedIncrement(&g_materialsIteratorPage2BlankCount);
            if (blankNo <= 120) {
                AppendMaterialsTraceLine(
                    "iterator11139-page2-mode0-firstgrid-blank #%ld ret=%p rva=%08x container=%p index=%u result=%p slot=%u item=%08x marker=%08x",
                    (long)blankNo,
                    (void*)retAddr,
                    (unsigned)rva,
                    (void*)container,
                    (unsigned)index,
                    (void*)result,
                    (unsigned)slot,
                    SafeReadDwordTrace(result + 0x04),
                    itemMarker);
            }
            return 0;
        }
    }
#endif
    // Diagnostic only for all other material renderer calls; returning 0 broadly
    // also blanks equipped/inventory items because this iterator is shared.
    if (fromMaterialsRenderer && InterlockedIncrement(&g_materialsIteratorLogCount) <= 160) {
        AppendMaterialsTraceLine(
            "iterator11139 ret=%p rva=%08x container=%p index=%u result=%p result00=%08x result04=%08x result2c=%08x page2=%ld mode=%u",
            (void*)retAddr,
            (unsigned)rva,
            (void*)container,
            (unsigned)index,
            (void*)result,
            SafeReadDwordTrace(result + 0x00),
            SafeReadDwordTrace(result + 0x04),
            SafeReadDwordTrace(result + 0x2c),
            (long)g_materialsPage2Active,
            (unsigned)SafeReadDwordTrace(clientBase + 0x11bc48));
    }
    return result;
}

extern "C" void __stdcall LogMaterialsD2ClientDrawEntry(uintptr_t savedEsp)
{
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    HMODULE hD2Client = GetModuleHandleA("D2Client.dll");
    uintptr_t base = (uintptr_t)hProject;
    uintptr_t clientBase = (uintptr_t)hD2Client;

    uintptr_t retAddr = SafeReadPtrTrace(savedEsp + 0x00);
    uintptr_t arg0 = SafeReadPtrTrace(savedEsp + 0x04);
    uintptr_t retRva = base && retAddr >= base ? retAddr - base : retAddr;
    uintptr_t clientRetRva = clientBase && retAddr >= clientBase ? retAddr - clientBase : retAddr;
    uintptr_t ptrPanel = base ? SafeReadPtrTrace(base + 0x410688) : 0;
    uintptr_t ptrExtra = base ? SafeReadPtrTrace(base + 0x410a74) : 0;
    uintptr_t ptrMode = clientBase ? (clientBase + 0x11bc48) : 0;
    uint32_t panel = SafeReadDwordTrace(ptrPanel);
    uint32_t extra = SafeReadDwordTrace(ptrExtra);
    bool materialsPanel = (panel == 0x0c && (extra == 1 || extra == 2));
    bool page2 = g_materialsPage2Active != 0;
#ifdef SOE_MATERIALS_ITERATOR_PAGE2_BURST_BLANK
    uint32_t drawMode = SafeReadDwordTrace(ptrMode);
    if (page2 && panel == 0x0c && extra == 0x02 && drawMode == 4) {
        uintptr_t drawContainer = SafeReadPtrTrace(arg0 + 0x60);
        InterlockedExchange(&g_materialsPage2Mode4BurstUntilTick, (LONG)(GetTickCount() + 120));
        InterlockedExchange((volatile LONG*)&g_materialsPage2Mode4BurstContainer, (LONG)drawContainer);
    }
#endif

    LONG n = InterlockedIncrement(&g_materialsD2ClientDrawEntryLogCount);
    LONG page2n = 0;
    const char* label = "d2client-draw-entry";
    if (page2 || materialsPanel) {
        page2n = InterlockedIncrement(&g_materialsD2ClientDrawEntryPage2LogCount);
        label = page2 ? "d2client-draw-entry-page2" : "d2client-draw-entry-materials";
        if (page2n > 480) return;
    } else if (n > 120) {
        return;
    }
    if (!page2 && !materialsPanel && n > 420) return;

    AppendMaterialsTraceLine(
        "%s #%ld/%ld ret=%p projectRva=%08x clientRva=%08x arg=%p arg00=%08x arg04=%08x arg08=%08x arg0c=%08x arg10=%08x arg14=%08x arg18=%08x arg1c=%08x arg20=%08x arg24=%08x arg28=%08x arg2c=%08x panel=%08x extra=%08x page2=%ld mode=%u projGate=%08x projPanel=%08x projMode=%08x projObj=%p globals bcf0=%08x bcf8=%08x bc34=%08x bc38=%08x bc48=%08x bcb864=%08x",
        label,
        (long)n,
        (long)page2n,
        (void*)retAddr,
        (unsigned)retRva,
        (unsigned)clientRetRva,
        (void*)arg0,
        SafeReadDwordTrace(arg0 + 0x00),
        SafeReadDwordTrace(arg0 + 0x04),
        SafeReadDwordTrace(arg0 + 0x08),
        SafeReadDwordTrace(arg0 + 0x0c),
        SafeReadDwordTrace(arg0 + 0x10),
        SafeReadDwordTrace(arg0 + 0x14),
        SafeReadDwordTrace(arg0 + 0x18),
        SafeReadDwordTrace(arg0 + 0x1c),
        SafeReadDwordTrace(arg0 + 0x20),
        SafeReadDwordTrace(arg0 + 0x24),
        SafeReadDwordTrace(arg0 + 0x28),
        SafeReadDwordTrace(arg0 + 0x2c),
        panel,
        extra,
        (long)g_materialsPage2Active,
        (unsigned)SafeReadDwordTrace(ptrMode),
        base ? SafeReadDwordTrace(base + 0x40edd4) : 0,
        base ? SafeReadDwordTrace(SafeReadPtrTrace(base + 0x41023c)) : 0,
        base ? SafeReadDwordTrace(SafeReadPtrTrace(base + 0x4101e4)) : 0,
        (void*)(base ? SafeReadPtrTrace(SafeReadPtrTrace(base + 0x410224)) : 0),
        clientBase ? SafeReadDwordTrace(clientBase + 0x11bcf0) : 0,
        clientBase ? SafeReadDwordTrace(clientBase + 0x11bcf8) : 0,
        clientBase ? SafeReadDwordTrace(clientBase + 0x11bc34) : 0,
        clientBase ? SafeReadDwordTrace(clientBase + 0x11bc38) : 0,
        clientBase ? SafeReadDwordTrace(clientBase + 0x11bc48) : 0,
        clientBase ? SafeReadDwordTrace(clientBase + 0x11b864) : 0);
}

extern "C" int __stdcall PrepareMaterialsD2ClientDrawEntryWrap(uintptr_t savedEsp)
{
#if defined(SOE_MATERIALS_WRAP_PAGE2_MODE4_EMPTY_CONTAINER) || defined(SOE_MATERIALS_WRAP_PAGE2_MODE0_EMPTY_CONTAINER)
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    HMODULE hD2Client = GetModuleHandleA("D2Client.dll");
    uintptr_t base = (uintptr_t)hProject;
    uintptr_t clientBase = (uintptr_t)hD2Client;
    if (!base || !clientBase || !g_materialsPage2Active) return 0;
    if (g_materialsD2ClientDrawWrapActive) return 0;

    uintptr_t retAddr = SafeReadPtrTrace(savedEsp + 0x00);
    uintptr_t retRva = base && retAddr >= base ? retAddr - base : 0;
    uintptr_t objectPtr = SafeReadPtrTrace(savedEsp + 0x04);
    uint32_t panel = SafeReadDwordTrace(SafeReadPtrTrace(base + 0x410688));
    uint32_t extra = SafeReadDwordTrace(SafeReadPtrTrace(base + 0x410a74));
    uint32_t mode = clientBase ? SafeReadDwordTrace(clientBase + 0x11bc48) : 0;
    uintptr_t container = objectPtr ? SafeReadPtrTrace(objectPtr + 0x60) : 0;
    bool targetMode4 = false;
    bool targetMode0 = false;
#ifdef SOE_MATERIALS_WRAP_PAGE2_MODE4_EMPTY_CONTAINER
    targetMode4 = (retRva == 0x18bea7 && mode == 4);
#endif
#ifdef SOE_MATERIALS_WRAP_PAGE2_MODE0_EMPTY_CONTAINER
    targetMode0 = (retRva == 0x18d10d && mode == 0);
#endif
    bool nearPage2Mode = (g_materialsPage2Active && panel == 0x0c && extra == 0x02 && (mode == 4 || mode == 0));
    if (nearPage2Mode) {
        LONG n = InterlockedIncrement(&g_materialsD2ClientDrawWrapMissCount);
        if (n <= 80) {
            AppendMaterialsTraceLine(
                "d2client-draw-wrap probe #%ld ret=%p rva=%08x object=%p container=%p panel=%08x extra=%08x mode=%u match4=%d match0=%d readable=%d",
                (long)n,
                (void*)retAddr,
                (unsigned)retRva,
                (void*)objectPtr,
                (void*)container,
                panel,
                extra,
                (unsigned)mode,
                (targetMode4 && objectPtr && container) ? 1 : 0,
                (targetMode0 && objectPtr && container) ? 1 : 0,
                (container && !IsBadReadPtr((void*)container, sizeof(g_materialsPage2EmptyContainer))) ? 1 : 0);
        }
    }
    if (!(targetMode4 || targetMode0) || panel != 0x0c || extra != 0x02 || !objectPtr || !container) {
        return 0;
    }
    if (IsBadReadPtr((void*)container, sizeof(g_materialsPage2EmptyContainer))) {
        return 0;
    }

    memcpy(g_materialsPage2EmptyContainer, (void*)container, sizeof(g_materialsPage2EmptyContainer));
    *(uintptr_t*)(g_materialsPage2EmptyContainer + 0x0c) = 0;
    *(uintptr_t*)(g_materialsPage2EmptyContainer + 0x10) = 0;
    *(uintptr_t*)(g_materialsPage2EmptyContainer + 0x14) = 0;

    DWORD oldProt = 0;
    if (!VirtualProtect((void*)(objectPtr + 0x60), sizeof(uintptr_t), PAGE_READWRITE, &oldProt)) {
        LONG n = InterlockedIncrement(&g_materialsD2ClientDrawWrapMissCount);
        if (n <= 20) {
            AppendMaterialsTraceLine(
                "d2client-draw-wrap protect-failed #%ld object=%p slot=%p container=%p gle=%lu",
                (long)n,
                (void*)objectPtr,
                (void*)(objectPtr + 0x60),
                (void*)container,
                GetLastError());
        }
        return 0;
    }
    *(uintptr_t*)(objectPtr + 0x60) = (uintptr_t)g_materialsPage2EmptyContainer;
    VirtualProtect((void*)(objectPtr + 0x60), sizeof(uintptr_t), oldProt, &oldProt);

    InterlockedExchange((volatile LONG*)&g_materialsD2ClientDrawWrapObject, (LONG)objectPtr);
    InterlockedExchange((volatile LONG*)&g_materialsD2ClientDrawWrapOriginalContainer, (LONG)container);
    InterlockedExchange(&g_materialsD2ClientDrawWrapActive, 1);
    LONG n = InterlockedIncrement(&g_materialsD2ClientDrawWrapCount);
    if (n <= 80) {
        AppendMaterialsTraceLine(
            "d2client-draw-wrap empty-container #%ld ret=%p rva=%08x object=%p container=%p fake=%p headWas=%p panel=%08x extra=%08x mode=%u target=%s",
            (long)n,
            (void*)retAddr,
            (unsigned)retRva,
            (void*)objectPtr,
            (void*)container,
            (void*)g_materialsPage2EmptyContainer,
            (void*)SafeReadPtrTrace(container + 0x0c),
            panel,
            extra,
            (unsigned)mode,
            targetMode0 ? "mode0" : "mode4");
    }
    return 1;
#else
    (void)savedEsp;
    return 0;
#endif
}

extern "C" void __stdcall RestoreMaterialsD2ClientDrawEntryWrap()
{
#if defined(SOE_MATERIALS_WRAP_PAGE2_MODE4_EMPTY_CONTAINER) || defined(SOE_MATERIALS_WRAP_PAGE2_MODE0_EMPTY_CONTAINER)
    if (!g_materialsD2ClientDrawWrapActive) return;
    uintptr_t objectPtr = (uintptr_t)g_materialsD2ClientDrawWrapObject;
    uintptr_t container = (uintptr_t)g_materialsD2ClientDrawWrapOriginalContainer;
    if (objectPtr && container && !IsBadWritePtr((void*)(objectPtr + 0x60), sizeof(uintptr_t))) {
        DWORD oldProt = 0;
        if (VirtualProtect((void*)(objectPtr + 0x60), sizeof(uintptr_t), PAGE_READWRITE, &oldProt)) {
            *(uintptr_t*)(objectPtr + 0x60) = container;
            VirtualProtect((void*)(objectPtr + 0x60), sizeof(uintptr_t), oldProt, &oldProt);
        }
    }
    InterlockedExchange(&g_materialsD2ClientDrawWrapActive, 0);
    InterlockedExchange((volatile LONG*)&g_materialsD2ClientDrawWrapObject, 0);
    InterlockedExchange((volatile LONG*)&g_materialsD2ClientDrawWrapOriginalContainer, 0);
#endif
}

static void TryInstallMaterialsIteratorHook()
{
    if (g_materialsD2Common11139Hooked) return;
    HMODULE hD2Client = GetModuleHandleA("D2Client.dll");
    uintptr_t d2ClientBase = (uintptr_t)hD2Client;
    if (!d2ClientBase) return;

    g_materialsD2Common11139Slot = (uintptr_t*)(d2ClientBase + 0x0ce4e0);
    if (!g_materialsD2Common11139Slot || IsBadReadPtr(g_materialsD2Common11139Slot, sizeof(uintptr_t))) return;
    uintptr_t current = *g_materialsD2Common11139Slot;
    if (!current || current == (uintptr_t)&HookD2Common11139) return;

    DWORD oldProt = 0;
    if (VirtualProtect(g_materialsD2Common11139Slot, sizeof(uintptr_t), PAGE_READWRITE, &oldProt)) {
        g_materialsOrigD2Common11139 = current;
        *g_materialsD2Common11139Slot = (uintptr_t)&HookD2Common11139;
        VirtualProtect(g_materialsD2Common11139Slot, sizeof(uintptr_t), oldProt, &oldProt);
        g_materialsD2Common11139Hooked = true;
        AppendMaterialsTraceLine(
            "iterator11139 lazy-hook slot=%p orig=%p hook=%p",
            (void*)g_materialsD2Common11139Slot,
            (void*)g_materialsOrigD2Common11139,
            (void*)&HookD2Common11139);
    }
}

static void DumpMaterialsDescriptorTable(uintptr_t base)
{
    uintptr_t table = SafeReadPtrTrace(base + 0x41076c);
    AppendMaterialsTraceLine("descriptor-table ptr=%p", (void*)table);
    if (!table) return;

    for (int i = 0; i <= 0xd8; ++i) {
        uintptr_t entry = table + (uintptr_t)i * 10;
        if (IsBadReadPtr((void*)entry, 10)) {
            AppendMaterialsTraceLine("descriptor[%03d] unreadable entry=%p", i, (void*)entry);
            break;
        }
        uint32_t itemId = *(uint32_t*)(entry + 0);
        uint16_t slot = *(uint16_t*)(entry + 4);
        uint32_t key = *(uint32_t*)(entry + 6);
        if (itemId != 0 || slot != 0xffff || key != 0) {
            AppendMaterialsTraceLine("descriptor[%03d] item=%03u/0x%02x slot=%u/0x%04x key=%u/0x%08x",
                i, (unsigned)itemId, (unsigned)itemId, (unsigned)slot, (unsigned)slot, (unsigned)key, (unsigned)key);
        }
    }
}

static void RebuildMaterialsDescriptorCaches(uintptr_t base, const char* reason)
{
    if (!base) return;
    typedef void (__cdecl *FnRebuildMaterialsDescriptors)();
    FnRebuildMaterialsDescriptors rebuildDescriptors = (FnRebuildMaterialsDescriptors)(base + 0x18f210);
    AppendMaterialsTraceLine("descriptor-view rebuild reason=%s fn=%p", reason ? reason : "?", (void*)rebuildDescriptors);
    rebuildDescriptors();
}

static void RestoreMaterialsDescriptorView()
{
    RestoreMaterialsCatalogWindow();

    if (!g_materialsDescriptorSnapshotReady || !g_materialsDescriptorSnapshotTable) return;
    if (IsBadWritePtr((void*)g_materialsDescriptorSnapshotTable, sizeof(g_materialsDescriptorSnapshot))) return;

    DWORD oldProt = 0;
    if (VirtualProtect((void*)g_materialsDescriptorSnapshotTable, sizeof(g_materialsDescriptorSnapshot), PAGE_READWRITE, &oldProt)) {
        memcpy((void*)g_materialsDescriptorSnapshotTable, g_materialsDescriptorSnapshot, sizeof(g_materialsDescriptorSnapshot));
        VirtualProtect((void*)g_materialsDescriptorSnapshotTable, sizeof(g_materialsDescriptorSnapshot), oldProt, &oldProt);
        AppendMaterialsTraceLine("descriptor-view restore table=%p", (void*)g_materialsDescriptorSnapshotTable);
    }
    g_materialsDescriptorSnapshotReady = false;
    g_materialsDescriptorSnapshotTable = 0;
}

static bool WriteMaterialsDword(uintptr_t addr, uint32_t value)
{
    if (!addr || IsBadWritePtr((void*)addr, sizeof(uint32_t))) return false;
    DWORD oldProt = 0;
    if (!VirtualProtect((void*)addr, sizeof(uint32_t), PAGE_READWRITE, &oldProt)) return false;
    *(uint32_t*)addr = value;
    VirtualProtect((void*)addr, sizeof(uint32_t), oldProt, &oldProt);
    return true;
}

static bool PatchProjectByte(uintptr_t addr, uint8_t expected, uint8_t value, const char* label)
{
    if (!addr || IsBadReadPtr((void*)addr, sizeof(uint8_t))) return false;
    uint8_t before = *(uint8_t*)addr;
    if (before != expected && before != value) {
        AppendMaterialsTraceLine(
            "patch-byte skipped label=%s addr=%p before=%02x expected=%02x value=%02x",
            label ? label : "?",
            (void*)addr,
            (unsigned)before,
            (unsigned)expected,
            (unsigned)value);
        return false;
    }

    DWORD oldProt = 0;
    if (!VirtualProtect((void*)addr, sizeof(uint8_t), PAGE_EXECUTE_READWRITE, &oldProt)) return false;
    *(uint8_t*)addr = value;
    FlushInstructionCache(GetCurrentProcess(), (void*)addr, sizeof(uint8_t));
    VirtualProtect((void*)addr, sizeof(uint8_t), oldProt, &oldProt);

    AppendMaterialsTraceLine(
        "patch-byte label=%s addr=%p before=%02x after=%02x",
        label ? label : "?",
        (void*)addr,
        (unsigned)before,
        (unsigned)*(uint8_t*)addr);
    return true;
}

static bool PatchProjectDword(uintptr_t addr, uint32_t expected, uint32_t value, const char* label)
{
    if (!addr || IsBadReadPtr((void*)addr, sizeof(uint32_t))) return false;
    uint32_t before = *(uint32_t*)addr;
    if (before != expected && before != value) {
        AppendMaterialsTraceLine(
            "patch-dword skipped label=%s addr=%p before=%08x expected=%08x value=%08x",
            label ? label : "?",
            (void*)addr,
            (unsigned)before,
            (unsigned)expected,
            (unsigned)value);
        return false;
    }

    DWORD oldProt = 0;
    if (!VirtualProtect((void*)addr, sizeof(uint32_t), PAGE_EXECUTE_READWRITE, &oldProt)) return false;
    *(uint32_t*)addr = value;
    FlushInstructionCache(GetCurrentProcess(), (void*)addr, sizeof(uint32_t));
    VirtualProtect((void*)addr, sizeof(uint32_t), oldProt, &oldProt);

    AppendMaterialsTraceLine(
        "patch-dword label=%s addr=%p before=%08x after=%08x",
        label ? label : "?",
        (void*)addr,
        (unsigned)before,
        (unsigned)*(uint32_t*)addr);
    return true;
}

#ifdef SOE_MATERIALS_PAGE2_MATERIAL_RULES
struct SoEPage2MaterialRule {
    uint32_t code;
    uint16_t slot;
    uint8_t width;
    uint8_t height;
};

static constexpr uint32_t SoECode3(char a, char b, char c)
{
    return (uint32_t)(uint8_t)a |
        ((uint32_t)(uint8_t)b << 8) |
        ((uint32_t)(uint8_t)c << 16);
}

static constexpr uint32_t SoECode4(char a, char b, char c, char d)
{
    return (uint32_t)(uint8_t)a |
        ((uint32_t)(uint8_t)b << 8) |
        ((uint32_t)(uint8_t)c << 16) |
        ((uint32_t)(uint8_t)d << 24);
}

static const SoEPage2MaterialRule kSoEPage2MaterialRules[] = {
    // Row 0: primary SoE currency. Slot 2 is reserved for Eternal Orb once confirmed.
    { SoECode3('m', 'f', 'o'), 0u, 1u, 1u },      // Mythic Orb
    { SoECode3('d', 'v', 'o'), 1u, 1u, 1u },      // Divine Orb
    { SoECode3('e', 'x', 'o'), 3u, 1u, 1u },      // Exalted Orb
    { SoECode4('s', 'r', 'o', 'r'), 4u, 1u, 1u }, // Sacred Orb
    { SoECode3('o', 'o', 'e'), 5u, 1u, 1u },      // Orb of Extraction
    { SoECode4('h', 'f', 'c', 'r'), 6u, 1u, 1u }, // Crystallised Cindersoul

    // Row 2: terrors.
    { SoECode4('t', 'r', 'o', 'o'), 20u, 1u, 1u }, // Terror of Opulence
    { SoECode4('t', 'r', 'o', 'e'), 21u, 1u, 1u }, // Terror of Ethereal
    { SoECode4('t', 'r', 'o', 'r'), 22u, 1u, 1u }, // Terror of Resplendence
    { SoECode4('t', 'r', 'o', 'a'), 23u, 1u, 1u }, // Terror of Absolute

    // Row 4: glyphs.
    { SoECode4('s', 'c', 'n', 'm'), 40u, 1u, 1u }, // Glyph of Nemesis
    { SoECode4('s', 'c', 'c', 'n'), 41u, 1u, 1u }, // Glyph of Corruption
    { SoECode4('s', 'c', 'a', 's'), 42u, 1u, 1u }, // Glyph of Adversaries
    { SoECode4('s', 'c', 'm', 'o'), 43u, 1u, 1u }, // Glyph of Moo
    { SoECode4('s', 'c', 'h', 'r'), 44u, 1u, 1u }, // Glyph of Lineage

    // Row 6: chisels. Slot 62 is reserved for regular Cartographer's Chisel once confirmed.
    { SoECode4('c', 'c', 's', 'a'), 60u, 1u, 1u }, // Chisel of Avarice
    { SoECode4('c', 'c', 'p', 'r'), 61u, 1u, 1u }, // Chisel of Procurement
};

static void SoECodeToText(uint32_t code, char out[5])
{
    out[0] = (char)(code & 0xffu);
    out[1] = (char)((code >> 8) & 0xffu);
    out[2] = (char)((code >> 16) & 0xffu);
    out[3] = (char)((code >> 24) & 0xffu);
    out[4] = 0;
    for (int i = 0; i < 4; ++i) {
        if ((unsigned char)out[i] < 0x20u || (unsigned char)out[i] >= 0x7fu) out[i] = '.';
    }
}

static bool IsD2RuneCode(uint32_t code)
{
    if ((code & 0xffu) != (uint8_t)'r') return false;

    uint8_t tens = (uint8_t)((code >> 8) & 0xffu);
    uint8_t ones = (uint8_t)((code >> 16) & 0xffu);
    if (tens < (uint8_t)'0' || tens > (uint8_t)'9' ||
        ones < (uint8_t)'0' || ones > (uint8_t)'9') {
        return false;
    }

    uint32_t rune = (uint32_t)(tens - (uint8_t)'0') * 10u + (uint32_t)(ones - (uint8_t)'0');
    return rune >= 1u && rune <= 33u;
}

static bool FindSoEPage2MaterialRule(uint32_t code, SoEPage2MaterialRule* out)
{
    if (out) {
        out->code = 0;
        out->slot = 0xffffu;
        out->width = 0;
        out->height = 0;
    }

    for (size_t i = 0; i < sizeof(kSoEPage2MaterialRules) / sizeof(kSoEPage2MaterialRules[0]); ++i) {
        if (kSoEPage2MaterialRules[i].code == code) {
            if (out) *out = kSoEPage2MaterialRules[i];
            return true;
        }
    }

    // Still needs confirmed item codes: Eternal Orb, Jeweller's Prism,
    // regular Cartographer's Chisel, and the Sigil Fragment variants.
    return false;
}

static bool GetSoEPage2NativeBaseCoord(uintptr_t base, uintptr_t object, uint32_t* outX, uint32_t* outY, uint32_t* outPage)
{
    if (outX) *outX = 0;
    if (outY) *outY = 0;
    if (outPage) *outPage = 0xffffffffu;
    if (!base || !object || IsBadReadPtr((void*)object, 0x18)) return false;

    uintptr_t data = SafeReadPtrTrace(object + 0x14);
    uint32_t page = data ? SafeReadDwordTrace(data + 0x1b4) : 0xffffffffu;
#ifdef SOE_MATERIALS_PAGE2_STASH_CLIENT_INDEX
    const uint32_t page2Index = (uint32_t)SOE_MATERIALS_PAGE2_STASH_CLIENT_INDEX;
#else
    const uint32_t page2Index = 11u;
#endif
    if (page != page2Index) return false;

    uintptr_t layoutPtr = SafeReadPtrTrace(base + 0x4150fc);
    uintptr_t layout = SafeReadPtrTrace(layoutPtr);
    if (!layout || IsBadReadPtr((void*)layout, 0xc00)) return false;

    uintptr_t visibleGrid = layout + 0x960u;
    uintptr_t hiddenGrid = layout + 0xb40u;
    uint32_t hiddenCellW = (uint32_t)(SafeReadDwordTrace(hiddenGrid + 0x10) & 0xffu);
    uint32_t hiddenCellH = (uint32_t)(SafeReadDwordTrace(hiddenGrid + 0x11) & 0xffu);
    uint32_t visibleCellH = (uint32_t)(SafeReadDwordTrace(visibleGrid + 0x11) & 0xffu);
    if (!hiddenCellW || !hiddenCellH || visibleCellH < hiddenCellH) return false;

    uint32_t rows = visibleCellH / hiddenCellH;
    uint32_t pagesPerColumn = rows ? rows - 1u : 0u;
    if (!pagesPerColumn) return false;

    if (outX) *outX = (page / pagesPerColumn) * hiddenCellW;
    if (outY) *outY = (page % pagesPerColumn) * hiddenCellH;
    if (outPage) *outPage = page;
    return true;
}

static void ApplySoEPage2MaterialRules(uintptr_t base, uintptr_t object, const char* reason)
{
    if (!base || !object || IsBadReadPtr((void*)object, 0x64)) return;

    uint32_t pageBaseX = 0;
    uint32_t pageBaseY = 0;
    uint32_t objectPage = 0xffffffffu;
    if (!GetSoEPage2NativeBaseCoord(base, object, &pageBaseX, &pageBaseY, &objectPage)) return;

    uintptr_t container = SafeReadPtrTrace(object + 0x60);
    uintptr_t node = container ? SafeReadPtrTrace(container + 0x0c) : 0;
    if (!node || IsBadReadPtr((void*)node, 0x34)) return;

    typedef uintptr_t (__stdcall *FnNext)(uintptr_t);
    FnNext next = (FnNext)(base + 0x1d1570);
    int visited = 0;
    while (node && !IsBadReadPtr((void*)node, 0x34) && visited++ < 220) {
        uintptr_t nextNode = next ? next(node) : 0;
        uint32_t unitType = SafeReadDwordTrace(node + 0x00);
        uintptr_t path = SafeReadPtrTrace(node + 0x2c);
        uint32_t code = unitType == 4 ? ItemCodeForUnit(node) : 0;
        SoEPage2MaterialRule rule = {};
        bool allowed = FindSoEPage2MaterialRule(code, &rule);
        bool blockedRune = IsD2RuneCode(code);

        if (path && !IsBadReadPtr((void*)path, 0x18)) {
            uint32_t currentX = SafeReadDwordTrace(path + 0x0c);
            uint32_t currentY = SafeReadDwordTrace(path + 0x10);
            uint32_t targetX = pageBaseX + (uint32_t)(rule.slot % 10u);
            uint32_t targetY = pageBaseY + (uint32_t)(rule.slot / 10u);
            char codeText[5] = {};
            SoECodeToText(code, codeText);

            LONG logNo = InterlockedIncrement(&g_materialsPage2RulesLogCount);
            if (logNo <= 220) {
                AppendMaterialsTraceLine(
                    "page2-rules #%ld reason=%s object=%p page=%u node=%p type=%u code=%s/%08x allowed=%d blockedRune=%d slot=%u before=%u,%u target=%u,%u base=%u,%u",
                    (long)logNo,
                    reason ? reason : "?",
                    (void*)object,
                    (unsigned)objectPage,
                    (void*)node,
                    (unsigned)unitType,
                    codeText,
                    (unsigned)code,
                    allowed ? 1 : 0,
                    blockedRune ? 1 : 0,
                    allowed ? (unsigned)rule.slot : 0xffffu,
                    (unsigned)currentX,
                    (unsigned)currentY,
                    allowed ? (unsigned)targetX : 0xffffffffu,
                    allowed ? (unsigned)targetY : 0xffffffffu,
                    (unsigned)pageBaseX,
                    (unsigned)pageBaseY);
            }

#ifdef SOE_MATERIALS_PAGE2_SNAP_ALLOWED
            if (allowed && (currentX != targetX || currentY != targetY)) {
                bool okX = WriteMaterialsDword(path + 0x0c, targetX);
                bool okY = WriteMaterialsDword(path + 0x10, targetY);
                LONG writeNo = InterlockedIncrement(&g_materialsPage2RulesWriteCount);
                if (writeNo <= 120) {
                    AppendMaterialsTraceLine(
                        "page2-rules snap #%ld code=%s/%08x slot=%u from=%u,%u to=%u,%u ok=%d/%d path=%p",
                        (long)writeNo,
                        codeText,
                        (unsigned)code,
                        (unsigned)rule.slot,
                        (unsigned)currentX,
                        (unsigned)currentY,
                        (unsigned)targetX,
                        (unsigned)targetY,
                        okX ? 1 : 0,
                        okY ? 1 : 0,
                        (void*)path);
                }
            }
#endif
        }

        if (!nextNode || nextNode == node) break;
        node = nextNode;
    }
}

#ifdef SOE_MATERIALS_ITERATOR_PAGE2_BRIDGE_UNITS
static uintptr_t FindSoEPage2BridgeUnit(uintptr_t base, uintptr_t container, uint32_t ordinal)
{
    if (!base || !container || IsBadReadPtr((void*)container, 0x20)) return 0;

#ifdef SOE_MATERIALS_PAGE2_STASH_CLIENT_INDEX
    const uint32_t page2Index = (uint32_t)SOE_MATERIALS_PAGE2_STASH_CLIENT_INDEX;
#else
    const uint32_t page2Index = 11u;
#endif

    uintptr_t head = SafeReadPtrTrace(container + 0x0c);
    if (!head || IsBadReadPtr((void*)head, 0x34)) return 0;

    typedef uintptr_t (__stdcall *FnNext)(uintptr_t);
    FnNext next = (FnNext)(base + 0x1d1570);
    uint32_t seen = 0;

    for (uint32_t wantedSlot = 0; wantedSlot < 180u; ++wantedSlot) {
        uintptr_t node = head;
        int visited = 0;
        while (node && !IsBadReadPtr((void*)node, 0x34) && visited++ < 260) {
            uintptr_t nextNode = next ? next(node) : 0;
            bool match = false;
            uint32_t code = 0;
            if (SafeReadDwordTrace(node + 0x00) == 4) {
                uintptr_t data = SafeReadPtrTrace(node + 0x14);
                uint32_t page = data ? SafeReadDwordTrace(data + 0x1b4) : 0xffffffffu;
                if (page == page2Index) {
                    SoEPage2MaterialRule rule = {};
                    code = ItemCodeForUnit(node);
                    match = FindSoEPage2MaterialRule(code, &rule) && rule.slot == wantedSlot;
                }
            }

            if (match) {
                if (seen == ordinal) return node;
                ++seen;
            }

            if (!nextNode || nextNode == node) break;
            node = nextNode;
        }
    }

    return 0;
}
#endif
#endif

static bool IsSoEPage2ReservedMaterialSlot(uint32_t slot)
{
#ifdef SOE_MATERIALS_PAGE2_MATERIAL_RULES
    for (size_t i = 0; i < sizeof(kSoEPage2MaterialRules) / sizeof(kSoEPage2MaterialRules[0]); ++i) {
        if (kSoEPage2MaterialRules[i].slot == slot) return true;
    }
#else
    (void)slot;
#endif
    return false;
}

static void DrawSoEPage2MaterialMask(uintptr_t base, const char* reason)
{
#if defined(SOE_MATERIALS_PAGE2_MATERIAL_RULES) && defined(SOE_MATERIALS_PAGE2_MATERIAL_MASK)
    if (!base || !SafeReadDwordTrace(base + 0x2d4b24)) return;

    uintptr_t objectSlot = SafeReadPtrTrace(base + 0x410224);
    uintptr_t currentObject = SafeReadPtrTrace(objectSlot);
    uintptr_t objectData = currentObject ? SafeReadPtrTrace(currentObject + 0x14) : 0;
    uint32_t objectPage = objectData ? SafeReadDwordTrace(objectData + 0x1b4) : 0xffffffffu;
#ifdef SOE_MATERIALS_PAGE2_STASH_CLIENT_INDEX
    const uint32_t page2Index = (uint32_t)SOE_MATERIALS_PAGE2_STASH_CLIENT_INDEX;
#else
    const uint32_t page2Index = 11u;
#endif
    if (objectPage != page2Index) return;

    uintptr_t ptrBaseX = SafeReadPtrTrace(base + 0x4100a0);
    uintptr_t ptrBaseY1 = SafeReadPtrTrace(base + 0x4101ec);
    uintptr_t ptrBaseY2 = SafeReadPtrTrace(base + 0x4101a8);
    int baseX = (int)SafeReadDwordTrace(ptrBaseX);
    int baseY = (int)SafeReadDwordTrace(ptrBaseY1) + (int)SafeReadDwordTrace(ptrBaseY2);
    if (baseX <= 0 || baseY <= 0) {
        baseX = 214;
        baseY = 540;
    }

    SoEProjectDrawRectFn drawRect = (SoEProjectDrawRectFn)(base + 0x1e6b60);
    if (!drawRect) return;

    const int cell = 42;
    const int coverCols = 13;
    const int coverRows = 18;
    const int logicalCols = 10;
    int left = baseX - 180;
    int top = baseY - 443;

    for (int r = 0; r < coverRows; ++r) {
        for (int c = 0; c < coverCols; ++c) {
            uint32_t slot = (c < logicalCols) ? (uint32_t)(r * logicalCols + c) : 0xffffffffu;
            if (c < logicalCols && IsSoEPage2ReservedMaterialSlot(slot)) continue;

            int x = left + c * cell;
            int y = top + r * cell;
            drawRect(x + 1, y + 1, x + cell - 1, y + cell - 1, 0, 0);
        }
    }

    for (size_t i = 0; i < sizeof(kSoEPage2MaterialRules) / sizeof(kSoEPage2MaterialRules[0]); ++i) {
        uint32_t slot = kSoEPage2MaterialRules[i].slot;
        int c = (int)(slot % (uint32_t)logicalCols);
        int r = (int)(slot / (uint32_t)logicalCols);
        if (c < 0 || c >= logicalCols || r < 0 || r >= coverRows) continue;

        int x = left + c * cell;
        int y = top + r * cell;
        drawRect(x, y, x + cell, y + 1, 0, 2);
        drawRect(x, y, x + 1, y + cell, 0, 2);
        drawRect(x, y + cell - 1, x + cell, y + cell, 0, 0);
        drawRect(x + cell - 1, y, x + cell, y + cell, 0, 0);
        drawRect(x + 2, y + 2, x + cell - 2, y + 3, 0, 1);
        drawRect(x + 2, y + 2, x + 3, y + cell - 2, 0, 1);
    }

    static DWORD lastMaskLogTick = 0;
    DWORD now = GetTickCount();
    if (now - lastMaskLogTick > 1000) {
        lastMaskLogTick = now;
        AppendMaterialsTraceLine(
            "page2-material-mask draw reason=%s objectPage=%08x pos=%d,%d cell=%d cover=%dx%d logicalCols=%d rules=%u fn=%p",
            reason ? reason : "?",
            (unsigned)objectPage,
            left,
            top,
            cell,
            coverCols,
            coverRows,
            logicalCols,
            (unsigned)(sizeof(kSoEPage2MaterialRules) / sizeof(kSoEPage2MaterialRules[0])),
            (void*)drawRect);
    }
#else
    (void)base;
    (void)reason;
#endif
}

static void DumpNativeStashPageGeometry(uintptr_t base, const char* reason)
{
    if (!base) return;

    uintptr_t layoutPtr = SafeReadPtrTrace(base + 0x4150fc);
    uintptr_t layout = SafeReadPtrTrace(layoutPtr);
    uintptr_t visibleGrid = layout ? layout + 0x960u : 0;
    uintptr_t hiddenGrid = layout ? layout + 0xb40u : 0;

    uint32_t hiddenCellW = (uint32_t)(SafeReadDwordTrace(hiddenGrid + 0x10) & 0xffu);
    uint32_t hiddenCellH = (uint32_t)(SafeReadDwordTrace(hiddenGrid + 0x11) & 0xffu);
    uint32_t visibleCellW = (uint32_t)(SafeReadDwordTrace(visibleGrid + 0x10) & 0xffu);
    uint32_t visibleCellH = (uint32_t)(SafeReadDwordTrace(visibleGrid + 0x11) & 0xffu);
    uint32_t cols = hiddenCellW ? (visibleCellW / hiddenCellW) : 0;
    uint32_t rows = hiddenCellH ? (visibleCellH / hiddenCellH) : 0;
    uint32_t pagesPerColumn = rows ? rows - 1u : 0;
    uint32_t nativeCapacity = cols * pagesPerColumn;
    uint32_t maxNativeIndex = nativeCapacity ? nativeCapacity - 1u : 0;

    AppendMaterialsTraceLine(
        "native-stash-geometry reason=%s layoutPtr=%p layout=%p visible=%p hidden=%p hiddenCell=%u,%u visibleCell=%u,%u cols=%u rows=%u pagesPerColumn=%u capacity=%u maxIndex=%u",
        reason ? reason : "?",
        (void*)layoutPtr,
        (void*)layout,
        (void*)visibleGrid,
        (void*)hiddenGrid,
        (unsigned)hiddenCellW,
        (unsigned)hiddenCellH,
        (unsigned)visibleCellW,
        (unsigned)visibleCellH,
        (unsigned)cols,
        (unsigned)rows,
        (unsigned)pagesPerColumn,
        (unsigned)nativeCapacity,
        (unsigned)maxNativeIndex);

    for (uint32_t page = 0; page < 16; ++page) {
        bool valid = false;
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t roundTrip = 0xffffffffu;
        if (hiddenCellW && hiddenCellH && pagesPerColumn) {
            if (page != 0) {
                x = (page / pagesPerColumn) * hiddenCellW;
                y = (page % pagesPerColumn) * hiddenCellH;
            }
            uint32_t maxX = visibleCellW >= hiddenCellW ? visibleCellW - hiddenCellW : 0;
            uint32_t maxY = visibleCellH >= hiddenCellH ? visibleCellH - hiddenCellH : 0;
            valid = (x <= maxX) && (y <= maxY);
            roundTrip = (x / hiddenCellW) * pagesPerColumn + (y / hiddenCellH);
        }
        AppendMaterialsTraceLine(
            "native-stash-geometry-map reason=%s page=%u coord=%u,%u valid=%d roundTrip=%u",
            reason ? reason : "?",
            (unsigned)page,
            (unsigned)x,
            (unsigned)y,
            valid ? 1 : 0,
            (unsigned)roundTrip);
    }
}

static void ApplyMaterialsPage2KeyedDescriptorPage(uintptr_t base, uint32_t key, const char* reason)
{
    if (!base || key <= 1) return;

    uintptr_t table = SafeReadPtrTrace(base + 0x41076c);
    if (!table || IsBadWritePtr((void*)table, sizeof(g_materialsDescriptorSnapshot))) return;

    if (!g_materialsDescriptorSnapshotReady || g_materialsDescriptorSnapshotTable != table) {
        memcpy(g_materialsDescriptorSnapshot, (void*)table, sizeof(g_materialsDescriptorSnapshot));
        g_materialsDescriptorSnapshotTable = table;
        g_materialsDescriptorSnapshotReady = true;
    }

    uint32_t itemIds[120] = {};
    int itemCount = 0;
    for (uint32_t idx = 0; idx < 120 && itemCount < (int)(sizeof(itemIds) / sizeof(itemIds[0])); ++idx) {
        uintptr_t catalog = base + 0x2d4668 + (uintptr_t)idx * 10;
        uint32_t itemId = SafeReadDwordTrace(catalog + 0);
        uint32_t nameId = SafeReadDwordTrace(catalog + 4) & 0xffffu;
        if (itemId == 108 || nameId == 0 || itemId > 0x200) continue;

        bool seen = false;
        for (int i = 0; i < itemCount; ++i) {
            if (itemIds[i] == itemId) {
                seen = true;
                break;
            }
        }
        if (!seen) itemIds[itemCount++] = itemId;
    }

    DWORD oldProt = 0;
    if (!VirtualProtect((void*)table, sizeof(g_materialsDescriptorSnapshot), PAGE_READWRITE, &oldProt)) return;

    int cleared = 0;
    int assigned = 0;
    for (int i = 0; i < 216; ++i) {
        uintptr_t entry = table + (uintptr_t)i * 10;
        uint16_t slot = *(uint16_t*)(entry + 4);
        uint32_t entryKey = *(uint32_t*)(entry + 6);
        if (entryKey == key || slot == 0xffff) {
            *(uint32_t*)(entry + 0) = 0;
            *(uint16_t*)(entry + 4) = 0xffff;
            *(uint32_t*)(entry + 6) = 0;
            ++cleared;
        }
    }

    for (int i = 0; i < 216 && assigned < itemCount; ++i) {
        uintptr_t entry = table + (uintptr_t)i * 10;
        uint16_t slot = *(uint16_t*)(entry + 4);
        if (slot != 0xffff) continue;
        *(uint32_t*)(entry + 0) = itemIds[assigned];
        *(uint16_t*)(entry + 4) = (uint16_t)assigned;
        *(uint32_t*)(entry + 6) = key;
        ++assigned;
    }

    VirtualProtect((void*)table, sizeof(g_materialsDescriptorSnapshot), oldProt, &oldProt);
    RebuildMaterialsDescriptorCaches(base, reason ? reason : "page2-keyed-descriptor");
    AppendMaterialsTraceLine(
        "descriptor-view page2-keyed key=%u reason=%s table=%p catalogItems=%d assigned=%d cleared=%d",
        (unsigned)key,
        reason ? reason : "?",
        (void*)table,
        itemCount,
        assigned,
        cleared);
}

static void MaintainMaterialsPage2NativeKey(uintptr_t base, const char* reason)
{
#ifdef SOE_MATERIALS_PAGE2_FORCE_NATIVE_KEY
    if (!base) return;
    const uint32_t key = (uint32_t)SOE_MATERIALS_PAGE2_FORCE_NATIVE_KEY;
    if (key <= 1) return;

    uint32_t before = SafeReadDwordTrace(base + 0x30dca8);
    if (before == key) return;

    ApplyMaterialsPage2KeyedDescriptorPage(base, key, reason ? reason : "page2-maintain-native-key");
#ifdef SOE_MATERIALS_PAGE2_KEYED_CATALOG_WINDOW
    ApplyMaterialsPage2CatalogWindow(base, reason ? reason : "page2-maintain-native-key");
#endif
    WriteMaterialsDword(base + 0x30dca8, key);
    WriteMaterialsDword(base + 0x30dc84, 1);
    AppendMaterialsTraceLine(
        "page2-maintain-native-key reason=%s before=%08x after=%08x dirty=%08x",
        reason ? reason : "?",
        (unsigned)before,
        (unsigned)SafeReadDwordTrace(base + 0x30dca8),
        (unsigned)SafeReadDwordTrace(base + 0x30dc84));
#else
    (void)base;
    (void)reason;
#endif
}

static void RestoreMaterialsCatalogWindow()
{
    if (!g_materialsCatalogSnapshotReady) return;

    bool okCount = WriteMaterialsDword(g_materialsCatalogCountPtr, g_materialsCatalogSavedCount);
    bool okIndex = WriteMaterialsDword(g_materialsCatalogIndexPtr, g_materialsCatalogSavedIndex);
    bool okCurrent = WriteMaterialsDword(g_materialsCatalogCurrentPtr, g_materialsCatalogSavedCurrent);
    bool okHover = WriteMaterialsDword(g_materialsCatalogHoverPtr, g_materialsCatalogSavedHover);
    AppendMaterialsTraceLine(
        "catalog-window restore count=%u/%d index=%u/%d current=%u/%d hover=%u/%d ptrs=%p,%p,%p,%p",
        (unsigned)g_materialsCatalogSavedCount,
        okCount ? 1 : 0,
        (unsigned)g_materialsCatalogSavedIndex,
        okIndex ? 1 : 0,
        (unsigned)g_materialsCatalogSavedCurrent,
        okCurrent ? 1 : 0,
        (unsigned)g_materialsCatalogSavedHover,
        okHover ? 1 : 0,
        (void*)g_materialsCatalogCountPtr,
        (void*)g_materialsCatalogIndexPtr,
        (void*)g_materialsCatalogCurrentPtr,
        (void*)g_materialsCatalogHoverPtr);

    g_materialsCatalogSnapshotReady = false;
    g_materialsCatalogCountPtr = 0;
    g_materialsCatalogIndexPtr = 0;
    g_materialsCatalogCurrentPtr = 0;
    g_materialsCatalogHoverPtr = 0;
}

static void ApplyMaterialsPage2DrawGate(uintptr_t base, const char* reason)
{
#ifdef SOE_MATERIALS_FORCE_PAGE2_DRAW_GATE
#ifndef SOE_MATERIALS_FORCE_PAGE2_DRAW_GATE_VALUE
#define SOE_MATERIALS_FORCE_PAGE2_DRAW_GATE_VALUE 0
#endif
    if (!base) return;
    uintptr_t gateAddr = base + 0x40edd4;
    uint32_t before = SafeReadDwordTrace(gateAddr);
    bool ok = WriteMaterialsDword(gateAddr, (uint32_t)SOE_MATERIALS_FORCE_PAGE2_DRAW_GATE_VALUE);
    uint32_t after = SafeReadDwordTrace(gateAddr);
    AppendMaterialsTraceLine(
        "page2-draw-gate force reason=%s addr=%p before=%08x after=%08x ok=%d",
        reason ? reason : "?",
        (void*)gateAddr,
        before,
        after,
        ok ? 1 : 0);
#else
    (void)base;
    (void)reason;
#endif
}

#ifdef SOE_MATERIALS_PATCH_GATE_SKIP
static void InstallMaterialsGateSkipPatch(uintptr_t base)
{
    if (!base || g_materialsGateSkipHooked) return;

    uintptr_t scanStart = base + 0x18be80;
    uintptr_t scanEnd = base + 0x18bea0;
    if (IsBadReadPtr((void*)scanStart, (UINT)(scanEnd - scanStart + 4))) {
        AppendMaterialsTraceLine("page2-gate-skip patch failed unreadable start=%p", (void*)scanStart);
        return;
    }

    uintptr_t patchAddr = 0;
    for (uintptr_t p = scanStart; p + 3 < scanEnd; ++p) {
        uint8_t* b = (uint8_t*)p;
        if (b[0] == 0x85 && b[1] == 0xDB && b[2] == 0x75) {
            patchAddr = p + 2;
            break;
        }
    }

    AppendMaterialsTraceLine(
        "page2-gate-skip scan start=%p bytes=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x patch=%p",
        (void*)scanStart,
        (unsigned)((uint8_t*)scanStart)[0],
        (unsigned)((uint8_t*)scanStart)[1],
        (unsigned)((uint8_t*)scanStart)[2],
        (unsigned)((uint8_t*)scanStart)[3],
        (unsigned)((uint8_t*)scanStart)[4],
        (unsigned)((uint8_t*)scanStart)[5],
        (unsigned)((uint8_t*)scanStart)[6],
        (unsigned)((uint8_t*)scanStart)[7],
        (unsigned)((uint8_t*)scanStart)[8],
        (unsigned)((uint8_t*)scanStart)[9],
        (unsigned)((uint8_t*)scanStart)[10],
        (unsigned)((uint8_t*)scanStart)[11],
        (void*)patchAddr);

    if (!patchAddr) {
        AppendMaterialsTraceLine("page2-gate-skip patch failed pattern-missing");
        return;
    }

    DWORD oldProt = 0;
    if (!VirtualProtect((void*)patchAddr, sizeof(g_materialsGateSkipOriginal), PAGE_EXECUTE_READWRITE, &oldProt)) {
        AppendMaterialsTraceLine("page2-gate-skip patch failed protect addr=%p", (void*)patchAddr);
        return;
    }

    memcpy(g_materialsGateSkipOriginal, (void*)patchAddr, sizeof(g_materialsGateSkipOriginal));
    ((uint8_t*)patchAddr)[0] = 0x90;
    ((uint8_t*)patchAddr)[1] = 0x90;
    FlushInstructionCache(GetCurrentProcess(), (void*)patchAddr, sizeof(g_materialsGateSkipOriginal));
    VirtualProtect((void*)patchAddr, sizeof(g_materialsGateSkipOriginal), oldProt, &oldProt);

    g_materialsGateSkipPatchAddr = patchAddr;
    g_materialsGateSkipHooked = true;
    AppendMaterialsTraceLine(
        "page2-gate-skip patched addr=%p original=%02x %02x now=%02x %02x",
        (void*)patchAddr,
        (unsigned)g_materialsGateSkipOriginal[0],
        (unsigned)g_materialsGateSkipOriginal[1],
        (unsigned)((uint8_t*)patchAddr)[0],
        (unsigned)((uint8_t*)patchAddr)[1]);
}

static void RestoreMaterialsGateSkipPatch()
{
    if (!g_materialsGateSkipHooked || !g_materialsGateSkipPatchAddr) return;

    DWORD oldProt = 0;
    if (VirtualProtect((void*)g_materialsGateSkipPatchAddr, sizeof(g_materialsGateSkipOriginal), PAGE_EXECUTE_READWRITE, &oldProt)) {
        memcpy((void*)g_materialsGateSkipPatchAddr, g_materialsGateSkipOriginal, sizeof(g_materialsGateSkipOriginal));
        FlushInstructionCache(GetCurrentProcess(), (void*)g_materialsGateSkipPatchAddr, sizeof(g_materialsGateSkipOriginal));
        VirtualProtect((void*)g_materialsGateSkipPatchAddr, sizeof(g_materialsGateSkipOriginal), oldProt, &oldProt);
        AppendMaterialsTraceLine("page2-gate-skip restored addr=%p", (void*)g_materialsGateSkipPatchAddr);
    }

    g_materialsGateSkipPatchAddr = 0;
    memset(g_materialsGateSkipOriginal, 0, sizeof(g_materialsGateSkipOriginal));
    g_materialsGateSkipHooked = false;
}
#endif

static void ApplyMaterialsPage2CatalogWindow(uintptr_t base, const char* reason)
{
    if (!base) return;

    uintptr_t countPtr = SafeReadPtrTrace(base + 0x410314);
    uintptr_t indexBasePtr = SafeReadPtrTrace(base + 0x4102f8);
    uintptr_t currentPtr = SafeReadPtrTrace(base + 0x41010c);
    uintptr_t hoverPtr = SafeReadPtrTrace(base + 0x41020c);
    if (!countPtr || !indexBasePtr || !currentPtr || !hoverPtr) {
        AppendMaterialsTraceLine(
            "catalog-window page2 missing-ptrs reason=%s count=%p index=%p current=%p hover=%p",
            reason ? reason : "?",
            (void*)countPtr,
            (void*)indexBasePtr,
            (void*)currentPtr,
            (void*)hoverPtr);
        return;
    }

    if (!g_materialsCatalogSnapshotReady ||
        g_materialsCatalogCountPtr != countPtr ||
        g_materialsCatalogIndexPtr != indexBasePtr ||
        g_materialsCatalogCurrentPtr != currentPtr ||
        g_materialsCatalogHoverPtr != hoverPtr) {
        g_materialsCatalogCountPtr = countPtr;
        g_materialsCatalogIndexPtr = indexBasePtr;
        g_materialsCatalogCurrentPtr = currentPtr;
        g_materialsCatalogHoverPtr = hoverPtr;
        g_materialsCatalogSavedCount = SafeReadDwordTrace(countPtr);
        g_materialsCatalogSavedIndex = SafeReadDwordTrace(indexBasePtr);
        g_materialsCatalogSavedCurrent = SafeReadDwordTrace(currentPtr);
        g_materialsCatalogSavedHover = SafeReadDwordTrace(hoverPtr);
        g_materialsCatalogSnapshotReady = true;
        AppendMaterialsTraceLine(
            "catalog-window snapshot reason=%s count=%u index=%u current=%u hover=%u ptrs=%p,%p,%p,%p",
            reason ? reason : "?",
            (unsigned)g_materialsCatalogSavedCount,
            (unsigned)g_materialsCatalogSavedIndex,
            (unsigned)g_materialsCatalogSavedCurrent,
            (unsigned)g_materialsCatalogSavedHover,
            (void*)countPtr,
            (void*)indexBasePtr,
            (void*)currentPtr,
            (void*)hoverPtr);
    }

#ifdef SOE_MATERIALS_PAGE2_MIRROR_PAGE1_CATALOG
    // Diagnostic probe: deliberately mirror the populated first page on page 2.
    // If this draws, the native page switch is healthy and the blank page is
    // caused by the page-2 material ids having no backing counts yet.
    const uint32_t page2IndexBase = 0;
    const uint32_t page2Count = 15;
#else
    // Native material page renderer shows up to 15 catalog rows from indexBase.
    // Index 67 is the first dense SoE-only run we found in the hidden catalog.
    const uint32_t page2IndexBase = 67;
    const uint32_t page2Count = 15;
#endif
    bool okCount = WriteMaterialsDword(countPtr, page2Count);
    bool okIndex = WriteMaterialsDword(indexBasePtr, page2IndexBase);
    bool okCurrent = WriteMaterialsDword(currentPtr, page2IndexBase);
    bool okHover = WriteMaterialsDword(hoverPtr, 0xFFFFFFFFu);

    // Mark materials UI dirty so ProjectDiablo redraws with the swapped native window.
    WriteMaterialsDword(base + 0x30dc84, 1);

    static LONG applyLogCount = 0;
    LONG n = InterlockedIncrement(&applyLogCount);
    if (n <= 40 || (n % 120) == 0) {
        AppendMaterialsTraceLine(
            "catalog-window page2 apply #%ld reason=%s count=%u/%d index=%u/%d current=%u/%d hover=ffffffff/%d dirty=%08x",
            n,
            reason ? reason : "?",
            (unsigned)page2Count,
            okCount ? 1 : 0,
            (unsigned)page2IndexBase,
            okIndex ? 1 : 0,
            (unsigned)page2IndexBase,
            okCurrent ? 1 : 0,
            okHover ? 1 : 0,
            SafeReadDwordTrace(base + 0x30dc84));
    }
}

static void ApplyMaterialsPage2DescriptorView(uintptr_t base)
{
    if (!base) return;
    uintptr_t table = SafeReadPtrTrace(base + 0x41076c);
    if (!table || IsBadWritePtr((void*)table, sizeof(g_materialsDescriptorSnapshot))) return;

    if (!g_materialsDescriptorSnapshotReady || g_materialsDescriptorSnapshotTable != table) {
        memcpy(g_materialsDescriptorSnapshot, (void*)table, sizeof(g_materialsDescriptorSnapshot));
        g_materialsDescriptorSnapshotTable = table;
        g_materialsDescriptorSnapshotReady = true;
    }

#ifdef SOE_MATERIALS_PAGE2_CLONE_PAGE1_SLOT_MAP
    {
    struct CloneSlotEntry {
        uint32_t itemId;
        uint16_t slot;
    };
    CloneSlotEntry entries[96] = {};
    int entryCount = 0;
    for (int i = 0; i < 216 && entryCount < (int)(sizeof(entries) / sizeof(entries[0])); ++i) {
        uintptr_t entry = table + (uintptr_t)i * 10;
        uint32_t itemId = *(uint32_t*)(entry + 0);
        uint16_t slot = *(uint16_t*)(entry + 4);
        uint32_t key = *(uint32_t*)(entry + 6);
        if (key == 1 && slot != 0xffff && slot < 0x0200 && itemId != 108) {
            entries[entryCount].itemId = itemId;
            entries[entryCount].slot = slot;
            ++entryCount;
        }
    }

    DWORD oldProt = 0;
    if (!VirtualProtect((void*)table, sizeof(g_materialsDescriptorSnapshot), PAGE_READWRITE, &oldProt)) return;

    int assigned = 0;
    int hidden = 0;
    for (int i = 0; i < 216; ++i) {
        uintptr_t entry = table + (uintptr_t)i * 10;
        uint16_t* slot = (uint16_t*)(entry + 4);
        uint32_t key = *(uint32_t*)(entry + 6);
        if (key == 1) {
            if (assigned < entryCount) {
                uint16_t newSlot = entries[assigned].slot;
#ifdef SOE_MATERIALS_PAGE2_ROTATE_CLONE_SLOTS
                newSlot = entries[(assigned + 5) % entryCount].slot;
#endif
                *slot = newSlot;
                ++assigned;
            } else {
                if (*slot != 0xffff) ++hidden;
                *slot = 0xffff;
            }
        } else if (key == 0) {
            if (*slot != 0xffff) ++hidden;
            *slot = 0xffff;
        }
    }

    VirtualProtect((void*)table, sizeof(g_materialsDescriptorSnapshot), oldProt, &oldProt);
    AppendMaterialsTraceLine(
        "descriptor-view page2-clone-page1-slot-map table=%p entries=%d assigned=%d hidden=%d rotated=%d",
        (void*)table,
        entryCount,
        assigned,
        hidden,
#ifdef SOE_MATERIALS_PAGE2_ROTATE_CLONE_SLOTS
        1
#else
        0
#endif
    );
    return;
    }
#endif

#ifdef SOE_MATERIALS_PAGE2_MIRROR_PAGE1_CATALOG
#if !defined(SOE_MATERIALS_PAGE2_DUPLICATE_MATERIAL_IDS)
    AppendMaterialsTraceLine("descriptor-view page2-mirror-first-page table=%p", (void*)table);
    return;
#else
    AppendMaterialsTraceLine("descriptor-view page2-mirror-first-page editable table=%p", (void*)table);
#endif
#endif

    DWORD oldProt = 0;
    if (!VirtualProtect((void*)table, sizeof(g_materialsDescriptorSnapshot), PAGE_READWRITE, &oldProt)) return;

    int hidden = 0;
    int assigned = 0;
    uint16_t slots[96] = {};
    int slotCount = 0;

    for (int i = 0; i < 216; ++i) {
        uintptr_t entry = table + (uintptr_t)i * 10;
        uint16_t slot = *(uint16_t*)(entry + 4);
        uint32_t key = *(uint32_t*)(entry + 6);
        if (key == 1 && slot != 0xffff && slot < 0x0200 && slotCount < (int)(sizeof(slots) / sizeof(slots[0]))) {
            slots[slotCount++] = slot;
        }
    }

#ifdef SOE_MATERIALS_PAGE2_DUPLICATE_MATERIAL_IDS
    int page2Slot = 0;
    for (int i = 0; i < 216; ++i) {
        uintptr_t entry = table + (uintptr_t)i * 10;
        uint16_t* slot = (uint16_t*)(entry + 4);
        uint32_t key = *(uint32_t*)(entry + 6);
        if (key == 1) {
            if (page2Slot < 120) {
                *slot = (uint16_t)page2Slot++;
                ++assigned;
            } else {
                if (*slot != 0xffff) ++hidden;
                *slot = 0xffff;
            }
        } else if (key == 0) {
            if (*slot != 0xffff) ++hidden;
            *slot = 0xffff;
        }
    }

    VirtualProtect((void*)table, sizeof(g_materialsDescriptorSnapshot), oldProt, &oldProt);
    AppendMaterialsTraceLine("descriptor-view page2-duplicate-ids table=%p hidden=%d assigned=%d", (void*)table, hidden, assigned);
    return;
#endif

    for (int i = 0; i < 216; ++i) {
        uintptr_t entry = table + (uintptr_t)i * 10;
        uint32_t itemId = *(uint32_t*)(entry + 0);
        uint16_t* slot = (uint16_t*)(entry + 4);
        uint32_t key = *(uint32_t*)(entry + 6);
        if (key == 1) {
            if (itemId >= 60 && itemId <= 107 && assigned < slotCount) {
                *slot = slots[assigned++];
            } else {
                if (*slot != 0xffff) ++hidden;
                *slot = 0xffff;
            }
        } else if (key == 0) {
            if (*slot != 0xffff) ++hidden;
            *slot = 0xffff;
        }
    }

    VirtualProtect((void*)table, sizeof(g_materialsDescriptorSnapshot), oldProt, &oldProt);
    AppendMaterialsTraceLine("descriptor-view page2-key1 table=%p hidden=%d assigned=%d slotCount=%d", (void*)table, hidden, assigned, slotCount);
}

static void SetMaterialsDescriptorPage2(uintptr_t base, bool active, const char* reason)
{
    if (!base) return;
    if (active) {
#ifdef SOE_MATERIALS_PAGE2_SET_MATERIAL_KEY
        if (!g_materialsSavedMaterialKeyReady) {
            g_materialsSavedMaterialKey = SafeReadDwordTrace(base + 0x30dca8);
            g_materialsSavedMaterialKeyReady = true;
        }
        WriteMaterialsDword(base + 0x30dca8, 1);
        AppendMaterialsTraceLine(
            "material-key page2 apply saved=%08x now=%08x reason=%s",
            (unsigned)g_materialsSavedMaterialKey,
            (unsigned)SafeReadDwordTrace(base + 0x30dca8),
            reason ? reason : "?");
#endif
        ApplyMaterialsPage2DescriptorView(base);
        ApplyMaterialsPage2CatalogWindow(base, reason ? reason : "page2");
        ApplyMaterialsPage2DrawGate(base, reason ? reason : "page2");
        InterlockedExchange(&g_materialsPage2Active, 1);
        InterlockedExchange(&g_materialsPendingExternalEntry, 0);
        InterlockedExchange(&g_materialsExternalArmed, 0);
        InterlockedExchange(&g_materialsSawExternalPage, 0);
        InterlockedExchange(&g_materialsLeftMaterials, 0);
        InterlockedExchange(&g_materialsRealEntryClickSeen, 0);
        InterlockedExchange(&g_materialsForceRealEntry, 0);
        InterlockedExchange(&g_materialsForceRealDraws, 0);
        InterlockedExchange(&g_materialsIteratorPage2LogCount, 0);
        InterlockedExchange(&g_materialsIteratorPage2BlankCount, 0);
        InterlockedExchange(&g_materialsIteratorStackLogCount, 0);
        InterlockedExchange(&g_materialsIteratorBridgeLogCount, 0);
        InterlockedExchange(&g_materialsD2ClientDrawEntryLogCount, 0);
        InterlockedExchange(&g_materialsD2ClientDrawEntryPage2LogCount, 0);
        RebuildMaterialsDescriptorCaches(base, reason ? reason : "page2");
    } else {
        InterlockedExchange(&g_materialsPage2Active, 0);
        InterlockedExchange(&g_materialsPage2RenderContainer, 0);
        HideSoEPage2Overlay();
        RestoreMaterialsDescriptorView();
#ifdef SOE_MATERIALS_PAGE2_SET_MATERIAL_KEY
        if (g_materialsSavedMaterialKeyReady) {
#ifdef SOE_MATERIALS_KEEP_PAGE2_KEY_AFTER_UI_LEFT
            if (reason && strcmp(reason, "ui-left-materials") == 0) {
                AppendMaterialsTraceLine(
                    "material-key keep-after-ui-left saved=%08x now=%08x reason=%s",
                    (unsigned)g_materialsSavedMaterialKey,
                    (unsigned)SafeReadDwordTrace(base + 0x30dca8),
                    reason);
            } else {
#endif
            WriteMaterialsDword(base + 0x30dca8, g_materialsSavedMaterialKey);
            AppendMaterialsTraceLine(
                "material-key restore saved=%08x now=%08x reason=%s",
                (unsigned)g_materialsSavedMaterialKey,
                (unsigned)SafeReadDwordTrace(base + 0x30dca8),
                reason ? reason : "?");
            g_materialsSavedMaterialKeyReady = false;
#ifdef SOE_MATERIALS_KEEP_PAGE2_KEY_AFTER_UI_LEFT
            }
#endif
        }
#endif
        RebuildMaterialsDescriptorCaches(base, reason ? reason : "restore");
    }

    char summary[256] = {};
    SummarizeMaterialsDescriptorTable(base, summary, sizeof(summary));
    AppendMaterialsTraceLine(
        "descriptor-page active=%d reason=%s summary=%s",
        active ? 1 : 0,
        reason ? reason : "?",
        summary);
}

static void SummarizeMaterialsDescriptorTable(uintptr_t base, char* out, size_t outSize)
{
    if (!out || outSize == 0) return;
    out[0] = 0;
    uintptr_t table = base ? SafeReadPtrTrace(base + 0x41076c) : 0;
    if (!table || IsBadReadPtr((void*)table, 0xd8 * 10)) {
        wsprintfA(out, "table=%p unreadable", (void*)table);
        return;
    }

    int visible0 = 0;
    int visible1 = 0;
    int visibleOther = 0;
    char first0[96] = {};
    char first1[96] = {};
    for (int i = 0; i < 0xd8; ++i) {
        uintptr_t entry = table + (uintptr_t)i * 10;
        uint32_t itemId = *(uint32_t*)(entry + 0);
        uint16_t slot = *(uint16_t*)(entry + 4);
        uint32_t key = *(uint32_t*)(entry + 6);
        if (slot == 0xffff) continue;

        if (key == 0) {
            ++visible0;
            if (lstrlenA(first0) < 72) {
                char chunk[24] = {};
                wsprintfA(chunk, "%s%u:%u", first0[0] ? "," : "", (unsigned)itemId, (unsigned)slot);
                lstrcatA(first0, chunk);
            }
        } else if (key == 1) {
            ++visible1;
            if (lstrlenA(first1) < 72) {
                char chunk[24] = {};
                wsprintfA(chunk, "%s%u:%u", first1[0] ? "," : "", (unsigned)itemId, (unsigned)slot);
                lstrcatA(first1, chunk);
            }
        } else {
            ++visibleOther;
        }
    }

    wsprintfA(out,
        "table=%p visible(key0=%d [%s] key1=%d [%s] other=%d)",
        (void*)table,
        visible0,
        first0[0] ? first0 : "-",
        visible1,
        first1[0] ? first1 : "-",
        visibleOther);
}

extern "C" void __stdcall LogMaterialsNativeCall(
    uint32_t rva,
    uintptr_t eax,
    uintptr_t ecx,
    uintptr_t edx,
    uintptr_t ebx,
    uintptr_t savedEspAfterFlags)
{
    LONG n = InterlockedIncrement(&g_materialsNativeLogCount);
    if (n > 420) return;

    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    uintptr_t callStack = savedEspAfterFlags + 4;
    uintptr_t retAddr = SafeReadPtrTrace(callStack);
    uintptr_t retRva = base && retAddr >= base ? retAddr - base : retAddr;
    uint32_t a1 = SafeReadDwordTrace(callStack + 4);
    uint32_t a2 = SafeReadDwordTrace(callStack + 8);
    uint32_t a3 = SafeReadDwordTrace(callStack + 12);
    uintptr_t ptr410998 = base ? SafeReadPtrTrace(base + 0x410998) : 0;
    uintptr_t ptr410688 = base ? SafeReadPtrTrace(base + 0x410688) : 0;
    uintptr_t ptr410a74 = base ? SafeReadPtrTrace(base + 0x410a74) : 0;
    char summary[256] = {};
    SummarizeMaterialsDescriptorTable(base, summary, sizeof(summary));

    AppendMaterialsTraceLine(
        "native rva=%08x ret=%p retRva=%08x eax=%p ecx=%p edx=%p ebx=%p a1=%08x a2=%08x a3=%08x stash=%08x mat=%08x panel=%08x extra=%08x active=%08x summary=%s",
        (unsigned)rva,
        (void*)retAddr,
        (unsigned)retRva,
        (void*)eax,
        (void*)ecx,
        (void*)edx,
        (void*)ebx,
        (unsigned)a1,
        (unsigned)a2,
        (unsigned)a3,
        base ? SafeReadDwordTrace(base + 0x2d4b24) : 0,
        base ? SafeReadDwordTrace(base + 0x30dca8) : 0,
        SafeReadDwordTrace(ptr410688),
        SafeReadDwordTrace(ptr410a74),
        SafeReadDwordTrace(ptr410998),
        summary);
}

static void TryNativeMaterialsPageSwitch(uint32_t page, const char* reason)
{
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    if (!base) return;

    if (InterlockedCompareExchange(&g_materialsNativeSwitchInProgress, 1, 0) != 0) {
        AppendMaterialsTraceLine("native-switch skipped reentrant page=%u reason=%s", (unsigned)page, reason ? reason : "?");
        return;
    }

    InterlockedExchange(&g_materialsPage2Active, 0);
    InterlockedExchange(&g_materialsPage2RenderContainer, 0);
    InterlockedExchange(&g_materialsPendingExternalEntry, 0);
    InterlockedExchange(&g_materialsExternalArmed, 0);
    InterlockedExchange(&g_materialsRealEntryClickSeen, 0);
    InterlockedExchange(&g_materialsSuppressNextSelfToggle, 0);
    InterlockedExchange(&g_materialsSuppressSelfToggleUntilTick, 0);
    InterlockedExchange(&g_materialsForceRealEntry, 0);
    InterlockedExchange(&g_materialsForceRealDraws, 0);
    InterlockedExchange(&g_materialsDescriptorLookupTraceCount, 0);
    InterlockedExchange(&g_materialsGridRenderLogCount, 0);
    InterlockedExchange(&g_materialsPageDrawLogCount, 0);
    InterlockedExchange(&g_materialsObjectDrawLogCount, 0);
    InterlockedExchange(&g_materialsContentArgDumpedPage2, 0);
    InterlockedExchange(&g_materialsContentArgDumpedNormal, 0);
    InterlockedExchange(&g_materialsIteratorLogCount, 0);
    InterlockedExchange(&g_materialsIteratorPage2LogCount, 0);
    InterlockedExchange(&g_materialsIteratorPage2BlankCount, 0);
    InterlockedExchange(&g_materialsIteratorStackLogCount, 0);
    InterlockedExchange(&g_materialsIteratorBridgeLogCount, 0);
    InterlockedExchange(&g_materialsD2ClientDrawEntryLogCount, 0);
    InterlockedExchange(&g_materialsD2ClientDrawEntryPage2LogCount, 0);
    HideSoEPage2Overlay();
    RestoreMaterialsDescriptorView();

#ifdef SOE_MATERIALS_CATALOG_PAGE
    if (page) {
        InterlockedExchange(&g_materialsPage2Active, 1);
        InterlockedExchange(&g_materialsPendingExternalEntry, 0);
        InterlockedExchange(&g_materialsExternalArmed, 0);
        InterlockedExchange(&g_materialsSawExternalPage, 0);
        InterlockedExchange(&g_materialsLeftMaterials, 0);
        InterlockedExchange(&g_materialsRealEntryClickSeen, 0);
        InterlockedExchange(&g_materialsForceRealEntry, 0);
        InterlockedExchange(&g_materialsForceRealDraws, 0);
        ApplyMaterialsPage2CatalogWindow(base, reason ? reason : "native-catalog-page");
        AppendMaterialsTraceLine(
            "native-switch catalog-window page=%u reason=%s",
            (unsigned)page,
            reason ? reason : "?");
        InterlockedExchange(&g_materialsNativeSwitchInProgress, 0);
        return;
    }
#endif

    uintptr_t page2Object = 0;
    uintptr_t page2Container = 0;
    if (page) {
        uintptr_t objectSlot = SafeReadPtrTrace(base + 0x410224);
        page2Object = SafeReadPtrTrace(objectSlot);
        page2Container = page2Object ? SafeReadPtrTrace(page2Object + 0x60) : 0;
        if (page2Container && !IsBadReadPtr((void*)page2Container, 0x20)) {
            InterlockedExchange(&g_materialsPage2RenderContainer, (LONG)page2Container);
        }
        AppendMaterialsTraceLine(
            "native-switch capture-render-container page=%u objectSlot=%p object=%p container=%p first=%08x",
            (unsigned)page,
            (void*)objectSlot,
            (void*)page2Object,
            (void*)page2Container,
            SafeReadDwordTrace(page2Container));
    }

    if (false && page) {
        ApplyMaterialsPage2DescriptorView(base);
        InterlockedExchange(&g_materialsPage2Active, 1);
        typedef void (__cdecl *FnRebuildMaterialsDescriptors)();
        FnRebuildMaterialsDescriptors rebuildDescriptors = (FnRebuildMaterialsDescriptors)(base + 0x18f210);
        rebuildDescriptors();
        char summary[256] = {};
        SummarizeMaterialsDescriptorTable(base, summary, sizeof(summary));
        AppendMaterialsTraceLine("native-switch descriptor-applied page=%u summary=%s", (unsigned)page, summary);
    }
    if (page) {
        InterlockedExchange(&g_materialsPage2Active, 1);
        char summary[256] = {};
        SummarizeMaterialsDescriptorTable(base, summary, sizeof(summary));
        AppendMaterialsTraceLine("native-switch descriptor-unchanged page=%u summary=%s", (unsigned)page, summary);
    }

    uintptr_t flagAddr = base + 0x30dca8;
    uint32_t before = SafeReadDwordTrace(flagAddr);
    uintptr_t widget = g_materialsLastUiWidget;
    if (!widget || IsBadWritePtr((void*)widget, 0x20)) {
        widget = 0;
    }

    if (widget) {
        typedef void (__stdcall *FnNativeMaterialPage)(uintptr_t);
        FnNativeMaterialPage nativePage = (FnNativeMaterialPage)(base + (page ? 0x192e20 : 0x192e80));
        AppendMaterialsTraceLine(
            "native-switch begin page=%u reason=%s flagBefore=%08x widget=%p nativePage=%p",
            (unsigned)page,
            reason ? reason : "?",
            before,
            (void*)widget,
            (void*)nativePage);
        nativePage(widget);
        AppendMaterialsTraceLine(
            "native-switch done-native page=%u flagAfter=%08x widget18=%08x widget1c=%08x",
            (unsigned)page,
            SafeReadDwordTrace(flagAddr),
            SafeReadDwordTrace(widget + 0x18),
            SafeReadDwordTrace(widget + 0x1c));
        InterlockedExchange(&g_materialsNativeSwitchInProgress, 0);
        return;
    }

    DWORD oldProt = 0;
    bool wrote = false;
    if (VirtualProtect((void*)flagAddr, sizeof(uint32_t), PAGE_READWRITE, &oldProt)) {
        *(uint32_t*)flagAddr = page ? 1u : 0u;
        VirtualProtect((void*)flagAddr, sizeof(uint32_t), oldProt, &oldProt);
        wrote = true;
    }

    typedef void (__stdcall *FnMaterialsTransition)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    FnMaterialsTransition transition = (FnMaterialsTransition)(base + 0x23f7d0);
    AppendMaterialsTraceLine(
        "native-switch begin-fallback page=%u reason=%s flagBefore=%08x wrote=%d transition=%p widget=missing",
        (unsigned)page,
        reason ? reason : "?",
        before,
        wrote ? 1 : 0,
        (void*)transition);

    transition(1, 0, 0, 0, 0);
    AppendMaterialsTraceLine(
        "native-switch done page=%u flagAfter=%08x",
        (unsigned)page,
        SafeReadDwordTrace(flagAddr));

    InterlockedExchange(&g_materialsNativeSwitchInProgress, 0);
}

static void TryNativeStashPageStep(const char* reason)
{
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    if (!base) return;

    uintptr_t widget = g_materialsLastUiWidget;
    if (!widget || IsBadWritePtr((void*)widget, 0x20)) {
        widget = 0;
    }

    uintptr_t pageIndexPtr = SafeReadPtrTrace(base + 0x410a8c);
    uintptr_t pageListPtr = SafeReadPtrTrace(base + 0x410958);
    uint32_t beforeIndex = SafeReadDwordTrace(pageIndexPtr);
    uintptr_t beforePage = 0;
    if (pageListPtr) {
        uintptr_t pageList = SafeReadPtrTrace(pageListPtr);
        beforePage = pageList ? pageList + (uintptr_t)beforeIndex * 0x550u : 0;
    }

    typedef void (__stdcall *FnStashNextPage)(uintptr_t);
    FnStashNextPage nextPage = (FnStashNextPage)(base + 0x193730);
    AppendMaterialsTraceLine(
        "native-stash-step begin reason=%s widget=%p handler=%p indexPtr=%p before=%u pageListPtr=%p beforePage=%p page00=%08x page120=%08x page124=%08x",
        reason ? reason : "?",
        (void*)widget,
        (void*)nextPage,
        (void*)pageIndexPtr,
        (unsigned)beforeIndex,
        (void*)pageListPtr,
        (void*)beforePage,
        SafeReadDwordTrace(beforePage + 0x00),
        SafeReadDwordTrace(beforePage + 0x120),
        SafeReadDwordTrace(beforePage + 0x124));

    if (widget) {
        nextPage(widget);
    }

    uint32_t afterIndex = SafeReadDwordTrace(pageIndexPtr);
    uintptr_t afterPage = 0;
    if (pageListPtr) {
        uintptr_t pageList = SafeReadPtrTrace(pageListPtr);
        afterPage = pageList ? pageList + (uintptr_t)afterIndex * 0x550u : 0;
    }
    AppendMaterialsTraceLine(
        "native-stash-step done reason=%s after=%u afterPage=%p page00=%08x page120=%08x page124=%08x panel=%08x extra=%08x",
        reason ? reason : "?",
        (unsigned)afterIndex,
        (void*)afterPage,
        SafeReadDwordTrace(afterPage + 0x00),
        SafeReadDwordTrace(afterPage + 0x120),
        SafeReadDwordTrace(afterPage + 0x124),
        SafeReadDwordTrace(SafeReadPtrTrace(base + 0x410688)),
        SafeReadDwordTrace(SafeReadPtrTrace(base + 0x410a74)));
}

static void TryNativeStashSelectPage2(const char* reason)
{
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    if (!base) return;

    uintptr_t widget = g_materialsLastUiWidget;
    if (!widget || IsBadWritePtr((void*)widget, 0x20)) {
        widget = 0;
    }

    uintptr_t pageIndexPtr = SafeReadPtrTrace(base + 0x410a8c);
    uintptr_t pageListPtr = SafeReadPtrTrace(base + 0x410958);
    uintptr_t panelPtr = SafeReadPtrTrace(base + 0x410688);
    uintptr_t extraPtr = SafeReadPtrTrace(base + 0x410a74);
    uintptr_t selectFlagPtr = SafeReadPtrTrace(base + 0x410a50);
    uint32_t beforeIndex = SafeReadDwordTrace(pageIndexPtr);
    uint32_t beforePanel = SafeReadDwordTrace(panelPtr);
    uint32_t beforeExtra = SafeReadDwordTrace(extraPtr);
    uintptr_t beforePage = 0;
    if (pageListPtr) {
        uintptr_t pageList = SafeReadPtrTrace(pageListPtr);
        beforePage = pageList ? pageList + (uintptr_t)beforeIndex * 0x550u : 0;
    }

    AppendMaterialsTraceLine(
        "native-stash-select begin reason=%s widget=%p indexPtr=%p before=%u panel=%08x extra=%08x flagPtr=%p flag=%08x beforePage=%p page00=%08x page120=%08x page124=%08x",
        reason ? reason : "?",
        (void*)widget,
        (void*)pageIndexPtr,
        (unsigned)beforeIndex,
        (unsigned)beforePanel,
        (unsigned)beforeExtra,
        (void*)selectFlagPtr,
        SafeReadDwordTrace(selectFlagPtr),
        (void*)beforePage,
        SafeReadDwordTrace(beforePage + 0x00),
        SafeReadDwordTrace(beforePage + 0x120),
        SafeReadDwordTrace(beforePage + 0x124));

    if (pageIndexPtr && !IsBadWritePtr((void*)pageIndexPtr, sizeof(uint32_t))) {
        *(uint32_t*)pageIndexPtr = 1u;
    }
    if (selectFlagPtr && !IsBadWritePtr((void*)selectFlagPtr, sizeof(uint32_t))) {
        *(uint32_t*)selectFlagPtr = 0u;
    }

    if (widget) {
        typedef void (__stdcall *FnStashSelect)(uintptr_t);
        FnStashSelect selectPage = (FnStashSelect)(base + 0x193670);
        selectPage(widget);
    }

    uint32_t afterIndex = SafeReadDwordTrace(pageIndexPtr);
    uintptr_t afterPage = 0;
    if (pageListPtr) {
        uintptr_t pageList = SafeReadPtrTrace(pageListPtr);
        afterPage = pageList ? pageList + (uintptr_t)afterIndex * 0x550u : 0;
    }
    AppendMaterialsTraceLine(
        "native-stash-select done reason=%s after=%u panel=%08x extra=%08x flag=%08x afterPage=%p page00=%08x page120=%08x page124=%08x",
        reason ? reason : "?",
        (unsigned)afterIndex,
        (unsigned)SafeReadDwordTrace(panelPtr),
        (unsigned)SafeReadDwordTrace(extraPtr),
        (unsigned)SafeReadDwordTrace(selectFlagPtr),
        (void*)afterPage,
        SafeReadDwordTrace(afterPage + 0x00),
        SafeReadDwordTrace(afterPage + 0x120),
        SafeReadDwordTrace(afterPage + 0x124));
}

static void TryNativeStashCoordSelectPage2(const char* reason)
{
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    if (!base) return;

    uintptr_t widget = g_materialsLastUiWidget;
    if (!widget || IsBadWritePtr((void*)widget, 0x20)) {
        widget = 0;
    }

    uintptr_t pageIndexPtr = SafeReadPtrTrace(base + 0x410a8c);
    uintptr_t panelPtr = SafeReadPtrTrace(base + 0x410688);
    uintptr_t extraPtr = SafeReadPtrTrace(base + 0x410a74);
    uintptr_t selectFlagPtr = SafeReadPtrTrace(base + 0x410a50);
    uint32_t beforeIndex = SafeReadDwordTrace(pageIndexPtr);
    uint32_t beforePanel = SafeReadDwordTrace(panelPtr);
    uint32_t beforeExtra = SafeReadDwordTrace(extraPtr);
    uint32_t beforeFlag = SafeReadDwordTrace(selectFlagPtr);
    uint32_t before0c = widget ? SafeReadDwordTrace(widget + 0x0c) : 0xffffffffu;
    uint32_t before18 = widget ? SafeReadDwordTrace(widget + 0x18) : 0xffffffffu;
    uint32_t before1c = widget ? SafeReadDwordTrace(widget + 0x1c) : 0xffffffffu;

    AppendMaterialsTraceLine(
        "native-stash-coord-select begin reason=%s widget=%p w0c=%08x w18=%08x w1c=%08x indexPtr=%p before=%u panel=%08x extra=%08x flagPtr=%p flag=%08x",
        reason ? reason : "?",
        (void*)widget,
        (unsigned)before0c,
        (unsigned)before18,
        (unsigned)before1c,
        (void*)pageIndexPtr,
        (unsigned)beforeIndex,
        (unsigned)beforePanel,
        (unsigned)beforeExtra,
        (void*)selectFlagPtr,
        (unsigned)beforeFlag);

    if (pageIndexPtr && !IsBadWritePtr((void*)pageIndexPtr, sizeof(uint32_t))) {
        *(uint32_t*)pageIndexPtr = 1u;
    }
    if (selectFlagPtr && !IsBadWritePtr((void*)selectFlagPtr, sizeof(uint32_t))) {
        *(uint32_t*)selectFlagPtr = 1u;
    }

    if (widget) {
        typedef void (__stdcall *FnStashCoordSelect)(uintptr_t);
        FnStashCoordSelect coordSelect = (FnStashCoordSelect)(base + 0x193610);
        coordSelect(widget);
    }

    AppendMaterialsTraceLine(
        "native-stash-coord-select done reason=%s after=%u panel=%08x extra=%08x flag=%08x w0c=%08x w18=%08x w1c=%08x",
        reason ? reason : "?",
        (unsigned)SafeReadDwordTrace(pageIndexPtr),
        (unsigned)SafeReadDwordTrace(panelPtr),
        (unsigned)SafeReadDwordTrace(extraPtr),
        (unsigned)SafeReadDwordTrace(selectFlagPtr),
        widget ? SafeReadDwordTrace(widget + 0x0c) : 0xffffffffu,
        widget ? SafeReadDwordTrace(widget + 0x18) : 0xffffffffu,
        widget ? SafeReadDwordTrace(widget + 0x1c) : 0xffffffffu);
}

static void TryNativePageListSelectorPage2(const char* reason)
{
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    if (!base) return;

#ifndef SOE_MATERIALS_PAGE2_SELECTOR_INDEX
#define SOE_MATERIALS_PAGE2_SELECTOR_INDEX 0
#endif
#ifndef SOE_MATERIALS_PAGE2_SELECTOR_PAGE_INDEX
#define SOE_MATERIALS_PAGE2_SELECTOR_PAGE_INDEX 1
#endif

    uintptr_t pageIndexPtr = SafeReadPtrTrace(base + 0x410a8c);
    uintptr_t legacyPageListPtr = SafeReadPtrTrace(base + 0x410958);
    uintptr_t selectorDescSlot = SafeReadPtrTrace(base + 0x410c24);
    uintptr_t selectorPageListSlot = SafeReadPtrTrace(base + 0x410b68);
    uintptr_t selectorMaxSlot = SafeReadPtrTrace(base + 0x410ca8);
    uintptr_t panelPtr = SafeReadPtrTrace(base + 0x410688);
    uintptr_t extraPtr = SafeReadPtrTrace(base + 0x410a74);
    uint32_t beforeIndex = SafeReadDwordTrace(pageIndexPtr);
    uint32_t selectorIndex = (uint32_t)SOE_MATERIALS_PAGE2_SELECTOR_INDEX;
    uint32_t targetPageIndex = (uint32_t)SOE_MATERIALS_PAGE2_SELECTOR_PAGE_INDEX;

    AppendMaterialsTraceLine(
        "native-page-list-selector begin reason=%s selector=%u targetPage=%u indexPtr=%p beforeIndex=%u panel=%08x extra=%08x descSlot=%p desc=%p pageListSlot=%p pageList=%p legacyPageListPtr=%p legacyPageList=%p maxSlot=%p max=%08x globalMax=%08x",
        reason ? reason : "?",
        (unsigned)selectorIndex,
        (unsigned)targetPageIndex,
        (void*)pageIndexPtr,
        (unsigned)beforeIndex,
        (unsigned)SafeReadDwordTrace(panelPtr),
        (unsigned)SafeReadDwordTrace(extraPtr),
        (void*)selectorDescSlot,
        (void*)SafeReadPtrTrace(selectorDescSlot),
        (void*)selectorPageListSlot,
        (void*)SafeReadPtrTrace(selectorPageListSlot),
        (void*)legacyPageListPtr,
        (void*)SafeReadPtrTrace(legacyPageListPtr),
        (void*)selectorMaxSlot,
        (unsigned)SafeReadDwordTrace(selectorMaxSlot),
        (unsigned)SafeReadDwordTrace(base + 0x30dce8));

    typedef void (__stdcall *FnPageListSelector)(uint32_t, uint32_t);
    FnPageListSelector selectPageList = (FnPageListSelector)(base + 0x194b90);
    selectPageList(0, selectorIndex);

    if (pageIndexPtr && !IsBadWritePtr((void*)pageIndexPtr, sizeof(uint32_t))) {
        *(uint32_t*)pageIndexPtr = targetPageIndex;
    }

#ifdef SOE_MATERIALS_PAGE2_SELECTOR_REFRESH
    {
        typedef void (__stdcall *FnMaterialsTransition)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
        FnMaterialsTransition transition = (FnMaterialsTransition)(base + 0x23f7d0);
        transition(1, 0, 0, 0, 0);
    }
#endif

    uint32_t afterIndex = SafeReadDwordTrace(pageIndexPtr);
    uintptr_t afterPageList = SafeReadPtrTrace(selectorPageListSlot);
    uintptr_t afterPage = afterPageList ? afterPageList + (uintptr_t)afterIndex * 0x550u : 0;
    AppendMaterialsTraceLine(
        "native-page-list-selector done reason=%s afterIndex=%u panel=%08x extra=%08x desc=%p pageList=%p afterPage=%p page00=%08x page120=%08x page124=%08x max=%08x globalMax=%08x",
        reason ? reason : "?",
        (unsigned)afterIndex,
        (unsigned)SafeReadDwordTrace(panelPtr),
        (unsigned)SafeReadDwordTrace(extraPtr),
        (void*)SafeReadPtrTrace(selectorDescSlot),
        (void*)afterPageList,
        (void*)afterPage,
        (unsigned)SafeReadDwordTrace(afterPage + 0x00),
        (unsigned)SafeReadDwordTrace(afterPage + 0x120),
        (unsigned)SafeReadDwordTrace(afterPage + 0x124),
        (unsigned)SafeReadDwordTrace(selectorMaxSlot),
        (unsigned)SafeReadDwordTrace(base + 0x30dce8));
    DumpMaterialsStateSnapshot("native-page-list-selector", base, 0, 0, true);
}

static void TryNativeStashPacketPage2(const char* reason)
{
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    if (!base) return;

#ifndef SOE_MATERIALS_PAGE2_STASH_PACKET_INDEX
#define SOE_MATERIALS_PAGE2_STASH_PACKET_INDEX 12
#endif
#ifndef SOE_MATERIALS_PAGE2_STASH_CLIENT_INDEX
#define SOE_MATERIALS_PAGE2_STASH_CLIENT_INDEX 11
#endif
#ifndef SOE_MATERIALS_PAGE2_STASH_CAP_VALUE
#define SOE_MATERIALS_PAGE2_STASH_CAP_VALUE 12
#endif

    uintptr_t panelPtr = SafeReadPtrTrace(base + 0x410688);
    uintptr_t extraPtr = SafeReadPtrTrace(base + 0x410a74);
    uintptr_t objectSlot = SafeReadPtrTrace(base + 0x410224);
    uintptr_t currentObject = SafeReadPtrTrace(objectSlot);
    uintptr_t currentContainer = currentObject ? SafeReadPtrTrace(currentObject + 0x60) : 0;
    uintptr_t playerSlot = SafeReadPtrTrace(base + 0x4159bc);
    uintptr_t player = SafeReadPtrTrace(playerSlot);
    uintptr_t playerContainer = player ? SafeReadPtrTrace(player + 0x60) : 0;
    const uint32_t packetIndex = (uint32_t)SOE_MATERIALS_PAGE2_STASH_PACKET_INDEX;
    const uint32_t clientIndex = (uint32_t)SOE_MATERIALS_PAGE2_STASH_CLIENT_INDEX;

    AppendMaterialsTraceLine(
        "native-stash-packet begin reason=%s packetIndex=%u clientIndex=%u panel=%08x extra=%08x stash=%08x currentTab=%08x sharedPage=%08x transition=%08x mat=%08x dirty=%08x flagA=%08x flagB=%08x flagC=%08x object=%p container=%p player=%p playerContainer=%p",
        reason ? reason : "?",
        (unsigned)packetIndex,
        (unsigned)clientIndex,
        (unsigned)SafeReadDwordTrace(panelPtr),
        (unsigned)SafeReadDwordTrace(extraPtr),
        (unsigned)SafeReadDwordTrace(base + 0x2d4b24),
        (unsigned)SafeReadDwordTrace(base + 0x40edd4),
        (unsigned)SafeReadDwordTrace(base + 0x2fd650),
        (unsigned)SafeReadDwordTrace(base + 0x30de48),
        (unsigned)SafeReadDwordTrace(base + 0x30dca8),
        (unsigned)SafeReadDwordTrace(base + 0x30dc84),
        (unsigned)SafeReadDwordTrace(base + 0x30dd14),
        (unsigned)SafeReadDwordTrace(base + 0x30dd30),
        (unsigned)SafeReadDwordTrace(base + 0x30de54),
        (void*)currentObject,
        (void*)currentContainer,
        (void*)player,
        (void*)playerContainer);

    DumpNativeStashPageGeometry(base, reason ? reason : "native-stash-packet");

#ifdef SOE_MATERIALS_PAGE2_PATCH_STASH_CAP
    {
        uint32_t capValue = (uint32_t)SOE_MATERIALS_PAGE2_STASH_CAP_VALUE;
        uint32_t maxIndex = capValue ? capValue - 1u : 0u;
        bool okApplyCap = PatchProjectDword(base + 0x22998d, 0x0000000bu, capValue, "stash-apply-byte-cap");
        bool okIncoming = PatchProjectByte(base + 0x2364ff, 0x0au, (uint8_t)maxIndex, "stash-incoming-page-byte-cap");
        bool okAddGridCap = PatchProjectByte(base + 0x22975a, 0x0bu, (uint8_t)capValue, "stash-add-grid-cmp");
        bool okAddGridMax = PatchProjectDword(base + 0x22975e, 0x0000000au, maxIndex, "stash-add-grid-max");
        bool okMoveGridCap = PatchProjectByte(base + 0x229849, 0x0bu, (uint8_t)capValue, "stash-move-grid-cmp");
        bool okMoveGridMax = PatchProjectDword(base + 0x22984b, 0x0000000au, maxIndex, "stash-move-grid-max");
        bool okPageToCoordCap = PatchProjectByte(base + 0x229a9d, 0x0bu, (uint8_t)capValue, "stash-page-to-coord-cmp");
        bool okPageToCoordMax = PatchProjectDword(base + 0x229aa3, 0x0000000au, maxIndex, "stash-page-to-coord-max");
        bool okCoordToPageMax = PatchProjectDword(base + 0x229ba7, 0x0000000au, maxIndex, "stash-coord-to-page-max");
        bool okCoordToPageCap = PatchProjectByte(base + 0x229bad, 0x0bu, (uint8_t)capValue, "stash-coord-to-page-cmp");
        bool okUiSelectCap = PatchProjectByte(base + 0x193aec, 0x0bu, (uint8_t)capValue, "stash-ui-select-cmp");
        bool okWheelWrapMax = PatchProjectDword(base + 0x19423a, 0x0000000au, maxIndex, "stash-wheel-wrap-max");
        bool okWheelWrapCap = PatchProjectByte(base + 0x194244, 0x0bu, (uint8_t)capValue, "stash-wheel-wrap-cmp");
#ifdef SOE_MATERIALS_PAGE2_PATCH_UI_DRAW_CAPS
        bool okDrawLoopCap = PatchProjectByte(base + 0x18bb92, 0x0au, (uint8_t)maxIndex, "stash-draw-tab-loop-cap");
        bool okTopTabLoopCap = PatchProjectByte(base + 0x1b503c, 0x0au, (uint8_t)maxIndex, "stash-top-tab-loop-cap");
#else
        bool okDrawLoopCap = false;
        bool okTopTabLoopCap = false;
#endif
#ifdef SOE_MATERIALS_PAGE2_PATCH_HOVER_CAPS
        bool okHoverPageCap = PatchProjectByte(base + 0x1b343d, 0x0bu, (uint8_t)capValue, "stash-hover-page-cmp");
        bool okHoverPageMax = PatchProjectDword(base + 0x1b3449, 0x0000000au, maxIndex, "stash-hover-page-max");
#else
        bool okHoverPageCap = false;
        bool okHoverPageMax = false;
#endif
        AppendMaterialsTraceLine(
            "native-stash-packet cap-patch-summary reason=%s capValue=%u maxIndex=%u apply=%d incoming=%d add=%d/%d move=%d/%d pageToCoord=%d/%d coordToPage=%d/%d ui=%d wheel=%d/%d draw=%d top=%d hover=%d/%d currentBytes incoming=%02x apply=%08x coordCap=%02x drawCap=%02x topCap=%02x hoverCap=%02x hoverMax=%08x",
            reason ? reason : "?",
            (unsigned)capValue,
            (unsigned)maxIndex,
            okApplyCap ? 1 : 0,
            okIncoming ? 1 : 0,
            okAddGridCap ? 1 : 0,
            okAddGridMax ? 1 : 0,
            okMoveGridCap ? 1 : 0,
            okMoveGridMax ? 1 : 0,
            okPageToCoordCap ? 1 : 0,
            okPageToCoordMax ? 1 : 0,
            okCoordToPageMax ? 1 : 0,
            okCoordToPageCap ? 1 : 0,
            okUiSelectCap ? 1 : 0,
            okWheelWrapMax ? 1 : 0,
            okWheelWrapCap ? 1 : 0,
            okDrawLoopCap ? 1 : 0,
            okTopTabLoopCap ? 1 : 0,
            okHoverPageCap ? 1 : 0,
            okHoverPageMax ? 1 : 0,
            (unsigned)(SafeReadDwordTrace(base + 0x2364ff) & 0xffu),
            (unsigned)SafeReadDwordTrace(base + 0x22998d),
            (unsigned)(SafeReadDwordTrace(base + 0x229bad) & 0xffu),
            (unsigned)(SafeReadDwordTrace(base + 0x18bb92) & 0xffu),
            (unsigned)(SafeReadDwordTrace(base + 0x1b503c) & 0xffu),
            (unsigned)(SafeReadDwordTrace(base + 0x1b343d) & 0xffu),
            (unsigned)SafeReadDwordTrace(base + 0x1b3449));
    }
#endif

#ifndef SOE_MATERIALS_PAGE2_SKIP_STASH_PACKET_SEND
    {
        typedef void (__fastcall *FnSendPacket)(void*, uint32_t);
        FnSendPacket sendPacket = (FnSendPacket)(base + 0x23ead0);
        uint8_t packet[2] = { 0x55u, (uint8_t)packetIndex };
        AppendMaterialsTraceLine(
            "native-stash-packet send reason=%s fn=%p bytes=%02x,%02x",
            reason ? reason : "?",
            (void*)sendPacket,
            (unsigned)packet[0],
            (unsigned)packet[1]);
        sendPacket(packet, 2);
    }
#else
    AppendMaterialsTraceLine(
        "native-stash-packet send-skipped reason=%s bytes=55,%02x",
        reason ? reason : "?",
        (unsigned)((uint8_t)packetIndex));
#endif

#ifdef SOE_MATERIALS_PAGE2_CALL_NATIVE_APPLY
    if (currentObject) {
#ifndef SOE_MATERIALS_PAGE2_NATIVE_APPLY_ARG
#define SOE_MATERIALS_PAGE2_NATIVE_APPLY_ARG 1
#endif
        uintptr_t objectData = SafeReadPtrTrace(currentObject + 0x14);
        uint32_t beforeCurrentTab = SafeReadDwordTrace(base + 0x40edd4);
        uint32_t beforeObjectPage = objectData ? SafeReadDwordTrace(objectData + 0x1b4) : 0xffffffffu;
        uint32_t beforeObjectBusy = objectData ? SafeReadDwordTrace(objectData + 0x1b8) : 0xffffffffu;
        typedef void (__fastcall *FnApplyStashPage)(uintptr_t, uint32_t, uint32_t);
        FnApplyStashPage applyStashPage = (FnApplyStashPage)(base + 0x229970);
        uint32_t applyArg = (uint32_t)SOE_MATERIALS_PAGE2_NATIVE_APPLY_ARG;
        AppendMaterialsTraceLine(
            "native-stash-packet apply-native reason=%s fn=%p object=%p data=%p pageByte=%u arg=%u beforeObjPage=%08x beforeBusy=%08x beforeCurrentTab=%08x",
            reason ? reason : "?",
            (void*)applyStashPage,
            (void*)currentObject,
            (void*)objectData,
            (unsigned)packetIndex,
            (unsigned)applyArg,
            (unsigned)beforeObjectPage,
            (unsigned)beforeObjectBusy,
            (unsigned)beforeCurrentTab);
        applyStashPage(currentObject, packetIndex, applyArg);
#ifdef SOE_MATERIALS_PAGE2_RESTORE_UI_TAB_AFTER_APPLY
        {
            uint32_t afterApplyTab = SafeReadDwordTrace(base + 0x40edd4);
            uint32_t restoreTab = beforeCurrentTab;
#ifdef SOE_MATERIALS_PAGE2_RESTORE_UI_TAB_VALUE
            restoreTab = (uint32_t)SOE_MATERIALS_PAGE2_RESTORE_UI_TAB_VALUE;
#endif
            bool restoreOk = WriteMaterialsDword(base + 0x40edd4, restoreTab);
            AppendMaterialsTraceLine(
                "native-stash-packet restore-ui-tab reason=%s beforeApply=%08x restore=%08x afterApplyTab=%08x afterRestore=%08x ok=%d objectPage=%08x",
                reason ? reason : "?",
                (unsigned)beforeCurrentTab,
                (unsigned)restoreTab,
                (unsigned)afterApplyTab,
                (unsigned)SafeReadDwordTrace(base + 0x40edd4),
                restoreOk ? 1 : 0,
                objectData ? (unsigned)SafeReadDwordTrace(objectData + 0x1b4) : 0xffffffffu);
        }
#endif
        AppendMaterialsTraceLine(
            "native-stash-packet apply-native-done reason=%s object=%p data=%p afterObjPage=%08x afterBusy=%08x afterCurrentTab=%08x",
            reason ? reason : "?",
            (void*)currentObject,
            (void*)objectData,
            objectData ? (unsigned)SafeReadDwordTrace(objectData + 0x1b4) : 0xffffffffu,
            objectData ? (unsigned)SafeReadDwordTrace(objectData + 0x1b8) : 0xffffffffu,
            (unsigned)SafeReadDwordTrace(base + 0x40edd4));
    }
#endif

#ifdef SOE_MATERIALS_PAGE2_PACKET_SET_LOCAL
    {
        uint32_t beforeLocal = SafeReadDwordTrace(base + 0x40edd4);
        bool localOk = WriteMaterialsDword(base + 0x40edd4, clientIndex);
        AppendMaterialsTraceLine(
            "native-stash-packet local-current-tab reason=%s before=%08x target=%08x after=%08x ok=%d",
            reason ? reason : "?",
            (unsigned)beforeLocal,
            (unsigned)clientIndex,
            (unsigned)SafeReadDwordTrace(base + 0x40edd4),
            localOk ? 1 : 0);
    }
#endif

#ifdef SOE_MATERIALS_PAGE2_STASH_PACKET_REFRESH
    {
        typedef void (__stdcall *FnMaterialsTransition)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
        FnMaterialsTransition transition = (FnMaterialsTransition)(base + 0x23f7d0);
        AppendMaterialsTraceLine(
            "native-stash-packet refresh reason=%s fn=%p",
            reason ? reason : "?",
            (void*)transition);
        transition(1, 0, 0, 0, 0);
    }
#endif

#ifdef SOE_MATERIALS_PAGE2_DESCRIPTOR_AFTER_STASH_PACKET
    SetMaterialsDescriptorPage2(base, true, reason ? reason : "native-stash-packet-descriptor");
#endif

    AppendMaterialsTraceLine(
        "native-stash-packet done reason=%s panel=%08x extra=%08x stash=%08x currentTab=%08x sharedPage=%08x transition=%08x mat=%08x dirty=%08x flagA=%08x flagB=%08x flagC=%08x",
        reason ? reason : "?",
        (unsigned)SafeReadDwordTrace(panelPtr),
        (unsigned)SafeReadDwordTrace(extraPtr),
        (unsigned)SafeReadDwordTrace(base + 0x2d4b24),
        (unsigned)SafeReadDwordTrace(base + 0x40edd4),
        (unsigned)SafeReadDwordTrace(base + 0x2fd650),
        (unsigned)SafeReadDwordTrace(base + 0x30de48),
        (unsigned)SafeReadDwordTrace(base + 0x30dca8),
        (unsigned)SafeReadDwordTrace(base + 0x30dc84),
        (unsigned)SafeReadDwordTrace(base + 0x30dd14),
        (unsigned)SafeReadDwordTrace(base + 0x30dd30),
        (unsigned)SafeReadDwordTrace(base + 0x30de54));
    DumpMaterialsStateSnapshot("native-stash-packet", base, currentObject, 0, true);
}

extern "C" void __stdcall LogMaterialsStorageCall(
    uint32_t rva,
    uintptr_t eax,
    uintptr_t ecx,
    uintptr_t edx,
    uintptr_t ebx,
    uintptr_t savedEspAfterFlags)
{
    LONG n = InterlockedIncrement(&g_materialsStorageLogCount);
    if (n > 520) return;

    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    uintptr_t callStack = savedEspAfterFlags + 4;
    uintptr_t retAddr = SafeReadPtrTrace(callStack);
    uintptr_t retRva = base && retAddr >= base ? retAddr - base : retAddr;
    uint32_t a1 = SafeReadDwordTrace(callStack + 4);
    uint32_t a2 = SafeReadDwordTrace(callStack + 8);
    uint32_t a3 = SafeReadDwordTrace(callStack + 12);
    uint32_t a4 = SafeReadDwordTrace(callStack + 16);
    uintptr_t ptr410688 = base ? SafeReadPtrTrace(base + 0x410688) : 0;
    uintptr_t ptr410a74 = base ? SafeReadPtrTrace(base + 0x410a74) : 0;
    uintptr_t ptr410998 = base ? SafeReadPtrTrace(base + 0x410998) : 0;
    uint32_t activeIndex = SafeReadDwordTrace(ptr410998);
    uint32_t activeItemId = 0xFFFFFFFFu;
    if (base && activeIndex != 0xFFFFFFFFu && activeIndex < 0x10000u) {
        activeItemId = SafeReadDwordTrace(base + 0x2d4668 + (uintptr_t)activeIndex * 10);
    }
    char summary[256] = {};
    SummarizeMaterialsDescriptorTable(base, summary, sizeof(summary));

    AppendMaterialsTraceLine(
        "storage rva=%08x ret=%p retRva=%08x eax=%p ecx=%p edx=%p ebx=%p a1=%08x a2=%08x a3=%08x a4=%08x stash=%08x mat=%08x panel=%08x extra=%08x page2=%ld left=%ld active=%08x activeItem=%08x f9a8=%08x f9b0=%08x f9ac=%p eddc=%p summary=%s",
        (unsigned)rva,
        (void*)retAddr,
        (unsigned)retRva,
        (void*)eax,
        (void*)ecx,
        (void*)edx,
        (void*)ebx,
        (unsigned)a1,
        (unsigned)a2,
        (unsigned)a3,
        (unsigned)a4,
        base ? SafeReadDwordTrace(base + 0x2d4b24) : 0,
        base ? SafeReadDwordTrace(base + 0x30dca8) : 0,
        SafeReadDwordTrace(ptr410688),
        SafeReadDwordTrace(ptr410a74),
        (long)g_materialsPage2Active,
        (long)g_materialsLeftMaterials,
        (unsigned)activeIndex,
        (unsigned)activeItemId,
        base ? SafeReadDwordTrace(base + 0x40f9a8) : 0,
        base ? SafeReadDwordTrace(base + 0x40f9b0) : 0,
        (void*)(base ? SafeReadPtrTrace(base + 0x40f9ac) : 0),
        (void*)(base ? SafeReadPtrTrace(base + 0x40eddc) : 0),
        summary);

    if (edx && !IsBadReadPtr((void*)edx, 0x80)) {
        AppendMaterialsTraceLine(
            "storage rva=%08x edx-block dwords00-7c=%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x",
            (unsigned)rva,
            SafeReadDwordTrace(edx + 0x00), SafeReadDwordTrace(edx + 0x04), SafeReadDwordTrace(edx + 0x08), SafeReadDwordTrace(edx + 0x0c),
            SafeReadDwordTrace(edx + 0x10), SafeReadDwordTrace(edx + 0x14), SafeReadDwordTrace(edx + 0x18), SafeReadDwordTrace(edx + 0x1c),
            SafeReadDwordTrace(edx + 0x20), SafeReadDwordTrace(edx + 0x24), SafeReadDwordTrace(edx + 0x28), SafeReadDwordTrace(edx + 0x2c),
            SafeReadDwordTrace(edx + 0x30), SafeReadDwordTrace(edx + 0x34), SafeReadDwordTrace(edx + 0x38), SafeReadDwordTrace(edx + 0x3c),
            SafeReadDwordTrace(edx + 0x40), SafeReadDwordTrace(edx + 0x44), SafeReadDwordTrace(edx + 0x48), SafeReadDwordTrace(edx + 0x4c),
            SafeReadDwordTrace(edx + 0x50), SafeReadDwordTrace(edx + 0x54), SafeReadDwordTrace(edx + 0x58), SafeReadDwordTrace(edx + 0x5c),
            SafeReadDwordTrace(edx + 0x60), SafeReadDwordTrace(edx + 0x64), SafeReadDwordTrace(edx + 0x68), SafeReadDwordTrace(edx + 0x6c),
            SafeReadDwordTrace(edx + 0x70), SafeReadDwordTrace(edx + 0x74), SafeReadDwordTrace(edx + 0x78), SafeReadDwordTrace(edx + 0x7c));
        DumpMaterialsPointerBlock("storage-edx", "edx+60", SafeReadPtrTrace(edx + 0x60));
        DumpMaterialsHcBlock("storage-edx", edx);
    }
}

extern "C" void __stdcall LogMaterialsMessageCall(uint32_t rva, uintptr_t ecxValue, uintptr_t originalEsp)
{
    LONG n = InterlockedIncrement(&g_materialsMessageLogCount);
    if (n > 1600) return;

    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    uintptr_t retAddr = SafeReadPtrTrace(originalEsp);
    uintptr_t retRva = base && retAddr >= base ? retAddr - base : retAddr;
    uintptr_t ptr410688 = base ? SafeReadPtrTrace(base + 0x410688) : 0;
    uintptr_t ptr410a74 = base ? SafeReadPtrTrace(base + 0x410a74) : 0;
    uintptr_t playerSlot = base ? SafeReadPtrTrace(base + 0x4159bc) : 0;
    uintptr_t player = SafeReadPtrTrace(playerSlot);
    uintptr_t playerContainer = player ? SafeReadPtrTrace(player + 0x60) : 0;
    uintptr_t objectSlot = base ? SafeReadPtrTrace(base + 0x410224) : 0;
    uintptr_t currentObject = SafeReadPtrTrace(objectSlot);
    uintptr_t currentContainer = currentObject ? SafeReadPtrTrace(currentObject + 0x60) : 0;
    bool readable = ecxValue && !IsBadReadPtr((void*)ecxValue, 0x20);

    AppendMaterialsTraceLine(
        "msg rva=%08x ret=%p retRva=%08x ecx=%p readable=%d bytes=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x dwords=%08x %08x %08x %08x panel=%08x extra=%08x stash=%08x mat=%08x playerSlot=%p player=%p player60=%p objSlot=%p obj=%p obj60=%p page2=%ld",
        (unsigned)rva,
        (void*)retAddr,
        (unsigned)retRva,
        (void*)ecxValue,
        readable ? 1 : 0,
        readable ? SafeReadByteTrace(ecxValue + 0x00) : 0,
        readable ? SafeReadByteTrace(ecxValue + 0x01) : 0,
        readable ? SafeReadByteTrace(ecxValue + 0x02) : 0,
        readable ? SafeReadByteTrace(ecxValue + 0x03) : 0,
        readable ? SafeReadByteTrace(ecxValue + 0x04) : 0,
        readable ? SafeReadByteTrace(ecxValue + 0x05) : 0,
        readable ? SafeReadByteTrace(ecxValue + 0x06) : 0,
        readable ? SafeReadByteTrace(ecxValue + 0x07) : 0,
        readable ? SafeReadByteTrace(ecxValue + 0x08) : 0,
        readable ? SafeReadByteTrace(ecxValue + 0x09) : 0,
        readable ? SafeReadByteTrace(ecxValue + 0x0a) : 0,
        readable ? SafeReadByteTrace(ecxValue + 0x0b) : 0,
        readable ? SafeReadDwordTrace(ecxValue + 0x00) : 0,
        readable ? SafeReadDwordTrace(ecxValue + 0x04) : 0,
        readable ? SafeReadDwordTrace(ecxValue + 0x08) : 0,
        readable ? SafeReadDwordTrace(ecxValue + 0x0c) : 0,
        SafeReadDwordTrace(ptr410688),
        SafeReadDwordTrace(ptr410a74),
        base ? SafeReadDwordTrace(base + 0x2d4b24) : 0,
        base ? SafeReadDwordTrace(base + 0x30dca8) : 0,
        (void*)playerSlot,
        (void*)player,
        (void*)playerContainer,
        (void*)objectSlot,
        (void*)currentObject,
        (void*)currentContainer,
        (long)g_materialsPage2Active);

    if (readable && (g_materialsPage2Active || n <= 120)) {
        uint32_t selector = SafeReadByteTrace(ecxValue + 0x05);
        ProbeMaterialsPacketGrid("player", base, playerContainer, selector, ecxValue);
        if (currentContainer && currentContainer != playerContainer) {
            ProbeMaterialsPacketGrid("current-object", base, currentContainer, selector, ecxValue);
        }
    }
}

extern "C" int __stdcall HandleMaterialsMsg1450Call(uintptr_t ecxValue, uintptr_t originalEsp)
{
    LogMaterialsMessageCall(0x231450, ecxValue, originalEsp);

#ifdef SOE_MATERIALS_BLOCK_PAGE2_TRANSFER_MSG
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    bool readable = ecxValue && !IsBadReadPtr((void*)ecxValue, 0x10);
    bool transferMsg = (
        readable &&
        SafeReadByteTrace(ecxValue + 0x00) == 0x2d &&
#ifdef SOE_MATERIALS_BLOCK_PAGE2_ALL_2D_MSG
        true);
#else
        SafeReadByteTrace(ecxValue + 0x01) == 0x09);
#endif
    if (g_materialsPage2Active && transferMsg) {
        AppendMaterialsTraceLine(
            "msg1450 page2-transfer blocked ecx=%p panel=%08x extra=%08x bytes=%02x %02x %02x %02x dwords=%08x %08x %08x %08x",
            (void*)ecxValue,
            base ? SafeReadDwordTrace(SafeReadPtrTrace(base + 0x410688)) : 0xffffffffu,
            base ? SafeReadDwordTrace(SafeReadPtrTrace(base + 0x410a74)) : 0xffffffffu,
            SafeReadByteTrace(ecxValue + 0x00),
            SafeReadByteTrace(ecxValue + 0x01),
            SafeReadByteTrace(ecxValue + 0x02),
            SafeReadByteTrace(ecxValue + 0x03),
            SafeReadDwordTrace(ecxValue + 0x00),
            SafeReadDwordTrace(ecxValue + 0x04),
            SafeReadDwordTrace(ecxValue + 0x08),
            SafeReadDwordTrace(ecxValue + 0x0c));
        return 1;
    }
#else
    (void)ecxValue;
    (void)originalEsp;
#endif

    return 0;
}

extern "C" void __stdcall LogMaterialsDescriptorLookup(uint32_t itemId, uint32_t key)
{
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    if (!base) return;

    uintptr_t ptr410688 = SafeReadPtrTrace(base + 0x410688);
    uintptr_t ptr410a74 = SafeReadPtrTrace(base + 0x410a74);
    uint32_t panel = SafeReadDwordTrace(ptr410688);
    uint32_t extra = SafeReadDwordTrace(ptr410a74);
    uint32_t materialFlag = SafeReadDwordTrace(base + 0x30dca8);
    if (!g_materialsPage2Active && !(panel == 0x0c && extra == 2)) return;

    LONG count = InterlockedIncrement(&g_materialsDescriptorLookupTraceCount);
    if (count > 360) return;

    uintptr_t table = SafeReadPtrTrace(base + 0x41076c);
    int index = -1;
    uint16_t slot = 0xffff;
    if (table && !IsBadReadPtr((void*)table, 216 * 10)) {
        for (int i = 0; i < 216; ++i) {
            uintptr_t entry = table + (uintptr_t)i * 10;
            if (*(uint32_t*)(entry + 0) == itemId && *(uint32_t*)(entry + 6) == key) {
                index = i;
                slot = *(uint16_t*)(entry + 4);
                break;
            }
        }
    }

    AppendMaterialsTraceLine(
        "descriptor-lookup #%ld item=%u/0x%02x key=%u idx=%d slot=%u/0x%04x table=%p panel=%08x extra=%08x mat=%08x page2=%ld left=%ld",
        count,
        itemId,
        itemId,
        key,
        index,
        slot,
        slot,
        (void*)table,
        panel,
        extra,
        materialFlag,
        g_materialsPage2Active,
        g_materialsLeftMaterials);
}

extern "C" uint32_t __stdcall RewriteMaterialsDescriptorLookupKey(uint32_t rva, uint32_t itemId, uint32_t key)
{
#ifdef SOE_MATERIALS_PAGE2_REWRITE_DESCRIPTOR_KEY
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    const uint32_t page2Key = (uint32_t)SOE_MATERIALS_PAGE2_REWRITE_DESCRIPTOR_KEY;
    if (!base || page2Key <= 1 || key != 1 || !g_materialsPage2Active) return key;

    uint32_t currentKey = SafeReadDwordTrace(base + 0x30dca8);

    static volatile LONG rewriteLogCount = 0;
    LONG count = InterlockedIncrement(&rewriteLogCount);
    if (count <= 160) {
        uintptr_t ptrPanel = SafeReadPtrTrace(base + 0x410688);
        uintptr_t ptrExtra = SafeReadPtrTrace(base + 0x410a74);
        AppendMaterialsTraceLine(
            "descriptor-rewrite #%ld rva=%08x item=%u key=%u->%u mat=%08x panel=%08x extra=%08x page2=%ld",
            (long)count,
            (unsigned)rva,
            (unsigned)itemId,
            (unsigned)key,
            (unsigned)page2Key,
            (unsigned)currentKey,
            SafeReadDwordTrace(ptrPanel),
            SafeReadDwordTrace(ptrExtra),
            (long)g_materialsPage2Active);
    }
    return page2Key;
#else
    (void)rva;
    (void)itemId;
    return key;
#endif
}

extern "C" void __stdcall LogMaterialsDescriptorHelper(uint32_t rva, uint32_t itemId, uint32_t key)
{
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    if (!base) return;

    uintptr_t ptr410688 = SafeReadPtrTrace(base + 0x410688);
    uintptr_t ptr410a74 = SafeReadPtrTrace(base + 0x410a74);
    uint32_t panel = SafeReadDwordTrace(ptr410688);
    uint32_t extra = SafeReadDwordTrace(ptr410a74);
    if (!g_materialsPage2Active && !(panel == 0x0c && extra == 2)) return;

    LONG count = InterlockedIncrement(&g_materialsDescriptorHelperTraceCount);
    if (count > 900) return;

    uintptr_t table = SafeReadPtrTrace(base + 0x41076c);
    int descriptorIndex = -1;
    uint16_t slot = 0xffff;
    if (table && !IsBadReadPtr((void*)table, 216 * 10)) {
        for (int i = 0; i < 216; ++i) {
            uintptr_t entry = table + (uintptr_t)i * 10;
            if (*(uint32_t*)(entry + 0) == itemId && *(uint32_t*)(entry + 6) == key) {
                descriptorIndex = i;
                slot = *(uint16_t*)(entry + 4);
                break;
            }
        }
    }

    int catalogIndex = -1;
    uint32_t catalogName = 0xFFFFFFFFu;
    uintptr_t catalogTable = base + 0x2d4668;
    if (!IsBadReadPtr((void*)catalogTable, 128 * 10)) {
        for (int i = 0; i < 128; ++i) {
            uintptr_t entry = catalogTable + (uintptr_t)i * 10;
            if (*(uint32_t*)(entry + 0) == itemId) {
                catalogIndex = i;
                catalogName = *(uint32_t*)(entry + 4);
                break;
            }
        }
    }

    AppendMaterialsTraceLine(
        "descriptor-helper #%ld rva=%08x item=%u/0x%02x key=%u descIdx=%d slot=%u/0x%04x catalogIdx=%d name=%u panel=%08x extra=%08x mat=%08x page2=%ld left=%ld",
        count,
        rva,
        itemId,
        itemId,
        key,
        descriptorIndex,
        slot,
        slot,
        catalogIndex,
        catalogName,
        panel,
        extra,
        SafeReadDwordTrace(base + 0x30dca8),
        g_materialsPage2Active,
        g_materialsLeftMaterials);
}

extern "C" void __stdcall LogMaterialsGridRender(uintptr_t objectPtr, uintptr_t savedEspAfterFlags)
{
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    if (!base) return;

    uintptr_t ptr410688 = SafeReadPtrTrace(base + 0x410688);
    uintptr_t ptr410a74 = SafeReadPtrTrace(base + 0x410a74);
    uint32_t panel = SafeReadDwordTrace(ptr410688);
    uint32_t extra = SafeReadDwordTrace(ptr410a74);
    bool materialsPanel = (panel == 0x0c && (extra == 1 || extra == 2));
    bool page2 = g_materialsPage2Active != 0;

#ifdef SOE_MATERIALS_PAGE2_MATERIAL_RULES
    ApplySoEPage2MaterialRules(base, objectPtr, "grid-render");
#endif

    LONG count = InterlockedIncrement(&g_materialsGridRenderLogCount);
    if (!materialsPanel && !page2 && count > 80) return;
    if (count > 520) return;

    uintptr_t retAddr = SafeReadPtrTrace(savedEspAfterFlags + 4);
    uintptr_t retRva = base && retAddr >= base ? retAddr - base : retAddr;
    uintptr_t renderRoot = objectPtr ? SafeReadPtrTrace(objectPtr + 0x60) : 0;
    uintptr_t listHead = renderRoot ? SafeReadPtrTrace(renderRoot + 0x0c) : 0;
    char summary[256] = {};
    SummarizeMaterialsDescriptorTable(base, summary, sizeof(summary));

    AppendMaterialsTraceLine(
        "grid-render #%ld ret=%p retRva=%08x object=%p root60=%p head=%p dd40=%04x stash=%08x mat=%08x panel=%08x extra=%08x page2=%ld left=%ld summary=%s obj60=%08x obj64=%08x root00=%08x root0c=%08x root10=%08x root14=%08x",
        count,
        (void*)retAddr,
        (unsigned)retRva,
        (void*)objectPtr,
        (void*)renderRoot,
        (void*)listHead,
        (unsigned)(SafeReadDwordTrace(base + 0x30dd40) & 0xffffu),
        SafeReadDwordTrace(base + 0x2d4b24),
        SafeReadDwordTrace(base + 0x30dca8),
        panel,
        extra,
        (long)g_materialsPage2Active,
        (long)g_materialsLeftMaterials,
        summary,
        SafeReadDwordTrace(objectPtr + 0x60),
        SafeReadDwordTrace(objectPtr + 0x64),
        SafeReadDwordTrace(renderRoot + 0x00),
        SafeReadDwordTrace(renderRoot + 0x0c),
        SafeReadDwordTrace(renderRoot + 0x10),
        SafeReadDwordTrace(renderRoot + 0x14));

    uintptr_t node = listHead;
    for (int i = 0; i < 8 && node && !IsBadReadPtr((void*)node, 0x34); ++i) {
        uintptr_t itemData = SafeReadPtrTrace(node + 0x14);
        uintptr_t node2c = SafeReadPtrTrace(node + 0x2c);
        AppendMaterialsTraceLine(
            "grid-render node[%d]=%p next?=%p itemData14=%p item45=%02x node2c=%p node2c_0c=%08x node2c_10=%08x dwords=%08x %08x %08x %08x %08x %08x %08x %08x",
            i,
            (void*)node,
            (void*)SafeReadPtrTrace(node + 0x00),
            (void*)itemData,
            (unsigned)(SafeReadDwordTrace(itemData + 0x44) >> 8) & 0xffu,
            (void*)node2c,
            SafeReadDwordTrace(node2c + 0x0c),
            SafeReadDwordTrace(node2c + 0x10),
            SafeReadDwordTrace(node + 0x00),
            SafeReadDwordTrace(node + 0x04),
            SafeReadDwordTrace(node + 0x08),
            SafeReadDwordTrace(node + 0x0c),
            SafeReadDwordTrace(node + 0x10),
            SafeReadDwordTrace(node + 0x14),
            SafeReadDwordTrace(node + 0x18),
            SafeReadDwordTrace(node + 0x1c));
        typedef uintptr_t (__stdcall *FnNext)(uintptr_t);
        HMODULE hProjectLocal = GetModuleHandleA("ProjectDiablo.dll");
        uintptr_t baseLocal = (uintptr_t)hProjectLocal;
        if (!baseLocal) break;
        FnNext next = (FnNext)(baseLocal + 0x1d1570);
        uintptr_t nextNode = next(node);
        if (nextNode == node) break;
        node = nextNode;
    }
}

extern "C" void __stdcall LogMaterialsPageDraw(uintptr_t savedEspAfterFlags)
{
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    if (!base) return;

    uintptr_t ptr410688 = SafeReadPtrTrace(base + 0x410688);
    uintptr_t ptr410a74 = SafeReadPtrTrace(base + 0x410a74);
    uint32_t panel = SafeReadDwordTrace(ptr410688);
    uint32_t extra = SafeReadDwordTrace(ptr410a74);
    bool materialsPanel = (panel == 0x0c && (extra == 1 || extra == 2));
    bool page2 = g_materialsPage2Active != 0;

    LONG countLog = InterlockedIncrement(&g_materialsPageDrawLogCount);
    if (!materialsPanel && !page2 && countLog > 80) return;
    if (countLog > 520) return;
    if (materialsPanel && countLog <= 3) {
        DumpMaterialsCatalogState(page2 ? "page-draw-page2" : "page-draw-materials", base);
    }

    uintptr_t retAddr = SafeReadPtrTrace(savedEspAfterFlags + 4);
    uintptr_t retRva = base && retAddr >= base ? retAddr - base : retAddr;

    uintptr_t countPtr = SafeReadPtrTrace(base + 0x410314);
    uintptr_t indexBasePtr = SafeReadPtrTrace(base + 0x4102f8);
    uintptr_t currentPtr = SafeReadPtrTrace(base + 0x41010c);
    uintptr_t hoverPtr = SafeReadPtrTrace(base + 0x41020c);
    uintptr_t yPtr = SafeReadPtrTrace(base + 0x4102d8);
    uintptr_t xPtr = SafeReadPtrTrace(base + 0x410144);
    uint32_t count = SafeReadDwordTrace(countPtr);
    uint32_t indexBase = SafeReadDwordTrace(indexBasePtr);
    uint32_t current = SafeReadDwordTrace(currentPtr);
    uint32_t hover = SafeReadDwordTrace(hoverPtr);
    uint32_t screenY = SafeReadDwordTrace(yPtr);
    uint32_t screenX = SafeReadDwordTrace(xPtr);

    char slots[768] = {};
    size_t used = 0;
    uint32_t slotCount = count > 16 ? 16 : count;
    for (uint32_t i = 0; i < slotCount; ++i) {
        uint32_t idx = indexBase + i;
        uintptr_t entry = base + 0x2d4668 + (uintptr_t)idx * 10;
        uint32_t itemId = SafeReadDwordTrace(entry + 0);
        uint32_t nameId = SafeReadDwordTrace(entry + 4) & 0xffffu;
        int wrote = _snprintf(
            slots + used,
            sizeof(slots) - used,
            "%s%u:%u/%u/%u",
            used ? "," : "",
            i,
            idx,
            itemId,
            nameId);
        if (wrote <= 0) break;
        used += (size_t)wrote;
        if (used >= sizeof(slots) - 1) break;
    }

    AppendMaterialsTraceLine(
        "page-draw #%ld ret=%p retRva=%08x panel=%08x extra=%08x page2=%ld left=%ld stash=%08x mat=%08x activeKey=%08x dirty=%08x hoverNibble=%02x countPtr=%p count=%u indexPtr=%p indexBase=%u currentPtr=%p current=%u hoverPtr=%p hover=%u screen=%u,%u slots=[%s]",
        countLog,
        (void*)retAddr,
        (unsigned)retRva,
        panel,
        extra,
        (long)g_materialsPage2Active,
        (long)g_materialsLeftMaterials,
        SafeReadDwordTrace(base + 0x2d4b24),
        SafeReadDwordTrace(base + 0x30dca8),
        SafeReadDwordTrace(base + 0x30dca8),
        SafeReadDwordTrace(base + 0x30dc84),
        (unsigned)(SafeReadDwordTrace(base + 0x30dc58) & 0xffu),
        (void*)countPtr,
        count,
        (void*)indexBasePtr,
        indexBase,
        (void*)currentPtr,
        current,
        (void*)hoverPtr,
        hover,
        screenX,
        screenY,
        slots);
}

extern "C" void __stdcall LogMaterialsObjectDraw(uintptr_t objectPtr, uintptr_t savedEspAfterFlags)
{
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    if (!base) return;

    uintptr_t ptr410688 = SafeReadPtrTrace(base + 0x410688);
    uintptr_t ptr410a74 = SafeReadPtrTrace(base + 0x410a74);
    uintptr_t ptrMode = SafeReadPtrTrace(base + 0x4101e4);
    uint32_t panel = SafeReadDwordTrace(ptr410688);
    uint32_t extra = SafeReadDwordTrace(ptr410a74);
    uint32_t mode = SafeReadDwordTrace(ptrMode);
    bool materialsPanel = (panel == 0x0c && (extra == 1 || extra == 2));
    bool page2 = g_materialsPage2Active != 0;

    LONG count = InterlockedIncrement(&g_materialsObjectDrawLogCount);
    if (!materialsPanel && !page2 && count > 80) return;
    if (count > 640) return;

    uintptr_t retAddr = SafeReadPtrTrace(savedEspAfterFlags + 4);
    uintptr_t retRva = base && retAddr >= base ? retAddr - base : retAddr;
    uintptr_t container = objectPtr ? SafeReadPtrTrace(objectPtr + 0x60) : 0;
    uintptr_t listHead = container ? SafeReadPtrTrace(container + 0x0c) : 0;
    uintptr_t objectSlot = SafeReadPtrTrace(base + 0x410224);
    uintptr_t currentObject = SafeReadPtrTrace(objectSlot);
    uintptr_t currentContainer = currentObject ? SafeReadPtrTrace(currentObject + 0x60) : 0;
    uintptr_t playerSlot = SafeReadPtrTrace(base + 0x4159bc);
    uintptr_t playerObject = SafeReadPtrTrace(playerSlot);
    uintptr_t playerContainer = playerObject ? SafeReadPtrTrace(playerObject + 0x60) : 0;
    uintptr_t resolvedDraw = SafeReadPtrTrace(base + 0x4186c4);
    char resolvedDesc[192] = {};
    DescribeAddressTrace(resolvedDraw, resolvedDesc, sizeof(resolvedDesc));

    AppendMaterialsTraceLine(
        "object-draw #%ld ret=%p retRva=%08x object=%p role current=%d player=%d objectSlot=%p currentObj=%p current60=%p playerSlot=%p playerObj=%p player60=%p mode=%u modePtr=%p resolvedDraw=%p %s container60=%p head=%p panel=%08x extra=%08x page2=%ld left=%ld projGate=%08x projPanel=%08x projMode=%08x stash=%08x mat=%08x obj=%08x %08x %08x %08x %08x %08x %08x %08x cont=%08x %08x %08x %08x %08x %08x %08x %08x",
        count,
        (void*)retAddr,
        (unsigned)retRva,
        (void*)objectPtr,
        objectPtr == currentObject ? 1 : 0,
        objectPtr == playerObject ? 1 : 0,
        (void*)objectSlot,
        (void*)currentObject,
        (void*)currentContainer,
        (void*)playerSlot,
        (void*)playerObject,
        (void*)playerContainer,
        (unsigned)mode,
        (void*)ptrMode,
        (void*)resolvedDraw,
        resolvedDesc,
        (void*)container,
        (void*)listHead,
        panel,
        extra,
        (long)g_materialsPage2Active,
        (long)g_materialsLeftMaterials,
        SafeReadDwordTrace(base + 0x40edd4),
        SafeReadDwordTrace(SafeReadPtrTrace(base + 0x41023c)),
        SafeReadDwordTrace(ptrMode),
        SafeReadDwordTrace(base + 0x2d4b24),
        SafeReadDwordTrace(base + 0x30dca8),
        SafeReadDwordTrace(objectPtr + 0x00),
        SafeReadDwordTrace(objectPtr + 0x04),
        SafeReadDwordTrace(objectPtr + 0x08),
        SafeReadDwordTrace(objectPtr + 0x0c),
        SafeReadDwordTrace(objectPtr + 0x10),
        SafeReadDwordTrace(objectPtr + 0x14),
        SafeReadDwordTrace(objectPtr + 0x18),
        SafeReadDwordTrace(objectPtr + 0x1c),
        SafeReadDwordTrace(container + 0x00),
        SafeReadDwordTrace(container + 0x04),
        SafeReadDwordTrace(container + 0x08),
        SafeReadDwordTrace(container + 0x0c),
        SafeReadDwordTrace(container + 0x10),
        SafeReadDwordTrace(container + 0x14),
        SafeReadDwordTrace(container + 0x18),
        SafeReadDwordTrace(container + 0x1c));

    if (materialsPanel || page2) {
        uintptr_t node = listHead;
        for (int i = 0; i < 10 && node && !IsBadReadPtr((void*)node, 0x54); ++i) {
            uintptr_t itemData = SafeReadPtrTrace(node + 0x14);
            uintptr_t node2c = SafeReadPtrTrace(node + 0x2c);
            AppendMaterialsTraceLine(
                "object-draw node[%d]=%p itemData=%p item45=%02x node2c=%p dwords=%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x item=%08x %08x %08x %08x %08x %08x %08x %08x node2cdata=%08x %08x %08x %08x %08x %08x %08x %08x %08x",
                i,
                (void*)node,
                (void*)itemData,
                (unsigned)((SafeReadDwordTrace(itemData + 0x44) >> 8) & 0xffu),
                (void*)node2c,
                SafeReadDwordTrace(node + 0x00),
                SafeReadDwordTrace(node + 0x04),
                SafeReadDwordTrace(node + 0x08),
                SafeReadDwordTrace(node + 0x0c),
                SafeReadDwordTrace(node + 0x10),
                SafeReadDwordTrace(node + 0x14),
                SafeReadDwordTrace(node + 0x18),
                SafeReadDwordTrace(node + 0x1c),
                SafeReadDwordTrace(node + 0x20),
                SafeReadDwordTrace(node + 0x24),
                SafeReadDwordTrace(node + 0x28),
                SafeReadDwordTrace(node + 0x2c),
                SafeReadDwordTrace(node + 0x30),
                SafeReadDwordTrace(node + 0x34),
                SafeReadDwordTrace(itemData + 0x40),
                SafeReadDwordTrace(itemData + 0x44),
                SafeReadDwordTrace(itemData + 0x48),
                SafeReadDwordTrace(itemData + 0x4c),
                SafeReadDwordTrace(itemData + 0x50),
                SafeReadDwordTrace(itemData + 0x54),
                SafeReadDwordTrace(itemData + 0x58),
                SafeReadDwordTrace(itemData + 0x5c),
                SafeReadDwordTrace(node2c + 0x00),
                SafeReadDwordTrace(node2c + 0x04),
                SafeReadDwordTrace(node2c + 0x08),
                SafeReadDwordTrace(node2c + 0x0c),
                SafeReadDwordTrace(node2c + 0x10),
                SafeReadDwordTrace(node2c + 0x14),
                SafeReadDwordTrace(node2c + 0x18),
                SafeReadDwordTrace(node2c + 0x1c),
                SafeReadDwordTrace(node2c + 0x20));
            typedef uintptr_t (__stdcall *FnNext)(uintptr_t);
            FnNext next = (FnNext)(base + 0x1d1570);
            uintptr_t nextNode = next(node);
            if (nextNode == node) break;
            node = nextNode;
        }
    }
}

extern "C" int __stdcall HandleMaterialsObjectDraw(uintptr_t objectPtr, uintptr_t savedEspAfterFlags)
{
    LogMaterialsObjectDraw(objectPtr, savedEspAfterFlags);

#if defined(SOE_MATERIALS_PAGE2_NATIVE_RENDERER_PATH) || defined(SOE_MATERIALS_SKIP_PAGE2_MODE0_OBJECT_DRAW)
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    uintptr_t base = (uintptr_t)hProject;
    if (!base) return 0;

    uintptr_t retAddr = SafeReadPtrTrace(savedEspAfterFlags + 4);
    uintptr_t retRva = (retAddr >= base) ? (retAddr - base) : 0;
    uintptr_t ptrPanel = SafeReadPtrTrace(base + 0x410688);
    uintptr_t ptrExtra = SafeReadPtrTrace(base + 0x410a74);
    uintptr_t ptrMode = SafeReadPtrTrace(base + 0x4101e4);
    uintptr_t objectSlot = SafeReadPtrTrace(base + 0x410224);
    uintptr_t currentObject = SafeReadPtrTrace(objectSlot);
#endif

#ifdef SOE_MATERIALS_PAGE2_NATIVE_RENDERER_PATH
    {
        uint32_t objectPage = 0xffffffffu;
        bool page2ObjectActive = IsMaterialsPage2ObjectActive(base, &objectPage);
        bool nativeObjectDraw = (retRva == 0x18bea7 || retRva == 0x18d10d);
        if (page2ObjectActive && nativeObjectDraw && objectPtr == currentObject) {
            LONG n = InterlockedIncrement(&g_materialsPage2NativeRendererSkipLogCount);
            if (n <= 100) {
                AppendMaterialsTraceLine(
                    "native-renderer object-skip #%ld ret=%p retRva=%08x object=%p current=%p container=%p objectPage=%08x mode=%u gate=%08x",
                    (long)n,
                    (void*)retAddr,
                    (unsigned)retRva,
                    (void*)objectPtr,
                    (void*)currentObject,
                    (void*)SafeReadPtrTrace(objectPtr + 0x60),
                    (unsigned)objectPage,
                    (unsigned)SafeReadDwordTrace(ptrMode),
                    (unsigned)SafeReadDwordTrace(base + 0x40edd4));
            }
            return 1;
        }
    }
#endif

#ifdef SOE_MATERIALS_SKIP_PAGE2_MODE0_OBJECT_DRAW
    if (!g_materialsPage2Active) return 0;
    if (retRva == 0x18d10d &&
        SafeReadDwordTrace(ptrPanel) == 0x0c &&
        SafeReadDwordTrace(ptrExtra) == 0x02 &&
        SafeReadDwordTrace(ptrMode) == 0 &&
        objectPtr == currentObject) {
        static volatile LONG skipLogCount = 0;
        LONG n = InterlockedIncrement(&skipLogCount);
        if (n <= 80) {
            AppendMaterialsTraceLine(
                "object-draw page2-mode0-skip #%ld ret=%p retRva=%08x object=%p current=%p container=%p gate=%08x",
                (long)n,
                (void*)retAddr,
                (unsigned)retRva,
                (void*)objectPtr,
                (void*)currentObject,
                (void*)SafeReadPtrTrace(objectPtr + 0x60),
                SafeReadDwordTrace(base + 0x40edd4));
        }
        return 1;
    }
#endif

    return 0;
}

extern "C" int __stdcall HandleMaterialsD2ClientSpriteDraw(uintptr_t savedEsp)
{
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    HMODULE hD2Client = GetModuleHandleA("D2Client.dll");
    uintptr_t base = (uintptr_t)hProject;
    uintptr_t clientBase = (uintptr_t)hD2Client;
    if (!base || !clientBase || !g_materialsPage2Active) return 0;

    uintptr_t retAddr = SafeReadPtrTrace(savedEsp + 0x00);
    uintptr_t retRva = (retAddr >= clientBase) ? (retAddr - clientBase) : 0;
    int32_t x = (int32_t)SafeReadDwordTrace(savedEsp + 0x04);
    int32_t y = (int32_t)SafeReadDwordTrace(savedEsp + 0x08);
    uint32_t panel = SafeReadDwordTrace(SafeReadPtrTrace(base + 0x410688));
    uint32_t extra = SafeReadDwordTrace(SafeReadPtrTrace(base + 0x410a74));
    uint32_t mode = clientBase ? SafeReadDwordTrace(clientBase + 0x11bc48) : 0;
    bool materialsPage2 = (panel == 0x0c && extra == 0x02);
    bool itemIconCaller = (retRva == 0x954e8 || retRva == 0x955bc || retRva == 0x95341);
    bool leftMaterialsArea = (x >= 20 && x <= 590 && y >= 60 && y <= 840);
    bool skip = materialsPage2 && itemIconCaller && leftMaterialsArea;

    LONG n = InterlockedIncrement(skip ? &g_materialsSpriteDrawSkipCount : &g_materialsSpriteDrawLogCount);
    if ((skip && n <= 180) || (!skip && n <= 120 && materialsPage2 && itemIconCaller)) {
        AppendMaterialsTraceLine(
            "sprite-draw %s #%ld ret=%p rva=%08x x=%d y=%d panel=%08x extra=%08x page2=%ld mode=%u globals=%d,%d caller=%d left=%d",
            skip ? "skip-left-page2" : "pass",
            (long)n,
            (void*)retAddr,
            (unsigned)retRva,
            (int)x,
            (int)y,
            panel,
            extra,
            (long)g_materialsPage2Active,
            (unsigned)mode,
            (int32_t)SafeReadDwordTrace(clientBase + 0x11bcf0),
            (int32_t)SafeReadDwordTrace(clientBase + 0x11bcf8),
            itemIconCaller ? 1 : 0,
            leftMaterialsArea ? 1 : 0);
    }

#ifdef SOE_MATERIALS_SKIP_PAGE2_LEFT_SPRITES
    return skip ? 1 : 0;
#else
    return 0;
#endif
}


extern "C" void __attribute__((naked)) SoEMaterialsDescriptorLookupHook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "pushl 20(%esp)\n\t"
        "pushl 24(%esp)\n\t"
        "call _LogMaterialsDescriptorLookup@8\n\t"
        "popal\n\t"
        "pushl %eax\n\t"
        "pushl %ecx\n\t"
        "pushl %edx\n\t"
        "pushl %ecx\n\t"
        "pushl $0x18d4c0\n\t"
        "call _RewriteMaterialsDescriptorLookupKey@12\n\t"
        "movl %eax, %edx\n\t"
        "popl %ecx\n\t"
        "popl %eax\n\t"
        "popfl\n\t"
        "jmp *_g_materialsDescriptorLookupTrampoline\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsDescriptorHasSlotHook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "movl 44(%esp), %eax\n\t"
        "movl 40(%esp), %edx\n\t"
        "pushl %eax\n\t"
        "pushl %edx\n\t"
        "pushl $0x18d520\n\t"
        "call _LogMaterialsDescriptorHelper@12\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsDescriptorHasSlotTrampoline\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsDescriptorTextHook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "movl 44(%esp), %eax\n\t"
        "movl 40(%esp), %edx\n\t"
        "pushl %eax\n\t"
        "pushl %edx\n\t"
        "pushl $0x18d590\n\t"
        "call _LogMaterialsDescriptorHelper@12\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsDescriptorTextTrampoline\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsGridRenderHook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "movl %esp, %esi\n\t"
        "pushl 12(%esi)\n\t"
        "pushl 24(%esi)\n\t"
        "call _LogMaterialsGridRender@8\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsGridRenderTrampoline\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsPageDrawHook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "pushl 12(%esp)\n\t"
        "call _LogMaterialsPageDraw@4\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsPageDrawTrampoline\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsObjectDrawHook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "movl %esp, %esi\n\t"
        "pushl 12(%esi)\n\t"
        "pushl 40(%esi)\n\t"
        "call _HandleMaterialsObjectDraw@8\n\t"
        "test %eax, %eax\n\t"
        "jnz 1f\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsObjectDrawTrampoline\n\t"
        "1:\n\t"
        "popal\n\t"
        "popfl\n\t"
        "ret $4\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsD2ClientSpriteDrawHook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "leal 36(%esp), %eax\n\t"
        "pushl %eax\n\t"
        "call _HandleMaterialsD2ClientSpriteDraw@4\n\t"
        "test %eax, %eax\n\t"
        "jnz 1f\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsD2ClientSpriteDrawTrampoline\n\t"
        "1:\n\t"
        "popal\n\t"
        "popfl\n\t"
        "xorl %eax, %eax\n\t"
        "ret $8\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsTraceE80Hook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "pushl 40(%esp)\n\t"
        "pushl $0x192e80\n\t"
        "call _LogMaterialsTabHit@8\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsTraceE80Trampoline\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsTraceE20Hook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "pushl 40(%esp)\n\t"
        "pushl $0x192e20\n\t"
        "call _LogMaterialsTabHit@8\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsTraceE20Trampoline\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsTraceF70Hook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "pushl 40(%esp)\n\t"
        "pushl $0x192f70\n\t"
        "call _LogMaterialsTabHit@8\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsTraceF70Trampoline\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsTraceDB0Hook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "pushl 40(%esp)\n\t"
        "pushl $0x192db0\n\t"
        "call _LogMaterialsTabHit@8\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsTraceDB0Trampoline\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsTraceEE0Hook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "pushl 40(%esp)\n\t"
        "pushl $0x192ee0\n\t"
        "call _LogMaterialsTabHit@8\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsTraceEE0Trampoline\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsTraceFF0Hook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "pushl 40(%esp)\n\t"
        "pushl $0x192ff0\n\t"
        "call _LogMaterialsTabHit@8\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsTraceFF0Trampoline\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsTrace3060Hook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "pushl 40(%esp)\n\t"
        "pushl $0x193060\n\t"
        "call _LogMaterialsTabHit@8\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsTrace3060Trampoline\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsNativeD560Hook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "movl %esp, %esi\n\t"
        "pushl 12(%esi)\n\t"
        "pushl 16(%esi)\n\t"
        "pushl 20(%esi)\n\t"
        "pushl 24(%esi)\n\t"
        "pushl 28(%esi)\n\t"
        "pushl $0x18d560\n\t"
        "call _LogMaterialsNativeCall@24\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsNativeD560Trampoline\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsNativeE7B0Hook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "movl %esp, %esi\n\t"
        "pushl 12(%esi)\n\t"
        "pushl 16(%esi)\n\t"
        "pushl 20(%esi)\n\t"
        "pushl 24(%esi)\n\t"
        "pushl 28(%esi)\n\t"
        "pushl $0x18e7b0\n\t"
        "call _LogMaterialsNativeCall@24\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsNativeE7B0Trampoline\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsNativeE860Hook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "movl %esp, %esi\n\t"
        "pushl 12(%esi)\n\t"
        "pushl 16(%esi)\n\t"
        "pushl 20(%esi)\n\t"
        "pushl 24(%esi)\n\t"
        "pushl 28(%esi)\n\t"
        "pushl $0x18e860\n\t"
        "call _LogMaterialsNativeCall@24\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsNativeE860Trampoline\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsNativeE8C0Hook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "movl %esp, %esi\n\t"
        "pushl 12(%esi)\n\t"
        "pushl 16(%esi)\n\t"
        "pushl 20(%esi)\n\t"
        "pushl 24(%esi)\n\t"
        "pushl 28(%esi)\n\t"
        "pushl $0x18e8c0\n\t"
        "call _LogMaterialsNativeCall@24\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsNativeE8C0Trampoline\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsNativeE940Hook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "movl %esp, %esi\n\t"
        "pushl 12(%esi)\n\t"
        "pushl 16(%esi)\n\t"
        "pushl 20(%esi)\n\t"
        "pushl 24(%esi)\n\t"
        "pushl 28(%esi)\n\t"
        "pushl $0x18e940\n\t"
        "call _LogMaterialsNativeCall@24\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsNativeE940Trampoline\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsNativeF3A0Hook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "movl %esp, %esi\n\t"
        "pushl 12(%esi)\n\t"
        "pushl 16(%esi)\n\t"
        "pushl 20(%esi)\n\t"
        "pushl 24(%esi)\n\t"
        "pushl 28(%esi)\n\t"
        "pushl $0x18f3a0\n\t"
        "call _LogMaterialsNativeCall@24\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsNativeF3A0Trampoline\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsNativeF5F0Hook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "movl %esp, %esi\n\t"
        "pushl 12(%esi)\n\t"
        "pushl 16(%esi)\n\t"
        "pushl 20(%esi)\n\t"
        "pushl 24(%esi)\n\t"
        "pushl 28(%esi)\n\t"
        "pushl $0x18f5f0\n\t"
        "call _LogMaterialsNativeCall@24\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsNativeF5F0Trampoline\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsStorageB910Hook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "movl %esp, %esi\n\t"
        "pushl 12(%esi)\n\t"
        "pushl 16(%esi)\n\t"
        "pushl 20(%esi)\n\t"
        "pushl 24(%esi)\n\t"
        "pushl 28(%esi)\n\t"
        "pushl $0x23b910\n\t"
        "call _LogMaterialsStorageCall@24\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsStorageB910Trampoline\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsStorageB9E0Hook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "movl %esp, %esi\n\t"
        "pushl 12(%esi)\n\t"
        "pushl 16(%esi)\n\t"
        "pushl 20(%esi)\n\t"
        "pushl 24(%esi)\n\t"
        "pushl 28(%esi)\n\t"
        "pushl $0x23b9e0\n\t"
        "call _LogMaterialsStorageCall@24\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsStorageB9E0Trampoline\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsStorageBAD0Hook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "movl %esp, %esi\n\t"
        "pushl 12(%esi)\n\t"
        "pushl 16(%esi)\n\t"
        "pushl 20(%esi)\n\t"
        "pushl 24(%esi)\n\t"
        "pushl 28(%esi)\n\t"
        "pushl $0x23bad0\n\t"
        "call _LogMaterialsStorageCall@24\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsStorageBAD0Trampoline\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsStorageD000Hook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "movl %esp, %esi\n\t"
        "pushl 12(%esi)\n\t"
        "pushl 16(%esi)\n\t"
        "pushl 20(%esi)\n\t"
        "pushl 24(%esi)\n\t"
        "pushl 28(%esi)\n\t"
        "pushl $0x23d000\n\t"
        "call _LogMaterialsStorageCall@24\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsStorageD000Trampoline\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsStorageD110Hook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "movl %esp, %esi\n\t"
        "pushl 12(%esi)\n\t"
        "pushl 16(%esi)\n\t"
        "pushl 20(%esi)\n\t"
        "pushl 24(%esi)\n\t"
        "pushl 28(%esi)\n\t"
        "pushl $0x23d110\n\t"
        "call _LogMaterialsStorageCall@24\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsStorageD110Trampoline\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsStorageD1D0Hook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "movl %esp, %esi\n\t"
        "pushl 12(%esi)\n\t"
        "pushl 16(%esi)\n\t"
        "pushl 20(%esi)\n\t"
        "pushl 24(%esi)\n\t"
        "pushl 28(%esi)\n\t"
        "pushl $0x23d1d0\n\t"
        "call _LogMaterialsStorageCall@24\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsStorageD1D0Trampoline\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsMsg1450Hook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "leal 36(%esp), %edx\n\t"
        "pushl %edx\n\t"
        "pushl %ecx\n\t"
        "call _HandleMaterialsMsg1450Call@8\n\t"
        "test %eax, %eax\n\t"
        "jnz 1f\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsMsg1450Trampoline\n\t"
        "1:\n\t"
        "popal\n\t"
        "popfl\n\t"
        "xorl %eax, %eax\n\t"
        "ret\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsMsg1560Hook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "leal 36(%esp), %edx\n\t"
        "pushl %edx\n\t"
        "pushl %ecx\n\t"
        "pushl $0x231560\n\t"
        "call _LogMaterialsMessageCall@12\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsMsg1560Trampoline\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsTraceUiHook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "pushl 40(%esp)\n\t"
        "pushl 36(%esp)\n\t"
        "call _LogMaterialsUiRefresh@8\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsTraceUiTrampoline\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsTraceTransitionHook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "leal 36(%esp), %eax\n\t"
        "pushl %eax\n\t"
        "call _HandleMaterialsTransition@4\n\t"
        "test %eax, %eax\n\t"
        "jnz 1f\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsTraceTransitionTrampoline\n\t"
        "1:\n\t"
        "popal\n\t"
        "popfl\n\t"
        "ret $0x14\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsActiveClickHook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "call _HandleMaterialsActiveClickEarly@0\n\t"
        "test %eax, %eax\n\t"
        "jnz 1f\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsActiveClickTrampoline\n\t"
        "1:\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsActiveClickBlockedReturn\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsHoverLookupHook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "pushl 24(%esp)\n\t"
        "call _ShouldBlockMaterialsHover@4\n\t"
        "test %eax, %eax\n\t"
        "jnz 1f\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsHoverLookupTrampoline\n\t"
        "1:\n\t"
        "popal\n\t"
        "popfl\n\t"
        "xor %eax, %eax\n\t"
        "ret $8\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsTraceDrawHook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "call _ShouldSkipMaterialsDraw@0\n\t"
        "test %eax, %eax\n\t"
        "jnz 1f\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_materialsTraceDrawTrampoline\n\t"
        "1:\n\t"
        "popal\n\t"
        "popfl\n\t"
        "ret\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsContentDrawCallHook()
{
    __asm__ __volatile__(
        "call _ShouldSkipMaterialsDraw@0\n\t"
        "pushl %eax\n\t"
        "pushl %eax\n\t"
        "pushl 12(%esp)\n\t"
        "call _LogMaterialsContentDrawArg@8\n\t"
        "popl %eax\n\t"
        "test %eax, %eax\n\t"
        "jnz 1f\n\t"
        "pushl 4(%esp)\n\t"
        "call *_g_materialsContentDrawTarget\n\t"
        "call _DrawMaterialsPage2ButtonOverlay@0\n\t"
        "ret $4\n\t"
        "1:\n\t"
        "call _DrawMaterialsPage2ButtonOverlay@0\n\t"
        "ret $4\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsD2ClientDrawEntryHook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "leal 36(%esp), %eax\n\t"
        "pushl %eax\n\t"
        "call _LogMaterialsD2ClientDrawEntry@4\n\t"
        "leal 36(%esp), %eax\n\t"
        "pushl %eax\n\t"
        "call _PrepareMaterialsD2ClientDrawEntryWrap@4\n\t"
        "test %eax, %eax\n\t"
        "jnz 1f\n\t"
        "popal\n\t"
        "popfl\n\t"
        "pushl 4(%esp)\n\t"
        "call *_g_materialsD2ClientDrawEntryTrampoline\n\t"
        "pushl %eax\n\t"
        "call _DrawMaterialsPage2ButtonOverlay@0\n\t"
        "popl %eax\n\t"
        "ret $4\n\t"
        "1:\n\t"
        "popal\n\t"
        "popfl\n\t"
        "pushl 4(%esp)\n\t"
        "call *_g_materialsD2ClientDrawEntryTrampoline\n\t"
        "pushl %eax\n\t"
        "call _RestoreMaterialsD2ClientDrawEntryWrap@0\n\t"
        "call _DrawMaterialsPage2ButtonOverlay@0\n\t"
        "popl %eax\n\t"
        "ret $4\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEMaterialsPage2CloneNativeButtonHook()
{
    __asm__ __volatile__(
        "pushal\n\t"
        "movl _g_materialsPage2CloneDrawSpriteFn, %ebx\n\t"
        "testl %ebx, %ebx\n\t"
        "jz 1f\n\t"
        "movl 8(%esp), %eax\n\t"
        "leal -0x2e0(%eax), %eax\n\t"
        "movl 4(%esp), %ecx\n\t"
#if defined(SOE_MATERIALS_PAGE2_ORIGINAL_MATERIAL_SLOT)
        "addl $0x1f, %ecx\n\t"
#else
        "addl $0x27, %ecx\n\t"
#endif
        "movl 0(%esp), %edx\n\t"
        "pushl $0\n\t"
        "pushl $5\n\t"
        "pushl $0xffffffff\n\t"
        "pushl %edx\n\t"
        "pushl %ecx\n\t"
        "pushl %eax\n\t"
        "call *%ebx\n\t"
        "1:\n\t"
        "popal\n\t"
        "pushl %eax\n\t"
        "movl _g_materialsPage2CloneHoverFlagAddr, %eax\n\t"
        "cmpl $0, (%eax)\n\t"
        "popl %eax\n\t"
        "jmp *_g_materialsPage2CloneReturn\n\t"
    );
}

static void InstallMaterialsTraceHooks()
{
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    if (!hProject) {
        AppendMaterialsTraceLine("materials trace install failed: ProjectDiablo.dll not loaded");
        return;
    }

    uintptr_t base = (uintptr_t)hProject;
    HMODULE hD2Client = GetModuleHandleA("D2Client.dll");
    uintptr_t d2ClientBase = (uintptr_t)hD2Client;
#if defined(SOE_MATERIALS_SHIFT_NATIVE_MATERIAL_BUTTON)
    PatchProjectByte(base + 0x18bfa3, 0x0f, 0xf0, "materials-button-draw-x-left");
    PatchProjectByte(base + 0x1b350f, 0x0f, 0xf0, "materials-button-hit-x-left");
#endif
#if defined(SOE_MATERIALS_DUPLICATE_NATIVE_MATERIAL_BUTTON)
    g_materialsPage2ClonePatchAddr = base + 0x18bfc0;
    g_materialsPage2CloneReturn = g_materialsPage2ClonePatchAddr + sizeof(g_materialsPage2CloneOriginal);
    g_materialsPage2CloneDrawSpriteFn = base + 0x1e6c20;
    g_materialsPage2CloneHoverFlagAddr = base + 0x30de54;
#endif
    g_materialsTraceE20PatchAddr = base + 0x192e20;
    g_materialsTraceE80PatchAddr = base + 0x192e80;
    g_materialsTraceF70PatchAddr = base + 0x192f70;
    g_materialsTraceDB0PatchAddr = base + 0x192db0;
    g_materialsTraceEE0PatchAddr = base + 0x192ee0;
    g_materialsTraceFF0PatchAddr = base + 0x192ff0;
    g_materialsTrace3060PatchAddr = base + 0x193060;
    g_materialsNativeD560PatchAddr = base + 0x18d560;
    g_materialsNativeE7B0PatchAddr = base + 0x18e7b0;
    g_materialsNativeE860PatchAddr = base + 0x18e860;
    g_materialsNativeE8C0PatchAddr = base + 0x18e8c0;
    g_materialsNativeE940PatchAddr = base + 0x18e940;
    g_materialsNativeF3A0PatchAddr = base + 0x18f3a0;
    g_materialsNativeF5F0PatchAddr = base + 0x18f5f0;
    g_materialsStorageB910PatchAddr = base + 0x23b910;
    g_materialsStorageB9E0PatchAddr = base + 0x23b9e0;
    g_materialsStorageBAD0PatchAddr = base + 0x23bad0;
    g_materialsStorageD000PatchAddr = base + 0x23d000;
    g_materialsStorageD110PatchAddr = base + 0x23d110;
    g_materialsStorageD1D0PatchAddr = base + 0x23d1d0;
    g_materialsTraceUiPatchAddr = base + 0x1f9a90;
    g_materialsTraceTransitionPatchAddr = base + 0x23f7d0;
    g_materialsTraceDrawPatchAddr = base + 0x18bbb0;
    g_materialsGridRenderPatchAddr = base + 0x18ba40;
    g_materialsPageDrawPatchAddr = base + 0x18af70;
    g_materialsContentDrawCallAddr = base + 0x18c35e;
    g_materialsContentDrawTarget = base + 0x1e9320;
    g_materialsObjectDrawPatchAddr = base + 0x1e9320;
    g_materialsD2ClientDrawEntryPatchAddr = d2ClientBase ? d2ClientBase + 0x958a0 : 0;
    g_materialsD2ClientSpriteDrawPatchAddr = d2ClientBase ? d2ClientBase + 0x6a5a0 : 0;
    g_materialsActiveClickPatchAddr = base + 0x193b7d;
    g_materialsHoverLookupPatchAddr = base + 0x18f5f0;
    g_materialsDescriptorLookupPatchAddr = base + 0x18d4c0;
    g_materialsDescriptorHasSlotPatchAddr = base + 0x18d520;
    g_materialsDescriptorTextPatchAddr = base + 0x18d590;
    g_materialsMsg1450PatchAddr = base + 0x231450;
    g_materialsMsg1560PatchAddr = base + 0x231560;
    TryInstallMaterialsIteratorHook();
#ifdef SOE_MATERIALS_PATCH_GATE_SKIP
    InstallMaterialsGateSkipPatch(base);
#endif
    g_materialsActiveClickBlockedReturn = base + 0x193b96;

    g_materialsTraceE20Hooked = PatchMaterialsJump(
        g_materialsTraceE20PatchAddr,
        (void*)&SoEMaterialsTraceE20Hook,
        g_materialsTraceE20Original,
        sizeof(g_materialsTraceE20Original),
        &g_materialsTraceE20Trampoline);

    g_materialsTraceE80Hooked = PatchMaterialsJump(
        g_materialsTraceE80PatchAddr,
        (void*)&SoEMaterialsTraceE80Hook,
        g_materialsTraceE80Original,
        sizeof(g_materialsTraceE80Original),
        &g_materialsTraceE80Trampoline);

    g_materialsTraceF70Hooked = PatchMaterialsJump(
        g_materialsTraceF70PatchAddr,
        (void*)&SoEMaterialsTraceF70Hook,
        g_materialsTraceF70Original,
        sizeof(g_materialsTraceF70Original),
        &g_materialsTraceF70Trampoline);

    g_materialsTraceDB0Hooked = PatchMaterialsJump(
        g_materialsTraceDB0PatchAddr,
        (void*)&SoEMaterialsTraceDB0Hook,
        g_materialsTraceDB0Original,
        sizeof(g_materialsTraceDB0Original),
        &g_materialsTraceDB0Trampoline);

    g_materialsTraceEE0Hooked = PatchMaterialsJump(
        g_materialsTraceEE0PatchAddr,
        (void*)&SoEMaterialsTraceEE0Hook,
        g_materialsTraceEE0Original,
        sizeof(g_materialsTraceEE0Original),
        &g_materialsTraceEE0Trampoline);

    g_materialsTraceFF0Hooked = PatchMaterialsJump(
        g_materialsTraceFF0PatchAddr,
        (void*)&SoEMaterialsTraceFF0Hook,
        g_materialsTraceFF0Original,
        sizeof(g_materialsTraceFF0Original),
        &g_materialsTraceFF0Trampoline);

    g_materialsTrace3060Hooked = PatchMaterialsJump(
        g_materialsTrace3060PatchAddr,
        (void*)&SoEMaterialsTrace3060Hook,
        g_materialsTrace3060Original,
        sizeof(g_materialsTrace3060Original),
        &g_materialsTrace3060Trampoline);

    g_materialsNativeD560Hooked = PatchMaterialsJump(
        g_materialsNativeD560PatchAddr,
        (void*)&SoEMaterialsNativeD560Hook,
        g_materialsNativeD560Original,
        6,
        &g_materialsNativeD560Trampoline);

    g_materialsNativeE7B0Hooked = PatchMaterialsJump(
        g_materialsNativeE7B0PatchAddr,
        (void*)&SoEMaterialsNativeE7B0Hook,
        g_materialsNativeE7B0Original,
        5,
        &g_materialsNativeE7B0Trampoline);

    g_materialsNativeE860Hooked = PatchMaterialsJump(
        g_materialsNativeE860PatchAddr,
        (void*)&SoEMaterialsNativeE860Hook,
        g_materialsNativeE860Original,
        8,
        &g_materialsNativeE860Trampoline);

    g_materialsNativeE8C0Hooked = PatchMaterialsJump(
        g_materialsNativeE8C0PatchAddr,
        (void*)&SoEMaterialsNativeE8C0Hook,
        g_materialsNativeE8C0Original,
        9,
        &g_materialsNativeE8C0Trampoline);

    g_materialsNativeE940Hooked = PatchMaterialsJump(
        g_materialsNativeE940PatchAddr,
        (void*)&SoEMaterialsNativeE940Hook,
        g_materialsNativeE940Original,
        8,
        &g_materialsNativeE940Trampoline);

    g_materialsNativeF3A0Hooked = PatchMaterialsJump(
        g_materialsNativeF3A0PatchAddr,
        (void*)&SoEMaterialsNativeF3A0Hook,
        g_materialsNativeF3A0Original,
        9,
        &g_materialsNativeF3A0Trampoline);

    g_materialsNativeF5F0Hooked = PatchMaterialsJump(
        g_materialsNativeF5F0PatchAddr,
        (void*)&SoEMaterialsNativeF5F0Hook,
        g_materialsNativeF5F0Original,
        5,
        &g_materialsNativeF5F0Trampoline);

    g_materialsDescriptorLookupHooked = PatchMaterialsJump(
        g_materialsDescriptorLookupPatchAddr,
        (void*)&SoEMaterialsDescriptorLookupHook,
        g_materialsDescriptorLookupOriginal,
        sizeof(g_materialsDescriptorLookupOriginal),
        &g_materialsDescriptorLookupTrampoline);

    g_materialsDescriptorHasSlotHooked = PatchMaterialsJump(
        g_materialsDescriptorHasSlotPatchAddr,
        (void*)&SoEMaterialsDescriptorHasSlotHook,
        g_materialsDescriptorHasSlotOriginal,
        sizeof(g_materialsDescriptorHasSlotOriginal),
        &g_materialsDescriptorHasSlotTrampoline);

    g_materialsDescriptorTextHooked = PatchMaterialsJump(
        g_materialsDescriptorTextPatchAddr,
        (void*)&SoEMaterialsDescriptorTextHook,
        g_materialsDescriptorTextOriginal,
        sizeof(g_materialsDescriptorTextOriginal),
        &g_materialsDescriptorTextTrampoline);

    g_materialsStorageB910Hooked = PatchMaterialsJump(
        g_materialsStorageB910PatchAddr,
        (void*)&SoEMaterialsStorageB910Hook,
        g_materialsStorageB910Original,
        6,
        &g_materialsStorageB910Trampoline);

    g_materialsStorageB9E0Hooked = PatchMaterialsJump(
        g_materialsStorageB9E0PatchAddr,
        (void*)&SoEMaterialsStorageB9E0Hook,
        g_materialsStorageB9E0Original,
        6,
        &g_materialsStorageB9E0Trampoline);

    g_materialsStorageBAD0Hooked = PatchMaterialsJump(
        g_materialsStorageBAD0PatchAddr,
        (void*)&SoEMaterialsStorageBAD0Hook,
        g_materialsStorageBAD0Original,
        6,
        &g_materialsStorageBAD0Trampoline);

    g_materialsStorageD000Hooked = PatchMaterialsJump(
        g_materialsStorageD000PatchAddr,
        (void*)&SoEMaterialsStorageD000Hook,
        g_materialsStorageD000Original,
        6,
        &g_materialsStorageD000Trampoline);

    g_materialsStorageD110Hooked = PatchMaterialsJump(
        g_materialsStorageD110PatchAddr,
        (void*)&SoEMaterialsStorageD110Hook,
        g_materialsStorageD110Original,
        6,
        &g_materialsStorageD110Trampoline);

    g_materialsStorageD1D0Hooked = PatchMaterialsJump(
        g_materialsStorageD1D0PatchAddr,
        (void*)&SoEMaterialsStorageD1D0Hook,
        g_materialsStorageD1D0Original,
        5,
        &g_materialsStorageD1D0Trampoline);

    g_materialsMsg1450Hooked = PatchMaterialsJump(
        g_materialsMsg1450PatchAddr,
        (void*)&SoEMaterialsMsg1450Hook,
        g_materialsMsg1450Original,
        9,
        &g_materialsMsg1450Trampoline);

    g_materialsMsg1560Hooked = PatchMaterialsJump(
        g_materialsMsg1560PatchAddr,
        (void*)&SoEMaterialsMsg1560Hook,
        g_materialsMsg1560Original,
        9,
        &g_materialsMsg1560Trampoline);

    g_materialsTraceUiHooked = PatchMaterialsJump(
        g_materialsTraceUiPatchAddr,
        (void*)&SoEMaterialsTraceUiHook,
        g_materialsTraceUiOriginal,
        sizeof(g_materialsTraceUiOriginal),
        &g_materialsTraceUiTrampoline);

    g_materialsTraceTransitionHooked = PatchMaterialsJump(
        g_materialsTraceTransitionPatchAddr,
        (void*)&SoEMaterialsTraceTransitionHook,
        g_materialsTraceTransitionOriginal,
        sizeof(g_materialsTraceTransitionOriginal),
        &g_materialsTraceTransitionTrampoline);

    g_materialsTraceDrawHooked = PatchMaterialsJump(
        g_materialsTraceDrawPatchAddr,
        (void*)&SoEMaterialsTraceDrawHook,
        g_materialsTraceDrawOriginal,
        sizeof(g_materialsTraceDrawOriginal),
        &g_materialsTraceDrawTrampoline);

    g_materialsGridRenderHooked = PatchMaterialsJump(
        g_materialsGridRenderPatchAddr,
        (void*)&SoEMaterialsGridRenderHook,
        g_materialsGridRenderOriginal,
        sizeof(g_materialsGridRenderOriginal),
        &g_materialsGridRenderTrampoline);

    g_materialsPageDrawHooked = PatchMaterialsJump(
        g_materialsPageDrawPatchAddr,
        (void*)&SoEMaterialsPageDrawHook,
        g_materialsPageDrawOriginal,
        sizeof(g_materialsPageDrawOriginal),
        &g_materialsPageDrawTrampoline);

    g_materialsContentDrawCallHooked = PatchMaterialsCall(
        g_materialsContentDrawCallAddr,
        (void*)&SoEMaterialsContentDrawCallHook,
        g_materialsContentDrawCallOriginal);

    g_materialsObjectDrawHooked = PatchMaterialsJump(
        g_materialsObjectDrawPatchAddr,
        (void*)&SoEMaterialsObjectDrawHook,
        g_materialsObjectDrawOriginal,
        sizeof(g_materialsObjectDrawOriginal),
        &g_materialsObjectDrawTrampoline);

    g_materialsD2ClientDrawEntryHooked = PatchMaterialsJump(
        g_materialsD2ClientDrawEntryPatchAddr,
        (void*)&SoEMaterialsD2ClientDrawEntryHook,
        g_materialsD2ClientDrawEntryOriginal,
        sizeof(g_materialsD2ClientDrawEntryOriginal),
        &g_materialsD2ClientDrawEntryTrampoline);

#if defined(SOE_MATERIALS_DUPLICATE_NATIVE_MATERIAL_BUTTON)
    g_materialsPage2CloneHooked = PatchMaterialsJump(
        g_materialsPage2ClonePatchAddr,
        (void*)&SoEMaterialsPage2CloneNativeButtonHook,
        g_materialsPage2CloneOriginal,
        sizeof(g_materialsPage2CloneOriginal),
        &g_materialsPage2CloneTrampoline);
#endif

#ifdef SOE_MATERIALS_SKIP_PAGE2_LEFT_SPRITES
    g_materialsD2ClientSpriteDrawHooked = PatchMaterialsJump(
        g_materialsD2ClientSpriteDrawPatchAddr,
        (void*)&SoEMaterialsD2ClientSpriteDrawHook,
        g_materialsD2ClientSpriteDrawOriginal,
        sizeof(g_materialsD2ClientSpriteDrawOriginal),
        &g_materialsD2ClientSpriteDrawTrampoline);
#endif

    g_materialsActiveClickHooked = PatchMaterialsJump(
        g_materialsActiveClickPatchAddr,
        (void*)&SoEMaterialsActiveClickHook,
        g_materialsActiveClickOriginal,
        sizeof(g_materialsActiveClickOriginal),
        &g_materialsActiveClickTrampoline);

    g_materialsHoverLookupHooked = false;

    AppendMaterialsTraceLine(
        "materials trace install base=%p passive=%d db0=%p ok=%d e20=%p ok=%d e80=%p ok=%d ee0=%p ok=%d f70=%p ok=%d ff0=%p ok=%d 3060=%p ok=%d ui=%p ok=%d transition=%p ok=%d draw=%p ok=%d grid=%p ok=%d pageDraw=%p ok=%d contentCall=%p ok=%d objectDraw=%p ok=%d d2clientDraw=%p ok=%d d2clientSprite=%p ok=%d activeClick=%p ok=%d hoverLookup=%p ok=%d iter11139Slot=%p orig=%p ok=%d",
        (void*)base,
        kMaterialsPassiveTrace ? 1 : 0,
        (void*)g_materialsTraceDB0PatchAddr,
        g_materialsTraceDB0Hooked ? 1 : 0,
        (void*)g_materialsTraceE20PatchAddr,
        g_materialsTraceE20Hooked ? 1 : 0,
        (void*)g_materialsTraceE80PatchAddr,
        g_materialsTraceE80Hooked ? 1 : 0,
        (void*)g_materialsTraceEE0PatchAddr,
        g_materialsTraceEE0Hooked ? 1 : 0,
        (void*)g_materialsTraceF70PatchAddr,
        g_materialsTraceF70Hooked ? 1 : 0,
        (void*)g_materialsTraceFF0PatchAddr,
        g_materialsTraceFF0Hooked ? 1 : 0,
        (void*)g_materialsTrace3060PatchAddr,
        g_materialsTrace3060Hooked ? 1 : 0,
        (void*)g_materialsTraceUiPatchAddr,
        g_materialsTraceUiHooked ? 1 : 0,
        (void*)g_materialsTraceTransitionPatchAddr,
        g_materialsTraceTransitionHooked ? 1 : 0,
        (void*)g_materialsTraceDrawPatchAddr,
        g_materialsTraceDrawHooked ? 1 : 0,
        (void*)g_materialsGridRenderPatchAddr,
        g_materialsGridRenderHooked ? 1 : 0,
        (void*)g_materialsPageDrawPatchAddr,
        g_materialsPageDrawHooked ? 1 : 0,
        (void*)g_materialsContentDrawCallAddr,
        g_materialsContentDrawCallHooked ? 1 : 0,
        (void*)g_materialsObjectDrawPatchAddr,
        g_materialsObjectDrawHooked ? 1 : 0,
        (void*)g_materialsD2ClientDrawEntryPatchAddr,
        g_materialsD2ClientDrawEntryHooked ? 1 : 0,
        (void*)g_materialsD2ClientSpriteDrawPatchAddr,
        g_materialsD2ClientSpriteDrawHooked ? 1 : 0,
        (void*)g_materialsActiveClickPatchAddr,
        g_materialsActiveClickHooked ? 1 : 0,
        (void*)g_materialsHoverLookupPatchAddr,
        g_materialsHoverLookupHooked ? 1 : 0,
        (void*)g_materialsD2Common11139Slot,
        (void*)g_materialsOrigD2Common11139,
        g_materialsD2Common11139Hooked ? 1 : 0);

    AppendMaterialsTraceLine(
        "materials descriptor lookup hook d4c0=%p ok=%d d520=%p ok=%d d590=%p ok=%d",
        (void*)g_materialsDescriptorLookupPatchAddr,
        g_materialsDescriptorLookupHooked ? 1 : 0,
        (void*)g_materialsDescriptorHasSlotPatchAddr,
        g_materialsDescriptorHasSlotHooked ? 1 : 0,
        (void*)g_materialsDescriptorTextPatchAddr,
        g_materialsDescriptorTextHooked ? 1 : 0);

    AppendMaterialsTraceLine(
        "materials native hooks d560=%p ok=%d e7b0=%p ok=%d e860=%p ok=%d e8c0=%p ok=%d e940=%p ok=%d f3a0=%p ok=%d f5f0=%p ok=%d",
        (void*)g_materialsNativeD560PatchAddr,
        g_materialsNativeD560Hooked ? 1 : 0,
        (void*)g_materialsNativeE7B0PatchAddr,
        g_materialsNativeE7B0Hooked ? 1 : 0,
        (void*)g_materialsNativeE860PatchAddr,
        g_materialsNativeE860Hooked ? 1 : 0,
        (void*)g_materialsNativeE8C0PatchAddr,
        g_materialsNativeE8C0Hooked ? 1 : 0,
        (void*)g_materialsNativeE940PatchAddr,
        g_materialsNativeE940Hooked ? 1 : 0,
        (void*)g_materialsNativeF3A0PatchAddr,
        g_materialsNativeF3A0Hooked ? 1 : 0,
        (void*)g_materialsNativeF5F0PatchAddr,
        g_materialsNativeF5F0Hooked ? 1 : 0);

    AppendMaterialsTraceLine(
        "materials storage hooks b910=%p ok=%d b9e0=%p ok=%d bad0=%p ok=%d d000=%p ok=%d d110=%p ok=%d d1d0=%p ok=%d",
        (void*)g_materialsStorageB910PatchAddr,
        g_materialsStorageB910Hooked ? 1 : 0,
        (void*)g_materialsStorageB9E0PatchAddr,
        g_materialsStorageB9E0Hooked ? 1 : 0,
        (void*)g_materialsStorageBAD0PatchAddr,
        g_materialsStorageBAD0Hooked ? 1 : 0,
        (void*)g_materialsStorageD000PatchAddr,
        g_materialsStorageD000Hooked ? 1 : 0,
        (void*)g_materialsStorageD110PatchAddr,
        g_materialsStorageD110Hooked ? 1 : 0,
        (void*)g_materialsStorageD1D0PatchAddr,
        g_materialsStorageD1D0Hooked ? 1 : 0);

    AppendMaterialsTraceLine(
        "materials message hooks 1450=%p ok=%d 1560=%p ok=%d",
        (void*)g_materialsMsg1450PatchAddr,
        g_materialsMsg1450Hooked ? 1 : 0,
        (void*)g_materialsMsg1560PatchAddr,
        g_materialsMsg1560Hooked ? 1 : 0);
}

static bool MaterialsTraceHooksInstalled()
{
    return g_materialsTraceE20Hooked
        || g_materialsTraceE80Hooked
        || g_materialsTraceDB0Hooked
        || g_materialsTraceEE0Hooked
        || g_materialsTraceFF0Hooked
        || g_materialsTrace3060Hooked
        || g_materialsTraceUiHooked
        || g_materialsTraceDrawHooked
        || g_materialsGridRenderHooked
        || g_materialsPageDrawHooked
        || g_materialsContentDrawCallHooked
        || g_materialsObjectDrawHooked
        || g_materialsActiveClickHooked
        || g_materialsMsg1450Hooked
        || g_materialsMsg1560Hooked;
}

static DWORD WINAPI MaterialsTraceRetryThread(LPVOID)
{
    AppendMaterialsTraceLine("materials trace retry thread start");
    for (int i = 0; i < 120; ++i) {
        if (MaterialsTraceHooksInstalled()) {
            AppendMaterialsTraceLine("materials trace retry thread done: hooks already installed at attempt=%d", i);
            return 0;
        }
        if (GetModuleHandleA("ProjectDiablo.dll")) {
            AppendMaterialsTraceLine("materials trace retry installing at attempt=%d", i);
            InstallMaterialsTraceHooks();
            if (MaterialsTraceHooksInstalled()) {
                AppendMaterialsTraceLine("materials trace retry installed at attempt=%d", i);
                return 0;
            }
        }
        Sleep(250);
    }
    AppendMaterialsTraceLine("materials trace retry timed out: hooks not installed");
    return 0;
}

static void RemoveMaterialsTraceHooks()
{
#ifdef SOE_MATERIALS_PATCH_GATE_SKIP
    RestoreMaterialsGateSkipPatch();
#endif
    if (g_materialsD2Common11139Hooked && g_materialsD2Common11139Slot && g_materialsOrigD2Common11139) {
        DWORD oldProt = 0;
        if (VirtualProtect(g_materialsD2Common11139Slot, sizeof(uintptr_t), PAGE_READWRITE, &oldProt)) {
            *g_materialsD2Common11139Slot = g_materialsOrigD2Common11139;
            VirtualProtect(g_materialsD2Common11139Slot, sizeof(uintptr_t), oldProt, &oldProt);
        }
        g_materialsD2Common11139Hooked = false;
    }
    if (g_materialsActiveClickHooked) {
        RestoreMaterialsJump(g_materialsActiveClickPatchAddr, g_materialsActiveClickOriginal, sizeof(g_materialsActiveClickOriginal), &g_materialsActiveClickTrampoline);
        g_materialsActiveClickHooked = false;
    }
    if (g_materialsHoverLookupHooked) {
        RestoreMaterialsJump(g_materialsHoverLookupPatchAddr, g_materialsHoverLookupOriginal, sizeof(g_materialsHoverLookupOriginal), &g_materialsHoverLookupTrampoline);
        g_materialsHoverLookupHooked = false;
    }
    if (g_materialsDescriptorLookupHooked) {
        RestoreMaterialsJump(g_materialsDescriptorLookupPatchAddr, g_materialsDescriptorLookupOriginal, sizeof(g_materialsDescriptorLookupOriginal), &g_materialsDescriptorLookupTrampoline);
        g_materialsDescriptorLookupHooked = false;
    }
    if (g_materialsDescriptorHasSlotHooked) {
        RestoreMaterialsJump(g_materialsDescriptorHasSlotPatchAddr, g_materialsDescriptorHasSlotOriginal, sizeof(g_materialsDescriptorHasSlotOriginal), &g_materialsDescriptorHasSlotTrampoline);
        g_materialsDescriptorHasSlotHooked = false;
    }
    if (g_materialsDescriptorTextHooked) {
        RestoreMaterialsJump(g_materialsDescriptorTextPatchAddr, g_materialsDescriptorTextOriginal, sizeof(g_materialsDescriptorTextOriginal), &g_materialsDescriptorTextTrampoline);
        g_materialsDescriptorTextHooked = false;
    }
    if (g_materialsTraceE80Hooked) {
        RestoreMaterialsJump(g_materialsTraceE80PatchAddr, g_materialsTraceE80Original, sizeof(g_materialsTraceE80Original), &g_materialsTraceE80Trampoline);
        g_materialsTraceE80Hooked = false;
    }
    if (g_materialsTraceE20Hooked) {
        RestoreMaterialsJump(g_materialsTraceE20PatchAddr, g_materialsTraceE20Original, sizeof(g_materialsTraceE20Original), &g_materialsTraceE20Trampoline);
        g_materialsTraceE20Hooked = false;
    }
    if (g_materialsTraceF70Hooked) {
        RestoreMaterialsJump(g_materialsTraceF70PatchAddr, g_materialsTraceF70Original, sizeof(g_materialsTraceF70Original), &g_materialsTraceF70Trampoline);
        g_materialsTraceF70Hooked = false;
    }
    if (g_materialsTraceDB0Hooked) {
        RestoreMaterialsJump(g_materialsTraceDB0PatchAddr, g_materialsTraceDB0Original, sizeof(g_materialsTraceDB0Original), &g_materialsTraceDB0Trampoline);
        g_materialsTraceDB0Hooked = false;
    }
    if (g_materialsTraceEE0Hooked) {
        RestoreMaterialsJump(g_materialsTraceEE0PatchAddr, g_materialsTraceEE0Original, sizeof(g_materialsTraceEE0Original), &g_materialsTraceEE0Trampoline);
        g_materialsTraceEE0Hooked = false;
    }
    if (g_materialsTraceFF0Hooked) {
        RestoreMaterialsJump(g_materialsTraceFF0PatchAddr, g_materialsTraceFF0Original, sizeof(g_materialsTraceFF0Original), &g_materialsTraceFF0Trampoline);
        g_materialsTraceFF0Hooked = false;
    }
    if (g_materialsTrace3060Hooked) {
        RestoreMaterialsJump(g_materialsTrace3060PatchAddr, g_materialsTrace3060Original, sizeof(g_materialsTrace3060Original), &g_materialsTrace3060Trampoline);
        g_materialsTrace3060Hooked = false;
    }
    if (g_materialsNativeD560Hooked) {
        RestoreMaterialsJump(g_materialsNativeD560PatchAddr, g_materialsNativeD560Original, 6, &g_materialsNativeD560Trampoline);
        g_materialsNativeD560Hooked = false;
    }
    if (g_materialsNativeE7B0Hooked) {
        RestoreMaterialsJump(g_materialsNativeE7B0PatchAddr, g_materialsNativeE7B0Original, 5, &g_materialsNativeE7B0Trampoline);
        g_materialsNativeE7B0Hooked = false;
    }
    if (g_materialsNativeE860Hooked) {
        RestoreMaterialsJump(g_materialsNativeE860PatchAddr, g_materialsNativeE860Original, 8, &g_materialsNativeE860Trampoline);
        g_materialsNativeE860Hooked = false;
    }
    if (g_materialsNativeE8C0Hooked) {
        RestoreMaterialsJump(g_materialsNativeE8C0PatchAddr, g_materialsNativeE8C0Original, 9, &g_materialsNativeE8C0Trampoline);
        g_materialsNativeE8C0Hooked = false;
    }
    if (g_materialsNativeE940Hooked) {
        RestoreMaterialsJump(g_materialsNativeE940PatchAddr, g_materialsNativeE940Original, 8, &g_materialsNativeE940Trampoline);
        g_materialsNativeE940Hooked = false;
    }
    if (g_materialsNativeF3A0Hooked) {
        RestoreMaterialsJump(g_materialsNativeF3A0PatchAddr, g_materialsNativeF3A0Original, 9, &g_materialsNativeF3A0Trampoline);
        g_materialsNativeF3A0Hooked = false;
    }
    if (g_materialsNativeF5F0Hooked) {
        RestoreMaterialsJump(g_materialsNativeF5F0PatchAddr, g_materialsNativeF5F0Original, 5, &g_materialsNativeF5F0Trampoline);
        g_materialsNativeF5F0Hooked = false;
    }
    if (g_materialsStorageB910Hooked) {
        RestoreMaterialsJump(g_materialsStorageB910PatchAddr, g_materialsStorageB910Original, 6, &g_materialsStorageB910Trampoline);
        g_materialsStorageB910Hooked = false;
    }
    if (g_materialsStorageB9E0Hooked) {
        RestoreMaterialsJump(g_materialsStorageB9E0PatchAddr, g_materialsStorageB9E0Original, 6, &g_materialsStorageB9E0Trampoline);
        g_materialsStorageB9E0Hooked = false;
    }
    if (g_materialsStorageBAD0Hooked) {
        RestoreMaterialsJump(g_materialsStorageBAD0PatchAddr, g_materialsStorageBAD0Original, 6, &g_materialsStorageBAD0Trampoline);
        g_materialsStorageBAD0Hooked = false;
    }
    if (g_materialsStorageD000Hooked) {
        RestoreMaterialsJump(g_materialsStorageD000PatchAddr, g_materialsStorageD000Original, 6, &g_materialsStorageD000Trampoline);
        g_materialsStorageD000Hooked = false;
    }
    if (g_materialsStorageD110Hooked) {
        RestoreMaterialsJump(g_materialsStorageD110PatchAddr, g_materialsStorageD110Original, 6, &g_materialsStorageD110Trampoline);
        g_materialsStorageD110Hooked = false;
    }
    if (g_materialsStorageD1D0Hooked) {
        RestoreMaterialsJump(g_materialsStorageD1D0PatchAddr, g_materialsStorageD1D0Original, 5, &g_materialsStorageD1D0Trampoline);
        g_materialsStorageD1D0Hooked = false;
    }
    if (g_materialsMsg1450Hooked) {
        RestoreMaterialsJump(g_materialsMsg1450PatchAddr, g_materialsMsg1450Original, 9, &g_materialsMsg1450Trampoline);
        g_materialsMsg1450Hooked = false;
    }
    if (g_materialsMsg1560Hooked) {
        RestoreMaterialsJump(g_materialsMsg1560PatchAddr, g_materialsMsg1560Original, 9, &g_materialsMsg1560Trampoline);
        g_materialsMsg1560Hooked = false;
    }
    if (g_materialsTraceUiHooked) {
        RestoreMaterialsJump(g_materialsTraceUiPatchAddr, g_materialsTraceUiOriginal, sizeof(g_materialsTraceUiOriginal), &g_materialsTraceUiTrampoline);
        g_materialsTraceUiHooked = false;
    }
    if (g_materialsTraceTransitionHooked) {
        RestoreMaterialsJump(g_materialsTraceTransitionPatchAddr, g_materialsTraceTransitionOriginal, sizeof(g_materialsTraceTransitionOriginal), &g_materialsTraceTransitionTrampoline);
        g_materialsTraceTransitionHooked = false;
    }
    if (g_materialsTraceDrawHooked) {
        RestoreMaterialsJump(g_materialsTraceDrawPatchAddr, g_materialsTraceDrawOriginal, sizeof(g_materialsTraceDrawOriginal), &g_materialsTraceDrawTrampoline);
        g_materialsTraceDrawHooked = false;
    }
    if (g_materialsGridRenderHooked) {
        RestoreMaterialsJump(g_materialsGridRenderPatchAddr, g_materialsGridRenderOriginal, sizeof(g_materialsGridRenderOriginal), &g_materialsGridRenderTrampoline);
        g_materialsGridRenderHooked = false;
    }
    if (g_materialsPageDrawHooked) {
        RestoreMaterialsJump(g_materialsPageDrawPatchAddr, g_materialsPageDrawOriginal, sizeof(g_materialsPageDrawOriginal), &g_materialsPageDrawTrampoline);
        g_materialsPageDrawHooked = false;
    }
    if (g_materialsContentDrawCallHooked) {
        RestoreMaterialsBytes(g_materialsContentDrawCallAddr, g_materialsContentDrawCallOriginal, sizeof(g_materialsContentDrawCallOriginal));
        g_materialsContentDrawCallHooked = false;
    }
    if (g_materialsObjectDrawHooked) {
        RestoreMaterialsJump(g_materialsObjectDrawPatchAddr, g_materialsObjectDrawOriginal, sizeof(g_materialsObjectDrawOriginal), &g_materialsObjectDrawTrampoline);
        g_materialsObjectDrawHooked = false;
    }
    if (g_materialsD2ClientDrawEntryHooked) {
        RestoreMaterialsJump(g_materialsD2ClientDrawEntryPatchAddr, g_materialsD2ClientDrawEntryOriginal, sizeof(g_materialsD2ClientDrawEntryOriginal), &g_materialsD2ClientDrawEntryTrampoline);
        g_materialsD2ClientDrawEntryHooked = false;
    }
    if (g_materialsPage2CloneHooked) {
        RestoreMaterialsJump(g_materialsPage2ClonePatchAddr, g_materialsPage2CloneOriginal, sizeof(g_materialsPage2CloneOriginal), &g_materialsPage2CloneTrampoline);
        g_materialsPage2CloneHooked = false;
    }
    if (g_materialsD2ClientSpriteDrawHooked) {
        RestoreMaterialsJump(g_materialsD2ClientSpriteDrawPatchAddr, g_materialsD2ClientSpriteDrawOriginal, sizeof(g_materialsD2ClientSpriteDrawOriginal), &g_materialsD2ClientSpriteDrawTrampoline);
        g_materialsD2ClientSpriteDrawHooked = false;
    }
}
#endif

#ifdef SOE_PGAME_DIAG
extern "C" volatile uintptr_t g_diagCachedPGame = 0;
extern "C" uintptr_t g_diagLifecycleReturn = 0;
static uint8_t   g_diagLifecycleOriginal[6] = {};
static uintptr_t g_diagLifecyclePatchAddr = 0;
static bool      g_diagLifecycleHooked = false;

#ifdef SOE_COMMAND_TRACE
extern "C" uintptr_t g_commandTraceReturn = 0;
static uint8_t   g_commandTraceOriginal[5] = {};
static uintptr_t g_commandTracePatchAddr = 0;
static bool      g_commandTraceHooked = false;
extern "C" uintptr_t g_projectCreateTraceReturn = 0;
extern "C" uintptr_t g_d2gameCreateTraceReturn = 0;
extern "C" uintptr_t g_d2gameTeardownTraceReturn = 0;
extern "C" uintptr_t g_projectCommandTraceReturn = 0;
static uint8_t   g_projectCreateTraceOriginal[8] = {};
static uint8_t   g_d2gameCreateTraceOriginal[5] = {};
static uint8_t   g_d2gameTeardownTraceOriginal[5] = {};
static uint8_t   g_projectCommandTraceOriginal[6] = {};
static uintptr_t g_projectCreateTracePatchAddr = 0;
static uintptr_t g_d2gameCreateTracePatchAddr = 0;
static uintptr_t g_d2gameTeardownTracePatchAddr = 0;
static uintptr_t g_projectCommandTracePatchAddr = 0;
static bool      g_createLifecycleTraceHooked = false;
static bool      g_projectCommandTraceHooked = false;

#ifdef SOE_D2NET_SEND_TRACE
extern "C" uintptr_t g_d2netSendTraceReturn = 0;
static uint8_t   g_d2netSendTraceOriginal[6] = {};
static uintptr_t g_d2netSendTracePatchAddr = 0;
static bool      g_d2netSendTraceHooked = false;
#endif

#ifdef SOE_D2NET_IAT_TRACE
extern "C" uintptr_t g_d2netOrig10002 = 0;
extern "C" uintptr_t g_d2netOrig10004 = 0;
extern "C" uintptr_t g_d2netOrig10005 = 0;
extern "C" uintptr_t g_d2netOrig10008 = 0;
extern "C" uintptr_t g_d2netOrig10012 = 0;
extern "C" uintptr_t g_d2netOrig10019 = 0;
extern "C" uintptr_t g_d2netOrig10020 = 0;
extern "C" uintptr_t g_d2netOrig10022 = 0;
extern "C" uintptr_t g_d2netOrig10024 = 0;
extern "C" uintptr_t g_d2netOrig10026 = 0;
extern "C" uintptr_t g_d2netOrig10028 = 0;
extern "C" uintptr_t g_d2netOrig10031 = 0;
extern "C" uintptr_t g_d2netOrig10033 = 0;
extern "C" uintptr_t g_d2netOrig10034 = 0;
static bool g_d2netIatTraceHooked = false;
#endif

#ifdef SOE_RESET_PROTOTYPE
static CRITICAL_SECTION g_resetCacheLock;
static bool      g_resetCacheLockReady = false;
static volatile LONG g_resetInProgress = 0;
static const size_t RESET_PAYLOAD_SIZE = 0x200;
static uint8_t   g_resetPayload[RESET_PAYLOAD_SIZE] = {};
static uint8_t   g_resetReplayPayload[RESET_PAYLOAD_SIZE] = {};
static uintptr_t g_resetA06Delta = 0;
static uintptr_t g_resetArgs[12] = {};
static bool      g_resetCachedCreate = false;

typedef void (__stdcall *FnProjectCreateWrapper)(
    uintptr_t, uintptr_t, void*, uintptr_t, uintptr_t, void*,
    uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
typedef void (__stdcall *FnD2GameSafeTeardown)();
#endif

#ifdef SOE_PACKET_RESET_PROTOTYPE
static CRITICAL_SECTION g_packetResetLock;
static bool      g_packetResetLockReady = false;
static volatile LONG g_packetResetInProgress = 0;
static const size_t PROJECT_PACKET_SIZE = 0x200;
static uint8_t   g_packetResetCreate[PROJECT_PACKET_SIZE] = {};
static uint8_t   g_packetResetLeave[PROJECT_PACKET_SIZE] = {};
static bool      g_packetResetHasCreate = false;
static bool      g_packetResetHasLeave = false;
typedef void (__stdcall *FnProjectCommandDispatcher)(void*);
#endif

#ifdef SOE_PUMP_LEAVE_PROTOTYPE
static volatile LONG g_pumpLeaveRequested = 0;
#endif

#ifdef SOE_WMCLOSE_LEAVE_PROTOTYPE
static volatile LONG g_wmCloseLeaveInProgress = 0;
#endif

#ifdef SOE_ESC_CLICK_LEAVE_PROTOTYPE
static volatile LONG g_escClickLeaveInProgress = 0;
#endif

#ifdef SOE_KEYBOARD_LEAVE_PROTOTYPE
static volatile LONG g_keyboardLeaveInProgress = 0;
#endif

#ifdef SOE_NATIVE_CLIENT_LEAVE_PROTOTYPE
static volatile LONG g_nativeClientLeaveInProgress = 0;
static volatile LONG g_nativeClientLeaveRequested = 0;
extern "C" volatile LONG g_nativeClientLeaveDidRun = 0;
extern "C" uintptr_t g_nativeClientHeartbeatReturn = 0;
static uint8_t   g_nativeClientHeartbeatOriginal[8] = {};
static uintptr_t g_nativeClientHeartbeatPatchAddr = 0;
static bool      g_nativeClientHeartbeatHooked = false;
typedef void (__cdecl *FnD2ClientNativeLeave)();
typedef void (__stdcall *FnD2ClientLeaveCallback)(void*);
#endif

#ifdef SOE_NATIVE_LEAVE_ENTRY_TRACE
extern "C" uintptr_t g_nativeLeaveEntryTraceReturn = 0;
static uint8_t   g_nativeLeaveEntryTraceOriginal[5] = {};
static uintptr_t g_nativeLeaveEntryTracePatchAddr = 0;
static bool      g_nativeLeaveEntryTraceHooked = false;
extern "C" uintptr_t g_nativeLeaveCallbackTraceReturn = 0;
static uint8_t   g_nativeLeaveCallbackTraceOriginal[7] = {};
static uintptr_t g_nativeLeaveCallbackTracePatchAddr = 0;
static bool      g_nativeLeaveCallbackTraceHooked = false;
#endif

#ifdef SOE_STORM_SMSG_TRACE
extern "C" uintptr_t g_stormSmsgTraceReturn = 0;
static uint8_t   g_stormSmsgTraceOriginal[6] = {};
static uintptr_t g_stormSmsgTracePatchAddr = 0;
static bool      g_stormSmsgTraceHooked = false;
#endif

#endif

#ifdef SOE_SPAWN_TRACE
extern "C" uintptr_t g_spawnTraceE1250Return = 0;
extern "C" uintptr_t g_spawnTraceD0320Return = 0;
static uint8_t   g_spawnTraceE1250Original[7] = {};
static uint8_t   g_spawnTraceD0320Original[8] = {};
static uintptr_t g_spawnTraceE1250PatchAddr = 0;
static uintptr_t g_spawnTraceD0320PatchAddr = 0;
static bool      g_spawnTraceHooked = false;
static volatile LONG g_spawnTraceE1250Count = 0;
static volatile LONG g_spawnTraceD0320Count = 0;
#endif
#endif

// Grail logger function pointers
typedef const wchar_t* (__fastcall *FnGetStringById)(uint16_t nameId);
static FnGetStringById g_GetStringById = nullptr;
static uintptr_t*      g_ppDataTables  = nullptr;   // points to D2Common's sgptDataTables

static const char* QualityString(uint32_t quality)
{
    switch (quality) {
        case 0: return "None";
        case 1: return "Inferior";
        case 2: return "Normal";
        case 3: return "Superior";
        case QUALITY_MAGIC: return "Magic";
        case QUALITY_SET: return "Set";
        case QUALITY_RARE: return "Rare";
        case QUALITY_UNIQUE: return "Unique";
        case 8: return "Crafted";
        case 9: return "Honorific";
        default: return "Unknown";
    }
}

static void ItemCodeToString(uint32_t code, char* out, size_t outSize)
{
    if (!out || outSize == 0) return;
    size_t written = 0;
    for (int i = 0; i < 4 && written + 1 < outSize; ++i) {
        char ch = (char)((code >> (i * 8)) & 0xFF);
        if (ch == '\0' || ch == ' ') continue;
        out[written++] = ch;
    }
    out[written] = '\0';
}

static void JsonEscape(const char* in, char* out, size_t outSize)
{
    if (!out || outSize == 0) return;
    size_t written = 0;
    const unsigned char* src = (const unsigned char*)(in ? in : "");
    while (*src && written + 1 < outSize) {
        unsigned char ch = *src++;
        if ((ch == '"' || ch == '\\') && written + 2 < outSize) {
            out[written++] = '\\';
            out[written++] = (char)ch;
        } else if (ch == '\r' || ch == '\n' || ch == '\t') {
            if (written + 2 >= outSize) break;
            out[written++] = '\\';
            out[written++] = ch == '\r' ? 'r' : (ch == '\n' ? 'n' : 't');
        } else if (ch >= 0x20) {
            out[written++] = (char)ch;
        }
    }
    out[written] = '\0';
}

static bool TryResolveUniqueSetItemName(uintptr_t pUnit, uint32_t quality, char* outName, size_t outNameSize)
{
    if (outName && outNameSize > 0) outName[0] = '\0';
    if (!outName || outNameSize == 0) return false;
    if (!g_GetStringById || !g_ppDataTables) return false;

    if (IsBadReadPtr((void*)pUnit, UNIT_ITEMDATA + 4)) return false;
    uintptr_t pItemData = *(uintptr_t*)(pUnit + UNIT_ITEMDATA);
    if (IsBadReadPtr((void*)pItemData, ITEM_FILEINDEX + 4)) return false;

    uint32_t fileIdx = *(uint32_t*)(pItemData + ITEM_FILEINDEX);
    uintptr_t pDataTables = *g_ppDataTables;
    if (IsBadReadPtr((void*)pDataTables, 0xC30)) return false;

    uint16_t nameId = 0;
    if (quality == QUALITY_UNIQUE) {
        uintptr_t pUniTxt = *(uintptr_t*)(pDataTables + DT_UNIQUE_ITEMS_TXT_PTR);
        if (IsBadReadPtr((void*)pUniTxt, (fileIdx + 1) * UNI_RECORD_SIZE)) return false;
        nameId = *(uint16_t*)(pUniTxt + fileIdx * UNI_RECORD_SIZE + UNI_NAME_ID);
    } else if (quality == QUALITY_SET) {
        uintptr_t pSetTxt = *(uintptr_t*)(pDataTables + DT_SET_ITEMS_TXT_PTR);
        if (IsBadReadPtr((void*)pSetTxt, (fileIdx + 1) * SET_RECORD_SIZE)) return false;
        nameId = *(uint16_t*)(pSetTxt + fileIdx * SET_RECORD_SIZE + SET_NAME_ID);
    } else {
        return false;
    }

    if (nameId == 0 || nameId == BAD_NAME_ID) return false;
    const wchar_t* wName = g_GetStringById(nameId);
    if (!wName || IsBadReadPtr((void*)wName, 2)) return false;

    WideCharToMultiByte(CP_ACP, 0, wName, -1, outName, (int)outNameSize - 1, nullptr, nullptr);
    outName[outNameSize - 1] = '\0';
    return outName[0] != '\0';
}

static void FlushHookDropEventLines(std::vector<std::string>& lines)
{
    if (lines.empty()) return;
    FILE* f = fopen("C:\\soe_companion_drops.log", "a");
    if (!f) return;
    for (const std::string& line : lines) {
        fwrite(line.data(), 1, line.size(), f);
    }
    fclose(f);
}

static DWORD WINAPI HookDropEventWriterThread(LPVOID)
{
    while (!g_shutdown) {
        WaitForSingleObject(g_hookDropEventReady, 500);
        std::vector<std::string> pending;
        if (g_hookDropEventLockReady) {
            EnterCriticalSection(&g_hookDropEventLock);
            pending.swap(g_hookDropEventQueue);
            LeaveCriticalSection(&g_hookDropEventLock);
        }
        FlushHookDropEventLines(pending);
    }

    std::vector<std::string> pending;
    if (g_hookDropEventLockReady) {
        EnterCriticalSection(&g_hookDropEventLock);
        pending.swap(g_hookDropEventQueue);
        LeaveCriticalSection(&g_hookDropEventLock);
    }
    FlushHookDropEventLines(pending);
    return 0;
}

static void StartHookDropEventWriter()
{
    if (!g_hookDropEventLockReady) {
        InitializeCriticalSection(&g_hookDropEventLock);
        g_hookDropEventLockReady = true;
    }
    if (!g_hookDropEventReady) {
        g_hookDropEventReady = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    }
    if (g_hookDropEventReady && !g_hookDropEventThread) {
        g_hookDropEventThread = CreateThread(nullptr, 0, HookDropEventWriterThread, nullptr, 0, nullptr);
    }
}

static void QueueHookDropEventLine(const char* line)
{
    if (!line || !g_hookDropEventLockReady) return;
    EnterCriticalSection(&g_hookDropEventLock);
    if (g_hookDropEventQueue.size() >= HOOK_DROP_EVENT_QUEUE_LIMIT) {
        g_hookDropEventQueue.erase(g_hookDropEventQueue.begin());
    }
    g_hookDropEventQueue.push_back(std::string(line));
    LeaveCriticalSection(&g_hookDropEventLock);
    if (g_hookDropEventReady) SetEvent(g_hookDropEventReady);
}

static void AppendHookDropEvent(uintptr_t pUnit, uint32_t quality, uint32_t flags)
{
    if (IsBadReadPtr((void*)pUnit, UNIT_ITEMDATA + 4)) return;
    uintptr_t pItemData = *(uintptr_t*)(pUnit + UNIT_ITEMDATA);
    if (IsBadReadPtr((void*)pItemData, ITEM_FILEINDEX + 4)) return;

    uint32_t itemClass = *(uint32_t*)(pUnit + UNIT_CLASS);
    uint32_t unitId = *(uint32_t*)(pUnit + UNIT_ID);
    uint32_t mode = *(uint32_t*)(pUnit + UNIT_MODE);
    uint32_t seed = 0;
    if (!IsBadReadPtr((void*)(pItemData + ITEM_SEED), 4)) {
        seed = *(uint32_t*)(pItemData + ITEM_SEED);
    }
    uint32_t fileIdx = *(uint32_t*)(pItemData + ITEM_FILEINDEX);

    char itemCode[8] = {};
    ItemCodeToString(ItemCodeForUnit(pUnit), itemCode, sizeof(itemCode));
    char canonical[256] = {};
    TryResolveUniqueSetItemName(pUnit, quality, canonical, sizeof(canonical));

    const char* displayName = canonical[0] ? canonical : (itemCode[0] ? itemCode : "Unknown item");
    char nameEsc[512] = {};
    char canonicalEsc[512] = {};
    char codeEsc[32] = {};
    JsonEscape(displayName, nameEsc, sizeof(nameEsc));
    JsonEscape(canonical, canonicalEsc, sizeof(canonicalEsc));
    JsonEscape(itemCode, codeEsc, sizeof(codeEsc));

    char line[1400] = {};
    snprintf(line, sizeof(line),
        "{\"eventId\":\"ijl11:%08X:%08X:%s:%u:%u\","
        "\"unitId\":%u,\"seed\":%u,\"itemCode\":\"%s\","
        "\"quality\":\"%s\",\"name\":\"%s\",\"baseName\":\"%s\","
        "\"canonicalName\":\"%s\",\"mode\":%u,\"isIdentified\":%s,"
        "\"isRuneword\":%s,\"isEthereal\":%s,\"fileIndex\":%u,"
        "\"class\":%u,\"source\":\"ijl11-drop-hook\",\"nameSource\":\"item-code\"}\n",
        seed, unitId, codeEsc, quality, fileIdx,
        unitId, seed, codeEsc,
        QualityString(quality), nameEsc, codeEsc,
        canonicalEsc, mode, (flags & IFLAG_IDENTIFIED) ? "true" : "false",
        (flags & IFLAG_RUNEWORD) ? "true" : "false",
        (flags & IFLAG_ETHEREAL) ? "true" : "false",
        fileIdx, itemClass);

    QueueHookDropEventLine(line);
}

// ── Grail log ─────────────────────────────────────────────────────────────────
static void AppendGrailLog(const char* itemName, uint32_t quality)
{
    const char* qualStr =
        quality == QUALITY_UNIQUE ? "unique" :
        quality == QUALITY_SET    ? "set"    :
        quality == QUALITY_RARE   ? "rare"   : "magic";

    static const char logPath[] = "C:\\grail_drops.log";

    FILE* f = fopen(logPath, "a");
    if (!f) return;
    fprintf(f, "%s|%s\n", itemName, qualStr);
    fclose(f);
}

static void TryLogItemName(uintptr_t pUnit, uint32_t quality)
{
    char ansi[256] = {};
    if (TryResolveUniqueSetItemName(pUnit, quality, ansi, sizeof(ansi))) {
        AppendGrailLog(ansi, quality);
    }
}

// ── SetItemFlag hook ───────────────────────────────────────────────────────────
// Original signature: void __stdcall SetItemFlag(UnitAny* pUnit, uint32_t flagMask, int enable)
static void __stdcall HookSetItemFlag(uintptr_t pUnit, uint32_t flagMask, int enable)
{
    // Call original first
    typedef void (__stdcall *FnSIF)(uintptr_t, uint32_t, int);
    ((FnSIF)g_origSetItemFlag)(pUnit, flagMask, enable);

    if (!enable) return;
    if (IsBadReadPtr((void*)pUnit, UNIT_ITEMDATA + 4)) return;

    // Only care about items (unit type 4)
    uint32_t unitType = *(uint32_t*)(pUnit + UNIT_TYPE);
    if (unitType != 4) return;

    uintptr_t pItemData = *(uintptr_t*)(pUnit + UNIT_ITEMDATA);
    if (IsBadReadPtr((void*)pItemData, ITEM_FLAGS + 4)) return;

    uint32_t quality = *(uint32_t*)(pItemData + ITEM_QUALITY);
    uint32_t flags   = *(uint32_t*)(pItemData + ITEM_FLAGS);

    // Apply DropIdentified
    bool shouldId = false;
    if (quality == QUALITY_MAGIC  && g_dropIdMagic)  shouldId = true;
    if (quality == QUALITY_MAGIC  && IsConfiguredCharm(pUnit)) shouldId = true;
    if (quality == QUALITY_SET    && g_dropIdSet)     shouldId = true;
    if (quality == QUALITY_RARE   && g_dropIdRare)    shouldId = true;
    if (quality == QUALITY_UNIQUE && g_dropIdUnique)  shouldId = true;

    if (shouldId && !(flags & IFLAG_IDENTIFIED)) {
        // Set the identified flag directly
        DWORD oldProtect;
        void* flagAddr = (void*)(pItemData + ITEM_FLAGS);
        if (!IsBadWritePtr(flagAddr, 4)) {
            *(uint32_t*)flagAddr = flags | IFLAG_IDENTIFIED;
            flags |= IFLAG_IDENTIFIED;
        }
    }

    // Grail log: only log when the initial creation flag is being set
    // (flagMask 0x80000 = the "item created" flag that fires exactly once)
    if (flagMask == 0x80000) {
        AppendHookDropEvent(pUnit, quality, flags);
        if (quality == QUALITY_UNIQUE || quality == QUALITY_SET) {
            TryLogItemName(pUnit, quality);
        }
    }
}

// ── IAT Hook setup ────────────────────────────────────────────────────────────
static bool FindAndInstallHook()
{
    // Find D2Game.dll's IAT slot for SetItemFlag
    HMODULE hD2Game = GetModuleHandleA("D2Game.dll");
    if (!hD2Game) return false;

    auto* dos = (IMAGE_DOS_HEADER*)hD2Game;
    auto* nt  = (IMAGE_NT_HEADERS*)((uint8_t*)hD2Game + dos->e_lfanew);
    auto& imp = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!imp.VirtualAddress) return false;

    auto* desc = (IMAGE_IMPORT_DESCRIPTOR*)((uint8_t*)hD2Game + imp.VirtualAddress);
    for (; desc->Name; ++desc) {
        const char* dllName = (const char*)((uint8_t*)hD2Game + desc->Name);
        if (_stricmp(dllName, "D2Common.dll") != 0) continue;

        auto* origThunk = (IMAGE_THUNK_DATA*)((uint8_t*)hD2Game + desc->OriginalFirstThunk);
        auto* iatThunk  = (IMAGE_THUNK_DATA*)((uint8_t*)hD2Game + desc->FirstThunk);

        for (; origThunk->u1.Function; ++origThunk, ++iatThunk) {
            if (IMAGE_SNAP_BY_ORDINAL(origThunk->u1.Ordinal)) {
                DWORD ord = IMAGE_ORDINAL(origThunk->u1.Ordinal);
                // SetItemFlag is D2Common ordinal 10977 (confirmed in previous session)
                if (ord == 10977) {
                    g_pIATSlot        = (uintptr_t*)&iatThunk->u1.Function;
                    g_origSetItemFlag = *g_pIATSlot;

                    // Patch the IAT slot
                    DWORD oldProt;
                    VirtualProtect(g_pIATSlot, sizeof(uintptr_t), PAGE_READWRITE, &oldProt);
                    *g_pIATSlot = (uintptr_t)&HookSetItemFlag;
                    VirtualProtect(g_pIATSlot, sizeof(uintptr_t), oldProt, &oldProt);
                    return true;
                }
            }
        }
    }
    return false;
}

static void SetupGrailLogger()
{
    // D2Lang GetStringById
    HMODULE hLang = GetModuleHandleA("D2Lang.dll");
    if (hLang) {
        g_GetStringById = (FnGetStringById)((uint8_t*)hLang + D2LANG_GETSTRING_RVA);
    }

    // D2Common sgptDataTables
    HMODULE hCommon = GetModuleHandleA("D2Common.dll");
    if (hCommon) {
        // sgptDataTables is a pointer-to-pointer: **ppDataTables
        uintptr_t* pp = (uintptr_t*)((uint8_t*)hCommon + SGPT_DATA_TABLES_RVA);
        if (!IsBadReadPtr(pp, 4)) {
            g_ppDataTables = pp;
        }
    }
}

#ifdef SOE_PGAME_DIAG
static void AppendDiagLine(const char* fmt, ...)
{
    FILE* f = fopen("C:\\soe_pgame_postload_diag.log", "a");
    if (!f) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);

    fprintf(f, "\n");
    fclose(f);
}

static uintptr_t SafeReadPtr(uintptr_t base, uintptr_t offset)
{
    uintptr_t addr = base + offset;
    if (!base || IsBadReadPtr((void*)addr, sizeof(uintptr_t))) return 0;
    return *(uintptr_t*)addr;
}

static void DumpPtrBlock(const char* label, uintptr_t ptr)
{
    if (!ptr) return;

    AppendDiagLine("%s base=%p +00=%p +04=%p +08=%p +0c=%p +10=%p +14=%p +18=%p +1c=%p +20=%p +24=%p +28=%p +2c=%p",
        label,
        (void*)ptr,
        (void*)SafeReadPtr(ptr, 0x00), (void*)SafeReadPtr(ptr, 0x04),
        (void*)SafeReadPtr(ptr, 0x08), (void*)SafeReadPtr(ptr, 0x0c),
        (void*)SafeReadPtr(ptr, 0x10), (void*)SafeReadPtr(ptr, 0x14),
        (void*)SafeReadPtr(ptr, 0x18), (void*)SafeReadPtr(ptr, 0x1c),
        (void*)SafeReadPtr(ptr, 0x20), (void*)SafeReadPtr(ptr, 0x24),
        (void*)SafeReadPtr(ptr, 0x28), (void*)SafeReadPtr(ptr, 0x2c));
}

static void DumpMonsterStateBlock(const char* label, int index, int bucket, uintptr_t unit)
{
    uintptr_t data = SafeReadPtr(unit, 0x14);
    uintptr_t act = SafeReadPtr(unit, 0x18);
    uintptr_t seed = SafeReadPtr(unit, 0x1c);
    uintptr_t path = SafeReadPtr(unit, 0x2c);
    uintptr_t room = SafeReadPtr(path, 0x1c);
    uintptr_t prevRoom = SafeReadPtr(path, 0x20);
    uintptr_t modeCtrl = SafeReadPtr(unit, 0x5c);
    uintptr_t modeBits = SafeReadPtr(modeCtrl, 0x58);
    uint32_t modeCtrl10 = (uint32_t)SafeReadPtr(modeCtrl, 0x10);
    uint32_t modeMask0 = (uint32_t)SafeReadPtr(modeBits, 0x00);
    uint32_t modeMask1 = (uint32_t)SafeReadPtr(modeBits, 0x04);
    uint32_t modeDirty0 = 0;
    uint32_t modeDirty1 = 0;
    if (modeBits && modeCtrl) {
        int modeCount = (int)SafeReadPtr((uintptr_t)g_ppDataTables ? *g_ppDataTables : 0, 0xc4);
        int modeDwords = (modeCount + 31) >> 5;
        modeDirty0 = (uint32_t)SafeReadPtr(modeBits, (uintptr_t)modeDwords * sizeof(uint32_t));
        modeDirty1 = (uint32_t)SafeReadPtr(modeBits, (uintptr_t)(modeDwords + 1) * sizeof(uint32_t));
    }

    AppendDiagLine(
        "%s #%02d bucket=%03d unit=%p type=%08x class=%08x guid=%08x mode=%08x data=%p act=%p seed=%p path=%p room=%p prevRoom=%p modeCtrl=%p modeCtrl+10=%08x modeBits=%p modeMask0=%08x modeMask1=%08x modeDirty0=%08x modeDirty1=%08x hasM1=%d hasM2=%d hasM4=%d hasM12=%d +20=%p +30=%p +34=%p +38=%p +3c=%p +40=%p +44=%p +48=%p +4c=%p +50=%p +c4=%p +c8=%p next=%p",
        label,
        index,
        bucket,
        (void*)unit,
        (unsigned)SafeReadPtr(unit, 0x00),
        (unsigned)SafeReadPtr(unit, 0x04),
        (unsigned)SafeReadPtr(unit, 0x0c),
        (unsigned)SafeReadPtr(unit, 0x10),
        (void*)data,
        (void*)act,
        (void*)seed,
        (void*)path,
        (void*)room,
        (void*)prevRoom,
        (void*)modeCtrl,
        (unsigned)modeCtrl10,
        (void*)modeBits,
        (unsigned)modeMask0,
        (unsigned)modeMask1,
        (unsigned)modeDirty0,
        (unsigned)modeDirty1,
        (modeMask0 & (1u << 1)) ? 1 : 0,
        (modeMask0 & (1u << 2)) ? 1 : 0,
        (modeMask0 & (1u << 4)) ? 1 : 0,
        (modeMask0 & (1u << 12)) ? 1 : 0,
        (void*)SafeReadPtr(unit, 0x20),
        (void*)SafeReadPtr(unit, 0x30),
        (void*)SafeReadPtr(unit, 0x34),
        (void*)SafeReadPtr(unit, 0x38),
        (void*)SafeReadPtr(unit, 0x3c),
        (void*)SafeReadPtr(unit, 0x40),
        (void*)SafeReadPtr(unit, 0x44),
        (void*)SafeReadPtr(unit, 0x48),
        (void*)SafeReadPtr(unit, 0x4c),
        (void*)SafeReadPtr(unit, 0x50),
        (void*)SafeReadPtr(unit, 0xc4),
        (void*)SafeReadPtr(unit, 0xc8),
        (void*)SafeReadPtr(unit, 0xe4));

    DumpPtrBlock(label, path);
    if (room) DumpPtrBlock("MON_ROOM", room);
    if (prevRoom && prevRoom != room) DumpPtrBlock("MON_PREVROOM", prevRoom);
    if (data) DumpPtrBlock("MON_DATA", data);
}

static void DumpUnitTableCounts(const char* label, uintptr_t pGame, uintptr_t tableOffset, uintptr_t playerUnit)
{
    if (!pGame) return;

    int total = 0;
    int byType[8] = {};
    int playerHits = 0;
    int nullType = 0;
    uintptr_t firstType1 = 0;
    uintptr_t firstType0 = 0;

    for (int bucket = 0; bucket < 128; ++bucket) {
        uintptr_t unit = SafeReadPtr(pGame, tableOffset + bucket * sizeof(uintptr_t));
        int guard = 0;
        while (unit && guard++ < 2048) {
            total++;
            uint32_t type = (uint32_t)SafeReadPtr(unit, 0x00);
            if (type < 8) byType[type]++;
            else nullType++;
            if (unit == playerUnit) playerHits++;
            if (type == 0 && !firstType0) firstType0 = unit;
            if (type == 1 && !firstType1) firstType1 = unit;
            unit = SafeReadPtr(unit, 0xe4);
        }
    }

    AppendDiagLine("UNITTABLE %s offset=%p total=%d t0=%d t1=%d t2=%d t3=%d t4=%d t5=%d t6=%d t7=%d other=%d playerHits=%d firstT0=%p firstT1=%p",
        label, (void*)tableOffset, total,
        byType[0], byType[1], byType[2], byType[3], byType[4], byType[5], byType[6], byType[7],
        nullType, playerHits, (void*)firstType0, (void*)firstType1);
}

static void DumpMonsterUnitSamples(uintptr_t pGame)
{
    if (!pGame) return;

    int sample = 0;
    int aliveSample = 0;
    int deadSample = 0;
    int otherSample = 0;
    int modes[32] = {};
    int otherMode = 0;

    for (int bucket = 0; bucket < 128; ++bucket) {
        uintptr_t unit = SafeReadPtr(pGame, 0x1320 + bucket * sizeof(uintptr_t));
        int guard = 0;
        while (unit && guard++ < 2048) {
            uint32_t type = (uint32_t)SafeReadPtr(unit, 0x00);
            uint32_t mode = (uint32_t)SafeReadPtr(unit, 0x10);
            if (mode < 32) modes[mode]++;
            else otherMode++;

            if (type == 1 && sample < 12) {
                DumpMonsterStateBlock("MONSAMPLE_ANY", sample, bucket, unit);
                sample++;
            }
            if (type == 1 && (mode == 1 || mode == 2 || mode == 4) && aliveSample < 8) {
                DumpMonsterStateBlock("MONSAMPLE_ALIVE", aliveSample, bucket, unit);
                aliveSample++;
            }
            if (type == 1 && mode == 12 && deadSample < 12) {
                DumpMonsterStateBlock("MONSAMPLE_DEAD", deadSample, bucket, unit);
                deadSample++;
            }
            if (type == 1 && !(mode == 1 || mode == 2 || mode == 4 || mode == 12) && otherSample < 8) {
                DumpMonsterStateBlock("MONSAMPLE_OTHER", otherSample, bucket, unit);
                otherSample++;
            }

            unit = SafeReadPtr(unit, 0xe4);
        }
    }

    AppendDiagLine(
        "MONMODE m00=%d m01=%d m02=%d m03=%d m04=%d m05=%d m06=%d m07=%d m08=%d m09=%d m10=%d m11=%d m12=%d m13=%d m14=%d m15=%d other=%d",
        modes[0], modes[1], modes[2], modes[3], modes[4], modes[5], modes[6], modes[7],
        modes[8], modes[9], modes[10], modes[11], modes[12], modes[13], modes[14], modes[15],
        otherMode);
}

#ifdef SOE_COMMAND_TRACE
static bool PatchCommandJump(uintptr_t patchAddr, void* hook, uint8_t* original, size_t patchLen, uintptr_t* returnAddr)
{
    DWORD oldProt = 0;
    if (!VirtualProtect((void*)patchAddr, patchLen, PAGE_EXECUTE_READWRITE, &oldProt)) return false;

    memcpy(original, (void*)patchAddr, patchLen);
    *returnAddr = patchAddr + patchLen;

    uint8_t patch[16] = {};
    patch[0] = 0xE9;
    *(int32_t*)&patch[1] = (int32_t)((uintptr_t)hook - (patchAddr + 5));
    for (size_t i = 5; i < patchLen; ++i) patch[i] = 0x90;
    memcpy((void*)patchAddr, patch, patchLen);
    FlushInstructionCache(GetCurrentProcess(), (void*)patchAddr, patchLen);
    VirtualProtect((void*)patchAddr, patchLen, oldProt, &oldProt);
    return true;
}

extern "C" void __stdcall LogD2GameCommandPacket(uintptr_t packet)
{
    if (!packet || IsBadReadPtr((void*)packet, 0x40)) {
        AppendDiagLine("cmd packet invalid packet=%p", (void*)packet);
        return;
    }

    const uint32_t clientId = *(uint32_t*)(packet + 0x00);
    const uint8_t command = *(uint8_t*)(packet + 0x04);
    const uint16_t word09 = *(uint16_t*)(packet + 0x09);
    const uint8_t byte0b = *(uint8_t*)(packet + 0x0b);
    const uint8_t byte15 = *(uint8_t*)(packet + 0x15);
    const uint8_t byte16 = *(uint8_t*)(packet + 0x16);
    const uint8_t byte17 = *(uint8_t*)(packet + 0x17);
    const uint16_t word29 = *(uint16_t*)(packet + 0x29);
    const uint32_t dword2b = *(uint32_t*)(packet + 0x2b);
    const uint8_t byte31 = *(uint8_t*)(packet + 0x31);

    char hex[3 * 64 + 1] = {};
    char* out = hex;
    for (int i = 0; i < 64; ++i) {
        wsprintfA(out, "%02X%s", *(uint8_t*)(packet + i), i == 63 ? "" : " ");
        out += lstrlenA(out);
    }

    char ascii05[17] = {};
    char ascii15[17] = {};
    for (int i = 0; i < 16; ++i) {
        uint8_t a = *(uint8_t*)(packet + 0x05 + i);
        uint8_t b = *(uint8_t*)(packet + 0x15 + i);
        ascii05[i] = (a >= 0x20 && a < 0x7f) ? (char)a : '.';
        ascii15[i] = (b >= 0x20 && b < 0x7f) ? (char)b : '.';
    }

    AppendDiagLine(
        "cmd '%c'/0x%02x packet=%p client=%08x word09=%04x byte0b=%02x byte15=%02x byte16=%02x byte17=%02x word29=%04x dword2b=%08x byte31=%02x ascii05='%s' ascii15='%s' hex64=%s",
        (command >= 0x20 && command < 0x7f) ? command : '.',
        command,
        (void*)packet,
        (unsigned)clientId,
        (unsigned)word09,
        (unsigned)byte0b,
        (unsigned)byte15,
        (unsigned)byte16,
        (unsigned)byte17,
        (unsigned)word29,
        (unsigned)dword2b,
        (unsigned)byte31,
        ascii05,
        ascii15,
        hex);
}

extern "C" void __attribute__((naked)) SoECommandTraceHook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "pushl 40(%esp)\n\t"
        "call _LogD2GameCommandPacket@4\n\t"
        "popal\n\t"
        "popfl\n\t"
        "subl $0x48, %esp\n\t"
        "pushl %ebx\n\t"
        "pushl %ebp\n\t"
        "jmp *_g_commandTraceReturn\n\t"
    );
}

extern "C" void __stdcall LogProjectCommandPacket(uintptr_t packet, uintptr_t retAddr, uintptr_t originalEsp)
{
    if (!packet || IsBadReadPtr((void*)packet, 0x40)) {
        AppendDiagLine("ProjectCommand invalid packet=%p ret=%p esp=%p", (void*)packet, (void*)retAddr, (void*)originalEsp);
        return;
    }

    uint32_t client = *(uint32_t*)(packet + 0x00);
    uint8_t command = *(uint8_t*)(packet + 0x04);

#ifdef SOE_PUMP_LEAVE_PROTOTYPE
    if (command == 0x6d && InterlockedCompareExchange(&g_pumpLeaveRequested, 0, 1) == 1) {
        if (!IsBadWritePtr((void*)(packet + 0x04), 1)) {
            *(uint8_t*)(packet + 0x04) = 0x69;
            command = 0x69;
            AppendDiagLine("pump leave prototype converted ProjectCommand packet=%p ret=%p esp=%p client=%08x m->i",
                (void*)packet,
                (void*)retAddr,
                (void*)originalEsp,
                (unsigned)client);
        } else {
            AppendDiagLine("pump leave prototype failed: packet command byte not writable packet=%p", (void*)packet);
        }
    }
#endif

    uint16_t word09 = *(uint16_t*)(packet + 0x09);
    uint8_t byte0b = *(uint8_t*)(packet + 0x0b);
    uint8_t byte15 = *(uint8_t*)(packet + 0x15);
    uint8_t byte16 = *(uint8_t*)(packet + 0x16);
    uint8_t byte17 = *(uint8_t*)(packet + 0x17);
    uint16_t word29 = *(uint16_t*)(packet + 0x29);
    uint32_t dword2b = *(uint32_t*)(packet + 0x2b);
    uint8_t byte31 = *(uint8_t*)(packet + 0x31);

#ifdef SOE_PACKET_RESET_PROTOTYPE
    if (g_packetResetLockReady && !IsBadReadPtr((void*)packet, PROJECT_PACKET_SIZE)) {
        EnterCriticalSection(&g_packetResetLock);
        if (command == 0x67) {
            memcpy(g_packetResetCreate, (void*)packet, PROJECT_PACKET_SIZE);
            g_packetResetHasCreate = true;
        } else if (command == 0x69) {
            memcpy(g_packetResetLeave, (void*)packet, PROJECT_PACKET_SIZE);
            g_packetResetHasLeave = true;
        }
        LeaveCriticalSection(&g_packetResetLock);
    }
#endif

    char hex[3 * 64 + 1] = {};
    char ascii[65] = {};
    char* out = hex;
    for (int i = 0; i < 64; ++i) {
        uint8_t b = *(uint8_t*)(packet + i);
        wsprintfA(out, "%02X%s", b, i == 63 ? "" : " ");
        out += lstrlenA(out);
        ascii[i] = (b >= 0x20 && b < 0x7f) ? (char)b : '.';
    }

    AppendDiagLine(
        "ProjectCommand '%c'/0x%02x packet=%p ret=%p esp=%p client=%08x word09=%04x byte0b=%02x byte15=%02x byte16=%02x byte17=%02x word29=%04x dword2b=%08x byte31=%02x ascii='%s' hex64=%s",
        (command >= 0x20 && command < 0x7f) ? command : '.',
        (unsigned)command,
        (void*)packet,
        (void*)retAddr,
        (void*)originalEsp,
        (unsigned)client,
        (unsigned)word09,
        (unsigned)byte0b,
        (unsigned)byte15,
        (unsigned)byte16,
        (unsigned)byte17,
        (unsigned)word29,
        (unsigned)dword2b,
        (unsigned)byte31,
        ascii,
        hex);
}

extern "C" void __attribute__((naked)) SoEProjectCommandTraceHook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "movl 40(%esp), %eax\n\t"
        "movl 36(%esp), %edx\n\t"
        "leal 36(%esp), %ecx\n\t"
        "pushl %ecx\n\t"
        "pushl %edx\n\t"
        "pushl %eax\n\t"
        "call _LogProjectCommandPacket@12\n\t"
        "popal\n\t"
        "popfl\n\t"
        "pushl %ebp\n\t"
        "movl %esp, %ebp\n\t"
        "subl $0x50, %esp\n\t"
        "jmp *_g_projectCommandTraceReturn\n\t"
    );
}

#ifdef SOE_D2NET_SEND_TRACE
static void DumpMaybePacketArg(const char* label, uintptr_t ptr)
{
    if (!ptr || IsBadReadPtr((void*)ptr, 32)) return;

    char hex[3 * 32 + 1] = {};
    char ascii[33] = {};
    char* out = hex;
    for (int i = 0; i < 32; ++i) {
        uint8_t b = *(uint8_t*)(ptr + i);
        wsprintfA(out, "%02X%s", b, i == 31 ? "" : " ");
        out += lstrlenA(out);
        ascii[i] = (b >= 0x20 && b < 0x7f) ? (char)b : '.';
    }
    AppendDiagLine("D2NetSend %s ptr=%p first=%02x ascii='%s' hex32=%s",
        label,
        (void*)ptr,
        (unsigned)*(uint8_t*)ptr,
        ascii,
        hex);
}

extern "C" void __stdcall LogD2NetSendEntry(uintptr_t originalEsp)
{
    if (!originalEsp || IsBadReadPtr((void*)originalEsp, 0x18)) {
        AppendDiagLine("D2NetSend invalid esp=%p", (void*)originalEsp);
        return;
    }

    uintptr_t ret = SafeReadPtr(originalEsp, 0x00);
    uintptr_t a1 = SafeReadPtr(originalEsp, 0x04);
    uintptr_t a2 = SafeReadPtr(originalEsp, 0x08);
    uintptr_t a3 = SafeReadPtr(originalEsp, 0x0c);
    uintptr_t a4 = SafeReadPtr(originalEsp, 0x10);
    AppendDiagLine("D2NetSend entry esp=%p ret=%p a1=%p a2=%p a3=%p a4=%p",
        (void*)originalEsp,
        (void*)ret,
        (void*)a1,
        (void*)a2,
        (void*)a3,
        (void*)a4);

    DumpMaybePacketArg("a1", a1);
    DumpMaybePacketArg("a2", a2);
    DumpMaybePacketArg("a3", a3);
    DumpMaybePacketArg("a4", a4);
}

extern "C" void __attribute__((naked)) SoED2NetSendTraceHook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "leal 36(%esp), %eax\n\t"
        "pushl %eax\n\t"
        "call _LogD2NetSendEntry@4\n\t"
        "popal\n\t"
        "popfl\n\t"
        "subl $0x40c, %esp\n\t"
        "jmp *_g_d2netSendTraceReturn\n\t"
    );
}
#endif

#ifdef SOE_D2NET_IAT_TRACE
static void DumpD2NetIatPtr(int ordinal, const char* label, uintptr_t ptr)
{
    if (!ptr || IsBadReadPtr((void*)ptr, 24)) return;

    char hex[3 * 24 + 1] = {};
    char ascii[25] = {};
    char* out = hex;
    for (int i = 0; i < 24; ++i) {
        uint8_t b = *(uint8_t*)(ptr + i);
        wsprintfA(out, "%02X%s", b, i == 23 ? "" : " ");
        out += lstrlenA(out);
        ascii[i] = (b >= 0x20 && b < 0x7f) ? (char)b : '.';
    }

    AppendDiagLine("D2NetIAT ord=%d %s ptr=%p first=%02x ascii='%s' hex24=%s",
        ordinal,
        label,
        (void*)ptr,
        (unsigned)*(uint8_t*)ptr,
        ascii,
        hex);
}

extern "C" void __stdcall LogD2NetIatCall(uintptr_t ordinal, uintptr_t originalEsp)
{
    if (!originalEsp || IsBadReadPtr((void*)originalEsp, 0x18)) {
        AppendDiagLine("D2NetIAT ord=%u invalid esp=%p", (unsigned)ordinal, (void*)originalEsp);
        return;
    }

    uintptr_t ret = SafeReadPtr(originalEsp, 0x00);
    uintptr_t a1 = SafeReadPtr(originalEsp, 0x04);
    uintptr_t a2 = SafeReadPtr(originalEsp, 0x08);
    uintptr_t a3 = SafeReadPtr(originalEsp, 0x0c);
    uintptr_t a4 = SafeReadPtr(originalEsp, 0x10);

    AppendDiagLine("D2NetIAT ord=%u esp=%p ret=%p a1=%p a2=%p a3=%p a4=%p",
        (unsigned)ordinal,
        (void*)originalEsp,
        (void*)ret,
        (void*)a1,
        (void*)a2,
        (void*)a3,
        (void*)a4);

    DumpD2NetIatPtr((int)ordinal, "a1", a1);
    DumpD2NetIatPtr((int)ordinal, "a2", a2);
    DumpD2NetIatPtr((int)ordinal, "a3", a3);
    DumpD2NetIatPtr((int)ordinal, "a4", a4);
}

#define SOE_D2NET_IAT_HOOK(ord) \
extern "C" void __attribute__((naked)) SoED2NetIatHook##ord() \
{ \
    __asm__ __volatile__( \
        "pushfl\n\t" \
        "pushal\n\t" \
        "leal 36(%esp), %eax\n\t" \
        "pushl %eax\n\t" \
        "pushl $" #ord "\n\t" \
        "call _LogD2NetIatCall@8\n\t" \
        "popal\n\t" \
        "popfl\n\t" \
        "jmp *_g_d2netOrig" #ord "\n\t" \
    ); \
}

SOE_D2NET_IAT_HOOK(10002)
SOE_D2NET_IAT_HOOK(10004)
SOE_D2NET_IAT_HOOK(10005)
SOE_D2NET_IAT_HOOK(10008)
SOE_D2NET_IAT_HOOK(10012)
SOE_D2NET_IAT_HOOK(10019)
SOE_D2NET_IAT_HOOK(10020)
SOE_D2NET_IAT_HOOK(10022)
SOE_D2NET_IAT_HOOK(10024)
SOE_D2NET_IAT_HOOK(10026)
SOE_D2NET_IAT_HOOK(10028)
SOE_D2NET_IAT_HOOK(10031)
SOE_D2NET_IAT_HOOK(10033)
SOE_D2NET_IAT_HOOK(10034)
#endif

extern "C" void __stdcall LogProjectCreateWrapper(uintptr_t originalEsp)
{
    if (!originalEsp || IsBadReadPtr((void*)originalEsp, 0x38)) {
        AppendDiagLine("project create wrapper invalid esp=%p", (void*)originalEsp);
        return;
    }

    AppendDiagLine(
        "ProjectCreateWrapper esp=%p ret=%p a01=%08x a02=%08x a03=%p a04=%08x a05=%08x a06=%p a07=%08x a08=%08x a09=%08x a10=%08x a11=%08x a12=%08x",
        (void*)originalEsp,
        (void*)SafeReadPtr(originalEsp, 0x00),
        (unsigned)SafeReadPtr(originalEsp, 0x04),
        (unsigned)SafeReadPtr(originalEsp, 0x08),
        (void*)SafeReadPtr(originalEsp, 0x0c),
        (unsigned)SafeReadPtr(originalEsp, 0x10),
        (unsigned)SafeReadPtr(originalEsp, 0x14),
        (void*)SafeReadPtr(originalEsp, 0x18),
        (unsigned)SafeReadPtr(originalEsp, 0x1c),
        (unsigned)SafeReadPtr(originalEsp, 0x20),
        (unsigned)SafeReadPtr(originalEsp, 0x24),
        (unsigned)SafeReadPtr(originalEsp, 0x28),
        (unsigned)SafeReadPtr(originalEsp, 0x2c),
        (unsigned)SafeReadPtr(originalEsp, 0x30));

    uintptr_t ptr03 = SafeReadPtr(originalEsp, 0x0c);
    uintptr_t ptr06 = SafeReadPtr(originalEsp, 0x18);
#ifdef SOE_RESET_PROTOTYPE
    if (g_resetCacheLockReady && ptr03 && ptr06 && ptr06 >= ptr03 && (ptr06 - ptr03) < RESET_PAYLOAD_SIZE && !IsBadReadPtr((void*)ptr03, RESET_PAYLOAD_SIZE)) {
        EnterCriticalSection(&g_resetCacheLock);
        for (int i = 0; i < 12; ++i) {
            g_resetArgs[i] = SafeReadPtr(originalEsp, 0x04 + (uintptr_t)i * sizeof(uintptr_t));
        }
        memcpy(g_resetPayload, (void*)ptr03, RESET_PAYLOAD_SIZE);
        g_resetA06Delta = ptr06 - ptr03;
        g_resetCachedCreate = true;
        LeaveCriticalSection(&g_resetCacheLock);
        AppendDiagLine("reset prototype cached create args char='%s' a06delta=%08x", (const char*)(g_resetPayload + g_resetA06Delta), (unsigned)g_resetA06Delta);
    } else if (g_resetCacheLockReady) {
        AppendDiagLine("reset prototype skipped create cache ptr03=%p ptr06=%p delta=%08x readable=%d",
            (void*)ptr03,
            (void*)ptr06,
            (unsigned)(ptr06 >= ptr03 ? ptr06 - ptr03 : 0),
            (ptr03 && !IsBadReadPtr((void*)ptr03, RESET_PAYLOAD_SIZE)) ? 1 : 0);
    }
#endif
    if (ptr03 && !IsBadReadPtr((void*)ptr03, 64)) {
        char hex[3 * 64 + 1] = {};
        char ascii[65] = {};
        char* out = hex;
        for (int i = 0; i < 64; ++i) {
            uint8_t b = *(uint8_t*)(ptr03 + i);
            wsprintfA(out, "%02X%s", b, i == 63 ? "" : " ");
            out += lstrlenA(out);
            ascii[i] = (b >= 0x20 && b < 0x7f) ? (char)b : '.';
        }
        AppendDiagLine("ProjectCreateWrapper a03 bytes ptr=%p ascii='%s' hex64=%s", (void*)ptr03, ascii, hex);
    }
    if (ptr06 && !IsBadReadPtr((void*)ptr06, 64)) {
        char hex[3 * 64 + 1] = {};
        char ascii[65] = {};
        char* out = hex;
        for (int i = 0; i < 64; ++i) {
            uint8_t b = *(uint8_t*)(ptr06 + i);
            wsprintfA(out, "%02X%s", b, i == 63 ? "" : " ");
            out += lstrlenA(out);
            ascii[i] = (b >= 0x20 && b < 0x7f) ? (char)b : '.';
        }
        AppendDiagLine("ProjectCreateWrapper a06 bytes ptr=%p ascii='%s' hex64=%s", (void*)ptr06, ascii, hex);
    }
}

extern "C" void __attribute__((naked)) SoEProjectCreateWrapperHook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "leal 36(%esp), %eax\n\t"
        "pushl %eax\n\t"
        "call _LogProjectCreateWrapper@4\n\t"
        "popal\n\t"
        "popfl\n\t"
        "movl 4(%esp), %eax\n\t"
        "pushl 48(%esp)\n\t"
        "jmp *_g_projectCreateTraceReturn\n\t"
    );
}

extern "C" void __stdcall LogD2GameCreateEntry(uintptr_t eax, uintptr_t ecx, uintptr_t edx, uintptr_t ebx, uintptr_t ebp, uintptr_t esi, uintptr_t edi, uintptr_t originalEsp)
{
    AppendDiagLine(
        "D2GameCreateEntry eax=%p ecx=%p edx=%p ebx=%p ebp=%p esi=%p edi=%p esp=%p ret=%p s04=%p s08=%p s0c=%p s10=%p s14=%p s18=%p",
        (void*)eax,
        (void*)ecx,
        (void*)edx,
        (void*)ebx,
        (void*)ebp,
        (void*)esi,
        (void*)edi,
        (void*)originalEsp,
        (void*)SafeReadPtr(originalEsp, 0x00),
        (void*)SafeReadPtr(originalEsp, 0x04),
        (void*)SafeReadPtr(originalEsp, 0x08),
        (void*)SafeReadPtr(originalEsp, 0x0c),
        (void*)SafeReadPtr(originalEsp, 0x10),
        (void*)SafeReadPtr(originalEsp, 0x14),
        (void*)SafeReadPtr(originalEsp, 0x18));
}

extern "C" void __stdcall LogD2GameCreateFrame(uintptr_t frame)
{
    uintptr_t edi = SafeReadPtr(frame, 0x00);
    uintptr_t esi = SafeReadPtr(frame, 0x04);
    uintptr_t ebp = SafeReadPtr(frame, 0x08);
    uintptr_t originalEsp = frame + 0x24;
    uintptr_t ebx = SafeReadPtr(frame, 0x10);
    uintptr_t edx = SafeReadPtr(frame, 0x14);
    uintptr_t ecx = SafeReadPtr(frame, 0x18);
    uintptr_t eax = SafeReadPtr(frame, 0x1c);
    LogD2GameCreateEntry(eax, ecx, edx, ebx, ebp, esi, edi, originalEsp);
}

extern "C" void __attribute__((naked)) SoED2GameCreateEntryHook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "pushl %esp\n\t"
        "call _LogD2GameCreateFrame@4\n\t"
        "popal\n\t"
        "popfl\n\t"
        "pushl %ecx\n\t"
        "pushl %ebp\n\t"
        "pushl %esi\n\t"
        "movl %eax, %esi\n\t"
        "jmp *_g_d2gameCreateTraceReturn\n\t"
    );
}

extern "C" void __stdcall LogD2GameTeardownEntry(uintptr_t eax, uintptr_t ecx, uintptr_t edx, uintptr_t ebx, uintptr_t ebp, uintptr_t esi, uintptr_t edi, uintptr_t originalEsp)
{
    AppendDiagLine(
        "D2GameTeardownEntry pGame(eax)=%p ecx=%p edx=%p ebx=%p ebp=%p esi=%p edi=%p esp=%p ret=%p pg+88=%p pg+18=%p pg+1c=%p pg+20=%p",
        (void*)eax,
        (void*)ecx,
        (void*)edx,
        (void*)ebx,
        (void*)ebp,
        (void*)esi,
        (void*)edi,
        (void*)originalEsp,
        (void*)SafeReadPtr(originalEsp, 0x00),
        (void*)SafeReadPtr(eax, 0x88),
        (void*)SafeReadPtr(eax, 0x18),
        (void*)SafeReadPtr(eax, 0x1c),
        (void*)SafeReadPtr(eax, 0x20));
}

extern "C" void __stdcall LogD2GameTeardownFrame(uintptr_t frame)
{
    uintptr_t edi = SafeReadPtr(frame, 0x00);
    uintptr_t esi = SafeReadPtr(frame, 0x04);
    uintptr_t ebp = SafeReadPtr(frame, 0x08);
    uintptr_t originalEsp = frame + 0x24;
    uintptr_t ebx = SafeReadPtr(frame, 0x10);
    uintptr_t edx = SafeReadPtr(frame, 0x14);
    uintptr_t ecx = SafeReadPtr(frame, 0x18);
    uintptr_t eax = SafeReadPtr(frame, 0x1c);
    LogD2GameTeardownEntry(eax, ecx, edx, ebx, ebp, esi, edi, originalEsp);
}

extern "C" void __attribute__((naked)) SoED2GameTeardownEntryHook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "pushl %esp\n\t"
        "call _LogD2GameTeardownFrame@4\n\t"
        "popal\n\t"
        "popfl\n\t"
        "pushl %ebx\n\t"
        "pushl %esi\n\t"
        "pushl %edi\n\t"
        "movl %ecx, %esi\n\t"
        "jmp *_g_d2gameTeardownTraceReturn\n\t"
    );
}

static bool InstallCommandTraceHook()
{
    HMODULE hD2Game = GetModuleHandleA("D2Game.dll");
    if (!hD2Game) return false;

    g_commandTracePatchAddr = (uintptr_t)hD2Game + 0xeaf30;
    bool ok = PatchCommandJump(g_commandTracePatchAddr, (void*)&SoECommandTraceHook, g_commandTraceOriginal, sizeof(g_commandTraceOriginal), &g_commandTraceReturn);
    g_commandTraceHooked = ok;
    AppendDiagLine("command trace hook patch=%p ret=%p ok=%d", (void*)g_commandTracePatchAddr, (void*)g_commandTraceReturn, ok ? 1 : 0);
    return ok;
}

static bool InstallProjectCommandTraceHook()
{
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    if (!hProject) return false;

    g_projectCommandTracePatchAddr = (uintptr_t)hProject + 0x1c9e50;
    bool ok = PatchCommandJump(g_projectCommandTracePatchAddr, (void*)&SoEProjectCommandTraceHook, g_projectCommandTraceOriginal, sizeof(g_projectCommandTraceOriginal), &g_projectCommandTraceReturn);
    g_projectCommandTraceHooked = ok;
    AppendDiagLine("project command trace hook patch=%p ret=%p ok=%d", (void*)g_projectCommandTracePatchAddr, (void*)g_projectCommandTraceReturn, ok ? 1 : 0);
    return ok;
}

#ifdef SOE_D2NET_SEND_TRACE
static bool InstallD2NetSendTraceHook()
{
    HMODULE hD2Net = GetModuleHandleA("D2Net.dll");
    if (!hD2Net) return false;

    g_d2netSendTracePatchAddr = (uintptr_t)hD2Net + 0x63e0;
    bool ok = PatchCommandJump(g_d2netSendTracePatchAddr, (void*)&SoED2NetSendTraceHook, g_d2netSendTraceOriginal, sizeof(g_d2netSendTraceOriginal), &g_d2netSendTraceReturn);
    g_d2netSendTraceHooked = ok;
    AppendDiagLine("D2Net send trace hook patch=%p ret=%p ok=%d", (void*)g_d2netSendTracePatchAddr, (void*)g_d2netSendTraceReturn, ok ? 1 : 0);
    return ok;
}
#endif

#ifdef SOE_D2NET_IAT_TRACE
struct D2NetIatTraceTarget
{
    WORD ordinal;
    void* hook;
    uintptr_t* original;
};

static D2NetIatTraceTarget g_d2netIatTraceTargets[] = {
    {10002, (void*)&SoED2NetIatHook10002, &g_d2netOrig10002},
    {10004, (void*)&SoED2NetIatHook10004, &g_d2netOrig10004},
    {10005, (void*)&SoED2NetIatHook10005, &g_d2netOrig10005},
    {10008, (void*)&SoED2NetIatHook10008, &g_d2netOrig10008},
    {10012, (void*)&SoED2NetIatHook10012, &g_d2netOrig10012},
    {10019, (void*)&SoED2NetIatHook10019, &g_d2netOrig10019},
    {10020, (void*)&SoED2NetIatHook10020, &g_d2netOrig10020},
    {10022, (void*)&SoED2NetIatHook10022, &g_d2netOrig10022},
    {10024, (void*)&SoED2NetIatHook10024, &g_d2netOrig10024},
    {10026, (void*)&SoED2NetIatHook10026, &g_d2netOrig10026},
    {10028, (void*)&SoED2NetIatHook10028, &g_d2netOrig10028},
    {10031, (void*)&SoED2NetIatHook10031, &g_d2netOrig10031},
    {10033, (void*)&SoED2NetIatHook10033, &g_d2netOrig10033},
    {10034, (void*)&SoED2NetIatHook10034, &g_d2netOrig10034},
};

static D2NetIatTraceTarget* FindD2NetIatTraceTarget(WORD ordinal)
{
    for (size_t i = 0; i < sizeof(g_d2netIatTraceTargets) / sizeof(g_d2netIatTraceTargets[0]); ++i) {
        if (g_d2netIatTraceTargets[i].ordinal == ordinal) return &g_d2netIatTraceTargets[i];
    }
    return nullptr;
}

static bool HookD2NetImportsForModule(HMODULE module, const char* moduleName)
{
    if (!module) return false;

    uint8_t* base = (uint8_t*)module;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (IsBadReadPtr(dos, sizeof(*dos)) || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (IsBadReadPtr(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE) return false;

    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress || !dir.Size) return false;

    IMAGE_IMPORT_DESCRIPTOR* imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress);
    bool any = false;

    for (; imp->Name; ++imp) {
        const char* dllName = (const char*)(base + imp->Name);
        if (IsBadStringPtrA(dllName, 32) || lstrcmpiA(dllName, "D2Net.dll") != 0) continue;

        IMAGE_THUNK_DATA* origThunk = (IMAGE_THUNK_DATA*)(base + imp->OriginalFirstThunk);
        IMAGE_THUNK_DATA* firstThunk = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        if (!imp->OriginalFirstThunk) origThunk = firstThunk;

        for (; origThunk->u1.AddressOfData; ++origThunk, ++firstThunk) {
            if (!IMAGE_SNAP_BY_ORDINAL(origThunk->u1.Ordinal)) continue;

            WORD ordinal = (WORD)IMAGE_ORDINAL(origThunk->u1.Ordinal);
            D2NetIatTraceTarget* target = FindD2NetIatTraceTarget(ordinal);
            if (!target) continue;

            uintptr_t current = (uintptr_t)firstThunk->u1.Function;
            if (current == (uintptr_t)target->hook) continue;
            if (!*target->original) *target->original = current;

            DWORD oldProt = 0;
            if (VirtualProtect(&firstThunk->u1.Function, sizeof(uintptr_t), PAGE_READWRITE, &oldProt)) {
                firstThunk->u1.Function = (uintptr_t)target->hook;
                VirtualProtect(&firstThunk->u1.Function, sizeof(uintptr_t), oldProt, &oldProt);
                any = true;
                AppendDiagLine("D2Net IAT trace hooked %s ordinal=%u slot=%p original=%p hook=%p",
                    moduleName,
                    (unsigned)ordinal,
                    (void*)&firstThunk->u1.Function,
                    (void*)current,
                    target->hook);
            }
        }
    }

    return any;
}

static bool InstallD2NetIatTraceHooks()
{
    bool any = false;
    any = HookD2NetImportsForModule(GetModuleHandleA("D2Client.dll"), "D2Client.dll") || any;
    any = HookD2NetImportsForModule(GetModuleHandleA(nullptr), "Game.exe") || any;
    g_d2netIatTraceHooked = any;
    AppendDiagLine("D2Net IAT trace installed any=%d", any ? 1 : 0);
    return any;
}

static void RestoreD2NetImportsForModule(HMODULE module, const char* moduleName)
{
    if (!module) return;

    uint8_t* base = (uint8_t*)module;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (IsBadReadPtr(dos, sizeof(*dos)) || dos->e_magic != IMAGE_DOS_SIGNATURE) return;

    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (IsBadReadPtr(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE) return;

    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress || !dir.Size) return;

    IMAGE_IMPORT_DESCRIPTOR* imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress);
    for (; imp->Name; ++imp) {
        const char* dllName = (const char*)(base + imp->Name);
        if (IsBadStringPtrA(dllName, 32) || lstrcmpiA(dllName, "D2Net.dll") != 0) continue;

        IMAGE_THUNK_DATA* origThunk = (IMAGE_THUNK_DATA*)(base + imp->OriginalFirstThunk);
        IMAGE_THUNK_DATA* firstThunk = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        if (!imp->OriginalFirstThunk) origThunk = firstThunk;

        for (; origThunk->u1.AddressOfData; ++origThunk, ++firstThunk) {
            if (!IMAGE_SNAP_BY_ORDINAL(origThunk->u1.Ordinal)) continue;

            WORD ordinal = (WORD)IMAGE_ORDINAL(origThunk->u1.Ordinal);
            D2NetIatTraceTarget* target = FindD2NetIatTraceTarget(ordinal);
            if (!target || !*target->original || (uintptr_t)firstThunk->u1.Function != (uintptr_t)target->hook) continue;

            DWORD oldProt = 0;
            if (VirtualProtect(&firstThunk->u1.Function, sizeof(uintptr_t), PAGE_READWRITE, &oldProt)) {
                firstThunk->u1.Function = *target->original;
                VirtualProtect(&firstThunk->u1.Function, sizeof(uintptr_t), oldProt, &oldProt);
                AppendDiagLine("D2Net IAT trace restored %s ordinal=%u", moduleName, (unsigned)ordinal);
            }
        }
    }
}

static void RemoveD2NetIatTraceHooks()
{
    if (!g_d2netIatTraceHooked) return;
    RestoreD2NetImportsForModule(GetModuleHandleA("D2Client.dll"), "D2Client.dll");
    RestoreD2NetImportsForModule(GetModuleHandleA(nullptr), "Game.exe");
    g_d2netIatTraceHooked = false;
}
#endif

static bool InstallCreateLifecycleTraceHooks()
{
    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    HMODULE hD2Game = GetModuleHandleA("D2Game.dll");
    if (!hProject || !hD2Game) return false;

    g_projectCreateTracePatchAddr = (uintptr_t)hProject + 0x240b30;
    g_d2gameCreateTracePatchAddr = (uintptr_t)hD2Game + 0x2c2d0;
    g_d2gameTeardownTracePatchAddr = (uintptr_t)hD2Game + 0x2cd30;

    bool okProject = PatchCommandJump(g_projectCreateTracePatchAddr, (void*)&SoEProjectCreateWrapperHook, g_projectCreateTraceOriginal, sizeof(g_projectCreateTraceOriginal), &g_projectCreateTraceReturn);
    bool okCreate = PatchCommandJump(g_d2gameCreateTracePatchAddr, (void*)&SoED2GameCreateEntryHook, g_d2gameCreateTraceOriginal, sizeof(g_d2gameCreateTraceOriginal), &g_d2gameCreateTraceReturn);
    bool okTeardown = PatchCommandJump(g_d2gameTeardownTracePatchAddr, (void*)&SoED2GameTeardownEntryHook, g_d2gameTeardownTraceOriginal, sizeof(g_d2gameTeardownTraceOriginal), &g_d2gameTeardownTraceReturn);

    g_createLifecycleTraceHooked = okProject || okCreate || okTeardown;
    AppendDiagLine(
        "create lifecycle trace hooks project=%p ret=%p ok=%d create=%p ret=%p ok=%d teardown=%p ret=%p ok=%d",
        (void*)g_projectCreateTracePatchAddr,
        (void*)g_projectCreateTraceReturn,
        okProject ? 1 : 0,
        (void*)g_d2gameCreateTracePatchAddr,
        (void*)g_d2gameCreateTraceReturn,
        okCreate ? 1 : 0,
        (void*)g_d2gameTeardownTracePatchAddr,
        (void*)g_d2gameTeardownTraceReturn,
        okTeardown ? 1 : 0);
    return g_createLifecycleTraceHooked;
}

static void RestorePatch(uintptr_t patchAddr, uint8_t* original, size_t len)
{
    if (!patchAddr) return;

    DWORD oldProt = 0;
    if (VirtualProtect((void*)patchAddr, len, PAGE_EXECUTE_READWRITE, &oldProt)) {
        memcpy((void*)patchAddr, original, len);
        FlushInstructionCache(GetCurrentProcess(), (void*)patchAddr, len);
        VirtualProtect((void*)patchAddr, len, oldProt, &oldProt);
    }
}

static void RemoveCommandTraceHook()
{
    if (!g_commandTraceHooked || !g_commandTracePatchAddr) return;

    RestorePatch(g_commandTracePatchAddr, g_commandTraceOriginal, sizeof(g_commandTraceOriginal));
    g_commandTraceHooked = false;
}

static void RemoveProjectCommandTraceHook()
{
    if (!g_projectCommandTraceHooked || !g_projectCommandTracePatchAddr) return;

    RestorePatch(g_projectCommandTracePatchAddr, g_projectCommandTraceOriginal, sizeof(g_projectCommandTraceOriginal));
    g_projectCommandTraceHooked = false;
}

#ifdef SOE_D2NET_SEND_TRACE
static void RemoveD2NetSendTraceHook()
{
    if (!g_d2netSendTraceHooked || !g_d2netSendTracePatchAddr) return;

    RestorePatch(g_d2netSendTracePatchAddr, g_d2netSendTraceOriginal, sizeof(g_d2netSendTraceOriginal));
    g_d2netSendTraceHooked = false;
}
#endif

static void RemoveCreateLifecycleTraceHooks()
{
    if (!g_createLifecycleTraceHooked) return;

    RestorePatch(g_projectCreateTracePatchAddr, g_projectCreateTraceOriginal, sizeof(g_projectCreateTraceOriginal));
    RestorePatch(g_d2gameCreateTracePatchAddr, g_d2gameCreateTraceOriginal, sizeof(g_d2gameCreateTraceOriginal));
    RestorePatch(g_d2gameTeardownTracePatchAddr, g_d2gameTeardownTraceOriginal, sizeof(g_d2gameTeardownTraceOriginal));
    g_createLifecycleTraceHooked = false;
}

#ifdef SOE_NATIVE_CLIENT_LEAVE_PROTOTYPE
extern "C" void __stdcall MaybeRunNativeClientLeaveOnClientThread()
{
    InterlockedExchange((LONG*)&g_nativeClientLeaveDidRun, 0);

    if (InterlockedCompareExchange(&g_nativeClientLeaveRequested, 0, 1) != 1) return;
    if (InterlockedCompareExchange(&g_nativeClientLeaveInProgress, 1, 0) != 0) return;

    HMODULE hD2Client = GetModuleHandleA("D2Client.dll");
    if (!hD2Client) {
        AppendDiagLine("native client leave request failed on heartbeat thread: D2Client not loaded");
        InterlockedExchange(&g_nativeClientLeaveInProgress, 0);
        return;
    }

#ifdef SOE_NATIVE_CALLBACK_LEAVE_PROTOTYPE
    uint8_t callbackArg[0x40] = {};
    FnD2ClientLeaveCallback leaveCallback = (FnD2ClientLeaveCallback)((uint8_t*)hD2Client + 0x439e0);
    AppendDiagLine("native leave callback prototype executing on D2Client heartbeat thread fn=%p arg=%p", (void*)leaveCallback, callbackArg);
    leaveCallback(callbackArg);
    AppendDiagLine("native leave callback prototype returned arg+18=%p arg+1c=%p",
        (void*)SafeReadPtr((uintptr_t)callbackArg, 0x18),
        (void*)SafeReadPtr((uintptr_t)callbackArg, 0x1c));
#else
    FnD2ClientNativeLeave nativeLeave = (FnD2ClientNativeLeave)((uint8_t*)hD2Client + 0x14660);
    AppendDiagLine("native client leave executing on D2Client heartbeat thread fn=%p", (void*)nativeLeave);
    nativeLeave();
    AppendDiagLine("native client leave heartbeat-thread call returned");
#endif

    InterlockedExchange((LONG*)&g_nativeClientLeaveDidRun, 1);
    InterlockedExchange(&g_nativeClientLeaveInProgress, 0);
}

extern "C" void __attribute__((naked)) SoENativeClientHeartbeatHook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "call _MaybeRunNativeClientLeaveOnClientThread@0\n\t"
        "popal\n\t"
        "popfl\n\t"
        "cmpl $0, _g_nativeClientLeaveDidRun\n\t"
        "jne 1f\n\t"
        "subl $0x24, %esp\n\t"
        "movl 0x6fbc9848, %eax\n\t"
        "jmp *_g_nativeClientHeartbeatReturn\n\t"
        "1:\n\t"
        "movl $0, _g_nativeClientLeaveDidRun\n\t"
        "ret\n\t"
    );
}

static bool InstallNativeClientHeartbeatHook()
{
    HMODULE hD2Client = GetModuleHandleA("D2Client.dll");
    if (!hD2Client) {
        AppendDiagLine("native client heartbeat hook failed: D2Client not loaded");
        return false;
    }

    g_nativeClientHeartbeatPatchAddr = (uintptr_t)hD2Client + 0x14b00;
    bool ok = PatchCommandJump(
        g_nativeClientHeartbeatPatchAddr,
        (void*)&SoENativeClientHeartbeatHook,
        g_nativeClientHeartbeatOriginal,
        sizeof(g_nativeClientHeartbeatOriginal),
        &g_nativeClientHeartbeatReturn);
    g_nativeClientHeartbeatHooked = ok;
    AppendDiagLine("native client heartbeat hook patch=%p ret=%p ok=%d",
        (void*)g_nativeClientHeartbeatPatchAddr,
        (void*)g_nativeClientHeartbeatReturn,
        ok ? 1 : 0);
    return ok;
}

static void RemoveNativeClientHeartbeatHook()
{
    if (!g_nativeClientHeartbeatHooked || !g_nativeClientHeartbeatPatchAddr) return;

    RestorePatch(g_nativeClientHeartbeatPatchAddr, g_nativeClientHeartbeatOriginal, sizeof(g_nativeClientHeartbeatOriginal));
    g_nativeClientHeartbeatHooked = false;
}
#endif

#ifdef SOE_NATIVE_LEAVE_ENTRY_TRACE
extern "C" void __stdcall LogNativeLeaveEntry(uintptr_t retAddr, uintptr_t callbackArg)
{
    AppendDiagLine(
        "native leave entry trace caller=%p callbackArg=%p arg+18=%p arg+1c=%p globals c3a4=%p c3e8=%p c404=%p ba2188=%p ba218c=%p",
        (void*)retAddr,
        (void*)callbackArg,
        (void*)SafeReadPtr(callbackArg, 0x18),
        (void*)SafeReadPtr(callbackArg, 0x1c),
        (void*)SafeReadPtr(0x6fbcc3a4, 0),
        (void*)SafeReadPtr(0x6fbcc3e8, 0),
        (void*)SafeReadPtr(0x6fbcc404, 0),
        (void*)SafeReadPtr(0x6fba2188, 0),
        (void*)SafeReadPtr(0x6fba218c, 0));
}

extern "C" void __attribute__((naked)) SoENativeLeaveEntryTraceHook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "movl 36(%esp), %eax\n\t"
        "movl 40(%esp), %ecx\n\t"
        "pushl %ecx\n\t"
        "pushl %eax\n\t"
        "call _LogNativeLeaveEntry@8\n\t"
        "popal\n\t"
        "popfl\n\t"
        "pushl %ecx\n\t"
        "leal 3(%esp), %eax\n\t"
        "jmp *_g_nativeLeaveEntryTraceReturn\n\t"
    );
}

static bool InstallNativeLeaveEntryTraceHook()
{
    HMODULE hD2Client = GetModuleHandleA("D2Client.dll");
    if (!hD2Client) {
        AppendDiagLine("native leave entry trace hook failed: D2Client not loaded");
        return false;
    }

    g_nativeLeaveEntryTracePatchAddr = (uintptr_t)hD2Client + 0x14660;
    bool ok = PatchCommandJump(
        g_nativeLeaveEntryTracePatchAddr,
        (void*)&SoENativeLeaveEntryTraceHook,
        g_nativeLeaveEntryTraceOriginal,
        sizeof(g_nativeLeaveEntryTraceOriginal),
        &g_nativeLeaveEntryTraceReturn);
    g_nativeLeaveEntryTraceHooked = ok;
    AppendDiagLine("native leave entry trace hook patch=%p ret=%p ok=%d",
        (void*)g_nativeLeaveEntryTracePatchAddr,
        (void*)g_nativeLeaveEntryTraceReturn,
        ok ? 1 : 0);
    return ok;
}

static void RemoveNativeLeaveEntryTraceHook()
{
    if (!g_nativeLeaveEntryTraceHooked || !g_nativeLeaveEntryTracePatchAddr) return;

    RestorePatch(g_nativeLeaveEntryTracePatchAddr, g_nativeLeaveEntryTraceOriginal, sizeof(g_nativeLeaveEntryTraceOriginal));
    g_nativeLeaveEntryTraceHooked = false;
}

extern "C" void __stdcall LogNativeLeaveCallbackContext(uintptr_t savedEsp)
{
    uintptr_t d[28] = {};
    for (int i = 0; i < 28; ++i) {
        d[i] = SafeReadPtr(savedEsp, (uintptr_t)i * sizeof(uintptr_t));
    }

    AppendDiagLine(
        "native leave callback context savedEsp=%p edi=%p esi=%p ebp=%p savedEspBeforePushad=%p ebx=%p edx=%p ecx=%p eax=%p eflags=%p",
        (void*)savedEsp,
        (void*)d[0],
        (void*)d[1],
        (void*)d[2],
        (void*)d[3],
        (void*)d[4],
        (void*)d[5],
        (void*)d[6],
        (void*)d[7],
        (void*)d[8]);
    AppendDiagLine(
        "native leave callback stack d09=%p d10=%p d11=%p d12=%p d13=%p d14=%p d15=%p d16=%p d17=%p d18=%p",
        (void*)d[9],
        (void*)d[10],
        (void*)d[11],
        (void*)d[12],
        (void*)d[13],
        (void*)d[14],
        (void*)d[15],
        (void*)d[16],
        (void*)d[17],
        (void*)d[18]);
    AppendDiagLine(
        "native leave callback stack d19=%p d20=%p d21=%p d22=%p d23=%p d24=%p d25=%p d26=%p d27=%p",
        (void*)d[19],
        (void*)d[20],
        (void*)d[21],
        (void*)d[22],
        (void*)d[23],
        (void*)d[24],
        (void*)d[25],
        (void*)d[26],
        (void*)d[27]);
    AppendDiagLine(
        "native leave callback object probes d10+18=%p d10+1c=%p d11+18=%p d11+1c=%p d12+18=%p d12+1c=%p globals c3a4=%p c3e8=%p c404=%p",
        (void*)SafeReadPtr(d[10], 0x18),
        (void*)SafeReadPtr(d[10], 0x1c),
        (void*)SafeReadPtr(d[11], 0x18),
        (void*)SafeReadPtr(d[11], 0x1c),
        (void*)SafeReadPtr(d[12], 0x18),
        (void*)SafeReadPtr(d[12], 0x1c),
        (void*)SafeReadPtr(0x6fbcc3a4, 0),
        (void*)SafeReadPtr(0x6fbcc3e8, 0),
        (void*)SafeReadPtr(0x6fbcc404, 0));
}

extern "C" void __attribute__((naked)) SoENativeLeaveCallbackTraceHook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "pushl %esp\n\t"
        "call _LogNativeLeaveCallbackContext@4\n\t"
        "popal\n\t"
        "popfl\n\t"
        "movl 0x6fbcc3a4, %eax\n\t"
        "testl %eax, %eax\n\t"
        "jmp *_g_nativeLeaveCallbackTraceReturn\n\t"
    );
}

static bool InstallNativeLeaveCallbackTraceHook()
{
    HMODULE hD2Client = GetModuleHandleA("D2Client.dll");
    if (!hD2Client) {
        AppendDiagLine("native leave callback trace hook failed: D2Client not loaded");
        return false;
    }

    g_nativeLeaveCallbackTracePatchAddr = (uintptr_t)hD2Client + 0x439e0;
    bool ok = PatchCommandJump(
        g_nativeLeaveCallbackTracePatchAddr,
        (void*)&SoENativeLeaveCallbackTraceHook,
        g_nativeLeaveCallbackTraceOriginal,
        sizeof(g_nativeLeaveCallbackTraceOriginal),
        &g_nativeLeaveCallbackTraceReturn);
    g_nativeLeaveCallbackTraceHooked = ok;
    AppendDiagLine("native leave callback trace hook patch=%p ret=%p ok=%d",
        (void*)g_nativeLeaveCallbackTracePatchAddr,
        (void*)g_nativeLeaveCallbackTraceReturn,
        ok ? 1 : 0);
    return ok;
}

static void RemoveNativeLeaveCallbackTraceHook()
{
    if (!g_nativeLeaveCallbackTraceHooked || !g_nativeLeaveCallbackTracePatchAddr) return;

    RestorePatch(g_nativeLeaveCallbackTracePatchAddr, g_nativeLeaveCallbackTraceOriginal, sizeof(g_nativeLeaveCallbackTraceOriginal));
    g_nativeLeaveCallbackTraceHooked = false;
}
#endif

#ifdef SOE_STORM_SMSG_TRACE
extern "C" void __stdcall LogStormSmsgDispatch(uintptr_t tag, uintptr_t hwnd, uintptr_t msg, uintptr_t payload)
{
    if (msg != WM_COMMAND && msg != WM_CLOSE && msg != WM_KEYDOWN && msg != WM_KEYUP && msg != WM_LBUTTONDOWN && msg != WM_LBUTTONUP) {
        return;
    }

    AppendDiagLine(
        "StormSMSG tag=%08x hwnd=%p msg=%04x payload=%p p00=%p p04=%p p08=%p p0c=%p p10=%p p14=%p p18=%p p1c=%p",
        (unsigned)tag,
        (void*)hwnd,
        (unsigned)msg,
        (void*)payload,
        (void*)SafeReadPtr(payload, 0x00),
        (void*)SafeReadPtr(payload, 0x04),
        (void*)SafeReadPtr(payload, 0x08),
        (void*)SafeReadPtr(payload, 0x0c),
        (void*)SafeReadPtr(payload, 0x10),
        (void*)SafeReadPtr(payload, 0x14),
        (void*)SafeReadPtr(payload, 0x18),
        (void*)SafeReadPtr(payload, 0x1c));
}

extern "C" void __attribute__((naked)) SoEStormSmsgTraceHook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "movl 52(%esp), %eax\n\t"
        "movl 48(%esp), %ecx\n\t"
        "movl 44(%esp), %edx\n\t"
        "movl 40(%esp), %ebx\n\t"
        "pushl %eax\n\t"
        "pushl %ecx\n\t"
        "pushl %edx\n\t"
        "pushl %ebx\n\t"
        "call _LogStormSmsgDispatch@16\n\t"
        "popal\n\t"
        "popfl\n\t"
        "subl $0x8, %esp\n\t"
        "pushl %ebx\n\t"
        "pushl %esi\n\t"
        "pushl %edi\n\t"
        "jmp *_g_stormSmsgTraceReturn\n\t"
    );
}

static bool InstallStormSmsgTraceHook()
{
    HMODULE hStorm = GetModuleHandleA("Storm.dll");
    if (!hStorm) {
        AppendDiagLine("Storm SMSG trace hook failed: Storm not loaded");
        return false;
    }

    g_stormSmsgTracePatchAddr = (uintptr_t)hStorm + 0x35dd0;
    bool ok = PatchCommandJump(
        g_stormSmsgTracePatchAddr,
        (void*)&SoEStormSmsgTraceHook,
        g_stormSmsgTraceOriginal,
        sizeof(g_stormSmsgTraceOriginal),
        &g_stormSmsgTraceReturn);
    g_stormSmsgTraceHooked = ok;
    AppendDiagLine("Storm SMSG trace hook patch=%p ret=%p ok=%d",
        (void*)g_stormSmsgTracePatchAddr,
        (void*)g_stormSmsgTraceReturn,
        ok ? 1 : 0);
    return ok;
}

static void RemoveStormSmsgTraceHook()
{
    if (!g_stormSmsgTraceHooked || !g_stormSmsgTracePatchAddr) return;

    RestorePatch(g_stormSmsgTracePatchAddr, g_stormSmsgTraceOriginal, sizeof(g_stormSmsgTraceOriginal));
    g_stormSmsgTraceHooked = false;
}
#endif
#endif

#ifdef SOE_SPAWN_TRACE
extern "C" void LogSpawnE1250(uintptr_t eax, uintptr_t ebx, uintptr_t ecx, uintptr_t edx, uintptr_t ebp, uintptr_t esi, uintptr_t edi, uintptr_t originalEsp)
{
    LONG count = InterlockedIncrement(&g_spawnTraceE1250Count);
    if (count > 160) return;

    uintptr_t ret = SafeReadPtr(originalEsp, 0x00);
    uintptr_t a1 = SafeReadPtr(originalEsp, 0x04);
    uintptr_t a2 = SafeReadPtr(originalEsp, 0x08);
    uintptr_t a3 = SafeReadPtr(originalEsp, 0x0c);
    uintptr_t a4 = SafeReadPtr(originalEsp, 0x10);
    uintptr_t a5 = SafeReadPtr(originalEsp, 0x14);

    AppendDiagLine(
        "SPAWN_E1250 #%ld pGame(ebx)=%p eax=%p ecx=%p edx=%p ebp=%p esi=%p edi=%p esp=%p ret=%p a1=%p a2=%p a3=%p a4=%p a5=%p pg+1c=%p pg+70=%p pg+94=%p pg+f0=%p",
        count,
        (void*)ebx, (void*)eax, (void*)ecx, (void*)edx, (void*)ebp, (void*)esi, (void*)edi,
        (void*)originalEsp, (void*)ret, (void*)a1, (void*)a2, (void*)a3, (void*)a4, (void*)a5,
        (void*)SafeReadPtr(ebx, 0x1c), (void*)SafeReadPtr(ebx, 0x70), (void*)SafeReadPtr(ebx, 0x94),
        (void*)(ebx + 0xf0));
}

extern "C" void LogAttachD0320(uintptr_t eax, uintptr_t ebx, uintptr_t ecx, uintptr_t edx, uintptr_t ebp, uintptr_t esi, uintptr_t edi, uintptr_t originalEsp)
{
    LONG count = InterlockedIncrement(&g_spawnTraceD0320Count);
    if (count > 240) return;

    uintptr_t ret = SafeReadPtr(originalEsp, 0x00);
    uintptr_t unit = SafeReadPtr(originalEsp, 0x04);
    uintptr_t arg2 = SafeReadPtr(originalEsp, 0x08);

    AppendDiagLine(
        "ATTACH_D0320 #%ld pGame(ecx)=%p unit=%p arg2=%p ret=%p eax=%p ebx=%p edx=%p ebp=%p esi=%p edi=%p unit+00=%p unit+04=%p unit+0c=%p unit+10=%p unit+14=%p unit+18=%p unit+1c=%p unit+20=%p unit+2c=%p unit+c4=%p unit+c8=%p unit+e4=%p pg1320=%p",
        count,
        (void*)ecx, (void*)unit, (void*)arg2, (void*)ret,
        (void*)eax, (void*)ebx, (void*)edx, (void*)ebp, (void*)esi, (void*)edi,
        (void*)SafeReadPtr(unit, 0x00), (void*)SafeReadPtr(unit, 0x04),
        (void*)SafeReadPtr(unit, 0x0c), (void*)SafeReadPtr(unit, 0x10),
        (void*)SafeReadPtr(unit, 0x14), (void*)SafeReadPtr(unit, 0x18),
        (void*)SafeReadPtr(unit, 0x1c), (void*)SafeReadPtr(unit, 0x20),
        (void*)SafeReadPtr(unit, 0x2c), (void*)SafeReadPtr(unit, 0xc4),
        (void*)SafeReadPtr(unit, 0xc8), (void*)SafeReadPtr(unit, 0xe4),
        (void*)SafeReadPtr(ecx, 0x1320));
}

extern "C" void __attribute__((naked)) SoESpawnE1250Hook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "movl %esp, %ebp\n\t"
        "leal 36(%ebp), %eax\n\t"
        "pushl %eax\n\t"
        "pushl 0(%ebp)\n\t"
        "pushl 4(%ebp)\n\t"
        "pushl 8(%ebp)\n\t"
        "pushl 20(%ebp)\n\t"
        "pushl 24(%ebp)\n\t"
        "pushl 16(%ebp)\n\t"
        "pushl 28(%ebp)\n\t"
        "call _LogSpawnE1250\n\t"
        "addl $36, %esp\n\t"
        "popal\n\t"
        "popfl\n\t"
        "movl 0x8(%esp), %eax\n\t"
        "movl 0x70(%ebx), %ecx\n\t"
        "jmp *_g_spawnTraceE1250Return\n\t"
    );
}

extern "C" void __attribute__((naked)) SoEAttachD0320Hook()
{
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "movl %esp, %ebp\n\t"
        "leal 36(%ebp), %eax\n\t"
        "pushl %eax\n\t"
        "pushl 0(%ebp)\n\t"
        "pushl 4(%ebp)\n\t"
        "pushl 8(%ebp)\n\t"
        "pushl 20(%ebp)\n\t"
        "pushl 24(%ebp)\n\t"
        "pushl 16(%ebp)\n\t"
        "pushl 28(%ebp)\n\t"
        "call _LogAttachD0320\n\t"
        "addl $36, %esp\n\t"
        "popal\n\t"
        "popfl\n\t"
        "pushl %ecx\n\t"
        "pushl %ebx\n\t"
        "pushl %ebp\n\t"
        "pushl %esi\n\t"
        "movl 0x14(%esp), %esi\n\t"
        "jmp *_g_spawnTraceD0320Return\n\t"
    );
}

static bool PatchJump(uintptr_t patchAddr, void* hook, uint8_t* original, size_t patchLen, uintptr_t* returnAddr)
{
    DWORD oldProt = 0;
    if (!VirtualProtect((void*)patchAddr, patchLen, PAGE_EXECUTE_READWRITE, &oldProt)) return false;

    memcpy(original, (void*)patchAddr, patchLen);
    *returnAddr = patchAddr + patchLen;

    uint8_t patch[16] = {};
    patch[0] = 0xE9;
    *(int32_t*)&patch[1] = (int32_t)((uintptr_t)hook - (patchAddr + 5));
    for (size_t i = 5; i < patchLen; ++i) patch[i] = 0x90;
    memcpy((void*)patchAddr, patch, patchLen);
    FlushInstructionCache(GetCurrentProcess(), (void*)patchAddr, patchLen);
    VirtualProtect((void*)patchAddr, patchLen, oldProt, &oldProt);
    return true;
}

static bool InstallSpawnTraceHooks()
{
    HMODULE hD2Game = GetModuleHandleA("D2Game.dll");
    if (!hD2Game) return false;

    g_spawnTraceE1250PatchAddr = (uintptr_t)hD2Game + 0xe1250;
    g_spawnTraceD0320PatchAddr = (uintptr_t)hD2Game + 0xd0320;

    bool ok1 = PatchJump(g_spawnTraceE1250PatchAddr, (void*)&SoESpawnE1250Hook, g_spawnTraceE1250Original, sizeof(g_spawnTraceE1250Original), &g_spawnTraceE1250Return);
    bool ok2 = PatchJump(g_spawnTraceD0320PatchAddr, (void*)&SoEAttachD0320Hook, g_spawnTraceD0320Original, sizeof(g_spawnTraceD0320Original), &g_spawnTraceD0320Return);
    g_spawnTraceHooked = ok1 && ok2;
    AppendDiagLine("spawn trace hooks e1250=%p ret=%p ok=%d d0320=%p ret=%p ok=%d",
        (void*)g_spawnTraceE1250PatchAddr, (void*)g_spawnTraceE1250Return, ok1 ? 1 : 0,
        (void*)g_spawnTraceD0320PatchAddr, (void*)g_spawnTraceD0320Return, ok2 ? 1 : 0);
    return g_spawnTraceHooked;
}
#endif

extern "C" void __attribute__((naked)) SoEDiagLifecycleReadyHook()
{
    __asm__ __volatile__(
        "movl %ebx, _g_diagCachedPGame\n\t"
        "movl 0x10(%esp), %eax\n\t"
        "xorl %edx, %edx\n\t"
        "jmp *_g_diagLifecycleReturn\n\t"
    );
}

static bool InstallLifecycleDiagHook()
{
    HMODULE hD2Game = GetModuleHandleA("D2Game.dll");
    if (!hD2Game) return false;

    g_diagLifecyclePatchAddr = (uintptr_t)hD2Game + 0x2c4ae;
    g_diagLifecycleReturn = g_diagLifecyclePatchAddr + sizeof(g_diagLifecycleOriginal);

    DWORD oldProt = 0;
    if (!VirtualProtect((void*)g_diagLifecyclePatchAddr, sizeof(g_diagLifecycleOriginal), PAGE_EXECUTE_READWRITE, &oldProt)) {
        return false;
    }

    memcpy(g_diagLifecycleOriginal, (void*)g_diagLifecyclePatchAddr, sizeof(g_diagLifecycleOriginal));

    uint8_t patch[6] = {};
    patch[0] = 0xE9;
    *(int32_t*)&patch[1] = (int32_t)((uintptr_t)&SoEDiagLifecycleReadyHook - (g_diagLifecyclePatchAddr + 5));
    patch[5] = 0x90;
    memcpy((void*)g_diagLifecyclePatchAddr, patch, sizeof(patch));
    FlushInstructionCache(GetCurrentProcess(), (void*)g_diagLifecyclePatchAddr, sizeof(patch));
    VirtualProtect((void*)g_diagLifecyclePatchAddr, sizeof(g_diagLifecycleOriginal), oldProt, &oldProt);

    g_diagLifecycleHooked = true;
    AppendDiagLine("installed lifecycle hook patch=%p return=%p", (void*)g_diagLifecyclePatchAddr, (void*)g_diagLifecycleReturn);
    return true;
}

static void RemoveLifecycleDiagHook()
{
    if (!g_diagLifecycleHooked || !g_diagLifecyclePatchAddr) return;

    DWORD oldProt = 0;
    if (VirtualProtect((void*)g_diagLifecyclePatchAddr, sizeof(g_diagLifecycleOriginal), PAGE_EXECUTE_READWRITE, &oldProt)) {
        memcpy((void*)g_diagLifecyclePatchAddr, g_diagLifecycleOriginal, sizeof(g_diagLifecycleOriginal));
        FlushInstructionCache(GetCurrentProcess(), (void*)g_diagLifecyclePatchAddr, sizeof(g_diagLifecycleOriginal));
        VirtualProtect((void*)g_diagLifecyclePatchAddr, sizeof(g_diagLifecycleOriginal), oldProt, &oldProt);
    }
    g_diagLifecycleHooked = false;
}

static void DumpCachedPGame()
{
    uintptr_t pGame = g_diagCachedPGame;
    uintptr_t client = SafeReadPtr(pGame, 0x88);
    uintptr_t unit = SafeReadPtr(client, 0x174);
    uintptr_t clientGame = SafeReadPtr(client, 0x1a8);
    uintptr_t u14 = SafeReadPtr(unit, 0x14);
    uintptr_t u2c = SafeReadPtr(unit, 0x2c);

    AppendDiagLine("F11 pGame=%p client=%p client+174(unit)=%p client+1a8=%p unit+14=%p unit+2c=%p",
        (void*)pGame, (void*)client, (void*)unit, (void*)clientGame, (void*)u14, (void*)u2c);

    DumpUnitTableCounts("G1120", pGame, 0x1120, unit);
    DumpUnitTableCounts("G1320", pGame, 0x1320, unit);
    DumpUnitTableCounts("G1520", pGame, 0x1520, unit);
    DumpUnitTableCounts("G1720", pGame, 0x1720, unit);
    DumpUnitTableCounts("G1920", pGame, 0x1920, unit);
    DumpUnitTableCounts("G1B24", pGame, 0x1b24, unit);
    DumpMonsterUnitSamples(pGame);

    if (u14) {
        AppendDiagLine("U14 base=%p +00=%p +04=%p +08=%p +0c=%p +10=%p +14=%p +18=%p +1c=%p",
            (void*)u14,
            (void*)SafeReadPtr(u14, 0x00), (void*)SafeReadPtr(u14, 0x04),
            (void*)SafeReadPtr(u14, 0x08), (void*)SafeReadPtr(u14, 0x0c),
            (void*)SafeReadPtr(u14, 0x10), (void*)SafeReadPtr(u14, 0x14),
            (void*)SafeReadPtr(u14, 0x18), (void*)SafeReadPtr(u14, 0x1c));
    }

    if (u2c) {
        AppendDiagLine("U2C base=%p +00=%p +04=%p +08=%p +0c=%p +10=%p +14=%p +18=%p +1c=%p +20=%p +24=%p +28=%p +2c=%p",
            (void*)u2c,
            (void*)SafeReadPtr(u2c, 0x00), (void*)SafeReadPtr(u2c, 0x04),
            (void*)SafeReadPtr(u2c, 0x08), (void*)SafeReadPtr(u2c, 0x0c),
            (void*)SafeReadPtr(u2c, 0x10), (void*)SafeReadPtr(u2c, 0x14),
            (void*)SafeReadPtr(u2c, 0x18), (void*)SafeReadPtr(u2c, 0x1c),
            (void*)SafeReadPtr(u2c, 0x20), (void*)SafeReadPtr(u2c, 0x24),
            (void*)SafeReadPtr(u2c, 0x28), (void*)SafeReadPtr(u2c, 0x2c));

        uintptr_t path1c = SafeReadPtr(u2c, 0x1c);
        uintptr_t path20 = SafeReadPtr(u2c, 0x20);
        if (path1c) {
            AppendDiagLine("PATH1C base=%p +00=%p +04=%p +08=%p +0c=%p +10=%p +14=%p +18=%p +1c=%p +20=%p +24=%p +28=%p +2c=%p",
                (void*)path1c,
                (void*)SafeReadPtr(path1c, 0x00), (void*)SafeReadPtr(path1c, 0x04),
                (void*)SafeReadPtr(path1c, 0x08), (void*)SafeReadPtr(path1c, 0x0c),
                (void*)SafeReadPtr(path1c, 0x10), (void*)SafeReadPtr(path1c, 0x14),
                (void*)SafeReadPtr(path1c, 0x18), (void*)SafeReadPtr(path1c, 0x1c),
                (void*)SafeReadPtr(path1c, 0x20), (void*)SafeReadPtr(path1c, 0x24),
                (void*)SafeReadPtr(path1c, 0x28), (void*)SafeReadPtr(path1c, 0x2c));
            DumpPtrBlock("PATH1C_10", SafeReadPtr(path1c, 0x10));
            DumpPtrBlock("PATH1C_20", SafeReadPtr(path1c, 0x20));
            DumpPtrBlock("PATH1C_2C", SafeReadPtr(path1c, 0x2c));
            uintptr_t path1c10 = SafeReadPtr(path1c, 0x10);
            DumpPtrBlock("PATH1C_10_20", SafeReadPtr(path1c10, 0x20));
            DumpPtrBlock("PATH1C_10_24", SafeReadPtr(path1c10, 0x24));
            DumpPtrBlock("PATH1C_10_08", SafeReadPtr(path1c10, 0x08));
            DumpPtrBlock("PATH1C_10_1C", SafeReadPtr(path1c10, 0x1c));
        }
        if (path20) {
            AppendDiagLine("PATH20 base=%p +00=%p +04=%p +08=%p +0c=%p +10=%p +14=%p +18=%p +1c=%p +20=%p +24=%p +28=%p +2c=%p",
                (void*)path20,
                (void*)SafeReadPtr(path20, 0x00), (void*)SafeReadPtr(path20, 0x04),
                (void*)SafeReadPtr(path20, 0x08), (void*)SafeReadPtr(path20, 0x0c),
                (void*)SafeReadPtr(path20, 0x10), (void*)SafeReadPtr(path20, 0x14),
                (void*)SafeReadPtr(path20, 0x18), (void*)SafeReadPtr(path20, 0x1c),
                (void*)SafeReadPtr(path20, 0x20), (void*)SafeReadPtr(path20, 0x24),
                (void*)SafeReadPtr(path20, 0x28), (void*)SafeReadPtr(path20, 0x2c));
            DumpPtrBlock("PATH20_10", SafeReadPtr(path20, 0x10));
            DumpPtrBlock("PATH20_20", SafeReadPtr(path20, 0x20));
            DumpPtrBlock("PATH20_2C", SafeReadPtr(path20, 0x2c));
            uintptr_t path2010 = SafeReadPtr(path20, 0x10);
            DumpPtrBlock("PATH20_10_20", SafeReadPtr(path2010, 0x20));
            DumpPtrBlock("PATH20_10_24", SafeReadPtr(path2010, 0x24));
            DumpPtrBlock("PATH20_10_08", SafeReadPtr(path2010, 0x08));
            DumpPtrBlock("PATH20_10_1C", SafeReadPtr(path2010, 0x1c));
        }
    }
}

static DWORD WINAPI DiagHotkeyThread(LPVOID)
{
    AppendDiagLine("F11 post-load diagnostic hotkey thread started");
    bool wasDown = false;
    while (!g_shutdown) {
        bool down = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
        if (down && !wasDown) DumpCachedPGame();
        wasDown = down;
        Sleep(50);
    }
    return 0;
}

#if defined(SOE_COMMAND_TRACE) && defined(SOE_RESET_PROTOTYPE)
static void TryExperimentalResetReplay()
{
    if (InterlockedCompareExchange(&g_resetInProgress, 1, 0) != 0) {
        AppendDiagLine("reset prototype ignored: already in progress");
        return;
    }

    uintptr_t args[12] = {};
    uintptr_t a06Delta = 0;
    bool cached = false;

    if (g_resetCacheLockReady) {
        EnterCriticalSection(&g_resetCacheLock);
        cached = g_resetCachedCreate;
        if (cached) {
            memcpy(args, g_resetArgs, sizeof(args));
            memcpy(g_resetReplayPayload, g_resetPayload, RESET_PAYLOAD_SIZE);
            a06Delta = g_resetA06Delta;
        }
        LeaveCriticalSection(&g_resetCacheLock);
    }

    if (!cached || a06Delta >= RESET_PAYLOAD_SIZE) {
        AppendDiagLine("reset prototype aborted: no cached create payload cached=%d a06delta=%08x", cached ? 1 : 0, (unsigned)a06Delta);
        InterlockedExchange(&g_resetInProgress, 0);
        return;
    }

    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    HMODULE hD2Game = GetModuleHandleA("D2Game.dll");
    if (!hProject || !hD2Game) {
        AppendDiagLine("reset prototype aborted: missing modules project=%p d2game=%p", hProject, hD2Game);
        InterlockedExchange(&g_resetInProgress, 0);
        return;
    }

    FnD2GameSafeTeardown safeTeardown = (FnD2GameSafeTeardown)((uint8_t*)hD2Game + 0x2ce20);
    FnProjectCreateWrapper createWrapper = (FnProjectCreateWrapper)((uint8_t*)hProject + 0x240b30);
    void* replayA03 = g_resetReplayPayload;
    void* replayA06 = g_resetReplayPayload + a06Delta;

    AppendDiagLine(
        "reset prototype start hotkey=F12 char='%s' teardown=%p create=%p a01=%08x a04=%08x a05=%08x a09=%08x",
        (const char*)replayA06,
        (void*)safeTeardown,
        (void*)createWrapper,
        (unsigned)args[0],
        (unsigned)args[3],
        (unsigned)args[4],
        (unsigned)args[8]);

    safeTeardown();
    AppendDiagLine("reset prototype teardown returned; replaying create after delay");
    Sleep(1200);

    createWrapper(
        args[0], args[1], replayA03, args[3], args[4], replayA06,
        args[6], args[7], args[8], args[9], args[10], args[11]);

    AppendDiagLine("reset prototype create returned");
    InterlockedExchange(&g_resetInProgress, 0);
}

static DWORD WINAPI ResetPrototypeHotkeyThread(LPVOID)
{
    AppendDiagLine("F12 experimental reset prototype hotkey thread started");
    bool wasDown = false;
    while (!g_shutdown) {
        bool down = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
        if (down && !wasDown) TryExperimentalResetReplay();
        wasDown = down;
        Sleep(50);
    }
    return 0;
}
#endif

#if defined(SOE_COMMAND_TRACE) && defined(SOE_PACKET_RESET_PROTOTYPE)
static void TryExperimentalPacketReset()
{
    if (InterlockedCompareExchange(&g_packetResetInProgress, 1, 0) != 0) {
        AppendDiagLine("packet reset ignored: already in progress");
        return;
    }

    bool hasCreate = false;
    bool hasLeave = false;
    uint8_t createPacket[PROJECT_PACKET_SIZE] = {};
    uint8_t leavePacket[PROJECT_PACKET_SIZE] = {};

    if (g_packetResetLockReady) {
        EnterCriticalSection(&g_packetResetLock);
        hasCreate = g_packetResetHasCreate;
        hasLeave = g_packetResetHasLeave;
        if (hasCreate) memcpy(createPacket, g_packetResetCreate, PROJECT_PACKET_SIZE);
        if (hasLeave) memcpy(leavePacket, g_packetResetLeave, PROJECT_PACKET_SIZE);
        LeaveCriticalSection(&g_packetResetLock);
    }

    if (!hasCreate || !hasLeave) {
        AppendDiagLine("packet reset aborted: cached create=%d cached leave=%d", hasCreate ? 1 : 0, hasLeave ? 1 : 0);
        InterlockedExchange(&g_packetResetInProgress, 0);
        return;
    }

    HMODULE hProject = GetModuleHandleA("ProjectDiablo.dll");
    if (!hProject) {
        AppendDiagLine("packet reset aborted: missing ProjectDiablo.dll");
        InterlockedExchange(&g_packetResetInProgress, 0);
        return;
    }

    FnProjectCommandDispatcher dispatch = (FnProjectCommandDispatcher)((uint8_t*)hProject + 0x1c9e50);

    AppendDiagLine("packet reset start hotkey=F12 dispatch=%p hasLeave=%d createName='%s'",
        (void*)dispatch,
        hasLeave ? 1 : 0,
        (const char*)(createPacket + 0x19));

    dispatch(leavePacket);
    AppendDiagLine("packet reset leave command returned; waiting before create");
    Sleep(2500);
    dispatch(createPacket);
    AppendDiagLine("packet reset create command returned");

    InterlockedExchange(&g_packetResetInProgress, 0);
}

static DWORD WINAPI PacketResetHotkeyThread(LPVOID)
{
    AppendDiagLine("F12 experimental packet reset hotkey thread started");
    bool wasDown = false;
    while (!g_shutdown) {
        bool down = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
        if (down && !wasDown) TryExperimentalPacketReset();
        wasDown = down;
        Sleep(50);
    }
    return 0;
}
#endif

#if defined(SOE_COMMAND_TRACE) && defined(SOE_PUMP_LEAVE_PROTOTYPE)
static DWORD WINAPI PumpLeaveHotkeyThread(LPVOID)
{
    AppendDiagLine("F12 pump-thread leave prototype hotkey thread started");
    bool wasDown = false;
    while (!g_shutdown) {
        bool down = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
        if (down && !wasDown) {
            InterlockedExchange(&g_pumpLeaveRequested, 1);
            AppendDiagLine("pump leave prototype requested by F12");
        }
        wasDown = down;
        Sleep(50);
    }
    return 0;
}
#endif
#endif

// ── Log header ────────────────────────────────────────────────────────────────
#if defined(SOE_COMMAND_TRACE) && defined(SOE_WMCLOSE_LEAVE_PROTOTYPE)
static void TryPostWmCloseLeave()
{
    if (InterlockedCompareExchange(&g_wmCloseLeaveInProgress, 1, 0) != 0) {
        AppendDiagLine("WM_CLOSE leave prototype ignored: already in progress");
        return;
    }

    HWND hwnd = GetForegroundWindow();
    DWORD ownerPid = 0;
    GetWindowThreadProcessId(hwnd, &ownerPid);
    DWORD currentPid = GetCurrentProcessId();
    char title[128] = {};
    if (hwnd) GetWindowTextA(hwnd, title, sizeof(title));

    if (!hwnd || ownerPid != currentPid) {
        AppendDiagLine("WM_CLOSE leave prototype aborted hwnd=%p ownerPid=%lu currentPid=%lu title='%s'",
            hwnd,
            (unsigned long)ownerPid,
            (unsigned long)currentPid,
            title);
        InterlockedExchange(&g_wmCloseLeaveInProgress, 0);
        return;
    }

#ifdef SOE_ESC_BEFORE_WMCLOSE
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_ESCAPE;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = VK_ESCAPE;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    UINT sent = SendInput(2, inputs, sizeof(INPUT));
    AppendDiagLine("WM_CLOSE leave prototype sent ESC first hwnd=%p sent=%u", hwnd, (unsigned)sent);
    Sleep(350);
#endif

    BOOL ok = PostMessageA(hwnd, WM_CLOSE, 0, 0);
    AppendDiagLine("WM_CLOSE leave prototype posted hwnd=%p ownerPid=%lu title='%s' ok=%d",
        hwnd,
        (unsigned long)ownerPid,
        title,
        ok ? 1 : 0);

    Sleep(1000);
    InterlockedExchange(&g_wmCloseLeaveInProgress, 0);
}

static DWORD WINAPI WmCloseLeaveHotkeyThread(LPVOID)
{
    AppendDiagLine("F12 WM_CLOSE leave prototype hotkey thread started");
    bool wasDown = false;
    while (!g_shutdown) {
        bool down = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
        if (down && !wasDown) TryPostWmCloseLeave();
        wasDown = down;
        Sleep(50);
    }
    return 0;
}
#endif

#if defined(SOE_COMMAND_TRACE) && defined(SOE_ESC_CLICK_LEAVE_PROTOTYPE)
static void SendKeyTap(WORD vk)
{
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = vk;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = vk;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

static void SendMouseClickAtScreenPoint(POINT pt)
{
    INPUT inputs[3] = {};
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dx = (LONG)((pt.x * 65535LL) / GetSystemMetrics(SM_CXSCREEN));
    inputs[0].mi.dy = (LONG)((pt.y * 65535LL) / GetSystemMetrics(SM_CYSCREEN));
    inputs[0].mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    inputs[2].type = INPUT_MOUSE;
    inputs[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(3, inputs, sizeof(INPUT));
}

static void TryEscClickLeave()
{
    if (InterlockedCompareExchange(&g_escClickLeaveInProgress, 1, 0) != 0) {
        AppendDiagLine("ESC-click leave prototype ignored: already in progress");
        return;
    }

    HWND hwnd = GetForegroundWindow();
    DWORD ownerPid = 0;
    GetWindowThreadProcessId(hwnd, &ownerPid);
    DWORD currentPid = GetCurrentProcessId();
    char title[128] = {};
    if (hwnd) GetWindowTextA(hwnd, title, sizeof(title));

    if (!hwnd || ownerPid != currentPid) {
        AppendDiagLine("ESC-click leave prototype aborted hwnd=%p ownerPid=%lu currentPid=%lu title='%s'",
            hwnd,
            (unsigned long)ownerPid,
            (unsigned long)currentPid,
            title);
        InterlockedExchange(&g_escClickLeaveInProgress, 0);
        return;
    }

    RECT client = {};
    GetClientRect(hwnd, &client);
    int width = client.right - client.left;
    int height = client.bottom - client.top;
    POINT pt = {};
    pt.x = (width * 31) / 100;
    pt.y = (height * 86) / 100;
    ClientToScreen(hwnd, &pt);

    AppendDiagLine("ESC-click leave prototype start hwnd=%p ownerPid=%lu title='%s' client=%dx%d clickClient=%d,%d clickScreen=%d,%d",
        hwnd,
        (unsigned long)ownerPid,
        title,
        width,
        height,
        (width * 31) / 100,
        (height * 86) / 100,
        pt.x,
        pt.y);

    SendKeyTap(VK_ESCAPE);
    Sleep(450);
    SendMouseClickAtScreenPoint(pt);
    AppendDiagLine("ESC-click leave prototype sent click");

    Sleep(1200);
    InterlockedExchange(&g_escClickLeaveInProgress, 0);
}

static DWORD WINAPI EscClickLeaveHotkeyThread(LPVOID)
{
    AppendDiagLine("F12 ESC-click leave prototype hotkey thread started");
    bool wasDown = false;
    while (!g_shutdown) {
        bool down = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
        if (down && !wasDown) TryEscClickLeave();
        wasDown = down;
        Sleep(50);
    }
    return 0;
}
#endif

#if defined(SOE_COMMAND_TRACE) && defined(SOE_KEYBOARD_LEAVE_PROTOTYPE)
static WORD KeyNameToVk(const char* key)
{
    if (!key || !key[0]) return 0;
    if (_stricmp(key, "ESC") == 0 || _stricmp(key, "ESCAPE") == 0) return VK_ESCAPE;
    if (_stricmp(key, "ENTER") == 0 || _stricmp(key, "RETURN") == 0) return VK_RETURN;
    if (_stricmp(key, "UP") == 0) return VK_UP;
    if (_stricmp(key, "DOWN") == 0) return VK_DOWN;
    if (_stricmp(key, "LEFT") == 0) return VK_LEFT;
    if (_stricmp(key, "RIGHT") == 0) return VK_RIGHT;
    if (_stricmp(key, "TAB") == 0) return VK_TAB;
    if (_stricmp(key, "SPACE") == 0) return VK_SPACE;
    if (key[1] == '\0') {
        char c = key[0];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return (WORD)c;
    }
    return 0;
}

static void SendKeyboardLeaveTap(WORD vk)
{
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = vk;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = vk;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

static void SendWindowClientClick(HWND hwnd, int x, int y)
{
    LPARAM pos = MAKELPARAM(x, y);
    PostMessageA(hwnd, WM_MOUSEMOVE, 0, pos);
    PostMessageA(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, pos);
    PostMessageA(hwnd, WM_LBUTTONUP, 0, pos);
}

static uintptr_t ReadD2ClientPlayerUnit()
{
    HMODULE hD2Client = GetModuleHandleA("D2Client.dll");
    if (!hD2Client) return 0;
    return SafeReadPtr((uintptr_t)hD2Client, D2CLIENT_PLAYER_UNIT_RVA);
}

static bool WaitForMainMenuReady(HWND expectedHwnd, int timeoutMs, int stableMs)
{
    DWORD start = GetTickCount();
    DWORD stableStart = 0;
    uintptr_t lastPlayerUnit = 0;
    if (timeoutMs < 500) timeoutMs = 500;
    if (stableMs < 100) stableMs = 100;

    while ((int)(GetTickCount() - start) <= timeoutMs) {
        HWND foreground = GetForegroundWindow();
        uintptr_t playerUnit = ReadD2ClientPlayerUnit();
        bool sameWindow = foreground == expectedHwnd;
        bool menuLike = playerUnit == 0;

        if (menuLike && sameWindow) {
            if (!stableStart) stableStart = GetTickCount();
            if ((int)(GetTickCount() - stableStart) >= stableMs) {
                AppendDiagLine("keyboard leave prototype main-menu ready playerUnit=%p stableMs=%d elapsedMs=%lu",
                    (void*)playerUnit,
                    stableMs,
                    (unsigned long)(GetTickCount() - start));
                return true;
            }
        } else {
            stableStart = 0;
            lastPlayerUnit = playerUnit;
        }

        Sleep(50);
    }

    AppendDiagLine("keyboard leave prototype main-menu wait timed out timeoutMs=%d stableMs=%d lastPlayerUnit=%p foreground=%p expected=%p",
        timeoutMs,
        stableMs,
        (void*)lastPlayerUnit,
        GetForegroundWindow(),
        expectedHwnd);
    return false;
}

static bool AutomationChordIsPressed(WORD keyCode, int modifiers)
{
    if (!keyCode) return false;
    if ((GetAsyncKeyState((int)keyCode) & 0x8000) == 0) return false;

    if ((modifiers & 0x0001) && (GetAsyncKeyState(VK_MENU) & 0x8000) == 0) return false;
    if ((modifiers & 0x0002) && (GetAsyncKeyState(VK_CONTROL) & 0x8000) == 0) return false;
    if ((modifiers & 0x0004) && (GetAsyncKeyState(VK_SHIFT) & 0x8000) == 0) return false;
    if ((modifiers & 0x0008) && (GetAsyncKeyState(VK_LWIN) & 0x8000) == 0 && (GetAsyncKeyState(VK_RWIN) & 0x8000) == 0) return false;

    return true;
}

static void GetAutomationIniPath(char* iniPath, size_t iniPathSize)
{
    if (!iniPath || iniPathSize == 0) return;
    iniPath[0] = '\0';
    GetModuleFileNameA(nullptr, iniPath, (DWORD)iniPathSize);
    char* slash = strrchr(iniPath, '\\');
    if (slash) slash[1] = '\0';
    strncat(iniPath, "DropIdentified.ini", iniPathSize - strlen(iniPath) - 1);
}

static WORD DifficultyNameToVk(const char* difficulty)
{
    if (!difficulty || !difficulty[0]) return 'R';
    if (_stricmp(difficulty, "NORMAL") == 0 || _stricmp(difficulty, "R") == 0) return 'R';
    if (_stricmp(difficulty, "NIGHTMARE") == 0 || _stricmp(difficulty, "N") == 0) return 'N';
    if (_stricmp(difficulty, "HELL") == 0 || _stricmp(difficulty, "H") == 0) return 'H';
    return 'R';
}

static void TrimToken(char* text)
{
    if (!text) return;
    char* start = text;
    while (*start == ' ' || *start == '\t') ++start;
    if (start != text) memmove(text, start, strlen(start) + 1);

    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t' || text[len - 1] == '\r' || text[len - 1] == '\n')) {
        text[--len] = '\0';
    }
}

static void TryKeyboardLeave()
{
    if (InterlockedCompareExchange(&g_keyboardLeaveInProgress, 1, 0) != 0) {
        AppendDiagLine("keyboard leave prototype ignored: already in progress");
        return;
    }

    HWND hwnd = GetForegroundWindow();
    DWORD ownerPid = 0;
    GetWindowThreadProcessId(hwnd, &ownerPid);
    DWORD currentPid = GetCurrentProcessId();
    char title[128] = {};
    if (hwnd) GetWindowTextA(hwnd, title, sizeof(title));

    if (!hwnd || ownerPid != currentPid) {
        AppendDiagLine("keyboard leave prototype aborted hwnd=%p ownerPid=%lu currentPid=%lu title='%s'",
            hwnd,
            (unsigned long)ownerPid,
            (unsigned long)currentPid,
            title);
        InterlockedExchange(&g_keyboardLeaveInProgress, 0);
        return;
    }

    char iniPath[MAX_PATH];
    GetAutomationIniPath(iniPath, sizeof(iniPath));

    int delayMs = GetPrivateProfileIntA("SaveExitAutomation", "DelayMs", 300, iniPath);
    if (delayMs < 50) delayMs = 50;
    if (delayMs > 2000) delayMs = 2000;
    int mainMenuWaitMs = GetPrivateProfileIntA("SaveExitAutomation", "MainMenuWaitMs", 1500, iniPath);
    if (mainMenuWaitMs < 500) mainMenuWaitMs = 500;
    if (mainMenuWaitMs > 30000) mainMenuWaitMs = 30000;
    int mainMenuStableMs = GetPrivateProfileIntA("SaveExitAutomation", "MainMenuStableMs", 800, iniPath);
    if (mainMenuStableMs < 100) mainMenuStableMs = 100;
    if (mainMenuStableMs > 5000) mainMenuStableMs = 5000;

    char difficulty[32] = {};
    GetPrivateProfileStringA("SaveExitAutomation", "Difficulty", "Normal", difficulty, sizeof(difficulty), iniPath);
    TrimToken(difficulty);

    RECT client = {};
    GetClientRect(hwnd, &client);
    int clientW = client.right - client.left;
    int clientH = client.bottom - client.top;
    bool percentCoords = GetPrivateProfileIntA("SaveExitAutomation", "CoordinateModePercent", 0, iniPath) != 0;

    AppendDiagLine("keyboard leave prototype start hwnd=%p ownerPid=%lu title='%s' delayMs=%d mainMenuWaitMs=%d mainMenuStableMs=%d difficulty='%s' client=%dx%d percentCoords=%d",
        hwnd,
        (unsigned long)ownerPid,
        title,
        delayMs,
        mainMenuWaitMs,
        mainMenuStableMs,
        difficulty,
        clientW,
        clientH,
        percentCoords ? 1 : 0);

    for (int i = 1; i <= 12; ++i) {
        char stepName[32] = {};
        char stepField[16] = {};
        char legacyKeyField[16] = {};
        sprintf(stepField, "Step%d", i);
        sprintf(legacyKeyField, "Key%d", i);
        const char* fallback = "";
        if (i == 1) fallback = "ESC";
        if (i == 2) fallback = "S";
        GetPrivateProfileStringA("SaveExitAutomation", stepField, "", stepName, sizeof(stepName), iniPath);
        if (!stepName[0]) {
            GetPrivateProfileStringA("SaveExitAutomation", legacyKeyField, fallback, stepName, sizeof(stepName), iniPath);
        }
        TrimToken(stepName);

        if (_stricmp(stepName, "WAIT_MAIN_MENU") == 0 || _stricmp(stepName, "WAITMAINMENU") == 0) {
            AppendDiagLine("keyboard leave prototype wait-main-menu step%d timeoutMs=%d stableMs=%d", i, mainMenuWaitMs, mainMenuStableMs);
            if (!WaitForMainMenuReady(hwnd, mainMenuWaitMs, mainMenuStableMs)) {
                AppendDiagLine("keyboard leave prototype aborted: main menu was not detected");
                InterlockedExchange(&g_keyboardLeaveInProgress, 0);
                return;
            }
            continue;
        }

        if (_stricmp(stepName, "DIFFICULTY") == 0) {
            WORD vk = DifficultyNameToVk(difficulty);
            AppendDiagLine("keyboard leave prototype difficulty step%d='%s' vk=%u", i, difficulty, (unsigned)vk);
            SendKeyboardLeaveTap(vk);
            Sleep((DWORD)delayMs);
            continue;
        }

        if (_stricmp(stepName, "CLICK") == 0) {
            char xField[24] = {};
            char yField[24] = {};
            sprintf(xField, "Step%dX", i);
            sprintf(yField, "Step%dY", i);
            int x = GetPrivateProfileIntA("SaveExitAutomation", xField, -1, iniPath);
            int y = GetPrivateProfileIntA("SaveExitAutomation", yField, -1, iniPath);
            if (x < 0 || y < 0) {
                x = GetPrivateProfileIntA("SaveExitAutomation", "ClickX", -1, iniPath);
                y = GetPrivateProfileIntA("SaveExitAutomation", "ClickY", -1, iniPath);
            }
            if (percentCoords && clientW > 0 && clientH > 0 && x >= 0 && y >= 0) {
                x = (clientW * x) / 100;
                y = (clientH * y) / 100;
            }
            if (x < 0 || y < 0 || x >= clientW || y >= clientH) {
                AppendDiagLine("keyboard leave prototype click%d skipped invalid x=%d y=%d client=%dx%d", i, x, y, clientW, clientH);
                continue;
            }

            AppendDiagLine("keyboard leave prototype click%d client=%d,%d", i, x, y);
            SendWindowClientClick(hwnd, x, y);
            Sleep((DWORD)delayMs);
            continue;
        }

        WORD vk = KeyNameToVk(stepName);
        if (!vk) continue;

        AppendDiagLine("keyboard leave prototype key%d='%s' vk=%u", i, stepName, (unsigned)vk);
        SendKeyboardLeaveTap(vk);
        Sleep((DWORD)delayMs);
    }

    InterlockedExchange(&g_keyboardLeaveInProgress, 0);
}

static DWORD WINAPI KeyboardLeaveHotkeyThread(LPVOID)
{
    AppendDiagLine("keyboard leave prototype hotkey thread started");
    bool wasDown = false;
    while (!g_shutdown) {
        char iniPath[MAX_PATH];
        GetAutomationIniPath(iniPath, sizeof(iniPath));
        WORD hotkeyVk = (WORD)GetPrivateProfileIntA("SaveExitAutomation", "HotkeyVk", VK_F12, iniPath);
        int hotkeyModifiers = GetPrivateProfileIntA("SaveExitAutomation", "HotkeyModifiers", 0, iniPath);
        bool down = AutomationChordIsPressed(hotkeyVk, hotkeyModifiers);
        if (down && !wasDown) TryKeyboardLeave();
        wasDown = down;
        Sleep(50);
    }
    return 0;
}
#endif

#if defined(SOE_COMMAND_TRACE) && defined(SOE_NATIVE_CLIENT_LEAVE_PROTOTYPE)
static void RequestNativeClientLeavePrototype()
{
    InterlockedExchange(&g_nativeClientLeaveRequested, 1);
    AppendDiagLine("native client leave requested by F12; waiting for D2Client heartbeat thread");
}

static DWORD WINAPI NativeClientLeaveHotkeyThread(LPVOID)
{
    AppendDiagLine("F12 native D2Client leave prototype request thread started");
    bool wasDown = false;
    while (!g_shutdown) {
        bool down = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
        if (down && !wasDown) RequestNativeClientLeavePrototype();
        wasDown = down;
        Sleep(50);
    }
    return 0;
}
#endif

static void WriteLogHeader()
{
    static const char logPath[] = "C:\\grail_drops.log";
    // Write a session separator so the import tool can see separate sessions
    FILE* f = fopen(logPath, "a");
    if (!f) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "# Session started %04d-%02d-%02d %02d:%02d:%02d\n",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    fclose(f);
}

// ── Hook installer thread ─────────────────────────────────────────────────────
static DWORD WINAPI InstallThread(LPVOID)
{
    Sleep(3000);  // Wait for game DLLs to finish loading

    LoadConfig();
    SetupGrailLogger();
    StartHookDropEventWriter();

#ifdef SOE_PGAME_DIAG
#if defined(SOE_COMMAND_TRACE) && defined(SOE_RESET_PROTOTYPE)
    if (!g_resetCacheLockReady) {
        InitializeCriticalSection(&g_resetCacheLock);
        g_resetCacheLockReady = true;
    }
#endif
#if defined(SOE_COMMAND_TRACE) && defined(SOE_PACKET_RESET_PROTOTYPE)
    if (!g_packetResetLockReady) {
        InitializeCriticalSection(&g_packetResetLock);
        g_packetResetLockReady = true;
    }
#endif
    InstallLifecycleDiagHook();
#ifdef SOE_COMMAND_TRACE
    InstallCommandTraceHook();
    InstallProjectCommandTraceHook();
#ifdef SOE_D2NET_SEND_TRACE
    InstallD2NetSendTraceHook();
#endif
#ifdef SOE_D2NET_IAT_TRACE
    InstallD2NetIatTraceHooks();
#endif
    InstallCreateLifecycleTraceHooks();
#ifdef SOE_NATIVE_CLIENT_LEAVE_PROTOTYPE
    InstallNativeClientHeartbeatHook();
#endif
#ifdef SOE_NATIVE_LEAVE_ENTRY_TRACE
    InstallNativeLeaveEntryTraceHook();
    InstallNativeLeaveCallbackTraceHook();
#endif
#ifdef SOE_STORM_SMSG_TRACE
    InstallStormSmsgTraceHook();
#endif
#endif
#ifdef SOE_SPAWN_TRACE
    InstallSpawnTraceHooks();
#endif
    CreateThread(nullptr, 0, DiagHotkeyThread, nullptr, 0, nullptr);
#if defined(SOE_COMMAND_TRACE) && defined(SOE_RESET_PROTOTYPE)
    CreateThread(nullptr, 0, ResetPrototypeHotkeyThread, nullptr, 0, nullptr);
#endif
#if defined(SOE_COMMAND_TRACE) && defined(SOE_PACKET_RESET_PROTOTYPE)
    CreateThread(nullptr, 0, PacketResetHotkeyThread, nullptr, 0, nullptr);
#endif
#if defined(SOE_COMMAND_TRACE) && defined(SOE_PUMP_LEAVE_PROTOTYPE)
    CreateThread(nullptr, 0, PumpLeaveHotkeyThread, nullptr, 0, nullptr);
#endif
#if defined(SOE_COMMAND_TRACE) && defined(SOE_WMCLOSE_LEAVE_PROTOTYPE)
    CreateThread(nullptr, 0, WmCloseLeaveHotkeyThread, nullptr, 0, nullptr);
#endif
#if defined(SOE_COMMAND_TRACE) && defined(SOE_ESC_CLICK_LEAVE_PROTOTYPE)
    CreateThread(nullptr, 0, EscClickLeaveHotkeyThread, nullptr, 0, nullptr);
#endif
#if defined(SOE_COMMAND_TRACE) && defined(SOE_KEYBOARD_LEAVE_PROTOTYPE)
    CreateThread(nullptr, 0, KeyboardLeaveHotkeyThread, nullptr, 0, nullptr);
#endif
#if defined(SOE_COMMAND_TRACE) && defined(SOE_NATIVE_CLIENT_LEAVE_PROTOTYPE)
    CreateThread(nullptr, 0, NativeClientLeaveHotkeyThread, nullptr, 0, nullptr);
#endif
#endif
#ifdef SOE_MATERIALS_TRACE
    InstallMaterialsTraceHooks();
    CreateThread(nullptr, 0, MaterialsTraceRetryThread, nullptr, 0, nullptr);
#endif

    if (FindAndInstallHook()) {
        g_hooked = true;
        WriteLogHeader();
    }

    return 0;
}

// ── DLL entry & proxy init ────────────────────────────────────────────────────
BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hInst);

        // Load original ijl11.dll
        char path[MAX_PATH];
        GetModuleFileNameA(hInst, path, MAX_PATH);
        char* fname = strrchr(path, '\\');
        if (fname) {
            static const char origName[] = "ijl11_orig.dll"; strcpy(fname + 1, origName);
        }
        g_orig = LoadLibraryA(path);
        if (g_orig) {
            g_ijlInit          = (pfnijlInit)         GetProcAddress(g_orig, "ijlInit");
            g_ijlFree          = (pfnijlFree)         GetProcAddress(g_orig, "ijlFree");
            g_ijlRead          = (pfnijlRead)         GetProcAddress(g_orig, "ijlRead");
            g_ijlWrite         = (pfnijlWrite)        GetProcAddress(g_orig, "ijlWrite");
            g_ijlErrorStr      = (pfnijlErrorStr)     GetProcAddress(g_orig, "ijlErrorStr");
            g_ijlGetLibVersion = (pfnijlGetLibVersion)GetProcAddress(g_orig, "ijlGetLibVersion");
        }

        CreateThread(nullptr, 0, InstallThread, nullptr, 0, nullptr);
    }
    else if (reason == DLL_PROCESS_DETACH) {
        g_shutdown = true;
        if (g_hookDropEventReady) {
            SetEvent(g_hookDropEventReady);
        }

        // Remove IAT hook
        if (g_hooked && g_pIATSlot) {
            DWORD oldProt;
            VirtualProtect(g_pIATSlot, sizeof(uintptr_t), PAGE_READWRITE, &oldProt);
            *g_pIATSlot = g_origSetItemFlag;
            VirtualProtect(g_pIATSlot, sizeof(uintptr_t), oldProt, &oldProt);
        }
#ifdef SOE_PGAME_DIAG
#ifdef SOE_COMMAND_TRACE
        RemoveCreateLifecycleTraceHooks();
#ifdef SOE_D2NET_SEND_TRACE
        RemoveD2NetSendTraceHook();
#endif
#ifdef SOE_D2NET_IAT_TRACE
        RemoveD2NetIatTraceHooks();
#endif
#ifdef SOE_NATIVE_CLIENT_LEAVE_PROTOTYPE
        RemoveNativeClientHeartbeatHook();
#endif
#ifdef SOE_NATIVE_LEAVE_ENTRY_TRACE
        RemoveNativeLeaveCallbackTraceHook();
        RemoveNativeLeaveEntryTraceHook();
#endif
#ifdef SOE_STORM_SMSG_TRACE
        RemoveStormSmsgTraceHook();
#endif
        RemoveProjectCommandTraceHook();
        RemoveCommandTraceHook();
#endif
        RemoveLifecycleDiagHook();
#if defined(SOE_COMMAND_TRACE) && defined(SOE_RESET_PROTOTYPE)
        if (g_resetCacheLockReady) {
            DeleteCriticalSection(&g_resetCacheLock);
            g_resetCacheLockReady = false;
        }
#endif
#if defined(SOE_COMMAND_TRACE) && defined(SOE_PACKET_RESET_PROTOTYPE)
        if (g_packetResetLockReady) {
            DeleteCriticalSection(&g_packetResetLock);
            g_packetResetLockReady = false;
        }
#endif
#endif
#ifdef SOE_MATERIALS_TRACE
        RemoveMaterialsTraceHooks();
#endif
        if (g_orig) FreeLibrary(g_orig);
    }
    return TRUE;
}
