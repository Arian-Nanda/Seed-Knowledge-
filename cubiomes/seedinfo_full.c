// seedinfo_full.c
// Given a seed and a Minecraft version, find the nearest instance of every
// major structure type, across Overworld, Nether, and End.
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

// Maps a version string like "1.20" to the cubiomes MC_ constant.
// Returns -1 if unrecognized.
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

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "Usage: %s <seed> <version> [fromX] [fromZ]\n", argv[0]);
        return 1;
    }

    int64_t seed = strtoll(argv[1], NULL, 10);
    int mc = parseVersion(argv[2]);
    if (mc < 0)
    {
        fprintf(stderr, "Unknown version: %s\n", argv[2]);
        return 1;
    }

    int fromX = 0;
    int fromZ = 0;
    if (argc >= 5)
    {
        fromX = atoi(argv[3]);
        fromZ = atoi(argv[4]);
    }

    Generator g;
    setupGenerator(&g, mc, 0);

    applySeed(&g, DIM_OVERWORLD, (uint64_t)seed);
    Pos spawn = getSpawn(&g);

    printf("{\n");
    printf("  \"seed\": \"%" PRId64 "\",\n", seed);
    printf("  \"version\": \"%s\",\n", argv[2]);
    printf("  \"spawn\": {\"x\": %d, \"z\": %d},\n", spawn.x, spawn.z);
    printf("  \"searchOrigin\": {\"x\": %d, \"z\": %d},\n", fromX, fromZ);
    printf("  \"structures\": {\n");

    int currentDim = 999;
    int desiredBlockRadius = 20000;

    for (int i = 0; i < NUM_STRUCTURES; i++)
    {
        StructureEntry *entry = &STRUCTURES[i];

        StructureConfig sconf;
        int supported = getStructureConfig(entry->type, mc, &sconf);

        printf("    \"%s\": ", entry->name);

        if (!supported)
        {
            // This structure type doesn't exist yet in the selected version.
            printf("null");
            if (i < NUM_STRUCTURES - 1) printf(",");
            printf("\n");
            continue;
        }

        if (entry->dim != currentDim)
        {
            applySeed(&g, entry->dim, (uint64_t)seed);
            currentDim = entry->dim;
        }

        int regionBlocks = sconf.regionSize * 16;
        if (regionBlocks <= 0) regionBlocks = 512;
        int searchRadius = (int)ceil((double)desiredBlockRadius / regionBlocks);

        int originRegX = (int)floor((double)fromX / regionBlocks);
        int originRegZ = (int)floor((double)fromZ / regionBlocks);

        int found = 0;
        Pos bestPos = {0, 0};
        double bestDist = -1;

        for (int rz = originRegZ - searchRadius; rz <= originRegZ + searchRadius; rz++)
        {
            for (int rx = originRegX - searchRadius; rx <= originRegX + searchRadius; rx++)
            {
                Pos p;
                if (!getStructurePos(entry->type, mc, (uint64_t)seed, rx, rz, &p))
                    continue;
                if (!isViableStructurePos(entry->type, &g, p.x, p.z, 0))
                    continue;

                double dx = p.x - fromX;
                double dz = p.z - fromZ;
                double dist = dx * dx + dz * dz;
                if (bestDist < 0 || dist < bestDist)
                {
                    bestDist = dist;
                    bestPos = p;
                    found = 1;
                }
            }
        }

        if (found)
        {
            double distance = sqrt(bestDist);
            printf("{\"x\": %d, \"z\": %d, \"distance\": %.1f}", bestPos.x, bestPos.z, distance);
        }
        else
            printf("null");

        if (i < NUM_STRUCTURES - 1)
            printf(",");
        printf("\n");
    }

    printf("  }\n");
    printf("}\n");

    return 0;
}