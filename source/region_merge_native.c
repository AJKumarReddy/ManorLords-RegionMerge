// RegionMergeNative: in-process helper for the RegionMerge Lua mod.
//
// Lua loads this DLL with package.loadlib(path, "*"). On attach it checks that
// the running game is the exact build these offsets came from, by comparing the
// first bytes of every routine it touches. On any mismatch it patches nothing
// and the game runs unchanged.
//
// A merged region is kept as a native Outpost of its town, so the game saves
// the link itself. The helper's job is to make that land behave as part of the
// town. The game answers "which region is this?" in three different ways, and
// each one needs handling of its own:
//
//   by position  getRegionByPos, behind placement, costs, construction,
//                workers and the HUD -- answers with the town.
//   by shape     isPointInside, geometry against a region's own border, used
//                when a road works out which regions it crosses -- the land
//                stands aside so its town answers instead, which is what puts
//                roads on merged land into the town's planning data.
//   by record    the lists a region keeps -- buildings, residents, trees,
//                roads -- which the helper moves or shares outright.
//
// Status goes to RegionMergeNative.status so Lua can see whether the helper is
// active; notable events go to RegionMergeNative.log.
//
// Offsets are for Manor Lords 0.8.104 (ManorLords-Win64-Shipping.exe) and were
// derived in research/re (see research/README.md).
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

// ---------------------------------------------------------------- game layout
// Routines the helper hooks or calls.
#define RVA_SET_FAMILY_HOME_EXEC 0x4a758f0 // ARegion::execSetFamilyHome (Lua's way in)
#define RVA_REGION_ADD_BUILDING  0x4bd4c70 // ARegion::addBuilding(ASMBuildingMaster*)
#define RVA_REGION_REMOVE_BLD    0x4bdafb0 // ARegion::removeBuilding(ASMBuildingMaster*)
#define RVA_REGION_STOCK_UPDATE  0x4bd3af0 // ARegion::updateStock(bld, mode, TArray<FGood>*, bool)
#define RVA_FIND_REGION          0x4cb7540 // ASMBuildingMaster::findRegionAndOwner(ARegion*)
#define RVA_REGION_CENTER        0x4bea720 // FVector* ARegion::getSettledRegionCenter(FVector*)
#define RVA_REGION_BY_POS        0x4c4f230 // ARegion* ARTSMultiEngineCPP::getRegionByPos(const FVector*, bool)
#define RVA_REGION_ADD_ROAD      0x4be1180 // bool ARegion::addRoadIfInside(ARoad*)
#define RVA_POINT_INSIDE         0x4beee50 // bool ARegion::isPointInside(const FVector*)
#define RVA_FIND_ROADPOINT       0x4adb7c0 // bool APawnCPP::findNearestRoadpoint(ARegion*, const FVector*, FVector* out, bool kingsOnly)
#define RVA_GET_STOCK            0x4beaee0 // int32 ARegion::getStockOfGood(int32 good, uint8 which, bool flag)
#define RVA_MERGE_GOODS          0x4b0f2a0 // addGoods(TArray<FGood>* dest, const TArray<FGood>* src, bool subtract, bool)
#define RVA_HAS_SURPLUS          0x4bede30 // bool ARegion::hasSurplusOfGoodsMinusReservedSimple(TArray<FGood>*, int32)
#define RVA_PERK_ACTIVE          0x4b35440 // bool UPerkHelperLibrary::IsEffectActive(TScriptInterface<IRegionProvider>, EPerkEffect)
#define RVA_STOCK_PTR            0x4beaec0 // FGood* ARegion::getStock(uint8 which) -> this+0x528+(which<<4)
#define RVA_ROAD_CLOSEST         0x4c4d250 // ARoad closest point to a position, worked out against a given ARegion
#define RVA_ROAD_REGIONS         0x4c7ae00 // ARoad::updateRegions(): rebuilds the road's region list
#define RVA_PLAN_SYNC            0x4c5d7d0 // files a road with the planning data of each of its regions
#define RVA_PLAN_DROP            0x4b3e620 // UCityPlanningComponent::removeRoad(ARoad*)
#define RVA_PANEL_REGION         0x4ae3f90 // APawnCPP::getRegionForRegionPanel -> pawn+0xcc0
#define RVA_SET_REGION           0x4c9a680 // ASMBuildingMaster::SetRegion (mov [rcx+2c8],rdx; ret)
#define RVA_TARRAY_GROW8         0x10b8e60 // TArray<8-byte>::ResizeGrow(arr, oldNum), called after Num++

// Individual `call` sites, redirected rather than hooked.
#define RVA_CALL_SET_REGION_LOAD 0x4c169a4 // save loader: SetRegion(bld, getRegionByPos(savedPos))
#define RVA_CALL_SET_REGION_NEW  0x4b64e9c // building spawn: SetRegion(bld, region)
#define RVA_CALL_BY_POS_RESTORE  0x4c14cfb // save loader: live region for each saved region Center

// ARegion
#define OFF_REG_TYPE       0x2d8 // settlementType (uint8)
#define OFF_REG_OUTPOST_TO 0x2e0 // outpostToRegion
#define OFF_REG_MASTER     0x318 // engine pointer
#define OFF_REG_ROADS      0x320 // native TArray<ARoad*>: the roads crossing it
#define OFF_REG_OWNER      0x350 // ownerPawn
#define OFF_REG_RESIDENTS  0x368 // residents (TArray<ASMUnit*>)
#define OFF_REG_BUILDINGS  0x658 // native TArray<ASMBuildingMaster*>
#define OFF_REG_STOCK      0x528 // TArray<FGood>, 24-byte elements; data +0x528, num +0x530
#define OFF_REG_PLANNING   0x738 // CityPlanningComponent: road snap points live here
#define OFF_PLAN_RECORDS   0x120 // UCityPlanningComponent: road spatial index (data +0x120, num +0x128)
#define OFF_REG_FOLIAGE    0xf70 // regionalFoliage (TArray<UInstancedStaticMeshComponent*>)
#define TYPE_OUTPOST       5     // ESettlementType::Outpost

