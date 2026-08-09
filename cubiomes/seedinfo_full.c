// seedinfo_full.c
// Given a seed and a Minecraft version, find the nearest instance of every
// major structure type, across Overworld, Nether, and End.
//
// Parallelism strategy: rather than one thread per structure type (which is
// unbalanced - some structures require far more grid checks than others),
// the full search area for every structure is broken into small row-range
// chunks. All chunks across all structures go into one shared work queue,
// and worker threads (one per available CPU core) pull chunks from that
// queue until it's empty. This keeps every core busy and avoids any single
// structure type bottlenecking the whole search.
#include "finders.h"
#include "generator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>

typedef struct {
    int type;
    const char *name;
    int dim;
} StructureEntry;

static StructureEntry STRUCTURES[] = {
    {Village,        "village",        DIM_OVERWORLD},
    {Desert_Pyramid, "desertPyramid",  DIM_OVERWORLD},
    {Jungle_Temple,  "jungleTemple",   DIM_OVERWORLD},
    {Swamp_Hut,      "swampHut",       DIM_OVERWORLD},
    {Igloo,          "igloo",          DIM_OVERWORLD},
    {Ocean_Ruin,     "oceanRuin",      DIM_OVERWORLD},
    {Shipwreck,      "shipwreck",      DIM_OVERWORLD},
    {Monument,       "oceanMonument",  DIM_OVERWORLD},
    {Mansion,        "woodlandMansion",DIM_OVERWORLD},
    {Outpost,        "pillagerOutpost",DIM_OVERWORLD},
    {Ruined_Portal,  "ruinedPortal",   DIM_OVERWORLD},
    {Ancient_City,   "ancientCity",    DIM_OVERWORLD},
    {Mineshaft,      "mineshaft",      DIM_OVERWORLD},
    {Trail_Ruins,    "trailRuins",     DIM_OVERWORLD},
    {Trial_Chambers, "trialChambers",  DIM_OVERWORLD},
    {Fortress,       "netherFortress", DIM_NETHER},
    {Bastion,        "bastionRemnant", DIM_NETHER},
    {Ruined_Portal_N,"ruinedPortalNether", DIM_NETHER},
    {End_City,       "endCity",        DIM_END},
    {Treasure,       "buriedTreasure", DIM_OVERWORLD},
    {Desert_Well,    "desertWell",     DIM_OVERWORLD},
    {Geode,          "amethystGeode",  DIM_OVERWORLD},
    {End_Gateway,    "endGateway",     DIM_END},
};

#define NUM_STRUCTURES (int)(sizeof(STRUCTURES) / sizeof(STRUCTURES[0]))
#define ROWS_PER_CHUNK 4  // how many region-rows each work unit covers

// A few structures are either placed at the chunk level (extremely dense)
// or have a more expensive per-check cost than most other structures,
// making the standard 20,000-block target needlessly slow. None of them
// need a large radius to find one nearby. Applied consistently wherever
// a structure's search radius is computed - the work-queue size
// pre-calculation and the actual work-unit generation MUST agree, or the
// queue array would overflow.
static int getBlockRadiusFor(int structureType, int desiredBlockRadius) {
    if (structureType == Treasure || structureType == Geode || structureType == End_Gateway) {
        return desiredBlockRadius < 2000 ? desiredBlockRadius : 2000;
    }
    if (structureType == Monument || structureType == End_City) {
        return desiredBlockRadius < 3000 ? desiredBlockRadius : 3000;
    }
    if (structureType == Desert_Well) {
        return desiredBlockRadius < 5000 ? desiredBlockRadius : 5000;
    }
    return desiredBlockRadius;
}

static int parseVersion(const char *v) {
    if (strcmp(v, "1.0") == 0)  return MC_1_0;
    if (strcmp(v, "1.1") == 0)  return MC_1_1;
    if (strcmp(v, "1.2") == 0)  return MC_1_2;
    if (strcmp(v, "1.3") == 0)  return MC_1_3;
    if (strcmp(v, "1.4") == 0)  return MC_1_4;
    if (strcmp(v, "1.5") == 0)  return MC_1_5;
    if (strcmp(v, "1.6") == 0)  return MC_1_6;
    if (strcmp(v, "1.7") == 0)  return MC_1_7;
    if (strcmp(v, "1.8") == 0)  return MC_1_8;
    if (strcmp(v, "1.9") == 0)  return MC_1_9;
    if (strcmp(v, "1.10") == 0) return MC_1_10;
    if (strcmp(v, "1.11") == 0) return MC_1_11;
    if (strcmp(v, "1.12") == 0) return MC_1_12;
    if (strcmp(v, "1.13") == 0) return MC_1_13;
    if (strcmp(v, "1.14") == 0) return MC_1_14;
    if (strcmp(v, "1.15") == 0) return MC_1_15;
    if (strcmp(v, "1.16") == 0) return MC_1_16;
    if (strcmp(v, "1.17") == 0) return MC_1_17;
    if (strcmp(v, "1.18") == 0) return MC_1_18;
    if (strcmp(v, "1.19") == 0) return MC_1_19;
    if (strcmp(v, "1.20") == 0) return MC_1_20;
    if (strcmp(v, "1.21") == 0) return MC_1_21;
    return -1;
}

