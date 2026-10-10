#include "frontier/worldgen/noise.h"
#include "frontier/worldgen/rng.h"
#include "frontier/worldgen/stages.h"

#include <stdlib.h>
#include <string.h>

// First pass: forests, meadow valleys, rock outcrops and marsh from noise, water distance and height

#define MAX_WATER_DISTANCE 16
#define MEADOW_WATER_DISTANCE 7
#define MARSH_WATER_DISTANCE 3

static const int DIRECTIONS[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };

static void compute_water_distance(const worldgen_world *world, uint8_t *distance, int *queue)
{
    int tiles = world->width * world->height;
    int head = 0;
    int tail = 0;
    for (int i = 0; i < tiles; i++) {
        if (world->water[i] != WORLDGEN_WATER_NONE) {
            distance[i] = 0;
            queue[tail++] = i;
        } else {
            distance[i] = MAX_WATER_DISTANCE;
        }
    }
    while (head < tail) {
        int index = queue[head++];
        if (distance[index] + 1 >= MAX_WATER_DISTANCE) {
            continue;
        }
        int x = index % world->width;
        int y = index / world->width;
        for (int d = 0; d < 4; d++) {
            int nx = x + DIRECTIONS[d][0];
            int ny = y + DIRECTIONS[d][1];
            if (!worldgen_world_contains(world, nx, ny)) {
                continue;
            }
            int neighbour = worldgen_world_index(world, nx, ny);
            if (distance[neighbour] > distance[index] + 1) {
                distance[neighbour] = (uint8_t) (distance[index] + 1);
                queue[tail++] = neighbour;
            }
        }
    }
}

void worldgen_generate_surface(worldgen_world *world)
{
    worldgen_rng rng;
    worldgen_rng_init(&rng, world->settings.seed, WORLDGEN_STAGE_SURFACE);
    worldgen_noise moisture_noise;
    worldgen_noise rock_noise;
    worldgen_noise patch_noise;
    worldgen_noise_init(&moisture_noise, &rng);
    worldgen_noise_init(&rock_noise, &rng);
    worldgen_noise_init(&patch_noise, &rng);

    size_t tiles = (size_t) world->width * world->height;
    uint8_t *water_distance = malloc(tiles);
    int *queue = malloc(tiles * sizeof(int));
    if (!water_distance || !queue) {
        free(water_distance);
        free(queue);
        return;
    }
    compute_water_distance(world, water_distance, queue);

    // forest setting 0..100 moves the tree line: more forest means a lower threshold
    double forest_threshold = 0.45 - world->settings.forest * 0.006;

    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            int index = worldgen_world_index(world, x, y);
            // cliff tiles do not show trees or rocks in the engine, so leave them as grass
            if (world->water[index] != WORLDGEN_WATER_NONE || worldgen_world_is_slope(world, x, y)) {
                world->surface[index] = WORLDGEN_SURFACE_GRASS;
                continue;
            }
            int level = world->level[index];
            int distance = water_distance[index];
            double wetness = 1.0 - (double) distance / MAX_WATER_DISTANCE;
            double moisture = worldgen_noise_fbm(&moisture_noise, x / 40.0, y / 40.0, 4, 2.0, 0.5) + 0.35 * wetness;
            double rockiness = worldgen_noise_fbm(&rock_noise, x / 14.0, y / 14.0, 3, 2.0, 0.5) + 0.12 * level;
            double patch = worldgen_noise_fbm(&patch_noise, x / 22.0, y / 22.0, 3, 2.0, 0.5);

            worldgen_surface surface = WORLDGEN_SURFACE_GRASS;
            if (level == 0 && distance <= MARSH_WATER_DISTANCE && moisture > 0.55) {
                surface = WORLDGEN_SURFACE_MARSH;
            } else if (rockiness > 0.62) {
                surface = WORLDGEN_SURFACE_ROCK;
            } else if (level <= 1 && distance <= MEADOW_WATER_DISTANCE && patch > -0.15) {
                surface = WORLDGEN_SURFACE_MEADOW;
            } else if (moisture > forest_threshold) {
                surface = WORLDGEN_SURFACE_FOREST;
            } else if (moisture > forest_threshold - 0.12) {
                surface = WORLDGEN_SURFACE_SHRUB;
            } else if (level <= 1 && patch > 0.45) {
                surface = WORLDGEN_SURFACE_MEADOW;
            }
            world->surface[index] = (uint8_t) surface;
        }
    }
    free(water_distance);
    free(queue);
}