// ASMBuildingMaster
#define OFF_BLD_REGION     0x2c8 // Region
#define OFF_BLD_OWNER      0x2e0 // ownerPawn
#define OFF_BLD_MASTER     0x2e8 // masterPtr (engine)
#define OFF_BLD_TYPE       0x3a8 // Data.bType
#define OFF_BLD_FUNCTION   0x3bc // buildingFunction (uint8)
#define OFF_BLD_INVENTORY  0x438 // Inventory (TArray<FGood>)
#define BTYPE_SETTLEMENT_CAMP 30 // anchors an outpost; stays on its own land
#define BFUNC_DECORATION   13    // map features; never counted or moved

// Other objects
#define OFF_ACTOR_ROOT     0x1b8 // AActor::RootComponent
#define OFF_COMP_LOCATION  0x1f0 // USceneComponent world location (3 doubles)
#define OFF_ENG_PLAYER     0x5a0 // ARTSMultiEngineCPP::playerRef
#define OFF_ROAD_POINTS    0x398 // ARoad native TArray<FVector>: cached world points
#define OFF_UNIT_REGION    0x328 // ASMUnit native ARegion*; the save files units under it
#define OFF_FRAME_LOCALS   0x28  // FFrame::Locals

// Commands from Lua arrive as SetFamilyHome(familyID = MAGIC_BASE - op, arg).
#define MAGIC_BASE (-777700)
enum { OP_MOVE_RESIDENT = 1, OP_MERGE_LAND = 2, OP_REFRESH_ROADS = 3, OP_REPORT_ROADS = 4,
       OP_SYNC_STOCK = 5 };

// ---------------------------------------------------------------- types
typedef struct { void *data; int32_t num, max; } TArrayRaw;
typedef struct { double x, y, z; } Vec3;

typedef void (*ExecFn)(void *ctx, void *stack, void *result);
typedef void (*RegionBldFn)(void *region, void *bld);
typedef void (*FindRegionFn)(void *bld, void *set_region);
typedef Vec3 *(*CenterFn)(void *region, Vec3 *out);
typedef void *(*RegionByPosFn)(void *engine, const Vec3 *pos, uint8_t skip_bounds);
typedef void (*GrowFn)(TArrayRaw *arr, int32_t old_num);
typedef void (*StockUpdateFn)(void *region, void *bld, int32_t mode, TArrayRaw *goods, uint8_t flag);
typedef uint8_t (*AddRoadFn)(void *region, void *road);
typedef uint8_t (*PointInsideFn)(void *region, const Vec3 *pos);
typedef uint8_t (*FindRoadpointFn)(void *pawn, void *region, const Vec3 *pos, Vec3 *out, uint8_t kings_only);
typedef int32_t (*GetStockFn)(void *region, int32_t good, uint8_t which, uint8_t flag);
typedef void *(*StockPtrFn)(void *region, uint8_t which);
typedef uint8_t (*PerkActiveFn)(void *provider, uint8_t perk);
typedef uint8_t (*HasSurplusFn)(void *region, void *goods, int32_t flag);
typedef void (*MergeGoodsFn)(TArrayRaw *dest, const TArrayRaw *src, uint8_t subtract, uint8_t flag);
typedef uint8_t (*RoadClosestFn)(void *road, void *out_point, const Vec3 *pos, void *out_b,
                                 void *out_dist, uint8_t flag, void *region);
typedef void (*RoadRegionsFn)(void *road);
typedef void (*PlanSyncFn)(void *engine, void *road);
typedef void (*PlanDropFn)(void *planning, void *road);
typedef void *(*PanelRegionFn)(void *pawn);

#include "signatures.h"

// ---------------------------------------------------------------- state
static uint8_t *g_base;
// Trampolines back into the game (stolen prologue + jump back), and plain entry
// points for the routines the helper only calls.
static ExecFn g_exec_orig;
static FindRegionFn g_find_orig;
static CenterFn g_center_orig;
static RegionByPosFn g_region_by_pos;
static AddRoadFn g_add_road_orig;
static PointInsideFn g_point_inside_orig;
static FindRoadpointFn g_find_roadpoint_orig;
static RoadClosestFn g_road_closest_orig;
static GetStockFn g_get_stock_orig;
static StockPtrFn g_stock_ptr_orig;
static PerkActiveFn g_perk_orig;
static HasSurplusFn g_has_surplus_orig;
static MergeGoodsFn g_merge_goods;
static RoadRegionsFn g_road_regions_orig;
static RegionBldFn g_add, g_remove;
static StockUpdateFn g_stock;
static GrowFn g_grow8;
static PlanSyncFn g_plan_sync;
static PlanDropFn g_plan_drop;
static PanelRegionFn g_panel_region_orig;

// While set, position lookups answer with the real land rather than its town.
static __thread int t_raw_lookup;
// While set, regions answer for their own shapes: the covering a town does over
// the land merged into it is off. Every lookup the helper makes for itself wants
// the real land, and it also stops that covering coming back round into itself.
static __thread int t_covering;
// While set, a road is working out which regions it crosses. The land each
// point really belongs to is kept across the regions asked about that point.
static __thread int t_road_regions;
static __thread Vec3 t_point;
static __thread uint8_t *t_point_land;

static char g_status_path[MAX_PATH];
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

