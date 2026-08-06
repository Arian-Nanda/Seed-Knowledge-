// seedcombo_bedrock.c
// Bedrock Edition version of seedcombo. Given a seed, a list of structure
// types, and how many of each is wanted, find the top 5 tightest clusters.
// Supports asking for multiple instances of the SAME structure type close
// together (e.g. two Woodland Mansions), guaranteed to be distinct
// physical structures, never the same one counted twice. Bedrock players
// are always on the latest version, so no version argument is needed.
//
// Search strategy: tries a small radius first (fast). If any requested
// structure type isn't found yet, automatically expands the search - but
// only scans the NEW ring of area added by the larger radius, never
// re-scanning ground already covered by a smaller tier.
#include "finders.h"
#include "Bfinders.h"
#include "generator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>

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
};
#define NUM_STRUCTURES (int)(sizeof(STRUCTURES) / sizeof(STRUCTURES[0]))

#define MAX_TYPES 20   // distinct structure types requested
#define MAX_SLOTS 30   // total instances requested (sum of counts)
#define MAX_CANDIDATES 60
#define COMBO_BUDGET 3000000.0
#define TOP_N 5

static const int TIERS[] = {20000, 40000, 60000, 80000, 100000};
#define NUM_TIERS (int)(sizeof(TIERS) / sizeof(TIERS[0]))

// Mineshafts/Treasure are extremely dense and Bedrock's per-cell check is
// much more computationally expensive than other structures, so cap their
// search target much lower. Monument/End City aren't dense but are still
// expensive per-check.
#define DENSE_STRUCTURE_MAX_RADIUS 2000
#define EXPENSIVE_STRUCTURE_MAX_RADIUS 3000

typedef struct { int x, z; double dist; } Candidate;

static int findStructureIndex(const char *name) {
    for (int i = 0; i < NUM_STRUCTURES; i++)
        if (strcmp(STRUCTURES[i].name, name) == 0) return i;
    return -1;
}

static Candidate rawCandidates[MAX_TYPES][MAX_CANDIDATES]; // kept sorted, bounded to K
static int rawCount[MAX_TYPES];
static int g_K = MAX_CANDIDATES; // set once numSlots is known

typedef struct { int x, z; } Point;
static Point candidates[MAX_TYPES][MAX_CANDIDATES];
static int candidateCount[MAX_TYPES];

static int requestedIdx[MAX_TYPES];    // distinct structure type indices
static int requestedCounts[MAX_TYPES]; // how many instances wanted of each
static int numRequested;               // number of DISTINCT types

static int slotType[MAX_SLOTS]; // slot -> index into requestedIdx/requestedCounts
static int numSlots;            // total instances requested (sum of counts)

typedef struct {
    double spread;
    Point points[MAX_SLOTS];
} ComboResult;
static ComboResult top5[TOP_N];
static int top5Count = 0;

static void tryInsertTop5(double spread, Point *points, int n) {
    if (top5Count < TOP_N) {
        top5[top5Count].spread = spread;
        memcpy(top5[top5Count].points, points, n * sizeof(Point));
        top5Count++;
        for (int i = top5Count - 1; i > 0 && top5[i].spread < top5[i-1].spread; i--) {
            ComboResult tmp = top5[i]; top5[i] = top5[i-1]; top5[i-1] = tmp;
        }
    } else if (spread < top5[TOP_N - 1].spread) {
        top5[TOP_N - 1].spread = spread;
        memcpy(top5[TOP_N - 1].points, points, n * sizeof(Point));
        for (int i = TOP_N - 1; i > 0 && top5[i].spread < top5[i-1].spread; i--) {
            ComboResult tmp = top5[i]; top5[i] = top5[i-1]; top5[i-1] = tmp;
        }
    }
}

static double maxPairwiseDist(Point *pts, int n) {
    double best = 0;
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            double dx = pts[i].x - pts[j].x;
            double dz = pts[i].z - pts[j].z;
            double d = sqrt(dx*dx + dz*dz);
            if (d > best) best = d;
        }
    }
    return best;
}

