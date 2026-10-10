#include "frontier/worldgen/stages.h"

static int touches_land(const worldgen_world *world, int x, int y)
{
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int nx = x + dx;
            int ny = y + dy;
            if (worldgen_world_contains(world, nx, ny) &&
                world->water[worldgen_world_index(world, nx, ny)] == WORLDGEN_WATER_NONE) {
                return 1;
            }
        }
    }
    return 0;
}

void worldgen_generate_water(worldgen_world *world)
{
    int tiles = world->width * world->height;
    for (int i = 0; i < tiles; i++) {
        if (world->elevation[i] < world->sea_level) {
            world->water[i] = WORLDGEN_WATER_DEEP;
            world->level[i] = 0;
        }
    }
    worldgen_enforce_single_steps(world);
    // shallows along every shore; boats need deep water
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            int index = worldgen_world_index(world, x, y);
            if (world->water[index] == WORLDGEN_WATER_DEEP && touches_land(world, x, y)) {
                world->water[index] = WORLDGEN_WATER_SHALLOW;
            }
        }
    }
}
