// seedinfo_full.c
// Given a seed, find the nearest instance of every major structure type,
// across Overworld, Nether, and End.
#include "finders.h"
#include "generator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

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
        fprintf(stderr, "Usage: %s <seed>\n", argv[0]);
        return 1;
    }

    int64_t seed = strtoll(argv[1], NULL, 10);
    int mc = MC_1_21;

    Generator g;
    setupGenerator(&g, mc, 0);

    // Spawn point (Overworld only)
    applySeed(&g, DIM_OVERWORLD, (uint64_t)seed);
    Pos spawn = getSpawn(&g);

    printf("{\n");
    printf("  \"seed\": %" PRId64 ",\n", seed);
    printf("  \"version\": \"1.21\",\n");
    printf("  \"spawn\": {\"x\": %d, \"z\": %d},\n", spawn.x, spawn.z);
    printf("  \"structures\": {\n");

    int currentDim = 999; // force first applySeed
    int searchRadius = 10; // regions in each direction

    for (int i = 0; i < NUM_STRUCTURES; i++)
    {
        StructureEntry *entry = &STRUCTURES[i];

        if (entry->dim != currentDim)
        {
            applySeed(&g, entry->dim, (uint64_t)seed);
            currentDim = entry->dim;
        }

        int found = 0;
        Pos bestPos = {0, 0};
        double bestDist = -1;

        for (int rz = -searchRadius; rz <= searchRadius; rz++)
        {
            for (int rx = -searchRadius; rx <= searchRadius; rx++)
            {
                Pos p;
                if (!getStructurePos(entry->type, mc, (uint64_t)seed, rx, rz, &p))
                    continue;
                if (!isViableStructurePos(entry->type, &g, p.x, p.z, 0))
                    continue;

                double dist = p.x * (double)p.x + p.z * (double)p.z;
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
            printf("{\"x\": %d, \"z\": %d}", bestPos.x, bestPos.z);
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