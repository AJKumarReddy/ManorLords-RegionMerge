// RegionMergeNative: tiny in-process helper for the RegionMerge Lua mod.
//
// Lua loads this DLL with package.loadlib(path, "*"). On attach it verifies the
// exact Manor Lords build by comparing code bytes, then redirects the exec thunk
// of ARegion::SetFamilyHome. Calls with a magic familyID are handled here; every
// other call runs the original game code unchanged. Status is written to
// RegionMergeNative.status next to the DLL so Lua can see whether it is active.
//
// Offsets are for Manor Lords 0.8.104 (ManorLords-Win64-Shipping.exe) and were
// derived in research/re (see research/README.md). On any mismatch nothing is
// patched and the mod falls back to doing nothing.
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

// ---- game layout (0.8.104) ----
#define RVA_SET_FAMILY_HOME_EXEC 0x4a758f0 // ARegion::execSetFamilyHome
#define RVA_REGION_ADD_BUILDING  0x4bd4c70 // ARegion::addBuilding(ASMBuildingMaster*)
#define RVA_REGION_REMOVE_BLD    0x4bdafb0 // ARegion::removeBuilding(ASMBuildingMaster*)
#define RVA_REGION_STOCK_UPDATE  0x4bd3af0 // ARegion::updateStock(bld, mode, TArray<FGood>*, bool)
#define RVA_FIND_REGION          0x4cb7540 // ASMBuildingMaster::findRegionAndOwner(ARegion* SetRegion)
#define RVA_REGION_CENTER        0x4bea720 // FVector* ARegion::getSettledRegionCenter(FVector* out)
#define RVA_REGION_BY_POS        0x4c4f230 // ARegion* ARTSMultiEngineCPP::getRegionByPos(const FVector*, bool)
#define RVA_SET_REGION           0x4c9a680 // ASMBuildingMaster::SetRegion (mov [rcx+2c8],rdx; ret)
#define RVA_CALL_SET_REGION_LOAD 0x4c169a4 // save loader: SetRegion(bld, getRegionByPos(savedPos))
#define RVA_CALL_SET_REGION_NEW  0x4b64e9c // building spawn: SetRegion(bld, region)
#define RVA_CALL_BY_POS_RESTORE  0x4c14cfb // save loader: live region for each saved region Center
#define RVA_TARRAY_GROW8         0x10b8e60 // TArray<8-byte>::ResizeGrow(arr, oldNum), called after Num++

#define OFF_FRAME_LOCALS   0x28  // FFrame::Locals
#define OFF_BLD_REGION     0x2c8 // ASMBuildingMaster::Region
#define OFF_BLD_OWNER      0x2e0 // ASMBuildingMaster::ownerPawn
#define OFF_BLD_MASTER     0x2e8 // ASMBuildingMaster::masterPtr (engine)
#define OFF_BLD_TYPE       0x3a8 // ASMBuildingMaster::Data.bType
#define OFF_BLD_FUNCTION   0x3bc // ASMBuildingMaster::buildingFunction (uint8)
#define OFF_ACTOR_ROOT     0x1b8 // AActor::RootComponent
#define OFF_COMP_LOCATION  0x1f0 // USceneComponent world location (3 doubles)
#define OFF_REG_TYPE       0x2d8 // ARegion::settlementType (uint8)
#define OFF_REG_OUTPOST_TO 0x2e0 // ARegion::outpostToRegion
#define OFF_REG_MASTER     0x318 // ARegion engine pointer
#define OFF_ENG_PLAYER     0x5a0 // ARTSMultiEngineCPP::playerRef
#define TYPE_OUTPOST       5
#define BTYPE_SETTLEMENT_CAMP 30 // anchors an outpost; stays in its own region
#define BFUNC_DECORATION   13    // map features; never counted or moved
#define OFF_BLD_INVENTORY  0x438 // ASMBuildingMaster::Inventory (TArray<FGood>)
#define OFF_REG_OWNER      0x350 // ARegion::ownerPawn
#define OFF_REG_BUILDINGS  0x658 // ARegion native TArray<ASMBuildingMaster*>
#define OFF_REG_RESIDENTS  0x368 // ARegion::residents (TArray<ASMUnit*>)
#define OFF_UNIT_REGION    0x328 // ASMUnit native ARegion*; the save files units under it

#define MAGIC_BASE (-777700) // familyID = MAGIC_BASE - op
enum { OP_MOVE_BUILDING = 1, OP_MOVE_RESIDENT = 3, OP_MOVE_REGION_BUILDINGS = 4 };

