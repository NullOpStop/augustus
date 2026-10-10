#ifndef FRONTIER_WORLDGEN_STAGES_H
#define FRONTIER_WORLDGEN_STAGES_H

#include "frontier/worldgen/world.h"

/**
 * @file
 * Generation stages, run in this order by worldgen_generate.
 * Each stage seeds its own RNG stream from (seed, stage id).
 */

typedef enum {
    WORLDGEN_STAGE_SHAPE = 1,
    WORLDGEN_STAGE_WATER,
    WORLDGEN_STAGE_SURFACE,
    WORLDGEN_STAGE_RESOURCES,
    WORLDGEN_STAGE_SITES,
    WORLDGEN_STAGE_VARIANT,
    WORLDGEN_STAGE_RAMPS
} worldgen_stage;

/**
 * The engine allows at most one level of difference between orthogonal neighbours.
 * Like map_elevation_remove_cliffs, lowers tiles until that holds.
 */
void worldgen_enforce_single_steps(worldgen_world *world);

/**
 * Template, continuous elevation and terraced engine levels.
 */
void worldgen_generate_shape(worldgen_world *world);

/**
 * Sea, lakes and rivers from a drainage simulation, plus shallows along every shore.
 */
void worldgen_generate_water(worldgen_world *world);

/**
 * Forests, meadow, rock outcrops and marsh.
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
