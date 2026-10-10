#include "map_stats.h"

#include "core/log.h"
#include "map/elevation.h"
#include "map/grid.h"
#include "map/terrain.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    int terrain;
} terrain_class;

static const terrain_class CLASSES[] = {
    {"water", TERRAIN_WATER},
    {"tree", TERRAIN_TREE},
    {"shrub", TERRAIN_SHRUB},
    {"rock", TERRAIN_ROCK},
    {"meadow", TERRAIN_MEADOW},
    {"marsh", TERRAIN_MARSHLAND},
};

#define CLASS_COUNT ((int) (sizeof(CLASSES) / sizeof(CLASSES[0])))
#define SPECK_SIZE 4

static int tile_index(int x, int y, int width)
{
    return x + y * width;
}

static int is_class(int x, int y, int terrain)
{
    return map_terrain_is(map_grid_offset(x, y), terrain) && !map_terrain_is(map_grid_offset(x, y), TERRAIN_ROAD);
}

// Patches of one terrain: how many, their median size, and how much of the terrain sits in specks of
// SPECK_SIZE tiles or fewer. Hand-made maps have few, large patches; noise has many tiny ones.
static void patch_stats(int width, int height, int terrain, int *patches, int *median, double *speck_share,
    int *queue, uint8_t *seen, int *sizes)
{
    memset(seen, 0, (size_t) width * height);
    int count = 0;
    int total = 0;
    int speck_tiles = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (seen[tile_index(x, y, width)] || !is_class(x, y, terrain)) {
                continue;
            }
            int head = 0;
            int tail = 0;
            queue[tail++] = tile_index(x, y, width);
            seen[queue[0]] = 1;
            while (head < tail) {
                int cx = queue[head] % width;
                int cy = queue[head++] / width;
                static const int dirs[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
                for (int d = 0; d < 4; d++) {
                    int nx = cx + dirs[d][0];
                    int ny = cy + dirs[d][1];
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height || seen[tile_index(nx, ny, width)] ||
                        !is_class(nx, ny, terrain)) {
                        continue;
                    }
                    seen[tile_index(nx, ny, width)] = 1;
                    queue[tail++] = tile_index(nx, ny, width);
                }
            }
            sizes[count++] = tail;
            total += tail;
            if (tail <= SPECK_SIZE) {
                speck_tiles += tail;
            }
        }
    }
    // insertion sort is fine for the patch counts of a 160x160 map
    for (int i = 1; i < count; i++) {
        int value = sizes[i];
        int j = i - 1;
        while (j >= 0 && sizes[j] > value) {
            sizes[j + 1] = sizes[j];
            j--;
        }
        sizes[j + 1] = value;
    }
    *patches = count;
    *median = count ? sizes[count / 2] : 0;
    *speck_share = total ? 100.0 * speck_tiles / total : 0;
}

void frontier_map_stats_log(const char *name)
{
    int width = map_grid_width();
    int height = map_grid_height();
    int tiles = width * height;
    int *queue = malloc(tiles * sizeof(int));
    int *sizes = malloc(tiles * sizeof(int));
    uint8_t *seen = malloc(tiles);
    if (!queue || !sizes || !seen) {
        free(queue);
        free(sizes);
        free(seen);
        return;
    }
    char line[512];
    int levels[8] = { 0 };
    int cliffs = 0;
    int ramps = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int grid_offset = map_grid_offset(x, y);
            int level = map_elevation_at(grid_offset);
            levels[level < 8 ? level : 7]++;
            cliffs += map_terrain_is(grid_offset, TERRAIN_ELEVATION) != 0;
            ramps += map_terrain_is(grid_offset, TERRAIN_ACCESS_RAMP) != 0;
        }
    }
    snprintf(line, sizeof(line), "MAPSTATS %s size %dx%d | cliff tiles %.1f%% | ramp tiles %d | levels 0:%.0f%% 1:%.0f%% "
        "2:%.0f%% 3:%.0f%% 4+:%.0f%%", name, width, height, 100.0 * cliffs / tiles, ramps,
        100.0 * levels[0] / tiles, 100.0 * levels[1] / tiles, 100.0 * levels[2] / tiles, 100.0 * levels[3] / tiles,
        100.0 * (levels[4] + levels[5] + levels[6] + levels[7]) / tiles);
    log_info(line, 0, 0);
    for (int c = 0; c < CLASS_COUNT; c++) {
        int count = 0;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                count += is_class(x, y, CLASSES[c].terrain);
            }
        }
        int patches, median;
        double speck_share;
        patch_stats(width, height, CLASSES[c].terrain, &patches, &median, &speck_share, queue, seen, sizes);
        snprintf(line, sizeof(line), "MAPSTATS %s %-6s %5.1f%% of map | %4d patches | median patch %4d | "
            "in specks of <=%d: %5.1f%%", name, CLASSES[c].name, 100.0 * count / tiles, patches, median, SPECK_SIZE,
            speck_share);
        log_info(line, 0, 0);
    }
    free(queue);
    free(sizes);
    free(seen);
}