static void write_status(const char *text) {
    FILE *f = fopen(g_status_path, "w");
    if (f) { fputs(text, f); fclose(f); }
}

// ---------------------------------------------------------------- arrays
static int array_contains(TArrayRaw *a, void *p) {
    void **d = (void **)a->data;
    for (int32_t i = 0; i < a->num; i++) if (d[i] == p) return 1;
    return 0;
}

// Append to a game-owned pointer array, growing it with the game's allocator.
static void array_append(TArrayRaw *a, void *p) {
    int32_t old = a->num;
    a->num = old + 1;
    if (a->num > a->max) g_grow8(a, old);
    ((void **)a->data)[old] = p;
}

// Append every pointer of `src` that `dst` does not already hold.
static int array_share(TArrayRaw *dst, TArrayRaw *src) {
    int added = 0;
    for (int32_t i = 0; i < src->num; i++) {
        void *p = ((void **)src->data)[i];
        if (!p || array_contains(dst, p)) continue;
        array_append(dst, p);
        added++;
    }
    return added;
}

// ---------------------------------------------------------------- the model
// The town a merged outpost belongs to: follow outpost links to a region of the
// same owner that is not itself an outpost. NULL if r is not merged land.
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

// Is `town` the town that `land` was merged into?
static int is_merged_into(uint8_t *land, uint8_t *town) {
    return land && town && land != town && merge_root(land) == town;
}

static int actor_location(uint8_t *actor, Vec3 *out) {
    uint8_t *root = *(uint8_t **)(actor + OFF_ACTOR_ROOT);
    if (!root) return 0;
    memcpy(out, root + OFF_COMP_LOCATION, sizeof *out);
    return 1;
}

// The settlement camp anchors an outpost and map decorations belong to the
// scenery: both stay on their own land however it is merged.
static int is_anchor(uint8_t *bld) {
    return *(int32_t *)(bld + OFF_BLD_TYPE) == BTYPE_SETTLEMENT_CAMP ||
           bld[OFF_BLD_FUNCTION] == BFUNC_DECORATION;
}

// The land a point really sits on: the game's own lookup with every region
// answering for its own shape, which is what the helper wants whenever it is
// working out the truth rather than answering the game.
static uint8_t *raw_region_at(uint8_t *eng, const Vec3 *pos) {
    int was = t_covering;
    t_covering = 1;
    uint8_t *r = g_region_by_pos(eng, pos, 0);
    t_covering = was;
    return r;
}

static uint8_t *raw_region_of(uint8_t *bld) {
    uint8_t *eng = *(uint8_t **)(bld + OFF_BLD_MASTER);
    Vec3 pos;
    if (!eng || !actor_location(bld, &pos)) return NULL;
    return raw_region_at(eng, &pos);
}

// ------------------------------------------------ which region is this? (1/3)
// By position. Every position lookup in the game goes through getRegionByPos:
// building placement and its cost check, construction, workers, the HUD. On the
// player's merged land it answers with the town, so the land really is part of
// it. The raw answer stays reachable through g_region_by_pos, for the save
// loader's region restore and for the helper's own checks.
// DIAGNOSTIC BUILD: the redirect is off. Merged land answers for itself, as it
// does while a region is merely claimed -- the one state in which snapping is
// known to work. Economy on merged land is expected to be wrong here; this
// build exists only to establish whether the redirect is what breaks snapping.
static int g_redirect_by_pos = 0;

static void *hooked_region_by_pos(void *engine, const Vec3 *pos, uint8_t skip_bounds) {
    uint8_t *r = g_region_by_pos(engine, pos, skip_bounds);
    if (t_raw_lookup || !g_redirect_by_pos) return r;
    uint8_t *root = merge_root(r);
    return root ? root : r;
}

// Buildings placed without a region get one by position, which already answers
// with the town; anchors are handed their own land instead.
static void hooked_find_region(uint8_t *bld, uint8_t *set_region) {
    if (bld && !set_region && !*(void **)(bld + OFF_BLD_REGION) && is_anchor(bld)) {
        uint8_t *raw = raw_region_of(bld);
        if (raw) set_region = raw;
    }
    g_find_orig(bld, set_region);
}

// The save loader finds each region again by the Center stored for it, and must
// see the land itself there rather than the town. Regions answer for their own
// shapes for the whole of this lookup, so the covering a town does over merged
// land cannot pull the answer onto the town and file the land's own data under
// it. Nested lookups keep the flag, which is what the restore wants.
static void *restore_region_by_pos(void *engine, const Vec3 *pos, uint8_t skip_bounds) {
    int was = t_covering;
    t_covering = 1;
    void *r = g_region_by_pos(engine, pos, skip_bounds);
    t_covering = was;
    return r;
}

// Loading a save, and spawning a finished building, assign the region found at
// the building's position through SetRegion. On merged land, hand over the town.
static void my_set_region(uint8_t *bld, uint8_t *region) {
    if (bld && is_anchor(bld)) {
        uint8_t *raw = raw_region_of(bld);
        if (raw) region = raw;
    } else if (bld && region) {
        uint8_t *root = merge_root(region);
        if (root) region = root;
    }
    *(uint8_t **)(bld + OFF_BLD_REGION) = region;
}

// Saves find each region again by the Center stored for it, and Center is the
// average of the region's buildings. Buildings of a merged town can pull that
// average onto other land; fall back to the region's own anchor point then.
static Vec3 *hooked_center(uint8_t *region, Vec3 *out) {
    Vec3 *res = g_center_orig(region, out);
    uint8_t *eng = region ? *(uint8_t **)(region + OFF_REG_MASTER) : NULL;
    if (eng && res && raw_region_at(eng, res) != region) {
        Vec3 anchor;
        if (actor_location(region, &anchor) && raw_region_at(eng, &anchor) == region) {
            nlog("center of %p (%.0f,%.0f) is outside it; using anchor (%.0f,%.0f)",
                 (void *)region, res->x, res->y, anchor.x, anchor.y);
            *res = anchor;
        }
    }
    return res;
}

