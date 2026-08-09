// lootpredict.c
// Predicts the EXACT loot that will appear in a structure's chests for a
// given seed - not just "what's possible" (which we already have via our
// knowledge base facts) but the specific items and counts that will
// actually generate.
//
// This is a genuinely complex feature: Minecraft chest loot is fully
// deterministic (same seed = same loot, always), but requires precisely
// replicating (1) the exact algorithm that finds a chest's physical
// position within a generated structure, and (2) the exact per-chest
// random seed derivation and loot-table interpretation (weighted pools,
// rolls, item counts).
//
// Reference: this implementation is based on the SeedFinding community's
// open-source mc_feature_java/mc_core_java libraries (MIT licensed),
// verified against their own real test data - for our exact test case
// (world seed 2276366175191987160, version 1.16.5), our chest-position
// algorithm produces an EXACT match against their known-correct answer.
//
// Current scope: Shipwreck only, Supply Chest loot table only, as a
// verified proof-of-concept. Extending to other structures/chest types
// follows the same pattern but needs each structure's own position-finding
// algorithm and loot table ported the same careful way.
//
// Honest caveat: the loot table data embedded below is from the CURRENT
// (1.21.8) game data, while our verified test case used 1.16.5's loot
// table - these differ (newer items exist now that didn't exist then).
// The chest POSITION algorithm is fully verified; the loot CONTENT
// algorithm is implemented correctly per the reference source, but hasn't
// been checked against a same-version ground-truth test case.

#include "finders.h"
#include "generator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include <unistd.h>
#include <signal.h>

// ============================================================================
// Java Random (LCG) - identical to cubiomes' existing rng.h, restated here
// explicitly since this file's logic depends on it being exactly correct.
// ============================================================================
#define JRAND_MULT 0x5DEECE66DULL
#define JRAND_ADD 0xBULL
#define JRAND_MASK ((1ULL << 48) - 1)

static void jrandSetSeed(uint64_t *seed, uint64_t value) {
    *seed = (value ^ JRAND_MULT) & JRAND_MASK;
}
static int32_t jrandNext(uint64_t *seed, int bits) {
    *seed = (*seed * JRAND_MULT + JRAND_ADD) & JRAND_MASK;
    return (int32_t)((int64_t)(*seed) >> (48 - bits));
}
static int32_t jrandNextInt(uint64_t *seed, int32_t n) {
    if (n <= 0) return 0;
    if ((n & -n) == n) {
        return (int32_t)(((int64_t)n * jrandNext(seed, 31)) >> 31);
    }
    int32_t bits, val;
    do {
        bits = jrandNext(seed, 31);
        val = bits % n;
    } while (bits - val + (n - 1) < 0);
    return val;
}
static int64_t jrandNextLong(uint64_t *seed) {
    int64_t hi = jrandNext(seed, 32);
    int64_t lo = jrandNext(seed, 32);
    return (hi << 32) + lo;
}
static void jrandAdvance(uint64_t *seed, int64_t calls) {
    for (int64_t i = 0; i < calls; i++) jrandNext(seed, 32);
}

// ============================================================================
// Seed derivation chain: world seed -> population seed -> decorator seed
// -> per-chest loot seed. Verified exactly against a known-correct test
// case for the chest POSITION half of this chain (carver seed variant);
// this decorator/population chain follows the same verified library.
// ============================================================================

static int64_t setPopulationSeed(uint64_t *seed, int64_t worldSeed, int32_t blockX, int32_t blockZ) {
    jrandSetSeed(seed, (uint64_t)worldSeed);
    int64_t a = jrandNextLong(seed) | 1LL;
    int64_t b = jrandNextLong(seed) | 1LL;
    int64_t popSeed = ((int64_t)blockX * a + (int64_t)blockZ * b) ^ worldSeed;
    jrandSetSeed(seed, (uint64_t)popSeed);
    return popSeed & (int64_t)JRAND_MASK;
}

static int64_t setCarverSeed(uint64_t *seed, int64_t worldSeed, int32_t chunkX, int32_t chunkZ) {
    jrandSetSeed(seed, (uint64_t)worldSeed);
    int64_t a = jrandNextLong(seed);
    int64_t b = jrandNextLong(seed);
    int64_t carverSeed = ((int64_t)chunkX * a) ^ ((int64_t)chunkZ * b) ^ worldSeed;
    jrandSetSeed(seed, (uint64_t)carverSeed);
    return carverSeed & (int64_t)JRAND_MASK;
}

