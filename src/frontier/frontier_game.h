#ifndef FRONTIER_FRONTIER_GAME_H
#define FRONTIER_FRONTIER_GAME_H

#include "frontier/worldgen/world.h"

/**
 * Generates a world from the settings, saves it as a scenario and starts playing it in the city window.
 * @return 1 on success, 0 on failure (the reason is logged)
 */
int frontier_game_start(const worldgen_settings *settings);

/**
 * @return Where the last started frontier world put the player's settlement, x < 0 if none
 */
worldgen_point frontier_game_start_point(void);

#endif // FRONTIER_FRONTIER_GAME_H
