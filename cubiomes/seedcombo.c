// seedcombo.c
// Given a seed and a list of structure types, find the top 5 tightest
// clusters (combinations with one instance of each requested type,
// minimizing the maximum pairwise distance between them).
#include "finders.h"
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
};
#define NUM_STRUCTURES (int)(sizeof(STRUCTURES) / sizeof(STRUCTURES[0]))

#define MAX_TYPES 20
#define MAX_CANDIDATES 60
#define COMBO_BUDGET 3000000.0
#define TOP_N 5

// Maps a version string like "1.20" to the cubiomes MC_ constant.
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

typedef struct { int x, z; } Point;

static int findStructureIndex(const char *name) {
    for (int i = 0; i < NUM_STRUCTURES; i++)
        if (strcmp(STRUCTURES[i].name, name) == 0) return i;
    return -1;
}

static Point candidates[MAX_TYPES][MAX_CANDIDATES];
static int candidateCount[MAX_TYPES];
static int requestedIdx[MAX_TYPES];
static int numRequested;

typedef struct {
    double spread;
    Point points[MAX_TYPES];
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

static Point currentCombo[MAX_TYPES];

static void enumerate(int depth) {
    if (depth == numRequested) {
        double spread = numRequested == 1 ? 0 : maxPairwiseDist(currentCombo, numRequested);
        tryInsertTop5(spread, currentCombo, numRequested);
        return;
    }
    for (int i = 0; i < candidateCount[depth]; i++) {
        currentCombo[depth] = candidates[depth][i];
        enumerate(depth + 1);
    }
}

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "Usage: %s <seed> <version> <originX> <originZ> <comma,separated,types> [blockRadius]\n", argv[0]);
        return 1;
    }

    int64_t seed = strtoll(argv[1], NULL, 10);
    int mc = parseVersion(argv[2]);
    if (mc < 0) {
        fprintf(stderr, "Unknown version: %s\n", argv[2]);
        return 1;
    }
    int originX = atoi(argv[3]);
    int originZ = atoi(argv[4]);

    int desiredBlockRadius = 20000;
    if (argc >= 7) {
        desiredBlockRadius = atoi(argv[6]);
        if (desiredBlockRadius <= 0) desiredBlockRadius = 20000;
    }

    char typesCopy[2048];
    strncpy(typesCopy, argv[5], sizeof(typesCopy) - 1);
    typesCopy[sizeof(typesCopy) - 1] = '\0';

    char *token = strtok(typesCopy, ",");
    char missing[MAX_TYPES][64];
    int numMissing = 0;

    while (token && numRequested < MAX_TYPES) {
        int idx = findStructureIndex(token);
        if (idx < 0) {
            fprintf(stderr, "Unknown structure type: %s\n", token);
            return 1;
        }
        requestedIdx[numRequested] = idx;
        numRequested++;
        token = strtok(NULL, ",");
    }

    if (numRequested == 0) {
        fprintf(stderr, "No structure types provided.\n");
        return 1;
    }

    int K = (int)floor(pow(COMBO_BUDGET, 1.0 / numRequested));
    if (K < 1) K = 1;
    if (K > MAX_CANDIDATES) K = MAX_CANDIDATES;

    Generator g;
    setupGenerator(&g, mc, 0);

    int currentDim = 999;

    for (int t = 0; t < numRequested; t++) {
        StructureEntry *entry = &STRUCTURES[requestedIdx[t]];

        StructureConfig sconf;
        int supported = getStructureConfig(entry->type, mc, &sconf);
        if (!supported) {
            candidateCount[t] = 0;
            strncpy(missing[numMissing], entry->name, 63);
            numMissing++;
            continue;
        }

        if (entry->dim != currentDim) {
            applySeed(&g, entry->dim, (uint64_t)seed);
            currentDim = entry->dim;
        }
        int regionBlocks = sconf.regionSize * 16;
        if (regionBlocks <= 0) regionBlocks = 512;
        int searchRadius = (int)ceil((double)desiredBlockRadius / regionBlocks);

        int originRegX = (int)floor((double)originX / regionBlocks);
        int originRegZ = (int)floor((double)originZ / regionBlocks);

        Point found[400];
        double foundDist[400];
        int foundCount = 0;

        for (int rz = originRegZ - searchRadius; rz <= originRegZ + searchRadius; rz++) {
            for (int rx = originRegX - searchRadius; rx <= originRegX + searchRadius; rx++) {
                Pos p;
                if (!getStructurePos(entry->type, mc, (uint64_t)seed, rx, rz, &p)) continue;
                if (!isViableStructurePos(entry->type, &g, p.x, p.z, 0)) continue;
                if (foundCount < 400) {
                    found[foundCount].x = p.x;
                    found[foundCount].z = p.z;
                    double dx = p.x - originX, dz = p.z - originZ;
                    foundDist[foundCount] = sqrt(dx*dx + dz*dz);
                    foundCount++;
                }
            }
        }

        for (int i = 1; i < foundCount; i++) {
            Point pk = found[i]; double dk = foundDist[i];
            int j = i - 1;
            while (j >= 0 && foundDist[j] > dk) {
                found[j+1] = found[j]; foundDist[j+1] = foundDist[j]; j--;
            }
            found[j+1] = pk; foundDist[j+1] = dk;
        }

        int take = foundCount < K ? foundCount : K;
        candidateCount[t] = take;
        for (int i = 0; i < take; i++) candidates[t][i] = found[i];

        if (take == 0) {
            strncpy(missing[numMissing], entry->name, 63);
            numMissing++;
        }
    }

    printf("{\n");
    printf("  \"seed\": \"%" PRId64 "\",\n", seed);
    printf("  \"origin\": {\"x\": %d, \"z\": %d},\n", originX, originZ);
    printf("  \"candidatePoolSize\": %d,\n", K);

    printf("  \"missing\": [");
    for (int i = 0; i < numMissing; i++) {
        printf("%s\"%s\"", i > 0 ? ", " : "", missing[i]);
    }
    printf("],\n");

    printf("  \"combinations\": [\n");
    if (numMissing == 0) {
        enumerate(0);
        for (int i = 0; i < top5Count; i++) {
            printf("    {\n");
            printf("      \"spread\": %.1f,\n", top5[i].spread);
            printf("      \"structures\": {\n");
            for (int t = 0; t < numRequested; t++) {
                printf("        \"%s\": {\"x\": %d, \"z\": %d}%s\n",
                    STRUCTURES[requestedIdx[t]].name,
                    top5[i].points[t].x, top5[i].points[t].z,
                    t < numRequested - 1 ? "," : "");
            }
            printf("      }\n");
            printf("    }%s\n", i < top5Count - 1 ? "," : "");
        }
    }
    printf("  ]\n");
    printf("}\n");

    return 0;
}