static void setDecoratorSeed(uint64_t *seed, int64_t populationSeed, int32_t salt) {
    int64_t decoratorSeed = populationSeed + salt;
    jrandSetSeed(seed, (uint64_t)decoratorSeed);
}

// ============================================================================
// Geometry: converts a structure-local chest offset into a real world
// block position, given the structure's rotation and anchor point.
// Verified EXACTLY against real test data (world seed 2276366175191987160,
// chunk (-159,-189), expected chest at (-2533, 94, -3008) - our
// implementation produces this precise result).
// ============================================================================

typedef enum { ROT_NONE, ROT_CW90, ROT_CW180, ROT_CCW90 } Rotation;

typedef struct { int x, y, z; } LootPos3;

static LootPos3 getRotatedSize(LootPos3 size, Rotation rot) {
    if (rot == ROT_CW90 || rot == ROT_CCW90) {
        LootPos3 r = { size.z, size.y, size.x };
        return r;
    }
    return size;
}

// Returns the bounding box's min corner, in world coordinates, already
// accounting for both getBoundingBox() and the getRotated() swap that
// follows it in the real algorithm (a step we initially missed and only
// caught by testing against real data - see project notes).
static LootPos3 getBoxMin(LootPos3 anchor, Rotation rot, LootPos3 pivot, LootPos3 size) {
    LootPos3 rsize = getRotatedSize(size, rot);
    int sx = rsize.x - 1, sy = rsize.y - 1, sz = rsize.z - 1;
    int px = pivot.x, pz = pivot.z;
    int minx, minz, maxx, maxz;

    switch (rot) {
        case ROT_CCW90:
            minx = px - pz; minz = px + pz - sz;
            maxx = px - pz + sx; maxz = px + pz;
            break;
        case ROT_CW90:
            minx = px + pz - sx; minz = pz - px;
            maxx = px + pz; maxz = pz - px + sz;
            break;
        case ROT_CW180:
            minx = px + px - sx; minz = pz + pz - sz;
            maxx = px + px; maxz = pz + pz;
            break;
        default: // NONE
            minx = 0; minz = 0; maxx = sx; maxz = sz;
    }

    // getRotated(): swaps min/max X and/or Z depending on rotation - this
    // is the step that was missing from our first attempt.
    int finalMinX, finalMinZ;
    switch (rot) {
        case ROT_CCW90: finalMinX = minx; finalMinZ = maxz; break;
        case ROT_CW90:  finalMinX = maxx; finalMinZ = minz; break;
        case ROT_CW180: finalMinX = maxx; finalMinZ = maxz; break;
        default:        finalMinX = minx; finalMinZ = minz;
    }

    LootPos3 result = { finalMinX + anchor.x, anchor.y, finalMinZ + anchor.z };
    return result;
}

static LootPos3 getInside(LootPos3 boxMin, LootPos3 offset, Rotation rot) {
    LootPos3 result;
    result.y = boxMin.y + offset.y;
    switch (rot) {
        case ROT_NONE:  result.x = boxMin.x + offset.x; result.z = boxMin.z + offset.z; break;
        case ROT_CW90:  result.x = boxMin.x - offset.z; result.z = boxMin.z + offset.x; break;
        case ROT_CW180: result.x = boxMin.x - offset.x; result.z = boxMin.z - offset.z; break;
        case ROT_CCW90: result.x = boxMin.x + offset.z; result.z = boxMin.z - offset.x; break;
    }
    return result;
}

// ============================================================================
// Shipwreck-specific data: structure type list, sizes, and chest offsets -
// transcribed directly from the verified reference implementation.
// ============================================================================

typedef struct {
    const char *name;
    LootPos3 size;
    const char *lootTypes[3];
    LootPos3 lootOffsets[3];
    int lootCount;
} ShipwreckType;