// The region panel. Merged land keeps its own place on the map, so every
// question about where something is still answers with the land -- which is
// what makes roads, snapping and plots work there exactly as they do at home.
// What the land is not is a settlement of its own: its families, its residents
// and its treasury all moved to the town. Asked which region the panel should
// show, merged land hands over its town, so clicking it reads the town's
// population, wealth and stores rather than the nothing the land has left.
static void *hooked_panel_region(void *pawn) {
    uint8_t *r = (uint8_t *)g_panel_region_orig(pawn);
    uint8_t *root = merge_root(r);
    return root ? root : r;
}

// ------------------------------------------------ which region is this? (2/3)
// By shape. A road remembers the regions it crosses, and the game keeps that
// list by asking each region whether it contains the road's points -- geometry
// against the region's own border, not the position lookup, so merged land
// answers for itself and its town is never named. That list decides whose
// planning data holds the road, and the planning data is where plot snapping,
// the snap markers, the road collision test and the remove-road tool all look.
// A road on merged land was therefore filed in one place and searched for in
// another: invisible to all four at once.
//
// The rebuild keeps the first region that answers and asks no further, so the
// land cannot simply be joined by its town -- it has to stand aside. While a
// road works out its regions, merged land does not answer for itself and its
// town answers in its place, so the road belongs to the town, which is what
// every one of those lookups asks. The game wrote the list, so the game's own
// cleanup takes the road back out when it is removed. Outside that one rebuild
// the regions keep their real shapes.
static uint8_t hooked_point_inside(void *region, const Vec3 *pos) {
    uint8_t *r = (uint8_t *)region;
    if (!t_road_regions || !r || !pos) return g_point_inside_orig(region, pos);
    if (merge_root(r)) return 0;
    uint8_t inside = g_point_inside_orig(region, pos);
    if (inside || r[OFF_REG_TYPE] == TYPE_OUTPOST) return inside;
    uint8_t *eng = *(uint8_t **)(r + OFF_REG_MASTER);
    if (!eng) return inside;
    // Which land this point really belongs to. The rebuild asks one region
    // after another about the same point, so the answer is worked out once and
    // kept; the standing-aside is off for it, to see the regions as they are.
    if (!t_point_land || pos->x != t_point.x || pos->y != t_point.y || pos->z != t_point.z) {
        t_road_regions--;
        t_point_land = raw_region_at(eng, pos);
        t_road_regions++;
        t_point = *pos;
    }
    return is_merged_into(t_point_land, r) ? 1 : inside;
}

// Merged land the helper has been told about, remembered by pointer so that a
// region can be recognised without reading anything out of an object that might
// not be one. Cleared whenever the world changes.
#define MAX_MERGED 64
static struct { void *land, *town; } g_merged[MAX_MERGED];
static int g_merged_n;
static uint8_t *g_merged_world;

static void note_merged(uint8_t *land, uint8_t *town) {
    uint8_t *eng = *(uint8_t **)(land + OFF_REG_MASTER);
    if (eng != g_merged_world) { g_merged_world = eng; g_merged_n = 0; }
    for (int i = 0; i < g_merged_n; i++)
        if (g_merged[i].land == land) { g_merged[i].town = town; return; }
    if (g_merged_n < MAX_MERGED) {
        g_merged[g_merged_n].land = land;
        g_merged[g_merged_n].town = town;
        g_merged_n++;
    }
}

static void *merged_town_of(void *obj) {
    for (int i = 0; i < g_merged_n; i++)
        if (g_merged[i].land == obj) return g_merged[i].town;
    return NULL;
}

// A region's development perks are its own, and merged land has none of its
// own: the perks the player bought belong to the town. Asked whether a perk is
// in effect for merged land, answer for its town, so a building there gets the
// same perks as one at home. The provider is an interface pair
// { object, interface }; the substitute is built at the same offset into the
// town, which is sound because both are regions of the same class.
static uint8_t hooked_perk_active(void *provider, uint8_t perk) {
    if (provider) {
        void **p = (void **)provider;
        void *town = merged_town_of(p[0]);
        if (town) {
            void *sub[2];
            sub[0] = town;
            sub[1] = p[1] ? (void *)((uint8_t *)town + ((uint8_t *)p[1] - (uint8_t *)p[0])) : NULL;
            return g_perk_orig(sub, perk);
        }
    }
    return g_perk_orig(provider, perk);
}

// ---------------------------------------------------------------- the economy
// Merged land keeps its own shape, so every question about where something is
// answers with the land -- which is what makes roads, snapping and plots on it
// work as they always did. What it does not have is goods: its buildings are
// registered with the town, so the town's stock is the real pool and the land's
// own is empty. Asked what it holds, merged land answers with its town's stock,
// which is what the cost of a building, the goods panel and construction read.
static int32_t hooked_get_stock(void *region, int32_t good, uint8_t which, uint8_t flag) {
    uint8_t *root = merge_root((uint8_t *)region);
    return g_get_stock_orig(root ? root : region, good, which, flag);
}

// "Not enough goods": the check behind a building's construction cost asks the
// region whether it has the goods to spare, and reads its stock without going
// through the accessors above. Merged land answers for its town here too, so a
// building placed there is paid for out of the town's storage.
static uint8_t hooked_has_surplus(void *region, void *goods, int32_t flag) {
    uint8_t *root = merge_root((uint8_t *)region);
    return g_has_surplus_orig(root ? root : region, goods, flag);
}

