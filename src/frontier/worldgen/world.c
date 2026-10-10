#include "world.h"

#include <stdlib.h>
#include <string.h>

static const char *TEMPLATE_NAMES[WORLDGEN_TEMPLATE_MAX] = {
    "river_valley", "coastal", "lakes", "highlands", "island"
};

void worldgen_settings_default(worldgen_settings *settings)
{
    settings->seed = 1;
    settings->size = 160;
    settings->climate = 0;
    settings->template_type = WORLDGEN_TEMPLATE_RIVER_VALLEY;
    settings->water = 50;
    settings->relief = 50;
    settings->forest = 50;
    settings->richness = 50;
    settings->natives = 50;
}

const char *worldgen_template_name(worldgen_template template_type)
{
    return template_type >= 0 && template_type < WORLDGEN_TEMPLATE_MAX ? TEMPLATE_NAMES[template_type] : "unknown";
}

int worldgen_template_from_name(const char *name)
{
    for (int i = 0; i < WORLDGEN_TEMPLATE_MAX; i++) {
        if (strcmp(name, TEMPLATE_NAMES[i]) == 0) {
            return i;
        }
    }
    return -1;
}

worldgen_world *worldgen_world_create(const worldgen_settings *settings)
{
    if (settings->size < WORLDGEN_MIN_SIZE || settings->size > WORLDGEN_MAX_SIZE) {
        return 0;
    }
    worldgen_world *world = calloc(1, sizeof(worldgen_world));
    if (!world) {
        return 0;
    }
    world->width = settings->size;
    world->height = settings->size;
    world->settings = *settings;
    world->inflow.x = world->inflow.y = -1;
    world->river_entry.x = world->river_entry.y = -1;
    world->river_exit.x = world->river_exit.y = -1;
    world->start.x = world->start.y = -1;
    world->entry.x = world->entry.y = -1;
    world->exit.x = world->exit.y = -1;
    size_t tiles = (size_t) world->width * world->height;
    world->elevation = calloc(tiles, sizeof(double));
    world->level = calloc(tiles, sizeof(uint8_t));
    world->water = calloc(tiles, sizeof(uint8_t));
    world->surface = calloc(tiles, sizeof(uint8_t));
    world->deposit = calloc(tiles, sizeof(uint8_t));
    world->variant = calloc(tiles, sizeof(uint8_t));
    world->road = calloc(tiles, sizeof(uint8_t));
    world->ramp = calloc(tiles, sizeof(uint8_t));
    if (!world->elevation || !world->level || !world->water || !world->surface || !world->deposit ||
        !world->variant || !world->road || !world->ramp) {
        worldgen_world_free(world);
        return 0;
    }
    return world;
}

void worldgen_world_free(worldgen_world *world)
{
    if (!world) {
        return;
    }
    free(world->elevation);
    free(world->level);
    free(world->water);
    free(world->surface);
    free(world->deposit);
    free(world->variant);
    free(world->road);
    free(world->ramp);
    free(world);
}

int worldgen_world_index(const worldgen_world *world, int x, int y)
{
    return x + y * world->width;
}

int worldgen_world_contains(const worldgen_world *world, int x, int y)
{
    return x >= 0 && y >= 0 && x < world->width && y < world->height;
}

int worldgen_world_is_slope(const worldgen_world *world, int x, int y)
{
    int level = world->level[worldgen_world_index(world, x, y)];
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (worldgen_world_contains(world, x + dx, y + dy) &&
                world->level[worldgen_world_index(world, x + dx, y + dy)] < level) {
                return 1;
            }
        }
    }
    return 0;
}