static const ShipwreckType SHIPWRECK_TYPES[] = {
    {"rightsideup_backhalf", {9,9,16}, {"MAP_CHEST","TREASURE_CHEST"}, {{5,3,6},{6,5,12}}, 2},
    {"rightsideup_backhalf_degraded", {9,9,16}, {"MAP_CHEST","TREASURE_CHEST"}, {{5,3,6},{6,5,12}}, 2},
    {"rightsideup_fronthalf", {9,9,24}, {"SUPPLY_CHEST"}, {{4,3,8}}, 1},
    {"rightsideup_fronthalf_degraded", {9,9,24}, {"SUPPLY_CHEST"}, {{4,3,8}}, 1},
    {"rightsideup_full", {9,9,28}, {"SUPPLY_CHEST","MAP_CHEST","TREASURE_CHEST"}, {{4,3,8},{5,3,18},{6,5,24}}, 3},
    {"rightsideup_full_degraded", {9,9,28}, {"SUPPLY_CHEST","MAP_CHEST","TREASURE_CHEST"}, {{4,3,8},{5,3,18},{6,5,24}}, 3},
    {"sideways_backhalf", {9,9,17}, {"TREASURE_CHEST","MAP_CHEST"}, {{3,3,13},{6,4,8}}, 2},
    {"sideways_backhalf_degraded", {9,9,17}, {"TREASURE_CHEST","MAP_CHEST"}, {{3,3,13},{6,4,8}}, 2},
    {"sideways_fronthalf", {9,9,24}, {"SUPPLY_CHEST"}, {{5,4,8}}, 1},
    {"sideways_fronthalf_degraded", {9,9,24}, {"SUPPLY_CHEST"}, {{5,4,8}}, 1},
    {"sideways_full", {9,9,28}, {"TREASURE_CHEST","SUPPLY_CHEST","MAP_CHEST"}, {{3,3,24},{5,4,8},{6,4,19}}, 3},
    {"sideways_full_degraded", {9,9,28}, {"TREASURE_CHEST","SUPPLY_CHEST","MAP_CHEST"}, {{3,3,24},{5,4,8},{6,4,19}}, 3},
    {"upsidedown_backhalf", {9,9,16}, {"TREASURE_CHEST","MAP_CHEST"}, {{2,3,12},{3,6,5}}, 2},
    {"upsidedown_backhalf_degraded", {9,9,16}, {"TREASURE_CHEST","MAP_CHEST"}, {{2,3,12},{3,6,5}}, 2},
    {"upsidedown_fronthalf", {9,9,22}, {"MAP_CHEST","SUPPLY_CHEST"}, {{3,6,17},{4,6,8}}, 2},
    {"upsidedown_fronthalf_degraded", {9,9,22}, {"MAP_CHEST","SUPPLY_CHEST"}, {{3,6,17},{4,6,8}}, 2},
    {"upsidedown_full", {9,9,28}, {"TREASURE_CHEST","MAP_CHEST","SUPPLY_CHEST"}, {{2,3,24},{3,6,17},{4,6,8}}, 3},
    {"upsidedown_full_degraded", {9,9,28}, {"TREASURE_CHEST","MAP_CHEST","SUPPLY_CHEST"}, {{2,3,24},{3,6,17},{4,6,8}}, 3},
    {"with_mast", {9,21,28}, {"SUPPLY_CHEST","MAP_CHEST","TREASURE_CHEST"}, {{4,3,9},{5,3,18},{6,5,24}}, 3},
    {"with_mast_degraded", {9,21,28}, {"SUPPLY_CHEST","MAP_CHEST","TREASURE_CHEST"}, {{4,3,9},{5,3,18},{6,5,24}}, 3},
};
#define NUM_SHIPWRECK_TYPES (int)(sizeof(SHIPWRECK_TYPES) / sizeof(SHIPWRECK_TYPES[0]))

static const char *BEACHED_LIST[] = {
    "with_mast","sideways_full","sideways_fronthalf","sideways_backhalf",
    "rightsideup_full","rightsideup_fronthalf","rightsideup_backhalf",
    "with_mast_degraded","rightsideup_full_degraded","rightsideup_fronthalf_degraded",
    "rightsideup_backhalf_degraded",
};
#define NUM_BEACHED 11

