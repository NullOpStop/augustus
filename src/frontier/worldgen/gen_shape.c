#include "frontier/worldgen/noise.h"
#include "frontier/worldgen/rng.h"
#include "frontier/worldgen/stages.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846

// Size of the base terrain features, in tiles. Larger worlds get more features, not bigger ones.
#define FEATURE_SCALE 72.0
#define WARP_STRENGTH 18.0
#define WARP_FREQUENCY (1.0 / 60.0)

#define MAJORITY_RADIUS 2
#define MAJORITY_PASSES 2
#define MIN_TERRACE_AREA 30
#define CLEANUP_PASSES 3

typedef struct {
    double angle;
    double cos_angle;
    double sin_angle;
    double phase[2];
    double sea_level;
    int relief;
} template_params;

static double clamp(double value, double min, double max)
{
    return value < min ? min : value > max ? max : value;
}

static void init_template(const worldgen_world *world, worldgen_rng *rng, template_params *params)
{
    const worldgen_settings *settings = &world->settings;
    params->angle = worldgen_rng_unit(rng) * 2 * PI;
    params->cos_angle = cos(params->angle);
    params->sin_angle = sin(params->angle);
    params->phase[0] = worldgen_rng_unit(rng) * 2 * PI;
    params->phase[1] = worldgen_rng_unit(rng) * 2 * PI;
    params->relief = settings->relief;

    double base_sea_level;
    switch (settings->template_type) {
        case WORLDGEN_TEMPLATE_COASTAL:
            base_sea_level = 0.36;
            break;
        case WORLDGEN_TEMPLATE_LAKES:
            base_sea_level = 0.34;
            break;
        case WORLDGEN_TEMPLATE_HIGHLANDS:
            base_sea_level = 0.16;
            params->relief = settings->relief + 30;
            break;
        case WORLDGEN_TEMPLATE_ISLAND:
            base_sea_level = 0.36;
            break;
        case WORLDGEN_TEMPLATE_RIVER_VALLEY:
        default:
            base_sea_level = 0.16;
            break;
    }
    params->sea_level = clamp(base_sea_level + (settings->water - 50) * 0.002, 0.05, 0.6);
    if (params->relief > 100) {
        params->relief = 100;
    }
}

// Big-picture shape of the world, in normalized coordinates (0..1). Negative pulls land down.
static double template_bias(const worldgen_world *world, const template_params *params, double u, double v)
{
    double cu = u - 0.5;
    double cv = v - 0.5;
    double along = cu * params->cos_angle + cv * params->sin_angle;
    double across = -cu * params->sin_angle + cv * params->cos_angle;

    switch (world->settings.template_type) {
        case WORLDGEN_TEMPLATE_RIVER_VALLEY: {
            // a meandering valley crossing the world, with land rising away from it
            double centre = 0.12 * sin(along * 2 * PI * 1.3 + params->phase[0]) +
                0.05 * sin(along * 2 * PI * 3.1 + params->phase[1]);
            double distance = fabs(across - centre);
            return -0.6 * exp(-(distance / 0.1) * (distance / 0.1)) + 0.9 * distance - 0.1;
        }
        case WORLDGEN_TEMPLATE_COASTAL: {
            // sea on one side, rising inland
            double coastline = -0.18 + 0.04 * sin(across * 2 * PI * 1.7 + params->phase[0]);
            return clamp((along - coastline) * 1.8, -0.9, 0.5);
        }
        case WORLDGEN_TEMPLATE_HIGHLANDS:
            return 0.3 - 0.3 * (cu * cu + cv * cv);
        case WORLDGEN_TEMPLATE_ISLAND: {
            double radius = sqrt(cu * cu + cv * cv) * 2;
            return 0.45 - 1.2 * radius * radius;
        }
        case WORLDGEN_TEMPLATE_LAKES:
        default:
            return 0;
    }
}

static void generate_elevation(worldgen_world *world, const worldgen_noise *noise, const template_params *params)
{
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            double wx = x;
            double wy = y;
            worldgen_noise_warp(noise, &wx, &wy, WARP_STRENGTH, WARP_FREQUENCY);
            double detail = worldgen_noise_fbm(noise, wx / FEATURE_SCALE, wy / FEATURE_SCALE, 6, 2.0, 0.5);
            double bias = template_bias(world, params, (double) x / world->width, (double) y / world->height);
            world->elevation[worldgen_world_index(world, x, y)] = clamp(0.5 + 0.5 * (0.6 * detail + bias), 0, 1);
        }
    }
}

// Turns continuous elevation into terraces. Most land stays low; relief decides how high it climbs.
static void quantize_levels(worldgen_world *world, const template_params *params)
{
    double exponent = 2.2 - 1.4 * params->relief / 100.0;
    int tiles = world->width * world->height;
    for (int i = 0; i < tiles; i++) {
        double land = (world->elevation[i] - params->sea_level) / (1.0 - params->sea_level);
        if (land <= 0) {
            world->level[i] = 0;
            continue;
        }
        int level = (int) (pow(land, exponent) * (WORLDGEN_MAX_LEVEL + 1.2));
        world->level[i] = (uint8_t) (level > WORLDGEN_MAX_LEVEL ? WORLDGEN_MAX_LEVEL : level);
    }
}

