#include "world_export.h"

#include "map/elevation.h"
#include "map/grid.h"
#include "map/image_context.h"
#include "map/random.h"
#include "map/terrain.h"
#include "map/tiles.h"
#include "scenario/editor_map.h"

// Must match MAP_SIZES in scenario/editor.c
static const int EDITOR_MAP_SIZES[] = { 40, 60, 80, 100, 120, 160 };

int frontier_world_export_size_index(int size)
{
    for (int i = 0; i < (int) (sizeof(EDITOR_MAP_SIZES) / sizeof(EDITOR_MAP_SIZES[0])); i++) {
        if (EDITOR_MAP_SIZES[i] == size) {
            return i;
        }
    }
    return -1;
}

static int terrain_for_tile(const worldgen_world *world, int index)
{
    switch (world->water[index]) {
        case WORLDGEN_WATER_DEEP:
            return TERRAIN_WATER;
        case WORLDGEN_WATER_SHALLOW:
            return TERRAIN_WATER | TERRAIN_SHALLOW_WATER;
        default:
            break;
    }
    switch (world->surface[index]) {
        case WORLDGEN_SURFACE_MEADOW:
            return TERRAIN_MEADOW;
        case WORLDGEN_SURFACE_SHRUB:
            return TERRAIN_SHRUB;
        case WORLDGEN_SURFACE_FOREST:
            return TERRAIN_TREE;
        case WORLDGEN_SURFACE_ROCK:
            return TERRAIN_ROCK;
        case WORLDGEN_SURFACE_MARSH:
            return TERRAIN_MARSHLAND;
        default:
            return 0;
    }
}

static int is_clear_land(const worldgen_world *world, int x, int y)
{
    int index = worldgen_world_index(world, x, y);
    return world->water[index] == WORLDGEN_WATER_NONE &&
        (world->surface[index] == WORLDGEN_SURFACE_GRASS || world->surface[index] == WORLDGEN_SURFACE_MEADOW);
}

// Placeholder until the sites stage exists: the clear land tile in column x closest to the middle
static int find_edge_point(const worldgen_world *world, int x, int *out_y)
{
    int middle = world->height / 2;
    for (int offset = 0; offset < world->height; offset++) {
        int y = middle + ((offset & 1) ? -(offset + 1) / 2 : offset / 2);
        if (worldgen_world_contains(world, x, y) && is_clear_land(world, x, y)) {
            *out_y = y;
            return 1;
        }
    }
    return 0;
}

static void update_tile_images(int width, int height)
{
    map_elevation_remove_cliffs();
    map_image_context_init();
    map_tiles_update_all_elevation_editor();
    map_tiles_update_all_water();
    map_tiles_update_all_marshland();
    map_tiles_update_all_rocks();
    map_tiles_update_all_empty_land();
    map_tiles_update_all_meadow();
    // trees and shrubs are not recalculated when a scenario loads, so set their images now
    map_tiles_update_region_trees(0, 0, width - 1, height - 1);
    map_tiles_update_region_shrub(0, 0, width - 1, height - 1);
}

int frontier_world_export(const worldgen_world *world)
{
    if (map_grid_width() != world->width || map_grid_height() != world->height) {
        return 0;
    }
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            int index = worldgen_world_index(world, x, y);
            int grid_offset = map_grid_offset(x, y);
            map_terrain_set(grid_offset, terrain_for_tile(world, index));
            map_elevation_set(grid_offset, world->level[index]);
            map_random_set(grid_offset, world->variant[index]);
        }
    }

    int edge_y;
    if (find_edge_point(world, 0, &edge_y)) {
        scenario_editor_set_entry_point(0, edge_y);
    }
    if (find_edge_point(world, world->width - 1, &edge_y)) {
        scenario_editor_set_exit_point(world->width - 1, edge_y);
    }

    if (world->river_entry.x >= 0) {
        scenario_editor_set_river_entry_point(world->river_entry.x, world->river_entry.y);
    }
    if (world->river_exit.x >= 0) {
        scenario_editor_set_river_exit_point(world->river_exit.x, world->river_exit.y);
    }

    update_tile_images(world->width, world->height);
    return 1;
}