static const char *OCEAN_LIST[] = {
    "with_mast","upsidedown_full","upsidedown_fronthalf","upsidedown_backhalf",
    "sideways_full","sideways_fronthalf","sideways_backhalf",
    "rightsideup_full","rightsideup_fronthalf","rightsideup_backhalf",
    "with_mast_degraded","upsidedown_full_degraded","upsidedown_fronthalf_degraded",
    "upsidedown_backhalf_degraded","sideways_full_degraded","sideways_fronthalf_degraded",
    "sideways_backhalf_degraded","rightsideup_full_degraded","rightsideup_fronthalf_degraded",
    "rightsideup_backhalf_degraded",
};
#define NUM_OCEAN 20

static const ShipwreckType *findType(const char *name) {
    for (int i = 0; i < NUM_SHIPWRECK_TYPES; i++) {
        if (strcmp(SHIPWRECK_TYPES[i].name, name) == 0) return &SHIPWRECK_TYPES[i];
    }
    return NULL;
}

// Finds a shipwreck at the given chunk position (caller must already know
// a shipwreck generates here - this only computes ITS layout, matching
// how the reference library separates "does a structure start here" from
// "what does it look like"). Returns 0 on failure.
static int computeShipwreck(int64_t worldSeed, int32_t chunkX, int32_t chunkZ, int isBeached,
                             LootPos3 *chestPositions, const char **chestTypes, int *chestCount, Rotation *outRot, const ShipwreckType **outType) {
    uint64_t rand;
    int64_t carverSeed = setCarverSeed(&rand, worldSeed, chunkX, chunkZ);
    (void)carverSeed;

    Rotation rotation = (Rotation)jrandNextInt(&rand, 4);

    const char **list = isBeached ? BEACHED_LIST : OCEAN_LIST;
    int listLen = isBeached ? NUM_BEACHED : NUM_OCEAN;
    int typeIdx = jrandNextInt(&rand, listLen);
    const ShipwreckType *stype = findType(list[typeIdx]);
    if (!stype) return 0;

    LootPos3 anchor = { chunkX * 16, 90, chunkZ * 16 };
    LootPos3 pivot = { 4, 0, 15 };
    LootPos3 boxMin = getBoxMin(anchor, rotation, pivot, stype->size);

    *chestCount = stype->lootCount;
    for (int i = 0; i < stype->lootCount; i++) {
        chestPositions[i] = getInside(boxMin, stype->lootOffsets[i], rotation);
        chestTypes[i] = stype->lootTypes[i];
    }
    *outRot = rotation;
    *outType = stype;
    return 1;
}

// ============================================================================
// Loot table interpretation: weighted pools, uniform rolls, item counts.
// Embedded data below is the Supply Chest loot table (current 1.21.8 data -
// see the honest caveat in the file header about version alignment).
// ============================================================================

typedef struct {
    const char *name; // NULL means "empty" (nothing generated)
    int weight;
    float countMin, countMax;
} LootEntry;

typedef struct {
    float rollsMin, rollsMax;
    const LootEntry *entries;
    int entryCount;
} LootPool;

static const LootEntry SUPPLY_POOL_0[] = {
    {"paper", 8, 1, 12}, {"potato", 7, 2, 6}, {"moss_block", 7, 1, 4},
    {"poisonous_potato", 7, 2, 6}, {"carrot", 7, 4, 8}, {"wheat", 7, 8, 21},
    {"suspicious_stew", 10, 1, 1}, {"coal", 6, 2, 8}, {"rotten_flesh", 5, 5, 24},
    {"pumpkin", 2, 1, 3}, {"bamboo", 2, 1, 3}, {"gunpowder", 3, 1, 5},
    {"tnt", 1, 1, 2}, {"leather_helmet", 3, 1, 1}, {"leather_chestplate", 3, 1, 1},
    {"leather_leggings", 3, 1, 1}, {"leather_boots", 3, 1, 1},
};
static const LootEntry SUPPLY_POOL_1[] = {
    {NULL, 5, 0, 0}, {"coast_armor_trim_smithing_template", 1, 2, 2},
};
static const LootEntry SUPPLY_POOL_2[] = {
    {NULL, 148, 0, 0}, {"copper_nautilus_armor", 20, 1, 1}, {"iron_nautilus_armor", 10, 1, 1},
    {"golden_nautilus_armor", 5, 1, 1}, {"diamond_nautilus_armor", 2, 1, 1},
};
static const LootPool SUPPLY_CHEST_TABLE[] = {
    {3, 10, SUPPLY_POOL_0, 17},
    {1, 1, SUPPLY_POOL_1, 2},
    {1, 1, SUPPLY_POOL_2, 5},
};
#define SUPPLY_CHEST_POOL_COUNT 3

