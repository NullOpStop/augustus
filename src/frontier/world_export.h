#ifndef FRONTIER_WORLD_EXPORT_H
#define FRONTIER_WORLD_EXPORT_H

#include "frontier/worldgen/world.h"

/**
 * @return Editor map size index (as used by game_file_editor_create_scenario) for a world size,
 *         or -1 when the engine cannot hold a world of that size yet
 */
int frontier_world_export_size_index(int size);

/**
 * Writes a generated world into the engine's map: terrain, elevation, entry and exit points and tile images.
 * Expects a blank editor map of the same size, created with game_file_editor_create_scenario.
 * @return 1 on success, 0 when the world does not fit the current map
 */
int frontier_world_export(const worldgen_world *world);

#endif // FRONTIER_WORLD_EXPORT_H
