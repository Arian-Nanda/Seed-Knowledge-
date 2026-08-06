// seedstronghold.c
// A dedicated, standalone Stronghold search - separate from the main
// seedinfo_full search because Stronghold's biome-based checking is
// significantly slower than other structures. This only runs when
// specifically requested, not as part of every regular seed search.
#include "finders.h"
#include "generator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>

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

    StrongholdIter sh;
    initFirstStronghold(&sh, mc, (uint64_t)seed);

    int found = 0;
    Pos bestPos = {0, 0};
    double bestDist = -1;
    int remaining;
    int checked = 0;
    // Minecraft generates a fixed total of ~129 strongholds. This cap is a
    // safety margin above that - the loop naturally stops once every
    // stronghold has been checked, regardless of how high this cap is set.
    const int MAX_STRONGHOLDS_TO_CHECK = 200;

    do {
        remaining = nextStronghold(&sh, &g);
        double dx = sh.pos.x - fromX;
        double dz = sh.pos.z - fromZ;
        double dist = dx * dx + dz * dz;
        if (bestDist < 0 || dist < bestDist)
        {
            bestDist = dist;
            bestPos = sh.pos;
            found = 1;
        }
        checked++;
    } while (remaining > 0 && checked < MAX_STRONGHOLDS_TO_CHECK);

    printf("{\n");
    printf("  \"seed\": %" PRId64 ",\n", seed);
    printf("  \"version\": \"%s\",\n", argv[2]);
    printf("  \"searchOrigin\": {\"x\": %d, \"z\": %d},\n", fromX, fromZ);
    printf("  \"strongholdsChecked\": %d,\n", checked);
    printf("  \"stronghold\": ");
    if (found)
    {
        double distance = sqrt(bestDist);
        printf("{\"x\": %d, \"z\": %d, \"distance\": %.1f}", bestPos.x, bestPos.z, distance);
    }
    else
    {
        printf("null");
    }
    printf("\n}\n");

    return 0;
}