static int64_t g_seed;
static int g_mc;
static int g_fromX, g_fromZ;
static int g_desiredBlockRadius = 20000;

typedef struct {
    int found;
    int x, z;
    double bestDist; // squared distance, for comparison
} StructureResult;
static StructureResult results[NUM_STRUCTURES];
static pthread_mutex_t resultMutex[NUM_STRUCTURES];

// One unit of work: check a range of rows (rz values) for one structure type.
typedef struct {
    int structureIdx;
    int regionBlocks;
    int originRegX, originRegZ, searchRadius;
    int rzStart, rzEnd; // inclusive range of rows to check
} WorkUnit;

static WorkUnit *workQueue;
static int workQueueSize = 0;
static int workQueueIndex = 0; // next unclaimed unit
static pthread_mutex_t queueMutex = PTHREAD_MUTEX_INITIALIZER;

void *worker(void *arg) {
    (void)arg;
    // Each worker thread needs its own Generator - not safe to share across threads.
    Generator threadGenerators[NUM_STRUCTURES];
    int generatorReady[NUM_STRUCTURES];
    memset(generatorReady, 0, sizeof(generatorReady));

    while (1) {
        WorkUnit unit;
        pthread_mutex_lock(&queueMutex);
        if (workQueueIndex >= workQueueSize) {
            pthread_mutex_unlock(&queueMutex);
            break;
        }
        unit = workQueue[workQueueIndex];
        workQueueIndex++;
        pthread_mutex_unlock(&queueMutex);

        StructureEntry *entry = &STRUCTURES[unit.structureIdx];

        if (!generatorReady[unit.structureIdx]) {
            setupGenerator(&threadGenerators[unit.structureIdx], g_mc, 0);
            applySeed(&threadGenerators[unit.structureIdx], entry->dim, (uint64_t)g_seed);
            generatorReady[unit.structureIdx] = 1;
        }
        Generator *g = &threadGenerators[unit.structureIdx];

        int localFound = 0;
        Pos localBestPos = {0, 0};
        double localBestDist = -1;

        for (int rz = unit.rzStart; rz <= unit.rzEnd; rz++) {
            for (int rx = unit.originRegX - unit.searchRadius; rx <= unit.originRegX + unit.searchRadius; rx++) {
                Pos p;
                if (!getStructurePos(entry->type, g_mc, (uint64_t)g_seed, rx, rz, &p))
                    continue;
                if (!isViableStructurePos(entry->type, g, p.x, p.z, 0))
                    continue;

                double dx = p.x - g_fromX;
                double dz = p.z - g_fromZ;
                double dist = dx * dx + dz * dz;
                if (localBestDist < 0 || dist < localBestDist) {
                    localBestDist = dist;
                    localBestPos = p;
                    localFound = 1;
                }
            }
        }

        if (localFound) {
            pthread_mutex_lock(&resultMutex[unit.structureIdx]);
            StructureResult *out = &results[unit.structureIdx];
            if (!out->found || localBestDist < out->bestDist) {
                out->found = 1;
                out->x = localBestPos.x;
                out->z = localBestPos.z;
                out->bestDist = localBestDist;
            }
            pthread_mutex_unlock(&resultMutex[unit.structureIdx]);
        }
    }
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "Usage: %s <seed> <version> [fromX] [fromZ]\n", argv[0]);
        return 1;
    }

    g_seed = strtoll(argv[1], NULL, 10);
    g_mc = parseVersion(argv[2]);
    if (g_mc < 0)
    {
        fprintf(stderr, "Unknown version: %s\n", argv[2]);
        return 1;
    }

    g_fromX = 0;
    g_fromZ = 0;
    if (argc >= 5)
    {
        g_fromX = atoi(argv[3]);
        g_fromZ = atoi(argv[4]);
    }

    Generator spawnGen;
    setupGenerator(&spawnGen, g_mc, 0);
    applySeed(&spawnGen, DIM_OVERWORLD, (uint64_t)g_seed);
    Pos spawn = getSpawn(&spawnGen);

    int supported[NUM_STRUCTURES];

    // Build the full work queue up front: for every supported structure,
    // split its search area into small row-chunks.
    int maxPossibleUnits = 0;
    for (int i = 0; i < NUM_STRUCTURES; i++) {
        StructureConfig sconf;
        supported[i] = getStructureConfig(STRUCTURES[i].type, g_mc, &sconf);
        pthread_mutex_init(&resultMutex[i], NULL);
        results[i].found = 0;
        if (!supported[i]) continue;

        int regionBlocks = sconf.regionSize * 16;
        if (regionBlocks <= 0) regionBlocks = 512;
        int effectiveRadius = getBlockRadiusFor(STRUCTURES[i].type, g_desiredBlockRadius);
        int searchRadius = (int)ceil((double)effectiveRadius / regionBlocks);
        int totalRows = 2 * searchRadius + 1;
        maxPossibleUnits += (totalRows + ROWS_PER_CHUNK - 1) / ROWS_PER_CHUNK;
    }

    workQueue = malloc(sizeof(WorkUnit) * maxPossibleUnits);
    workQueueSize = 0;

    for (int i = 0; i < NUM_STRUCTURES; i++) {
        if (!supported[i]) continue;

        StructureConfig sconf;
        getStructureConfig(STRUCTURES[i].type, g_mc, &sconf);
        int regionBlocks = sconf.regionSize * 16;
        if (regionBlocks <= 0) regionBlocks = 512;
        int effectiveRadius = getBlockRadiusFor(STRUCTURES[i].type, g_desiredBlockRadius);
        int searchRadius = (int)ceil((double)effectiveRadius / regionBlocks);
        int originRegX = (int)floor((double)g_fromX / regionBlocks);
        int originRegZ = (int)floor((double)g_fromZ / regionBlocks);

        for (int rzStart = originRegZ - searchRadius; rzStart <= originRegZ + searchRadius; rzStart += ROWS_PER_CHUNK) {
            int rzEnd = rzStart + ROWS_PER_CHUNK - 1;
            if (rzEnd > originRegZ + searchRadius) rzEnd = originRegZ + searchRadius;

            WorkUnit unit;
            unit.structureIdx = i;
            unit.regionBlocks = regionBlocks;
            unit.originRegX = originRegX;
            unit.originRegZ = originRegZ;
            unit.searchRadius = searchRadius;
            unit.rzStart = rzStart;
            unit.rzEnd = rzEnd;
            workQueue[workQueueSize++] = unit;
        }
    }

    // Detect available CPU cores at runtime, capped at a sane maximum.
    // Detect available CPU cores, but cap conservatively - constrained
    // hosting environments (like free-tier cloud containers) often report
    // the HOST machine's full core count via sysconf, even when the
    // actual allocated CPU is a tiny throttled slice (e.g. 0.1 of a
    // core). Spawning many threads to compete over that sliver causes
    // more OS scheduling overhead than actual progress - genuinely
    // slower than using fewer threads, not faster. Capping at 2 is safe
    // on real multi-core machines (still gets real parallelism) and
    // avoids the over-threading trap on constrained single-core-equivalent
    // hosting.
    long nCores = sysconf(_SC_NPROCESSORS_ONLN);
    if (nCores < 1) nCores = 1;
    if (nCores > 2) nCores = 2;

    pthread_t *threads = malloc(sizeof(pthread_t) * nCores);
    for (int i = 0; i < nCores; i++) {
        pthread_create(&threads[i], NULL, worker, NULL);
    }
    for (int i = 0; i < nCores; i++) {
        pthread_join(threads[i], NULL);
    }
    free(threads);
    free(workQueue);

    printf("{\n");
    printf("  \"seed\": \"%" PRId64 "\",\n", g_seed);
    printf("  \"version\": \"%s\",\n", argv[2]);
    printf("  \"spawn\": {\"x\": %d, \"z\": %d},\n", spawn.x, spawn.z);
    printf("  \"searchOrigin\": {\"x\": %d, \"z\": %d},\n", g_fromX, g_fromZ);
    printf("  \"structures\": {\n");

    for (int i = 0; i < NUM_STRUCTURES; i++)
    {
        printf("    \"%s\": ", STRUCTURES[i].name);
        if (supported[i] && results[i].found)
        {
            printf("{\"x\": %d, \"z\": %d, \"distance\": %.1f}",
                results[i].x, results[i].z, sqrt(results[i].bestDist));
        }
        else
        {
            printf("null");
        }
        if (i < NUM_STRUCTURES - 1) printf(",");
        printf("\n");
    }

    printf("  }\n");
    printf("}\n");

    return 0;
}