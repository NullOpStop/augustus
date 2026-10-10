#ifndef FRONTIER_WORLDGEN_PREVIEW_H
#define FRONTIER_WORLDGEN_PREVIEW_H

#include "frontier/worldgen/world.h"

#include <stdint.h>

/**
 * @file
 * Map colours for world previews, shared by the in-game setup screen and the PNG preview tool.
 */

/**
 * @return Opaque ARGB colour for a tile: terrain blended over a height tint, cliffs shaded darker
 */
uint32_t worldgen_preview_color(const worldgen_world *world, int x, int y);

#endif // FRONTIER_WORLDGEN_PREVIEW_H