// Replaces each level with the most common level around it, smoothing ragged terrace edges
static void majority_filter(worldgen_world *world, uint8_t *scratch)
{
    int counts[WORLDGEN_MAX_LEVEL + 1];
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            memset(counts, 0, sizeof(counts));
            for (int dy = -MAJORITY_RADIUS; dy <= MAJORITY_RADIUS; dy++) {
                for (int dx = -MAJORITY_RADIUS; dx <= MAJORITY_RADIUS; dx++) {
                    if (worldgen_world_contains(world, x + dx, y + dy)) {
                        counts[world->level[worldgen_world_index(world, x + dx, y + dy)]]++;
                    }
                }
            }
            int index = worldgen_world_index(world, x, y);
            int best = world->level[index];
            for (int level = 0; level <= WORLDGEN_MAX_LEVEL; level++) {
                if (counts[level] > counts[best]) {
                    best = level;
                }
            }
            scratch[index] = (uint8_t) best;
        }
    }
    memcpy(world->level, scratch, (size_t) world->width * world->height);
}

static const int DIRECTIONS[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };

// Merges terraces smaller than MIN_TERRACE_AREA into the most common neighbouring level
static void remove_small_terraces(worldgen_world *world, int *queue, uint8_t *visited)
{
    int tiles = world->width * world->height;
    memset(visited, 0, (size_t) tiles);
    for (int start = 0; start < tiles; start++) {
        if (visited[start]) {
            continue;
        }
        int level = world->level[start];
        int neighbour_counts[WORLDGEN_MAX_LEVEL + 1] = { 0 };
        int head = 0;
        int tail = 0;
        queue[tail++] = start;
        visited[start] = 1;
        while (head < tail) {
            int index = queue[head++];
            int x = index % world->width;
            int y = index / world->width;
            for (int d = 0; d < 4; d++) {
                int nx = x + DIRECTIONS[d][0];
                int ny = y + DIRECTIONS[d][1];
                if (!worldgen_world_contains(world, nx, ny)) {
                    continue;
                }
                int neighbour = worldgen_world_index(world, nx, ny);
                if (world->level[neighbour] != level) {
                    neighbour_counts[world->level[neighbour]]++;
                } else if (!visited[neighbour]) {
                    visited[neighbour] = 1;
                    queue[tail++] = neighbour;
                }
            }
        }
        if (tail >= MIN_TERRACE_AREA) {
            continue;
        }
        int replacement = -1;
        for (int candidate = 0; candidate <= WORLDGEN_MAX_LEVEL; candidate++) {
            if (neighbour_counts[candidate] &&
                (replacement < 0 || neighbour_counts[candidate] > neighbour_counts[replacement])) {
                replacement = candidate;
            }
        }
        if (replacement >= 0) {
            for (int i = 0; i < tail; i++) {
                world->level[queue[i]] = (uint8_t) replacement;
            }
        }
    }
}

void worldgen_enforce_single_steps(worldgen_world *world)
{
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int y = 0; y < world->height; y++) {
            for (int x = 0; x < world->width; x++) {
                int index = worldgen_world_index(world, x, y);
                for (int d = 0; d < 4; d++) {
                    int nx = x + DIRECTIONS[d][0];
                    int ny = y + DIRECTIONS[d][1];
                    if (!worldgen_world_contains(world, nx, ny)) {
                        continue;
                    }
                    int limit = world->level[worldgen_world_index(world, nx, ny)] + 1;
                    if (world->level[index] > limit) {
                        world->level[index] = (uint8_t) limit;
                        changed = 1;
                    }
                }
            }
        }
    }
}

void worldgen_generate_shape(worldgen_world *world)
{
    worldgen_rng rng;
    worldgen_rng_init(&rng, world->settings.seed, WORLDGEN_STAGE_SHAPE);
    worldgen_noise noise;
    worldgen_noise_init(&noise, &rng);
    template_params params;
    init_template(world, &rng, &params);
    world->sea_level = params.sea_level;

    generate_elevation(world, &noise, &params);
    quantize_levels(world, &params);

    size_t tiles = (size_t) world->width * world->height;
    uint8_t *scratch = malloc(tiles);
    int *queue = malloc(tiles * sizeof(int));
    if (scratch && queue) {
        for (int pass = 0; pass < MAJORITY_PASSES; pass++) {
            majority_filter(world, scratch);
        }
        for (int pass = 0; pass < CLEANUP_PASSES; pass++) {
            remove_small_terraces(world, queue, scratch);
        }
    }
    free(scratch);
    free(queue);
    worldgen_enforce_single_steps(world);
}