static Point currentCombo[MAX_SLOTS];
// Tracks which candidate indices are already used for each DISTINCT type
// within the combination currently being built, so multiple slots
// requesting the same type never pick the identical physical structure twice.
static int usedCandidate[MAX_TYPES][MAX_CANDIDATES];

static void enumerate(int depth) {
    if (depth == numSlots) {
        double spread = numSlots == 1 ? 0 : maxPairwiseDist(currentCombo, numSlots);
        tryInsertTop5(spread, currentCombo, numSlots);
        return;
    }
    int t = slotType[depth];
    for (int i = 0; i < candidateCount[t]; i++) {
        if (usedCandidate[t][i]) continue;
        usedCandidate[t][i] = 1;
        currentCombo[depth] = candidates[t][i];
        enumerate(depth + 1);
        usedCandidate[t][i] = 0;
    }
}

static void scanCells(Generator *g, StructureEntry *entry, int mc, int64_t seed,
                       int origX, int origZ, int rawIdx,
                       int rzFrom, int rzTo, int rxFrom, int rxTo) {
    for (int rz = rzFrom; rz <= rzTo; rz++) {
        for (int rx = rxFrom; rx <= rxTo; rx++) {
            Pos p;
            if (!getBedrockStructurePos(entry->type, mc, (uint64_t)seed, rx, rz, &p)) continue;
            if (!isViableBedrockStructurePos(entry->type, g, p.x, p.z, 0)) continue;

            double dx = p.x - origX, dz = p.z - origZ;
            double dist = sqrt(dx*dx + dz*dz);

            int n = rawCount[rawIdx];
            if (n < g_K) {
                int j = n - 1;
                while (j >= 0 && rawCandidates[rawIdx][j].dist > dist) {
                    rawCandidates[rawIdx][j+1] = rawCandidates[rawIdx][j];
                    j--;
                }
                rawCandidates[rawIdx][j+1].x = p.x;
                rawCandidates[rawIdx][j+1].z = p.z;
                rawCandidates[rawIdx][j+1].dist = dist;
                rawCount[rawIdx]++;
            } else if (dist < rawCandidates[rawIdx][g_K - 1].dist) {
                int j = g_K - 2;
                while (j >= 0 && rawCandidates[rawIdx][j].dist > dist) {
                    rawCandidates[rawIdx][j+1] = rawCandidates[rawIdx][j];
                    j--;
                }
                rawCandidates[rawIdx][j+1].x = p.x;
                rawCandidates[rawIdx][j+1].z = p.z;
                rawCandidates[rawIdx][j+1].dist = dist;
            }
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <seed> <originX> <originZ> <comma,separated,types> [comma,separated,counts]\n", argv[0]);
        return 1;
    }

    int64_t seed = strtoll(argv[1], NULL, 10);
    int mc = MC_NEWEST; // Bedrock players are always on the latest version
    int originX = atoi(argv[2]);
    int originZ = atoi(argv[3]);

    char typesCopy[2048];
    strncpy(typesCopy, argv[4], sizeof(typesCopy) - 1);
    typesCopy[sizeof(typesCopy) - 1] = '\0';

    char countsCopy[512];
    int haveCounts = (argc >= 6);
    if (haveCounts) {
        strncpy(countsCopy, argv[5], sizeof(countsCopy) - 1);
        countsCopy[sizeof(countsCopy) - 1] = '\0';
    }

    char *typesSaveptr = NULL;
    char *countsSaveptr = NULL;
    char *token = strtok_r(typesCopy, ",", &typesSaveptr);
    char *countToken = haveCounts ? strtok_r(countsCopy, ",", &countsSaveptr) : NULL;
    char missing[MAX_TYPES][64];
    int numMissing = 0;
    int active[MAX_TYPES];

    while (token && numRequested < MAX_TYPES) {
        int idx = findStructureIndex(token);
        if (idx < 0) {
            fprintf(stderr, "Unknown structure type: %s\n", token);
            return 1;
        }
        requestedIdx[numRequested] = idx;
        int count = 1;
        if (countToken) {
            count = atoi(countToken);
            if (count < 1) count = 1;
            countToken = strtok_r(NULL, ",", &countsSaveptr);
        }
        requestedCounts[numRequested] = count;
        numRequested++;
        token = strtok_r(NULL, ",", &typesSaveptr);
    }

    if (numRequested == 0) {
        fprintf(stderr, "No structure types provided.\n");
        return 1;
    }

    for (int t = 0; t < numRequested; t++) {
        for (int c = 0; c < requestedCounts[t]; c++) {
            if (numSlots >= MAX_SLOTS) break;
            slotType[numSlots] = t;
            numSlots++;
        }
    }

    if (numSlots == 0) {
        fprintf(stderr, "No structure instances requested.\n");
        return 1;
    }

    g_K = (int)floor(pow(COMBO_BUDGET, 1.0 / numSlots));
    if (g_K < 1) g_K = 1;
    if (g_K > MAX_CANDIDATES) g_K = MAX_CANDIDATES;

    Generator g;
    setupGenerator(&g, mc, 0);
    int currentDim = 999;

    StructureConfig sconfs[MAX_TYPES];
    for (int t = 0; t < numRequested; t++) {
        StructureEntry *entry = &STRUCTURES[requestedIdx[t]];
        int supported = getBedrockStructureConfig(entry->type, mc, &sconfs[t]);
        active[t] = supported;
        rawCount[t] = 0;
        if (!supported) {
            strncpy(missing[numMissing], entry->name, 63);
            numMissing++;
        }
        if (requestedCounts[t] > MAX_CANDIDATES) {
            requestedCounts[t] = MAX_CANDIDATES;
        }
    }

    int radiusUsed = TIERS[0];
    int tiersUsed = 1;
    int metThreshold = 0;
    const double TIGHTNESS_THRESHOLD = 150.0;

    if (numMissing == 0) {
        for (int ti = 0; ti < NUM_TIERS; ti++) {
            for (int t = 0; t < numRequested; t++) {
                StructureEntry *entry = &STRUCTURES[requestedIdx[t]];
                StructureConfig *sconf = &sconfs[t];

                if (entry->dim != currentDim) {
                    applySeed(&g, entry->dim, (uint64_t)seed);
                    currentDim = entry->dim;
                }

                int regionBlocks = sconf->regionSize * 16;
                if (regionBlocks <= 0) regionBlocks = 512;

                int effectiveTierRadius = TIERS[ti];
                if ((entry->type == Mineshaft || entry->type == Treasure) && effectiveTierRadius > DENSE_STRUCTURE_MAX_RADIUS) {
                    effectiveTierRadius = DENSE_STRUCTURE_MAX_RADIUS;
                } else if ((entry->type == Monument || entry->type == End_City) && effectiveTierRadius > EXPENSIVE_STRUCTURE_MAX_RADIUS) {
                    effectiveTierRadius = EXPENSIVE_STRUCTURE_MAX_RADIUS;
                }
                int rNew = (int)ceil((double)effectiveTierRadius / regionBlocks);
                int originRegX = (int)floor((double)originX / regionBlocks);
                int originRegZ = (int)floor((double)originZ / regionBlocks);

                if (ti == 0) {
                    scanCells(&g, entry, mc, seed, originX, originZ, t,
                        originRegZ - rNew, originRegZ + rNew,
                        originRegX - rNew, originRegX + rNew);
                } else {
                    int effectivePrevRadius = TIERS[ti - 1];
                    if ((entry->type == Mineshaft || entry->type == Treasure) && effectivePrevRadius > DENSE_STRUCTURE_MAX_RADIUS) {
                        effectivePrevRadius = DENSE_STRUCTURE_MAX_RADIUS;
                    } else if ((entry->type == Monument || entry->type == End_City) && effectivePrevRadius > EXPENSIVE_STRUCTURE_MAX_RADIUS) {
                        effectivePrevRadius = EXPENSIVE_STRUCTURE_MAX_RADIUS;
                    }
                    int rPrev = (int)ceil((double)effectivePrevRadius / regionBlocks);
                    if (rNew <= rPrev) continue;

                    scanCells(&g, entry, mc, seed, originX, originZ, t,
                        originRegZ - rNew, originRegZ - rPrev - 1,
                        originRegX - rNew, originRegX + rNew);
                    scanCells(&g, entry, mc, seed, originX, originZ, t,
                        originRegZ + rPrev + 1, originRegZ + rNew,
                        originRegX - rNew, originRegX + rNew);
                    scanCells(&g, entry, mc, seed, originX, originZ, t,
                        originRegZ - rPrev, originRegZ + rPrev,
                        originRegX - rNew, originRegX - rPrev - 1);
                    scanCells(&g, entry, mc, seed, originX, originZ, t,
                        originRegZ - rPrev, originRegZ + rPrev,
                        originRegX + rPrev + 1, originRegX + rNew);
                }
            }

            radiusUsed = TIERS[ti];
            tiersUsed = ti + 1;

            int allFound = 1;
            for (int t = 0; t < numRequested; t++) {
                if (rawCount[t] < requestedCounts[t]) { allFound = 0; break; }
            }
            if (!allFound) continue;

            for (int t = 0; t < numRequested; t++) {
                candidateCount[t] = rawCount[t];
                for (int i = 0; i < rawCount[t]; i++) {
                    candidates[t][i].x = rawCandidates[t][i].x;
                    candidates[t][i].z = rawCandidates[t][i].z;
                }
            }
            memset(usedCandidate, 0, sizeof(usedCandidate));
            top5Count = 0;
            enumerate(0);

            if (top5Count > 0 && top5[0].spread <= TIGHTNESS_THRESHOLD) {
                metThreshold = 1;
                break;
            }
        }

        for (int t = 0; t < numRequested; t++) {
            if (rawCount[t] < requestedCounts[t]) {
                strncpy(missing[numMissing], STRUCTURES[requestedIdx[t]].name, 63);
                numMissing++;
            }
        }
    }

    printf("{\n");
    printf("  \"seed\": %" PRId64 ",\n", seed);
    printf("  \"origin\": {\"x\": %d, \"z\": %d},\n", originX, originZ);
    printf("  \"candidatePoolSize\": %d,\n", g_K);
    printf("  \"radiusUsed\": %d,\n", radiusUsed);
    printf("  \"tiersUsed\": %d,\n", tiersUsed);
    printf("  \"widened\": %s,\n", tiersUsed > 1 ? "true" : "false");
    printf("  \"metThreshold\": %s,\n", metThreshold ? "true" : "false");

    printf("  \"missing\": [");
    for (int i = 0; i < numMissing; i++) {
        printf("%s\"%s\"", i > 0 ? ", " : "", missing[i]);
    }
    printf("],\n");

    printf("  \"combinations\": [\n");
    if (numMissing == 0) {
        for (int i = 0; i < top5Count; i++) {
            printf("    {\n");
            printf("      \"spread\": %.1f,\n", top5[i].spread);
            printf("      \"structures\": {\n");
            int instanceNum[MAX_TYPES] = {0};
            for (int s = 0; s < numSlots; s++) {
                int t = slotType[s];
                const char *name = STRUCTURES[requestedIdx[t]].name;
                instanceNum[t]++;
                if (requestedCounts[t] > 1) {
                    printf("        \"%s_%d\": {\"x\": %d, \"z\": %d}%s\n",
                        name, instanceNum[t],
                        top5[i].points[s].x, top5[i].points[s].z,
                        s < numSlots - 1 ? "," : "");
                } else {
                    printf("        \"%s\": {\"x\": %d, \"z\": %d}%s\n",
                        name,
                        top5[i].points[s].x, top5[i].points[s].z,
                        s < numSlots - 1 ? "," : "");
                }
            }
            printf("      }\n");
            printf("    }%s\n", i < top5Count - 1 ? "," : "");
        }
    }
    printf("  ]\n");
    printf("}\n");

    return 0;
}