typedef struct { void *data; int32_t num, max; } TArrayRaw;
typedef void (*ExecFn)(void *ctx, void *stack, void *result);
typedef void (*RegionBldFn)(void *region, void *bld);
typedef struct { double x, y, z; } Vec3;
typedef void (*FindRegionFn)(void *bld, void *set_region);
typedef Vec3 *(*CenterFn)(void *region, Vec3 *out);
typedef void *(*RegionByPosFn)(void *engine, const Vec3 *pos, uint8_t skip_bounds);
typedef void (*GrowFn)(TArrayRaw *arr, int32_t old_num);
typedef void (*StockUpdateFn)(void *region, void *bld, int32_t mode, TArrayRaw *goods, uint8_t flag);

#include "signatures.h"

static uint8_t *g_base;
static ExecFn g_original; // trampoline: stolen prologue + jump back
static RegionBldFn g_add, g_remove;
static StockUpdateFn g_stock;
static GrowFn g_grow8;
static FindRegionFn g_find_orig;
static CenterFn g_center_orig;
static RegionByPosFn g_region_by_pos;
static char g_log_path[MAX_PATH];
static volatile LONG g_log_lines;

static void nlog(const char *fmt, ...) {
    if (InterlockedIncrement(&g_log_lines) > 2000) return;
    FILE *f = fopen(g_log_path, "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}
static char g_status_path[MAX_PATH];

static void write_status(const char *text) {
    FILE *f = fopen(g_status_path, "w");
    if (f) { fputs(text, f); fclose(f); }
}

static int array_contains(TArrayRaw *a, void *p) {
    void **d = (void **)a->data;
    for (int32_t i = 0; i < a->num; i++) if (d[i] == p) return 1;
    return 0;
}

// Move a building from its current region into `to`, keeping stock caches exact.
static int move_building(uint8_t *to, uint8_t *bld) {
    if (!to || !bld) return 0;
    uint8_t *from = *(uint8_t **)(bld + OFF_BLD_REGION);
    if (!from || from == to) return 0;
    if (!array_contains((TArrayRaw *)(from + OFF_REG_BUILDINGS), bld)) return 0;
    if (array_contains((TArrayRaw *)(to + OFF_REG_BUILDINGS), bld)) return 0;
    // 1. zero this building's cached stock in the old region (sync against an
    //    empty inventory), 2. unregister it there, 3. register it in the new
    //    region, which adds its real inventory to that region's stock.
    TArrayRaw *inv = (TArrayRaw *)(bld + OFF_BLD_INVENTORY);
    int32_t saved = inv->num;
    inv->num = 0;
    g_stock(from, bld, 0, NULL, 0);
    inv->num = saved;
    g_remove(from, bld);
    *(uint8_t **)(bld + OFF_BLD_REGION) = to;
    g_add(to, bld);
    *(void **)(bld + OFF_BLD_OWNER) = *(void **)(to + OFF_REG_OWNER);
    return 1;
}

// Move a resident unit from its region's residents list to `to`'s list and
// repoint its native region, using the game's allocator for the append.
static int move_resident(uint8_t *to, uint8_t *unit) {
    if (!to || !unit) return 0;
    uint8_t *from = *(uint8_t **)(unit + OFF_UNIT_REGION);
    if (!from || from == to) return 0;
    TArrayRaw *src = (TArrayRaw *)(from + OFF_REG_RESIDENTS);
    TArrayRaw *dst = (TArrayRaw *)(to + OFF_REG_RESIDENTS);
    void **d = (void **)src->data;
    int32_t i = 0;
    while (i < src->num && d[i] != unit) i++;
    if (i == src->num) return 0;
    memmove(&d[i], &d[i + 1], (size_t)(src->num - i - 1) * sizeof(void *));
    src->num--;
    int32_t old = dst->num;
    dst->num = old + 1;
    if (dst->num > dst->max) g_grow8(dst, old);
    ((void **)dst->data)[old] = unit;
    *(uint8_t **)(unit + OFF_UNIT_REGION) = to;
    return 1;
}

// The town a merged outpost belongs to: follow outpost links to a region of
// the same owner that is not itself an outpost. NULL if r is not merged land.
static uint8_t *merge_root(uint8_t *r) {
    if (!r || r[OFF_REG_TYPE] != TYPE_OUTPOST) return NULL;
    void *owner = *(void **)(r + OFF_REG_OWNER);
    if (!owner) return NULL;
    uint8_t *eng = *(uint8_t **)(r + OFF_REG_MASTER);
    void *player = eng ? *(void **)(eng + OFF_ENG_PLAYER) : NULL;
    if (player && owner != player) return NULL; // only the player's land
    uint8_t *cur = r;
    for (int i = 0; i < 8 && cur && cur[OFF_REG_TYPE] == TYPE_OUTPOST; i++)
        cur = *(uint8_t **)(cur + OFF_REG_OUTPOST_TO);
    if (!cur || cur == r || cur[OFF_REG_TYPE] == TYPE_OUTPOST) return NULL;
    if (*(void **)(cur + OFF_REG_OWNER) != owner) return NULL;
    return cur;
}

static int actor_location(uint8_t *actor, Vec3 *out) {
    uint8_t *root = *(uint8_t **)(actor + OFF_ACTOR_ROOT);
    if (!root) return 0;
    memcpy(out, root + OFF_COMP_LOCATION, sizeof *out);
    return 1;
}

// Buildings get their region when they are placed and again on every load,
// by position. On merged land, register them with the parent town instead so
// families, jobs and stock stay in one settlement across saves.
static void hooked_find_region(uint8_t *bld, uint8_t *set_region) {
    if (bld && !set_region && !*(void **)(bld + OFF_BLD_REGION) &&
        *(int32_t *)(bld + OFF_BLD_TYPE) != BTYPE_SETTLEMENT_CAMP &&
        bld[OFF_BLD_FUNCTION] != BFUNC_DECORATION) {
        uint8_t *eng = *(uint8_t **)(bld + OFF_BLD_MASTER);
        Vec3 pos;
        if (eng && actor_location(bld, &pos)) {
            uint8_t *here = g_region_by_pos(eng, &pos, 0);
            uint8_t *root = merge_root(here);
            if (root) {
                set_region = root;
                nlog("register bld %p type %d on merged land %p -> town %p", (void *)bld,
                     *(int32_t *)(bld + OFF_BLD_TYPE), (void *)here, (void *)root);
            }
        }
    }
    g_find_orig(bld, set_region);
}

// Every position lookup in the game goes through getRegionByPos: building
// placement and its cost check, construction, workers, the HUD. On the
// player's merged land it answers with the parent town, so the land really is
// part of that town. The raw answer (g_region_by_pos) is kept for the save
// loader's region restore and for this helper's own checks.
static void *hooked_region_by_pos(void *engine, const Vec3 *pos, uint8_t skip_bounds) {
    uint8_t *r = g_region_by_pos(engine, pos, skip_bounds);
    uint8_t *root = merge_root(r);
    return root ? root : r;
}

// Loading a save (and spawning a finished building) assigns the region found
// at the building's position through SetRegion, then registers the building
// with it. On merged land, hand the parent town to SetRegion instead.
static void my_set_region(uint8_t *bld, uint8_t *region) {
    int anchor = bld && (*(int32_t *)(bld + OFF_BLD_TYPE) == BTYPE_SETTLEMENT_CAMP ||
                         bld[OFF_BLD_FUNCTION] == BFUNC_DECORATION);
    if (bld && anchor) { // stays on its own land, not the town the lookup now reports
        uint8_t *eng = *(uint8_t **)(bld + OFF_BLD_MASTER);
        Vec3 pos;
        if (eng && actor_location(bld, &pos)) {
            uint8_t *raw = g_region_by_pos(eng, &pos, 0);
            if (raw) region = raw;
        }
    }
    if (bld && region && !anchor) {
        uint8_t *root = merge_root(region);
        if (root) {
            nlog("load/spawn bld %p type %d on merged land %p -> town %p", (void *)bld,
                 *(int32_t *)(bld + OFF_BLD_TYPE), (void *)region, (void *)root);
            region = root;
        }
    }
    *(uint8_t **)(bld + OFF_BLD_REGION) = region;
}

// Point a 5-byte `call rel32` at `hook` via a jump stub within +-2GB.
static uint8_t *g_stub_page;
static int g_stub_used;
static int redirect_call(uint8_t *site, uint8_t *expected_target, void *hook) {
    if (site[0] != 0xE8) return 0;
    if (site + 5 + *(int32_t *)(site + 1) != expected_target) return 0;
    if (!g_stub_page) {
        for (int64_t delta = 0x10000000; delta < 0x70000000 && !g_stub_page; delta += 0x1000000)
            g_stub_page = VirtualAlloc(g_base - delta, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!g_stub_page) return 0;
    }
    uint8_t *stub = g_stub_page + g_stub_used;
    g_stub_used += 16;
    stub[0] = 0xFF; stub[1] = 0x25; *(uint32_t *)(stub + 2) = 0;
    *(uint64_t *)(stub + 6) = (uint64_t)hook;
    int64_t rel = stub - (site + 5);
    if (rel > INT32_MAX || rel < INT32_MIN) return 0;
    DWORD old;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) return 0;
    *(int32_t *)(site + 1) = (int32_t)rel;
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 1;
}

// Saves find each region again by the Center stored for it, and Center is the
// average of the region's buildings. Buildings of a merged town can pull that
// average onto other land; fall back to the region's own anchor point then.
static Vec3 *hooked_center(uint8_t *region, Vec3 *out) {
    Vec3 *res = g_center_orig(region, out);
    uint8_t *eng = region ? *(uint8_t **)(region + OFF_REG_MASTER) : NULL;
    if (eng && res && g_region_by_pos(eng, res, 0) != region) {
        Vec3 anchor;
        if (actor_location(region, &anchor) && g_region_by_pos(eng, &anchor, 0) == region) {
            nlog("center of %p (%.0f,%.0f) is outside it; using anchor (%.0f,%.0f)",
                 (void *)region, res->x, res->y, anchor.x, anchor.y);
            *res = anchor;
        }
    }
    return res;
}

// Move every building of region `from` into `to`, except the settlement camp
// (the outpost's anchor) and map decorations. Walks a snapshot of the region's
// own native list, which the game keeps free of destroyed buildings.
static int move_region_buildings(uint8_t *to, uint8_t *from) {
    if (!to || !from || to == from) return 0;
    TArrayRaw *list = (TArrayRaw *)(from + OFF_REG_BUILDINGS);
    int32_t n = list->num;
    if (n <= 0) return 0;
    void **snap = HeapAlloc(GetProcessHeap(), 0, (size_t)n * sizeof(void *));
    if (!snap) return 0;
    memcpy(snap, list->data, (size_t)n * sizeof(void *));
    int moved = 0;
    for (int32_t i = 0; i < n; i++) {
        uint8_t *b = snap[i];
        if (!b || *(int32_t *)(b + OFF_BLD_TYPE) == BTYPE_SETTLEMENT_CAMP || b[OFF_BLD_FUNCTION] == BFUNC_DECORATION)
            continue;
        if (*(uint8_t **)(b + OFF_BLD_REGION) == from) moved += move_building(to, b);
    }
    HeapFree(GetProcessHeap(), 0, snap);
    if (moved) nlog("moved %d buildings %p -> %p", moved, (void *)from, (void *)to);
    return moved;
}

static void hooked_exec(void *ctx, void *stack, void *result) {
    uint8_t *locals = *(uint8_t **)((uint8_t *)stack + OFF_FRAME_LOCALS);
    void *code = *(void **)((uint8_t *)stack + 0x20);
    // Only direct ProcessEvent calls (Code == NULL, as UE4SS makes them) carry
    // a params struct in Locals; blueprint calls pass through untouched.
    if (code == NULL && locals) {
        int32_t family = *(int32_t *)locals;
        if (family <= MAGIC_BASE && family > MAGIC_BASE - 16) {
            int op = MAGIC_BASE - family;
            void *bld = *(void **)(locals + 8);
            if (op == OP_MOVE_BUILDING) move_building((uint8_t *)ctx, (uint8_t *)bld);
            else if (op == OP_MOVE_RESIDENT) move_resident((uint8_t *)ctx, (uint8_t *)bld);
            else if (op == OP_MOVE_REGION_BUILDINGS) move_region_buildings((uint8_t *)ctx, (uint8_t *)bld);
            return;
        }
    }
    g_original(ctx, stack, result);
}

// Redirect `target` to `hook`; `steal` bytes of position-independent prologue
// are copied into a trampoline that continues the original.
static void *hook_fn(uint8_t *target, void *hook, int steal) {
    uint8_t *tramp = VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return NULL;
    memcpy(tramp, target, steal);
    tramp[steal] = 0xFF; tramp[steal + 1] = 0x25; *(uint32_t *)(tramp + steal + 2) = 0;
    *(uint64_t *)(tramp + steal + 6) = (uint64_t)(target + steal);
    uint8_t patch[32];
    patch[0] = 0xFF; patch[1] = 0x25; *(uint32_t *)(patch + 2) = 0;
    *(uint64_t *)(patch + 6) = (uint64_t)hook;
    for (int i = 14; i < steal; i++) patch[i] = 0xCC;
    DWORD old;
    if (!VirtualProtect(target, steal, PAGE_EXECUTE_READWRITE, &old)) return NULL;
    memcpy(target, patch, steal);
    VirtualProtect(target, steal, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, steal);
    return tramp;
}

static int install(void) {
    g_base = (uint8_t *)GetModuleHandleA(NULL);
    uint8_t *exec = g_base + RVA_SET_FAMILY_HOME_EXEC;
    uint8_t *add = g_base + RVA_REGION_ADD_BUILDING;
    uint8_t *rem = g_base + RVA_REGION_REMOVE_BLD;
    uint8_t *stk = g_base + RVA_REGION_STOCK_UPDATE;
    uint8_t *grow = g_base + RVA_TARRAY_GROW8;
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    if (!strstr(exe, "ManorLords-Win64-Shipping.exe")) { write_status("DISABLED not Manor Lords"); return 0; }
    if (memcmp(exec, SIG_EXEC, sizeof SIG_EXEC) || memcmp(add, SIG_ADD, sizeof SIG_ADD) ||
        memcmp(rem, SIG_REMOVE, sizeof SIG_REMOVE) || memcmp(stk, SIG_STOCK, sizeof SIG_STOCK) ||
        memcmp(grow, SIG_GROW8, sizeof SIG_GROW8) ||
        memcmp(g_base + RVA_FIND_REGION, SIG_FIND_REGION, sizeof SIG_FIND_REGION) ||
        memcmp(g_base + RVA_REGION_CENTER, SIG_CENTER, sizeof SIG_CENTER) ||
        memcmp(g_base + RVA_REGION_BY_POS, SIG_REGION_BY_POS, sizeof SIG_REGION_BY_POS)) {
        write_status("DISABLED game build mismatch (expected 0.8.104)");
        return 0;
    }
    // verify the SetRegion call sites before patching anything
    uint8_t *sites[2] = { g_base + RVA_CALL_SET_REGION_LOAD, g_base + RVA_CALL_SET_REGION_NEW };
    for (int i = 0; i < 2; i++)
        if (sites[i][0] != 0xE8 || sites[i] + 5 + *(int32_t *)(sites[i] + 1) != g_base + RVA_SET_REGION) {
            write_status("DISABLED game build mismatch (SetRegion call sites)");
            return 0;
        }
    g_add = (RegionBldFn)add;
    g_remove = (RegionBldFn)rem;
    g_stock = (StockUpdateFn)stk;
    g_grow8 = (GrowFn)grow;

    uint8_t *restore_site = g_base + RVA_CALL_BY_POS_RESTORE;
    if (restore_site[0] != 0xE8 || restore_site + 5 + *(int32_t *)(restore_site + 1) != g_base + RVA_REGION_BY_POS) {
        write_status("DISABLED game build mismatch (region restore call site)");
        return 0;
    }
    g_region_by_pos = (RegionByPosFn)hook_fn(g_base + RVA_REGION_BY_POS, (void *)&hooked_region_by_pos, 15);
    if (!g_region_by_pos) { write_status("DISABLED hook install failed"); return 0; }
    g_original = (ExecFn)hook_fn(exec, (void *)&hooked_exec, 15);
    g_find_orig = (FindRegionFn)hook_fn(g_base + RVA_FIND_REGION, (void *)&hooked_find_region, 16);
    g_center_orig = (CenterFn)hook_fn(g_base + RVA_REGION_CENTER, (void *)&hooked_center, 20);
    if (!g_original || !g_find_orig || !g_center_orig) { write_status("DISABLED hook install failed"); return 0; }
    uint8_t *setr = g_base + RVA_SET_REGION;
    if (!redirect_call(g_base + RVA_CALL_SET_REGION_LOAD, setr, (void *)&my_set_region) ||
        !redirect_call(g_base + RVA_CALL_SET_REGION_NEW, setr, (void *)&my_set_region) ||
        !redirect_call(restore_site, g_base + RVA_REGION_BY_POS, (void *)g_region_by_pos)) {
        write_status("DISABLED SetRegion call sites not found");
        return 0;
    }
    write_status("OK 0.8.104");
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameA(inst, g_status_path, MAX_PATH);
        char *dot = strrchr(g_status_path, '.');
        if (dot) strcpy(dot, ".status");
        strcpy(g_log_path, g_status_path);
        dot = strrchr(g_log_path, '.');
        if (dot) strcpy(dot, ".log");
        DeleteFileA(g_log_path);
        // keep the DLL loaded for the life of the process: the patch points into it
        HMODULE self;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           (LPCSTR)&DllMain, &self);
        install();
    }
    return TRUE;
}
