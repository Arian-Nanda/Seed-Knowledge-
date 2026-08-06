// seedinfo_full_bedrock.c
// Bedrock Edition version of seedinfo_full. Bedrock players are always on
// the current version (no way to opt into old versions like Java allows),
// so this always uses the latest supported version internally - no version
// argument needed.
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

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <seed> [fromX] [fromZ]\n", argv[0]);
        return 1;
    }

    int64_t seed = strtoll(argv[1], NULL, 10);
    int mc = MC_NEWEST; // Bedrock players are always on the latest version

    int fromX = 0;
    int fromZ = 0;
    if (argc >= 4)
    {
        fromX = atoi(argv[2]);
        fromZ = atoi(argv[3]);
    }

    Generator g;
    setupGenerator(&g, mc, 0);

    applySeed(&g, DIM_OVERWORLD, (uint64_t)seed);
    Pos spawn = getSpawn(&g);

    printf("{\n");
    printf("  \"seed\": %" PRId64 ",\n", seed);
    printf("  \"platform\": \"bedrock\",\n");
    printf("  \"spawn\": {\"x\": %d, \"z\": %d},\n", spawn.x, spawn.z);
    printf("  \"searchOrigin\": {\"x\": %d, \"z\": %d},\n", fromX, fromZ);
    printf("  \"structures\": {\n");

    int currentDim = 999;
    int desiredBlockRadius = 20000;

    for (int i = 0; i < NUM_STRUCTURES; i++)
    {
        StructureEntry *entry = &STRUCTURES[i];

        StructureConfig sconf;
        int supported = getBedrockStructureConfig(entry->type, mc, &sconf);

        printf("    \"%s\": ", entry->name);

        if (!supported)
        {
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

        // Mineshafts and Treasure are extremely dense and Bedrock's per-cell
        // check is computationally expensive for them, so give them much
        // smaller search targets. Monument and End City aren't dense, but
        // each check is expensive - they still converge correctly at a
        // moderate radius. None of them need a large radius to find one nearby.
        int thisBlockRadius = desiredBlockRadius;
        if (entry->type == Mineshaft || entry->type == Treasure) {
            thisBlockRadius = 2000;
        } else if (entry->type == Monument || entry->type == End_City) {
            thisBlockRadius = 3000;
        }
        int searchRadius = (int)ceil((double)thisBlockRadius / regionBlocks);

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
                if (!getBedrockStructurePos(entry->type, mc, (uint64_t)seed, rx, rz, &p))
                    continue;
                if (!isViableBedrockStructurePos(entry->type, &g, p.x, p.z, 0))
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