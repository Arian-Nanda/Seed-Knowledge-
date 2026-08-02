// seedinfo.c
// Proof of concept: given a Minecraft seed, print spawn point and nearest village as JSON.
#include "finders.h"
#include "generator.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <seed>\n", argv[0]);
        return 1;
    }

    int64_t seed = strtoll(argv[1], NULL, 10);
    int mc = MC_1_21; // latest supported version

    Generator g;
    setupGenerator(&g, mc, 0);
    applySeed(&g, DIM_OVERWORLD, (uint64_t)seed);

    // Spawn point
    Pos spawn = getSpawn(&g);

    // Search nearby regions for the closest village.
    StructureConfig sconf;
    getStructureConfig(Village, mc, &sconf);

    int found = 0;
    Pos villagePos = {0, 0};
    double bestDist = -1;

    // Search a grid of regions around the origin (regions are ~32 chunks = 512 blocks wide by default,
    // exact size depends on sconf.regionSize).
    int searchRadius = 10; // regions in each direction
    for (int rz = -searchRadius; rz <= searchRadius; rz++)
    {
        for (int rx = -searchRadius; rx <= searchRadius; rx++)
        {
            Pos p;
            if (!getStructurePos(Village, mc, (uint64_t)seed, rx, rz, &p))
                continue;
            if (!isViableStructurePos(Village, &g, p.x, p.z, 0))
                continue;

            double dist = p.x * (double)p.x + p.z * (double)p.z;
            if (bestDist < 0 || dist < bestDist)
            {
                bestDist = dist;
                villagePos = p;
                found = 1;
            }
        }
    }

    printf("{\n");
    printf("  \"seed\": %" PRId64 ",\n", seed);
    printf("  \"version\": \"1.21\",\n");
    printf("  \"spawn\": {\"x\": %d, \"z\": %d},\n", spawn.x, spawn.z);
    if (found)
        printf("  \"nearestVillage\": {\"x\": %d, \"z\": %d}\n", villagePos.x, villagePos.z);
    else
        printf("  \"nearestVillage\": null\n");
    printf("}\n");

    return 0;
}