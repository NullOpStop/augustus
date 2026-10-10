#include "frontier/worldgen/noise.h"
#include "frontier/worldgen/rng.h"
#include "frontier/worldgen/stages.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// What covers the land, placed as features like on hand-made maps: forests of natural shapes with ragged
// edges, lone trees and shrubs scattered over open grass, rock in a few clusters, yellow floodplain meadows
// along rivers and lakes, and the odd patch of marsh.

#define MAX_WATER_DISTANCE 32
#define FLOODPLAIN_WIDTH 6

static const int DIRECTIONS[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };

typedef struct {
    worldgen_world *world;
    worldgen_rng rng;
    worldgen_noise noise;
    double scale;
    uint8_t *water_distance;
} surface_context;

static double random_between(surface_context *c, double min, double max)
{
    return min + (max - min) * worldgen_rng_unit(&c->rng);
}

static void compute_water_distance(surface_context *c)
{
    worldgen_world *world = c->world;
    int tiles = world->width * world->height;
    int *queue = malloc(tiles * sizeof(int));
    if (!queue) {
        memset(c->water_distance, MAX_WATER_DISTANCE, tiles);
        return;
    }
    int head = 0;
    int tail = 0;
    for (int i = 0; i < tiles; i++) {
        if (world->water[i] != WORLDGEN_WATER_NONE) {
            c->water_distance[i] = 0;
            queue[tail++] = i;
        } else {
            c->water_distance[i] = MAX_WATER_DISTANCE;
        }
    }
    while (head < tail) {
        int index = queue[head++];
        if (c->water_distance[index] + 1 >= MAX_WATER_DISTANCE) {
            continue;
        }
        int x = index % world->width;
        int y = index / world->width;
        for (int d = 0; d < 4; d++) {
            int nx = x + DIRECTIONS[d][0];
            int ny = y + DIRECTIONS[d][1];
            if (!worldgen_world_contains(world, nx, ny)) {
                continue;
            }
            int neighbour = worldgen_world_index(world, nx, ny);
            if (c->water_distance[neighbour] > c->water_distance[index] + 1) {
                c->water_distance[neighbour] = (uint8_t) (c->water_distance[index] + 1);
                queue[tail++] = neighbour;
            }
        }
    }
    free(queue);
}

static int is_open(const worldgen_world *world, int x, int y)
{
    int index = worldgen_world_index(world, x, y);
    return world->water[index] == WORLDGEN_WATER_NONE && world->surface[index] == WORLDGEN_SURFACE_GRASS &&
        !worldgen_world_is_slope(world, x, y);
}

// Distance from a feature centre, as a share of its radius, with a smooth noisy outline
static double blob_distance(surface_context *c, int x, int y, double cx, double cy, double radius, int salt)
{
    double dx = x - cx;
    double dy = y - cy;
    double wobble = worldgen_noise_fbm(&c->noise, x / (radius * 0.9) + salt * 5.3, y / (radius * 0.9) - salt * 2.9,
        3, 2.0, 0.5);
    return sqrt(dx * dx + dy * dy) / radius - 0.3 * wobble;
}

static double forest_field(surface_context *c, int x, int y)
{
    const worldgen_world *world = c->world;
    // broad clusters and clearings, detailed edges, and a lean towards the map margins
    double field = worldgen_noise_fbm(&c->noise, x / 36.0, y / 36.0, 4, 2.0, 0.5) +
        0.6 * worldgen_noise_fbm(&c->noise, x / 90.0 + 200, y / 90.0 - 200, 2, 2.0, 0.5);
    double margin = world->width * 0.12;
    double to_edge = x < y ? x : y;
    if (world->width - 1 - x < to_edge) {
        to_edge = world->width - 1 - x;
    }
    if (world->height - 1 - y < to_edge) {
        to_edge = world->height - 1 - y;
    }
    if (to_edge < margin) {
        field += 0.25 * (1 - to_edge / margin);
    }
    return field;
}