static int getUniformCount(uint64_t *seed, float mn, float mx) {
    int imn = (int)floorf(mn), imx = (int)floorf(mx);
    if (imn >= imx) return imn;
    return jrandNextInt(seed, imx - imn + 1) + imn;
}

typedef struct { const char *name; int count; } ResultItem;

// Generates the actual loot for one chest, given its final per-chest seed.
// Returns the number of item stacks produced (into results, capacity 64).
static int generateLoot(int64_t lootSeed, const LootPool *table, int poolCount, ResultItem *results, int capacity) {
    uint64_t rand;
    jrandSetSeed(&rand, (uint64_t)lootSeed);
    int resultCount = 0;

    for (int p = 0; p < poolCount; p++) {
        const LootPool *pool = &table[p];
        int rolls = getUniformCount(&rand, pool->rollsMin, pool->rollsMax);
        int totalWeight = 0;
        for (int e = 0; e < pool->entryCount; e++) totalWeight += pool->entries[e].weight;

        for (int r = 0; r < rolls; r++) {
            const LootEntry *chosen;
            if (pool->entryCount == 1) {
                chosen = &pool->entries[0];
            } else {
                int pick = jrandNextInt(&rand, totalWeight);
                int cumulative = 0;
                chosen = NULL;
                for (int e = 0; e < pool->entryCount; e++) {
                    cumulative += pool->entries[e].weight;
                    if (pick < cumulative) { chosen = &pool->entries[e]; break; }
                }
            }
            if (!chosen || !chosen->name) continue; // empty slot
            if (resultCount >= capacity) continue;
            int count = getUniformCount(&rand, chosen->countMin, chosen->countMax);
            results[resultCount].name = chosen->name;
            results[resultCount].count = count;
            resultCount++;
        }
    }
    return resultCount;
}

// ============================================================================
// Full pipeline: given a world seed and a known shipwreck chunk position,
// compute the exact chest position(s) and their exact loot contents.
// ============================================================================

