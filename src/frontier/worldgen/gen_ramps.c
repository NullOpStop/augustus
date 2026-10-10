#include "frontier/worldgen/rng.h"
#include "frontier/worldgen/stages.h"

#include <stdlib.h>

// Access ramps joining every terrace to the land below it.
//
// The engine's ramp is a 2x2 block on a straight terrace edge, climbing in one direction:
//   row 0: flat tiles on the lower level      (part of the ramp)
//   row 1: the cliff tiles on the upper level (part of the ramp)
//   row 2: flat tiles on the upper level
// Without ramps, raised ground can never be reached, and players cannot build ramps themselves.

static const int DIRECTIONS[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };

typedef struct {
    int x;          // ramp row 0 tile; the second column is one step along the perpendicular
    int y;
    int direction;  // index into DIRECTIONS: the way up
    int lower_region;
    int upper_region;
} ramp_site;

static int is_open_land(const worldgen_world *world, int index)
{
    return world->water[index] == WORLDGEN_WATER_NONE && world->surface[index] != WORLDGEN_SURFACE_MARSH &&
        !world->road[index];
}

// Connected areas of land at one level
static int label_regions(const worldgen_world *world, int *region, int *queue)
{
    int tiles = world->width * world->height;
    int count = 0;
    for (int i = 0; i < tiles; i++) {
        region[i] = -1;
    }
    for (int start = 0; start < tiles; start++) {
        if (region[start] >= 0 || world->water[start] != WORLDGEN_WATER_NONE) {
            continue;
        }
        int id = count++;
        int head = 0;
        int tail = 0;
        queue[tail++] = start;
        region[start] = id;
        while (head < tail) {
            int x = queue[head] % world->width;
            int y = queue[head++] / world->width;
            for (int d = 0; d < 4; d++) {
                int nx = x + DIRECTIONS[d][0];
                int ny = y + DIRECTIONS[d][1];
                if (!worldgen_world_contains(world, nx, ny)) {
                    continue;
                }
                int neighbour = worldgen_world_index(world, nx, ny);
                if (region[neighbour] < 0 && world->water[neighbour] == WORLDGEN_WATER_NONE &&
                    world->level[neighbour] == world->level[start]) {
                    region[neighbour] = id;
                    queue[tail++] = neighbour;
                }
            }
        }
    }
    return count;
}

// Level at a position relative to a ramp: along the climb direction and across it. -1 outside the world.
static int level_at(const worldgen_world *world, const ramp_site *site, int along, int across, int *index)
{
    int dx = DIRECTIONS[site->direction][0];
    int dy = DIRECTIONS[site->direction][1];
    // perpendicular: rotate the climb direction a quarter turn
    int px = -dy;
    int py = dx;
    int x = site->x + along * dx + across * px;
    int y = site->y + along * dy + across * py;
    if (!worldgen_world_contains(world, x, y)) {
        return -1;
    }
    *index = worldgen_world_index(world, x, y);
    return world->level[*index];
}

// Checks the engine's ramp rules, plus a flat approach tile below so the road can reach it.
// Positions along the climb: -1 approach, 0 ramp lower row, 1 ramp cliff row, 2 flat top.
static int is_valid_site(const worldgen_world *world, const uint8_t *ramps, const int *region, ramp_site *site)
{
    int index;
    int lower = level_at(world, site, 0, 0, &index);
    if (lower < 0 || lower >= WORLDGEN_MAX_LEVEL) {
        return 0;
    }
    int upper = lower + 1;
    for (int along = -1; along <= 2; along++) {
        for (int across = 0; across <= 1; across++) {
            int level = level_at(world, site, along, across, &index);
            if (level != (along <= 0 ? lower : upper) || !is_open_land(world, index) || ramps[index]) {
                return 0;
            }
            int x = index % world->width;
            int y = index / world->width;
            // the engine needs one free tile around the ramp
            if (x < 1 || y < 1 || x >= world->width - 1 || y >= world->height - 1) {
                return 0;
            }
            if (along == 0) {
                site->lower_region = region[index];
            } else if (along == 2) {
                site->upper_region = region[index];
            }
        }
    }
    // approach, lower row and top row must be flat: nothing around them lower than their own level
    for (int along = -2; along <= 3; along++) {
        for (int across = -1; across <= 2; across++) {
            int minimum = along <= 0 ? lower : upper;
            int level = level_at(world, site, along, across, &index);
            if (level >= 0 && level < minimum) {
                return 0;
            }
        }
    }
    return 1;
}

