// seedinfo_full.c
// Given a seed, find the nearest instance of every major structure type,
// across Overworld, Nether, and End.
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
    int dim; // DIM_OVERWORLD, DIM_NETHER, or DIM_END
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

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <seed> [fromX] [fromZ]\n", argv[0]);
        return 1;
    }

    int64_t seed = strtoll(argv[1], NULL, 10);
    int mc = MC_1_21;

    // Optional: search relative to a custom point instead of (0,0)/spawn.
    int fromX = 0;
    int fromZ = 0;
    if (argc >= 4)
    {
        fromX = atoi(argv[2]);
        fromZ = atoi(argv[3]);
    }

    Generator g;
    setupGenerator(&g, mc, 0);

    // Spawn point (Overworld only) - always reported regardless of search origin
    applySeed(&g, DIM_OVERWORLD, (uint64_t)seed);
    Pos spawn = getSpawn(&g);

    printf("{\n");
    printf("  \"seed\": %" PRId64 ",\n", seed);
    printf("  \"version\": \"1.21\",\n");
    printf("  \"spawn\": {\"x\": %d, \"z\": %d},\n", spawn.x, spawn.z);
    printf("  \"searchOrigin\": {\"x\": %d, \"z\": %d},\n", fromX, fromZ);
    printf("  \"structures\": {\n");

    int currentDim = 999; // force first applySeed
    int searchRadius = 10; // regions in each direction, centered on the search origin

    for (int i = 0; i < NUM_STRUCTURES; i++)
    {
        StructureEntry *entry = &STRUCTURES[i];

        if (entry->dim != currentDim)
        {
            applySeed(&g, entry->dim, (uint64_t)seed);
            currentDim = entry->dim;
        }

        StructureConfig sconf;
        getStructureConfig(entry->type, mc, &sconf);
        int regionBlocks = sconf.regionSize * 16; // blocks per region for this structure type
        if (regionBlocks <= 0) regionBlocks = 512; // fallback safety

        // Which region index contains our search origin, for this structure's region size
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

        printf("    \"%s\": ", entry->name);
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