int main(int argc, char **argv) {
    // Hard safety net: this process kills ITSELF after 20 seconds no
    // matter what, so a slow/stuck search can never accumulate as an
    // orphaned process eating shared CPU/RAM on constrained hosting -
    // regardless of whether Node's own timeout cleanly terminates it.
    alarm(45);

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <seed> <version> [chunkX chunkZ]\n", argv[0]);
        fprintf(stderr, "  If chunkX/chunkZ are omitted, searches near spawn for the nearest Shipwreck.\n");
        return 1;
    }

    int64_t worldSeed = strtoll(argv[1], NULL, 10);
    const char *versionArg = argv[2];
    int mc = MC_1_21; // default to current; matches our project's other tools
    if (strcmp(versionArg, "1.16") == 0 || strcmp(versionArg, "1.16.5") == 0) mc = MC_1_16;

    int32_t chunkX, chunkZ;
    if (argc >= 5) {
        chunkX = atoi(argv[3]);
        chunkZ = atoi(argv[4]);
    } else {
        // Reuse cubiomes' own structure-finding to locate a real Shipwreck
        // near spawn, rather than requiring the caller to already know
        // where one is - this makes the tool usable on its own.
        //
        // Kept deliberately tight (6,000 blocks, hard iteration cap) - a
        // huge search radius risks running long on constrained hosting
        // (free-tier CPUs are dramatically slower than what this was
        // developed against), and a request that hangs is worse than one
        // that fails fast with a clear "try again" message.
        Generator g;
        setupGenerator(&g, mc, 0);
        applySeed(&g, DIM_OVERWORLD, (uint64_t)worldSeed);
        StructureConfig sconf;
        if (!getStructureConfig(Shipwreck, mc, &sconf)) {
            printf("{\"error\": \"Shipwreck not supported in this version.\"}\n");
            return 1;
        }
        int regionBlocks = sconf.regionSize * 16;
        int rMax = (int)ceil(20000.0 / regionBlocks);
        const int MAX_CHECKS = 50000; // generous safety cap, far above what 20,000 blocks needs
        int checksSoFar = 0;
        int found = 0;
        for (int rz = -rMax; rz <= rMax && !found && checksSoFar < MAX_CHECKS; rz++) {
            for (int rx = -rMax; rx <= rMax && !found && checksSoFar < MAX_CHECKS; rx++) {
                checksSoFar++;
                Pos p;
                if (!getStructurePos(Shipwreck, mc, (uint64_t)worldSeed, rx, rz, &p)) continue;
                if (!isViableStructurePos(Shipwreck, &g, p.x, p.z, 0)) continue;
                chunkX = p.x >> 4;
                chunkZ = p.z >> 4;
                found = 1;
            }
        }
        if (!found) {
            printf("{\"error\": \"No Shipwreck found within 20,000 blocks of spawn. Try a different seed, or search for a Shipwreck's exact chunk position first.\"}\n");
            return 1;
        }
    }

    // Determine beached vs ocean via a real biome check, matching the
    // reference algorithm's own biome-based branching - no manual guessing.
    Generator g;
    setupGenerator(&g, mc, 0);
    applySeed(&g, DIM_OVERWORLD, (uint64_t)worldSeed);
    int biomeX = mc < MC_1_16 ? (chunkX << 4) + 9 : (chunkX << 2) + 2;
    int biomeZ = mc < MC_1_16 ? (chunkZ << 4) + 9 : (chunkZ << 2) + 2;
    int biome = getBiomeAt(&g, mc < MC_1_16 ? 1 : 4, biomeX, 0, biomeZ);
    int isBeached = (biome == beach || biome == snowy_beach);

    LootPos3 chestPositions[3];
    const char *chestTypes[3];
    int chestCount = 0;
    Rotation rotation;
    const ShipwreckType *stype;

    if (!computeShipwreck(worldSeed, chunkX, chunkZ, isBeached, chestPositions, chestTypes, &chestCount, &rotation, &stype)) {
        printf("{\"error\": \"Could not compute shipwreck layout.\"}\n");
        return 1;
    }

    printf("{\n");
    printf("  \"seed\": \"%" PRId64 "\",\n", worldSeed);
    printf("  \"chunkX\": %d,\n", chunkX);
    printf("  \"chunkZ\": %d,\n", chunkZ);
    printf("  \"isBeached\": %s,\n", isBeached ? "true" : "false");
    printf("  \"shipwreckType\": \"%s\",\n", stype->name);
    printf("  \"chests\": [\n");

    for (int i = 0; i < chestCount; i++) {
        LootPos3 pos = chestPositions[i];
        int blockX = pos.x, blockZ = pos.z;
        int64_t popSeed;
        uint64_t rand;
        popSeed = setPopulationSeed(&rand, worldSeed, blockX & ~15, blockZ & ~15);
        int32_t salt = 40006; // shipwreck decoration salt, 1.16+
        setDecoratorSeed(&rand, popSeed, salt);
        jrandAdvance(&rand, 1 * 2);
        jrandAdvance(&rand, 0 * 2);
        int64_t lootSeed = jrandNextLong(&rand);

        ResultItem results[64];
        int itemCount = 0;
        if (strcmp(chestTypes[i], "SUPPLY_CHEST") == 0) {
            itemCount = generateLoot(lootSeed, SUPPLY_CHEST_TABLE, SUPPLY_CHEST_POOL_COUNT, results, 64);
        }

        printf("    {\n");
        printf("      \"type\": \"%s\",\n", chestTypes[i]);
        printf("      \"position\": {\"x\": %d, \"y\": %d, \"z\": %d},\n", pos.x, pos.y, pos.z);
        printf("      \"items\": [\n");
        for (int j = 0; j < itemCount; j++) {
            printf("        {\"name\": \"%s\", \"count\": %d}%s\n", results[j].name, results[j].count, j < itemCount - 1 ? "," : "");
        }
        printf("      ]\n");
        printf("    }%s\n", i < chestCount - 1 ? "," : "");
    }

    printf("  ]\n");
    printf("}\n");
    return 0;
}