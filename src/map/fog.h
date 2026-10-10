#ifndef MAP_FOG_H
#define MAP_FOG_H

#include "core/buffer.h"

/**
 * @file
 * Fog of war for frontier games: tiles stay hidden until the player's people or buildings come near.
 * When fog is disabled, which is the case for every other game and in the editor, every tile is explored.
 */

/**
 * Disables fog: every tile counts as explored
 */
void map_fog_clear(void);

/**
 * Enables fog with every tile hidden
 */
void map_fog_enable(void);

int map_fog_is_enabled(void);

int map_fog_is_explored(int grid_offset);

/**
 * @return 1 if every tile of the size x size area with its top-left corner at (x, y) is explored
 */
int map_fog_area_is_explored(int x, int y, int size);

void map_fog_reveal(int x, int y, int radius);

/**
 * Reveals the land around the player's people and buildings. Call once per game tick.
 */
void map_fog_update(void);

void map_fog_save_state(buffer *buf);

void map_fog_load_state(buffer *buf);

#endif // MAP_FOG_H