static int find_root(int *parent, int region)
{
    while (parent[region] != region) {
        parent[region] = parent[parent[region]];
        region = parent[region];
    }
    return region;
}

static void reserve(const worldgen_world *world, uint8_t *ramps, const ramp_site *site)
{
    int index;
    for (int along = -1; along <= 2; along++) {
        for (int across = -1; across <= 2; across++) {
            if (level_at(world, site, along, across, &index) >= 0 && !ramps[index]) {
                ramps[index] = WORLDGEN_RAMP_RESERVED;
            }
        }
    }
    int origin_x = world->width;
    int origin_y = world->height;
    for (int along = 0; along <= 1; along++) {
        for (int across = 0; across <= 1; across++) {
            level_at(world, site, along, across, &index);
            ramps[index] = (uint8_t) ((along == 0 ? WORLDGEN_RAMP_LOWER : WORLDGEN_RAMP_UPPER) | site->direction);
            int x = index % world->width;
            int y = index / world->width;
            if (x < origin_x) {
                origin_x = x;
            }
            if (y < origin_y) {
                origin_y = y;
            }
        }
    }
    ramps[worldgen_world_index(world, origin_x, origin_y)] |= WORLDGEN_RAMP_ORIGIN;
}

void worldgen_generate_ramps(worldgen_world *world)
{
    worldgen_rng rng;
    worldgen_rng_init(&rng, world->settings.seed, WORLDGEN_STAGE_RAMPS);
    int tiles = world->width * world->height;
    int *region = malloc(tiles * sizeof(int));
    int *queue = malloc(tiles * sizeof(int));
    ramp_site *sites = malloc(tiles * sizeof(ramp_site));
    int *parent = malloc(tiles * sizeof(int));
    if (!region || !queue || !sites || !parent) {
        free(region);
        free(queue);
        free(sites);
        free(parent);
        return;
    }
    int region_count = label_regions(world, region, queue);
    for (int i = 0; i < region_count; i++) {
        parent[i] = i;
    }

    int site_count = 0;
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            for (int d = 0; d < 4; d++) {
                ramp_site site = { x, y, d, -1, -1 };
                if (is_valid_site(world, world->ramp, region, &site)) {
                    sites[site_count++] = site;
                }
            }
        }
    }
    // random order so ramps spread around rather than bunching at the top of the map
    for (int i = site_count - 1; i > 0; i--) {
        int j = worldgen_rng_between(&rng, 0, i + 1);
        ramp_site swap = sites[i];
        sites[i] = sites[j];
        sites[j] = swap;
    }
    // one ramp for every terrace that is not yet reachable from the ground below it
    for (int i = 0; i < site_count; i++) {
        ramp_site *site = &sites[i];
        int lower_root = find_root(parent, site->lower_region);
        int upper_root = find_root(parent, site->upper_region);
        if (lower_root == upper_root || !is_valid_site(world, world->ramp, region, site)) {
            continue;
        }
        parent[upper_root] = lower_root;
        reserve(world, world->ramp, site);
    }
    // keep only the ramps themselves
    for (int i = 0; i < tiles; i++) {
        if (world->ramp[i] == WORLDGEN_RAMP_RESERVED) {
            world->ramp[i] = 0;
        } else if (world->ramp[i]) {
            world->surface[i] = WORLDGEN_SURFACE_GRASS;
        }
    }
    free(region);
    free(queue);
    free(sites);
    free(parent);
}