// Forests from a noise field, cut at whatever level gives the wanted share of open land: natural shapes
// of every size, like the woods on hand-made maps, with ragged edges of shrubs and stray trees
static void place_forests(surface_context *c)
{
    worldgen_world *world = c->world;
    int tiles = world->width * world->height;
    double *field = malloc(tiles * sizeof(double));
    double *sorted = malloc(tiles * sizeof(double));
    if (!field || !sorted) {
        free(field);
        free(sorted);
        return;
    }
    int open = 0;
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            int index = worldgen_world_index(world, x, y);
            field[index] = -100;
            if (is_open(world, x, y)) {
                field[index] = forest_field(c, x, y);
                sorted[open++] = field[index];
            }
        }
    }
    if (!open) {
        free(field);
        free(sorted);
        return;
    }
    double share = 0.10 + world->settings.forest * 0.0012;
    // the cut-off is the value that share of open tiles exceed: pick it by partial selection
    int target = (int) (open * (1 - share));
    int low = 0;
    int high = open - 1;
    while (low < high) {
        double pivot = sorted[(low + high) / 2];
        int i = low;
        int j = high;
        while (i <= j) {
            while (sorted[i] < pivot) {
                i++;
            }
            while (sorted[j] > pivot) {
                j--;
            }
            if (i <= j) {
                double swap = sorted[i];
                sorted[i++] = sorted[j];
                sorted[j--] = swap;
            }
        }
        if (target <= j) {
            high = j;
        } else if (target >= i) {
            low = i;
        } else {
            break;
        }
    }
    double threshold = sorted[target];
    for (int i = 0; i < tiles; i++) {
        if (field[i] < -50) {
            continue;
        }
        double roll = worldgen_rng_unit(&c->rng);
        if (field[i] > threshold) {
            world->surface[i] = roll < 0.96 ? WORLDGEN_SURFACE_FOREST : WORLDGEN_SURFACE_SHRUB;
        } else if (field[i] > threshold - 0.07) {
            if (roll < 0.25) {
                world->surface[i] = WORLDGEN_SURFACE_FOREST;
            } else if (roll < 0.45) {
                world->surface[i] = WORLDGEN_SURFACE_SHRUB;
            }
        }
    }
    free(field);
    free(sorted);
}

static void scatter_lone_plants(surface_context *c)
{
    worldgen_world *world = c->world;
    double tree_chance = 0.012 + world->settings.forest * 0.0002;
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            if (!is_open(world, x, y)) {
                continue;
            }
            // lone trees gather in loose clumps; much of the open land stays clear
            double clumping = worldgen_noise_fbm(&c->noise, x / 18.0 + 40, y / 18.0 - 40, 2, 2.0, 0.5);
            if (clumping < 0.1) {
                worldgen_rng_unit(&c->rng);
                continue;
            }
            double roll = worldgen_rng_unit(&c->rng) / (clumping + 0.2);
            int index = worldgen_world_index(world, x, y);
            if (roll < tree_chance) {
                world->surface[index] = WORLDGEN_SURFACE_FOREST;
            } else if (roll < tree_chance * 2.5) {
                world->surface[index] = WORLDGEN_SURFACE_SHRUB;
            }
        }
    }
}

