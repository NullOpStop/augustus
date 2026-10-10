#ifndef FRONTIER_WORLDGEN_STAGES_H
#define FRONTIER_WORLDGEN_STAGES_H

#include "frontier/worldgen/world.h"

/**
 * @file
 * Generation stages, run in this order by worldgen_generate.
 * Each stage seeds its own RNG stream from (seed, stage id).
 */

// The values seed each stage's random stream; changing one changes every world, so keep them fixed
typedef enum {
    WORLDGEN_STAGE_LAYOUT = 1,
    WORLDGEN_STAGE_SURFACE = 3,
    WORLDGEN_STAGE_RESOURCES = 4,
    WORLDGEN_STAGE_SITES = 5,
    WORLDGEN_STAGE_VARIANT = 6,
    WORLDGEN_STAGE_RAMPS = 7
} worldgen_stage;

/**
 * The engine allows at most one level of difference between orthogonal neighbours.
 * Like map_elevation_remove_cliffs, lowers tiles until that holds.
 */
void worldgen_enforce_single_steps(worldgen_world *world);

/**
 * The big picture: sea or island coast, rivers, lakes and ponds, and raised plateaus with smooth outlines.
 */
void worldgen_generate_layout(worldgen_world *world);

/**
 * Groves, scattered trees and shrubs, rock outcrops, floodplain meadows and marsh.
 */
void worldgen_generate_surface(worldgen_world *world);

/**
 * Access ramps joining every terrace to the land below it, where the terrain allows.
 */
void worldgen_generate_ramps(worldgen_world *world);

/**
 * Start site, land entry and exit on the map edge, and the road joining them through the start.
 */
void worldgen_generate_sites(worldgen_world *world);

#endif // FRONTIER_WORLDGEN_STAGES_H
