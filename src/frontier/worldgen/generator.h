#ifndef FRONTIER_WORLDGEN_GENERATOR_H
#define FRONTIER_WORLDGEN_GENERATOR_H

#include "frontier/worldgen/world.h"

/**
 * Generates a whole world from the settings. The same settings always produce the same world.
 * @return The world, to be freed with worldgen_world_free, or 0 on failure
 */
worldgen_world *worldgen_generate(const worldgen_settings *settings);

#endif // FRONTIER_WORLDGEN_GENERATOR_H