// The goods array itself. Anything that takes the region's stock list and reads
// it directly -- rather than asking for one good at a time -- comes through
// here, so merged land has to hand over its town's list the same way.
static void *hooked_stock_ptr(void *region, uint8_t which) {
    uint8_t *root = merge_root((uint8_t *)region);
    return g_stock_ptr_orig(root ? root : region, which);
}

// Every road-against-a-region question in the game comes through here: the
// closest point of one road, worked out for the region it was given. It is what
// the snap markers, plot-to-road snapping and the road collision test all end
// up calling, by different routes.
//
// A road crossing merged land is in the town's list, but all of its points fall
// outside the town's own border, so worked out against the town the road is
// passed over and a plot there has nothing to snap to. The land answers for
// those roads exactly, so a question asked of a town about a position on land
// merged into it is handed to that land instead. The town keeps everything
// else: the plot is still placed, paid for and owned by the town, because that
// is decided by the position lookup and SetRegion, not by this.
static uint8_t hooked_road_closest(void *road, void *out_point, const Vec3 *pos, void *out_b,
                                   void *out_dist, uint8_t flag, void *region) {
    uint8_t *r = (uint8_t *)region;
    if (r && pos && r[OFF_REG_TYPE] != TYPE_OUTPOST) {
        uint8_t *eng = *(uint8_t **)(r + OFF_REG_MASTER);
        if (eng) {
            uint8_t *land = raw_region_at(eng, pos);
            if (is_merged_into(land, r)) region = land;
        }
    }
    return g_road_closest_orig(road, out_point, pos, out_b, out_dist, flag, region);
}

static void hooked_road_regions(void *road) {
    t_point_land = NULL; // nothing remembered from an earlier road
    t_road_regions++;
    g_road_regions_orig(road);
    t_road_regions--;
}

// A region also keeps the plain list of roads crossing it, which is what the
// road searches read: snapping one road to another, and the link to the King's
// Road. ARegion::addRoadIfInside fills it, and wants the road to overlap the
// region's bounds and one of its points to answer with this region. On merged
// land both tests miss -- the land is answered with its town, and the town's
// bounds stop at its own border -- so the road reached no list at all.
//
// Merged land runs the real test with raw answers, keeping its own list as the
// game would have it; a town takes any road crossing land merged into it.
// Removing a road sweeps every region's list, so the extra entry is cleaned up
// by the game itself.
static uint8_t hooked_add_road(void *region, void *road) {
    uint8_t *r = (uint8_t *)region, *rd = (uint8_t *)road;
    if (!r || !rd) return g_add_road_orig(region, road);
    if (merge_root(r)) {
        t_raw_lookup++;
        uint8_t inside = g_add_road_orig(region, road);
        t_raw_lookup--;
        return inside;
    }
    if (g_add_road_orig(region, road)) return 1;
    // Only the player's own towns can have land merged into them; every other
    // region keeps the answer the game just gave, at no cost.
    uint8_t *eng = *(uint8_t **)(r + OFF_REG_MASTER);
    if (!eng || r[OFF_REG_TYPE] == TYPE_OUTPOST) return 0;
    void *player = *(void **)(eng + OFF_ENG_PLAYER);
    if (!player || *(void **)(r + OFF_REG_OWNER) != player) return 0;
    TArrayRaw *pts = (TArrayRaw *)(rd + OFF_ROAD_POINTS);
    if (!pts->data || pts->num <= 0) return 0;
    // Walk the road's points, at most 100 of them evenly spaced, as the game
    // does when it tests a road against a region of its own.
    int32_t step = pts->num > 100 ? pts->num / 100 : 1;
    for (int32_t i = 0; i < pts->num; i += step) {
        if (is_merged_into(raw_region_at(eng, &((Vec3 *)pts->data)[i]), r)) {
            TArrayRaw *list = (TArrayRaw *)(r + OFF_REG_ROADS);
            if (!array_contains(list, road)) array_append(list, road);
            return 1;
        }
    }
    return 0;
}

// ------------------------------------------------ which region is this? (3/3)
// By record. Move a building into `to`, keeping stock caches exact.
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

// Every building of `from` except its camp and map decorations. Walks a
// snapshot of the region's own list, which the game keeps free of destroyed
// buildings -- the engine-wide list does not.
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
        if (!b || is_anchor(b)) continue;
        if (*(uint8_t **)(b + OFF_BLD_REGION) == from) moved += move_building(to, b);
    }
    HeapFree(GetProcessHeap(), 0, snap);
    if (moved) nlog("moved %d buildings %p -> %p", moved, (void *)from, (void *)to);
    return moved;
}

// Move a resident unit into `to`'s list and repoint its native region: the save
// files units under it, and a stale pointer there crashes the next load.
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
    array_append(dst, unit);
    *(uint8_t **)(unit + OFF_UNIT_REGION) = to;
    return 1;
}

// Every resident of `from`, moved to `to`. Livestock are residents too -- the
// game counts a region's animals out of the same list it counts its people --
// and a region works out which animal belongs in which pasture by pairing its
// own residents against its own buildings. The buildings on merged land are
// registered with the town, so an animal left behind on the land can never be
// paired with the pasture it lives in: born or bought there, it becomes an
// animal no pasture will house and no count will show.
//
// The merge moved residents once and never again, so only what existed at that
// moment ever arrived, and land merged as a fresh claim moved none at all.
// This runs on every sweep beside the building move, which is what keeps the
// two lists on the same side.
static int move_region_residents(uint8_t *to, uint8_t *from) {
    if (!to || !from || to == from) return 0;
    TArrayRaw *list = (TArrayRaw *)(from + OFF_REG_RESIDENTS);
    int32_t n = list->num;
    if (n <= 0) return 0;
    void **snap = HeapAlloc(GetProcessHeap(), 0, (size_t)n * sizeof(void *));
    if (!snap) return 0;
    memcpy(snap, list->data, (size_t)n * sizeof(void *));
    int moved = 0;
    for (int32_t i = 0; i < n; i++)
        if (snap[i]) moved += move_resident(to, (uint8_t *)snap[i]);
    HeapFree(GetProcessHeap(), 0, snap);
    if (moved) nlog("moved %d residents %p -> %p", moved, (void *)from, (void *)to);
    return moved;
}

