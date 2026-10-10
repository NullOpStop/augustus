#ifndef FRONTIER_FRONTIER_GAME_H
#define FRONTIER_FRONTIER_GAME_H

#include "frontier/worldgen/world.h"

/**
 * Generates a world from the settings, saves it as a scenario and starts playing it in the city window.
 * @return 1 on success, 0 on failure (the reason is logged)
 */
int frontier_game_start(const worldgen_settings *settings);

#endif // FRONTIER_FRONTIER_GAME_H
