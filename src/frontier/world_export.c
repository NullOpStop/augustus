#include "world_export.h"

#include "core/image.h"
#include "core/log.h"
#include "editor/tool_restriction.h"
#include "map/building_tiles.h"
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
    if (world->road[index]) {
        return TERRAIN_ROAD;
    }
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
    map_tiles_update_all_roads();
}

// Ramps need the cliff tiles the elevation pass marks, so they go in after it; the editor's own check
// confirms each one matches what the engine expects
static void place_ramps(const worldgen_world *world)
{
    int placed = 0;
    int rejected = 0;
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            if (!(world->ramp[worldgen_world_index(world, x, y)] & WORLDGEN_RAMP_ORIGIN)) {
                continue;
            }
            map_tile tile = { x, y, map_grid_offset(x, y) };
            int orientation;
            if (!editor_tool_can_place_access_ramp(&tile, &orientation)) {
                rejected++;
                continue;
            }
            map_building_tiles_add(0, x, y, 2, image_group(GROUP_TERRAIN_ACCESS_RAMP) + orientation,
                TERRAIN_ACCESS_RAMP);
            placed++;
        }
    }
    if (rejected) {
        log_error("Frontier: ramps rejected by the engine:", 0, rejected);
    }
    log_info("Frontier: ramps placed:", 0, placed);
}

static void update_images_after_ramps(void)
{
    map_image_context_reset_water();
    map_image_context_reset_elevation();
    map_tiles_update_all_elevation_editor();
    map_tiles_update_all_rocks();
    map_tiles_update_all_empty_land();
    map_tiles_update_all_meadow();
    map_tiles_update_all_water();
    map_tiles_update_all_marshland();
    map_tiles_update_all_roads();
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

    if (world->entry.x >= 0) {
        scenario_editor_set_entry_point(world->entry.x, world->entry.y);
    }
    if (world->exit.x >= 0) {
        scenario_editor_set_exit_point(world->exit.x, world->exit.y);
    }

    if (world->river_entry.x >= 0) {
        scenario_editor_set_river_entry_point(world->river_entry.x, world->river_entry.y);
    }
    if (world->river_exit.x >= 0) {
        scenario_editor_set_river_exit_point(world->river_exit.x, world->river_exit.y);
    }

    update_tile_images(world->width, world->height);
    place_ramps(world);
    update_images_after_ramps();
    return 1;
}
