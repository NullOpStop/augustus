#ifndef FRONTIER_MAP_STATS_H
#define FRONTIER_MAP_STATS_H

/**
 * Developer tool: logs what the loaded map is made of, so generated maps can be compared with hand-made ones.
 * Shares of each terrain, how many tiles are cliffs, and how terrain clumps into patches.
 */
void frontier_map_stats_log(const char *name);

#endif // FRONTIER_MAP_STATS_H