// Trees live in per-region foliage components, and buildings, workers and
// position queries search the list of the region they belong to. Give the town
// the land's components; the land keeps its own, so code that indexes that list
// by species or divides by its size is unaffected. The roads crossing the land
// are shared the same way, so the town's road searches see them.
static void share_lists(uint8_t *to, uint8_t *from) {
    if (!to || !from || to == from) return;
    int trees = array_share((TArrayRaw *)(to + OFF_REG_FOLIAGE),
                            (TArrayRaw *)(from + OFF_REG_FOLIAGE));
    int roads = array_share((TArrayRaw *)(to + OFF_REG_ROADS),
                            (TArrayRaw *)(from + OFF_REG_ROADS));
    if (trees || roads)
        nlog("shared %d foliage components and %d roads %p -> %p", trees, roads,
             (void *)from, (void *)to);
}

// Roads already on the land worked out their regions while it still stood
// alone, so they named the land and not its town. Have each work it out again.
// The game appends a planning record per road without checking for one already
// there, so Lua asks for this once for a piece of land and never again.
// The planning sync files a road by appending a record to an array the game
// never checks for one already there (UCityPlanningComponent::addRoad appends
// to +0x520), and the matching removeRoad clears a different member (+0x278),
// so a record once appended cannot be taken back out. A road crossing two
// merged parcels would otherwise be filed under their town twice, and the
// snap markers, plot snapping and the road collision test all read that array.
//
// So the helper syncs each road at most once per world. The set is cleared
// whenever the engine changes, which is every load and every new game.
#define MAX_SYNCED_ROADS 1024
static void *g_synced[MAX_SYNCED_ROADS];
static int g_synced_n;
static uint8_t *g_synced_world;

static int sync_once(uint8_t *eng, void *road) {
    if (eng != g_synced_world) { g_synced_world = eng; g_synced_n = 0; }
    for (int i = 0; i < g_synced_n; i++) if (g_synced[i] == road) return 0;
    if (g_synced_n == MAX_SYNCED_ROADS) {
        nlog("road sync set full; not filing %p again", road);
        return 0;
    }
    g_synced[g_synced_n++] = road;
    g_plan_sync(eng, road);
    return 1;
}

// Roads already on the land worked out their regions while it still stood
// alone, so they named the land and not its town. Have each work it out again,
// then file it with the planning data of the regions it now names -- which is
// what the snap markers, plot snapping and the road collision test read.
static int refresh_roads(uint8_t *land) {
    if (!land) return 0;
    uint8_t *eng = *(uint8_t **)(land + OFF_REG_MASTER);
    if (!eng) return 0;
    void *plan = *(void **)(land + OFF_REG_PLANNING);
    TArrayRaw *roads = (TArrayRaw *)(land + OFF_REG_ROADS);
    int n = 0, filed = 0;
    for (int32_t i = 0; i < roads->num; i++) {
        void *road = ((void **)roads->data)[i];
        if (!road) continue;
        // The land no longer names this road after the rebuild, so the game
        // would never come back to clear the land's own record of it.
        if (plan) g_plan_drop(plan, road);
        hooked_road_regions(road); // idempotent: rebuilds the road's region list
        filed += sync_once(eng, road);
        n++;
    }
    {
        void *pl = *(void **)(land + OFF_REG_PLANNING);
        TArrayRaw *recs = pl ? (TArrayRaw *)((uint8_t *)pl + OFF_PLAN_RECORDS) : NULL;
        if (n) nlog("refreshed %d roads on merged land %p (%d newly filed, land planRecords now %d)",
                    n, (void *)land, filed, recs ? recs->num : -1);
    }
    return n;
}

// The cost of a building is checked against the region's stock read straight
// out of the region -- no call to go through, so no answer to give. The only
// way merged land can hold what its town holds is to really hold it.
//
// The land has no stock of its own to lose: every building on it is registered
// with the town, so the town's storage is the only real pool. Its list is kept
// as a copy of the town's, made with the game's own routine so the memory stays
// the game's. The copy is refreshed on every sweep: what is there now is taken
// back out, then the town's list is added in.
#define GOOD_SIZE 24
// A region keeps two goods lists, side by side: getStock(which) hands back
// this+0x528 for one and this+0x538 for the other. Both are copied -- the cost
// of a building is checked against one of them, and filling only the first left
// the panel showing the town's goods while the cost still found nothing.
static int drain_one(uint8_t *land, int which) {
    TArrayRaw *dst = (TArrayRaw *)(land + OFF_REG_STOCK + which * 16);
    if (dst->num <= 0 || !dst->data) return 0;
    // Take out what is there, from a copy: the routine reads its source while
    // it writes its destination, and here they would be the same list.
    size_t bytes = (size_t)dst->num * GOOD_SIZE;
    void *copy = HeapAlloc(GetProcessHeap(), 0, bytes);
    if (!copy) return 0;
    memcpy(copy, dst->data, bytes);
    TArrayRaw held = { copy, dst->num, dst->num };
    g_merge_goods(dst, &held, 1, 1);
    HeapFree(GetProcessHeap(), 0, copy);
    return 1;
}