// Rock gathers in a few outcrops, mostly on the edges of raised ground
static void place_rock(surface_context *c)
{
    worldgen_world *world = c->world;
    int outcrops = (int) ((8 + world->settings.relief / 8.0) * sqrt(c->scale) + 0.5);
    int placed = 0;
    for (int attempt = 0; attempt < 500 && placed < outcrops; attempt++) {
        int x = worldgen_rng_between(&c->rng, 0, world->width);
        int y = worldgen_rng_between(&c->rng, 0, world->height);
        int index = worldgen_world_index(world, x, y);
        // prefer high ground; accept low ground now and then
        if (world->water[index] != WORLDGEN_WATER_NONE ||
            (world->level[index] == 0 && worldgen_rng_unit(&c->rng) < 0.75)) {
            continue;
        }
        double radius = random_between(c, 2.5, 5.0) * sqrt(c->scale);
        int salt = worldgen_rng_between(&c->rng, 0, 1000);
        int reach = (int) (radius * 1.5) + 1;
        for (int ry = y - reach; ry <= y + reach; ry++) {
            for (int rx = x - reach; rx <= x + reach; rx++) {
                if (!worldgen_world_contains(world, rx, ry) || worldgen_world_is_slope(world, rx, ry) ||
                    world->water[worldgen_world_index(world, rx, ry)] != WORLDGEN_WATER_NONE) {
                    continue;
                }
                double d = blob_distance(c, rx, ry, x, y, radius, salt);
                if (d < 0.8 || (d < 1.1 && worldgen_rng_unit(&c->rng) < 0.4)) {
                    world->surface[worldgen_world_index(world, rx, ry)] = WORLDGEN_SURFACE_ROCK;
                }
            }
        }
        placed++;
    }
}

// Yellow meadow on the low ground along rivers and lakes, in broad patches
static void place_floodplains(surface_context *c)
{
    worldgen_world *world = c->world;
    double width = FLOODPLAIN_WIDTH * sqrt(c->scale);
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            int index = worldgen_world_index(world, x, y);
            int distance = c->water_distance[index];
            if (distance == 0 || distance > width || world->level[index] != 0 || !is_open(world, x, y)) {
                continue;
            }
            double patch = worldgen_noise_fbm(&c->noise, x / 14.0, y / 14.0, 3, 2.0, 0.5);
            if (patch > -0.1 + 0.5 * distance / width) {
                world->surface[index] = WORLDGEN_SURFACE_MEADOW;
            }
        }
    }
}

static void place_marsh(surface_context *c)
{
    worldgen_world *world = c->world;
    int patches = world->settings.water >= 50 ? worldgen_rng_between(&c->rng, 0, 3) : 0;
    int placed = 0;
    for (int attempt = 0; attempt < 400 && placed < patches; attempt++) {
        int x = worldgen_rng_between(&c->rng, 0, world->width);
        int y = worldgen_rng_between(&c->rng, 0, world->height);
        int index = worldgen_world_index(world, x, y);
        if (c->water_distance[index] != 1 || world->level[index] != 0) {
            continue;
        }
        double radius = random_between(c, 2.0, 3.5);
        int salt = worldgen_rng_between(&c->rng, 0, 1000);
        for (int ry = y - 5; ry <= y + 5; ry++) {
            for (int rx = x - 5; rx <= x + 5; rx++) {
                if (worldgen_world_contains(world, rx, ry) && is_open(world, rx, ry) &&
                    world->level[worldgen_world_index(world, rx, ry)] == 0 &&
                    blob_distance(c, rx, ry, x, y, radius, salt) < 1) {
                    world->surface[worldgen_world_index(world, rx, ry)] = WORLDGEN_SURFACE_MARSH;
                }
            }
        }
        placed++;
    }
}

void worldgen_generate_surface(worldgen_world *world)
{
    surface_context c;
    c.world = world;
    worldgen_rng_init(&c.rng, world->settings.seed, WORLDGEN_STAGE_SURFACE);
    worldgen_noise_init(&c.noise, &c.rng);
    c.scale = world->width / 160.0;
    int tiles = world->width * world->height;
    c.water_distance = malloc(tiles);
    if (!c.water_distance) {
        return;
    }
    memset(world->surface, WORLDGEN_SURFACE_GRASS, tiles);
    compute_water_distance(&c);

    place_rock(&c);
    place_floodplains(&c);
    place_marsh(&c);
    place_forests(&c);
    scatter_lone_plants(&c);

    free(c.water_distance);
}