// A region's goods list is the game's running total of what its own buildings
// hold. Merged land has none of its own -- every building on it is registered
// with the town -- so the only honest total there is nothing at all. Asked what
// it holds, it answers with its town's stock through the accessors above, which
// is what a building's cost, the goods panel and construction read.
//
// Earlier versions kept the land's own list filled with a copy of the town's.
// The land then reported goods it did not hold, and that total was written to
// the save. Draining is what clears it, and it keeps running rather than simply
// stopping: a save written by an earlier version carries the copied total, and
// the game would go on believing it. An empty list drains to nothing and costs
// nothing, so this settles by itself once the land is honest.
static int drain_stock(uint8_t *land) {
    if (!land || !g_merge_goods) return 0;
    int drained = drain_one(land, 0) + drain_one(land, 1);
    if (drained) {
        static volatile LONG n;
        if (InterlockedIncrement(&n) <= 5)
            nlog("DRAIN land %p: cleared its copied goods total", (void *)land);
    }
    return drained;
}

// ---------------------------------------------------------------- Lua's way in
// Lua reaches the helper through ARegion::SetFamilyHome with a familyID no real
// family could have. Every other call runs the game's own code.
static void hooked_exec(void *ctx, void *stack, void *result) {
    uint8_t *locals = *(uint8_t **)((uint8_t *)stack + OFF_FRAME_LOCALS);
    void *code = *(void **)((uint8_t *)stack + 0x20);
    // Only direct ProcessEvent calls (Code == NULL, as UE4SS makes them) carry
    // a params struct in Locals; blueprint calls pass through untouched.
    if (code == NULL && locals) {
        int32_t family = *(int32_t *)locals;
        if (family <= MAGIC_BASE && family > MAGIC_BASE - 16) {
            int op = MAGIC_BASE - family;
            void *arg = *(void **)(locals + 8);
            if (op == OP_MOVE_RESIDENT) {
                move_resident((uint8_t *)ctx, (uint8_t *)arg);
            } else if (op == OP_MERGE_LAND) { // arg is the merged region
                note_merged((uint8_t *)arg, (uint8_t *)ctx);
                move_region_buildings((uint8_t *)ctx, (uint8_t *)arg);
                move_region_residents((uint8_t *)ctx, (uint8_t *)arg);
                share_lists((uint8_t *)ctx, (uint8_t *)arg);
            } else if (op == OP_SYNC_STOCK) { // arg is the merged region
                drain_stock((uint8_t *)arg);
            } else if (op == OP_REFRESH_ROADS) { // arg is the merged region
                refresh_roads((uint8_t *)arg);
            } else if (op == OP_REPORT_ROADS) { // read-only: what the road lookup sees
                uint8_t *r = (uint8_t *)arg;
                if (r) {
                    TArrayRaw *roads = (TArrayRaw *)(r + OFF_REG_ROADS);
                    void *pl = *(void **)(r + OFF_REG_PLANNING);
                    TArrayRaw *recs = pl ? (TArrayRaw *)((uint8_t *)pl + OFF_PLAN_RECORDS) : NULL;
                    nlog("REPORT region %p type %d root %p roads=%d planRecords=%d", (void *)r,
                         r[OFF_REG_TYPE], (void *)merge_root(r), roads->num,
                         recs ? recs->num : -1);
                    for (int32_t i = 0; i < roads->num && i < 40; i++) {
                        uint8_t *rd = ((uint8_t **)roads->data)[i];
                        TArrayRaw *pts = rd ? (TArrayRaw *)(rd + OFF_ROAD_POINTS) : NULL;
                        nlog("REPORT   %p road %p type %d kings %d pts %d", (void *)r, (void *)rd,
                             rd ? rd[0x2b4] : -1, rd ? rd[0x2b2] : -1, pts ? pts->num : -1);
                    }
                }
            }
            return;
        }
    }
    g_exec_orig(ctx, stack, result);
}

// ---------------------------------------------------------------- patching
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

// Every routine the helper hooks or calls, against the bytes it was built for,
// and every call site it redirects, against where it should still point.
// Nothing is patched unless all of them match.
static int build_matches(void) {
    static const struct { int32_t rva; const uint8_t *sig; } routines[] = {
        { RVA_SET_FAMILY_HOME_EXEC, SIG_EXEC },         { RVA_REGION_ADD_BUILDING, SIG_ADD },
        { RVA_REGION_REMOVE_BLD,    SIG_REMOVE },       { RVA_REGION_STOCK_UPDATE, SIG_STOCK },
        { RVA_TARRAY_GROW8,         SIG_GROW8 },        { RVA_FIND_REGION,         SIG_FIND_REGION },
        { RVA_REGION_CENTER,        SIG_CENTER },       { RVA_REGION_BY_POS,       SIG_REGION_BY_POS },
        { RVA_REGION_ADD_ROAD,      SIG_ADD_ROAD },     { RVA_POINT_INSIDE,        SIG_POINT_INSIDE },
        { RVA_ROAD_REGIONS,         SIG_ROAD_REGIONS }, { RVA_PLAN_SYNC,           SIG_PLAN_SYNC },
        { RVA_PLAN_DROP,            SIG_PLAN_REMOVE },  { RVA_ROAD_CLOSEST,        SIG_ROAD_CLOSEST },
        { RVA_GET_STOCK,            SIG_GET_STOCK },    { RVA_STOCK_PTR,           SIG_STOCK_PTR },
        { RVA_PERK_ACTIVE,          SIG_PERK_ACTIVE },  { RVA_HAS_SURPLUS,         SIG_HAS_SURPLUS },
        { RVA_MERGE_GOODS,          SIG_MERGE_GOODS },  { RVA_PANEL_REGION,        SIG_PANEL_REGION },
    };
    for (int i = 0; i < (int)(sizeof routines / sizeof *routines); i++)
        if (memcmp(g_base + routines[i].rva, routines[i].sig, 32)) return 0;
    static const struct { int32_t site, target; } calls[] = {
        { RVA_CALL_SET_REGION_LOAD, RVA_SET_REGION },
        { RVA_CALL_SET_REGION_NEW,  RVA_SET_REGION },
        { RVA_CALL_BY_POS_RESTORE,  RVA_REGION_BY_POS },
    };
    for (int i = 0; i < (int)(sizeof calls / sizeof *calls); i++) {
        uint8_t *s = g_base + calls[i].site;
        if (s[0] != 0xE8 || s + 5 + *(int32_t *)(s + 1) != g_base + calls[i].target) return 0;
    }
    return 1;
}

static int install(void) {
    g_base = (uint8_t *)GetModuleHandleA(NULL);
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    if (!strstr(exe, "ManorLords-Win64-Shipping.exe")) {
        write_status("DISABLED not Manor Lords");
        return 0;
    }
    if (!build_matches()) {
        write_status("DISABLED game build mismatch (expected 0.8.104)");
        return 0;
    }
    // Routines the helper only calls.
    g_add = (RegionBldFn)(g_base + RVA_REGION_ADD_BUILDING);
    g_remove = (RegionBldFn)(g_base + RVA_REGION_REMOVE_BLD);
    g_stock = (StockUpdateFn)(g_base + RVA_REGION_STOCK_UPDATE);
    g_grow8 = (GrowFn)(g_base + RVA_TARRAY_GROW8);
    g_plan_sync = (PlanSyncFn)(g_base + RVA_PLAN_SYNC);
    g_plan_drop = (PlanDropFn)(g_base + RVA_PLAN_DROP);
    g_merge_goods = (MergeGoodsFn)(g_base + RVA_MERGE_GOODS);

    // getRegionByPos first: the other hooks use its trampoline for raw answers.
    g_region_by_pos = (RegionByPosFn)hook_fn(g_base + RVA_REGION_BY_POS, (void *)&hooked_region_by_pos, 15);
    if (!g_region_by_pos) { write_status("DISABLED hook install failed"); return 0; }
    g_exec_orig = (ExecFn)hook_fn(g_base + RVA_SET_FAMILY_HOME_EXEC, (void *)&hooked_exec, 15);
    g_find_orig = (FindRegionFn)hook_fn(g_base + RVA_FIND_REGION, (void *)&hooked_find_region, 16);
    g_center_orig = (CenterFn)hook_fn(g_base + RVA_REGION_CENTER, (void *)&hooked_center, 20);
    g_add_road_orig = (AddRoadFn)hook_fn(g_base + RVA_REGION_ADD_ROAD, (void *)&hooked_add_road, 15);
    g_point_inside_orig = (PointInsideFn)hook_fn(g_base + RVA_POINT_INSIDE, (void *)&hooked_point_inside, 14);
    g_road_regions_orig = (RoadRegionsFn)hook_fn(g_base + RVA_ROAD_REGIONS, (void *)&hooked_road_regions, 16);
    g_road_closest_orig = (RoadClosestFn)hook_fn(g_base + RVA_ROAD_CLOSEST, (void *)&hooked_road_closest, 17);
    // 17, not 16: the boundaries are 2,3,7,9,13,17 and 16 would split `shl rdx,4`
    g_get_stock_orig = (GetStockFn)hook_fn(g_base + RVA_GET_STOCK, (void *)&hooked_get_stock, 17);
    // boundaries 3,7,13,16,17: 16 keeps the trailing `ret` intact
    g_stock_ptr_orig = (StockPtrFn)hook_fn(g_base + RVA_STOCK_PTR, (void *)&hooked_stock_ptr, 16);
    // boundaries 2,6,11,14: 14 is exactly the patch size
    g_perk_orig = (PerkActiveFn)hook_fn(g_base + RVA_PERK_ACTIVE, (void *)&hooked_perk_active, 14);
    // boundaries 5,10,15,20: 15 is the first at or past the 14 the patch needs
    g_has_surplus_orig = (HasSurplusFn)hook_fn(g_base + RVA_HAS_SURPLUS, (void *)&hooked_has_surplus, 15);
    // 8 bytes of body then padding; 14 is the patch size and the stolen `ret`
    // means the trampoline never reaches its jump back
    g_panel_region_orig = (PanelRegionFn)hook_fn(g_base + RVA_PANEL_REGION, (void *)&hooked_panel_region, 14);
    if (!g_exec_orig || !g_find_orig || !g_center_orig || !g_add_road_orig ||
        !g_point_inside_orig || !g_road_regions_orig || !g_road_closest_orig ||
        !g_panel_region_orig) {
        write_status("DISABLED hook install failed");
        return 0;
    }
    uint8_t *setr = g_base + RVA_SET_REGION;
    if (!redirect_call(g_base + RVA_CALL_SET_REGION_LOAD, setr, (void *)&my_set_region) ||
        !redirect_call(g_base + RVA_CALL_SET_REGION_NEW, setr, (void *)&my_set_region) ||
        // the save loader finds each region again by its stored Center, and
        // must see the land itself there rather than the town
        !redirect_call(g_base + RVA_CALL_BY_POS_RESTORE, g_base + RVA_REGION_BY_POS,
                       (void *)&restore_region_by_pos)) {
        write_status("DISABLED call sites not found");
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
        // keep the DLL loaded for the life of the process: the patches jump into it
        HMODULE self;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           (LPCSTR)&DllMain, &self);
        install();
    }
    return TRUE;
}
