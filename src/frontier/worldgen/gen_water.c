#include "frontier/worldgen/heap.h"
#include "frontier/worldgen/noise.h"
#include "frontier/worldgen/rng.h"
#include "frontier/worldgen/stages.h"

#include <stdlib.h>

// Sea, lakes and rivers from a drainage simulation.
//
// A priority flood from the outlets (sea and the downhill map edges) visits every tile in order of the lowest
// level water would need to reach to drain away. That gives, per tile:
// - the filled level: tiles well below it sit in a closed basin and become lakes
// - a downstream neighbour: following it leads to an outlet, so summing tiles upstream gives river flow

// On flat ground (lake beds, filled basins) every tile drains at the same level; a small coherent noise slope
// makes rivers meander there instead of running in straight lines
#define FLAT_SLOPE 1e-5
#define MEANDER_SCALE 10.0

static const int DIRECTIONS[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };

typedef struct {
    uint8_t *sea;
    double *filled;
    int *downstream;
    int *order;
    double *flow;
} drainage;

static int is_edge(const worldgen_world *world, int x, int y)
{
    return x == 0 || y == 0 || x == world->width - 1 || y == world->height - 1;
}

static int is_inflow(const worldgen_world *world, int x, int y)
{
    return x == world->inflow.x && y == world->inflow.y;
}

static int is_downstream_edge(const worldgen_world *world, int x, int y);

// Sea is low ground connected to the (downstream) map edge; other low ground becomes a lake instead
static void find_sea(const worldgen_world *world, uint8_t *sea, int *queue)
{
    int head = 0;
    int tail = 0;
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            int index = worldgen_world_index(world, x, y);
            sea[index] = 0;
            if (is_downstream_edge(world, x, y) && !is_inflow(world, x, y) &&
                world->elevation[index] < world->sea_level) {
                sea[index] = 1;
                queue[tail++] = index;
            }
        }
    }
    while (head < tail) {
        int x = queue[head] % world->width;
        int y = queue[head++] / world->width;
        for (int d = 0; d < 4; d++) {
            int nx = x + DIRECTIONS[d][0];
            int ny = y + DIRECTIONS[d][1];
            if (!worldgen_world_contains(world, nx, ny) || is_inflow(world, nx, ny)) {
                continue;
            }
            int neighbour = worldgen_world_index(world, nx, ny);
            if (!sea[neighbour] && world->elevation[neighbour] < world->sea_level) {
                sea[neighbour] = 1;
                queue[tail++] = neighbour;
            }
        }
    }
}

static int is_downstream_edge(const worldgen_world *world, int x, int y)
{
    if (!is_edge(world, x, y)) {
        return 0;
    }
    if (world->drain_x == 0 && world->drain_y == 0) {
        return 1;
    }
    double u = (x + 0.5) / world->width - 0.5;
    double v = (y + 0.5) / world->height - 0.5;
    return u * world->drain_x + v * world->drain_y > 0.25;
}

static int is_outlet(const worldgen_world *world, const uint8_t *sea, int x, int y)
{
    if (is_inflow(world, x, y)) {
        return 0;
    }
    if (sea[worldgen_world_index(world, x, y)]) {
        return 1;
    }
    return is_downstream_edge(world, x, y);
}

static int flood(const worldgen_world *world, const worldgen_noise *noise, drainage *result)
{
    int tiles = world->width * world->height;
    worldgen_heap heap;
    heap.items = malloc(tiles * sizeof(worldgen_heap_item));
    uint8_t *visited = calloc(tiles, 1);
    if (!heap.items || !visited) {
        free(heap.items);
        free(visited);
        return 0;
    }
    heap.size = 0;

    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            if (is_outlet(world, result->sea, x, y)) {
                int index = worldgen_world_index(world, x, y);
                visited[index] = 1;
                result->filled[index] = world->elevation[index];
                result->downstream[index] = -1;
                worldgen_heap_push(&heap, world->elevation[index], index);
            }
        }
    }
    int visited_count = 0;
    while (heap.size > 0) {
        worldgen_heap_item item = worldgen_heap_pop(&heap);
        result->order[visited_count++] = item.index;
        int x = item.index % world->width;
        int y = item.index / world->width;
        for (int d = 0; d < 4; d++) {
            int nx = x + DIRECTIONS[d][0];
            int ny = y + DIRECTIONS[d][1];
            if (!worldgen_world_contains(world, nx, ny)) {
                continue;
            }
            int neighbour = worldgen_world_index(world, nx, ny);
            if (visited[neighbour]) {
                continue;
            }
            visited[neighbour] = 1;
            double level = world->elevation[neighbour];
            if (level <= item.key) {
                double meander = worldgen_noise_fbm(noise, nx / MEANDER_SCALE, ny / MEANDER_SCALE, 2, 2.0, 0.5);
                level = item.key + FLAT_SLOPE * (1.5 + meander);
            }
            result->filled[neighbour] = level;
            result->downstream[neighbour] = item.index;
            worldgen_heap_push(&heap, result->filled[neighbour], neighbour);
        }
    }
    free(heap.items);
    free(visited);
    return 1;
}

static void accumulate_flow(const worldgen_world *world, drainage *result, double inflow)
{
    int tiles = world->width * world->height;
    for (int i = 0; i < tiles; i++) {
        result->flow[i] = 1;
    }
    if (world->inflow.x >= 0) {
        result->flow[worldgen_world_index(world, world->inflow.x, world->inflow.y)] += inflow;
    }
    // the flood order goes from outlets upstream, so walking it backwards adds each tile before its downstream
    for (int i = tiles - 1; i >= 0; i--) {
        int index = result->order[i];
        if (result->downstream[index] >= 0) {
            result->flow[result->downstream[index]] += result->flow[index];
        }
    }
}

static void stamp_water(worldgen_world *world, int x, int y, int radius)
{
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy <= radius * radius + radius && worldgen_world_contains(world, x + dx, y + dy)) {
                world->water[worldgen_world_index(world, x + dx, y + dy)] = WORLDGEN_WATER_DEEP;
            }
        }
    }
}

static int river_radius(int tiles, double flow)
{
    if (flow >= tiles * 0.25) {
        return 2;
    }
    if (flow >= tiles * 0.06) {
        return 1;
    }
    return 0;
}

// When the main river ends in a bay, continue the shipping route through the water to the nearest downstream edge
static int route_to_edge(worldgen_world *world, int start, int *queue)
{
    int tiles = world->width * world->height;
    int *previous = malloc(tiles * sizeof(int));
    if (!previous) {
        return -1;
    }
    for (int i = 0; i < tiles; i++) {
        previous[i] = -2;
    }
    int head = 0;
    int tail = 0;
    int edge = -1;
    queue[tail++] = start;
    previous[start] = -1;
    while (head < tail && edge < 0) {
        int index = queue[head++];
        int x = index % world->width;
        int y = index / world->width;
        if (is_downstream_edge(world, x, y) && !is_inflow(world, x, y)) {
            edge = index;
            break;
        }
        for (int d = 0; d < 4; d++) {
            int nx = x + DIRECTIONS[d][0];
            int ny = y + DIRECTIONS[d][1];
            if (!worldgen_world_contains(world, nx, ny)) {
                continue;
            }
            int neighbour = worldgen_world_index(world, nx, ny);
            if (previous[neighbour] == -2 && world->water[neighbour] != WORLDGEN_WATER_NONE) {
                previous[neighbour] = index;
                queue[tail++] = neighbour;
            }
        }
    }
    for (int index = edge; index >= 0; index = previous[index]) {
        stamp_water(world, index % world->width, index / world->width, 1);
    }
    free(previous);
    return edge;
}

static void place_rivers(worldgen_world *world, const drainage *result, int *scratch)
{
    int tiles = world->width * world->height;
    double threshold = tiles * (0.025 - (world->settings.water - 50) * 0.0002);
    for (int index = 0; index < tiles; index++) {
        if (result->flow[index] >= threshold) {
            stamp_water(world, index % world->width, index / world->width, river_radius(tiles, result->flow[index]));
        }
    }
    if (world->inflow.x < 0) {
        return;
    }
    // the river from the inflow is the trade route: keep it navigable (3+ wide) all the way to its outlet
    int index = worldgen_world_index(world, world->inflow.x, world->inflow.y);
    int last = index;
    while (index >= 0) {
        int radius = river_radius(tiles, result->flow[index]);
        stamp_water(world, index % world->width, index / world->width, radius < 1 ? 1 : radius);
        last = index;
        index = result->downstream[index];
    }
    world->river_entry = world->inflow;
    int last_x = last % world->width;
    int last_y = last / world->width;
    int distance_from_entry = abs(last_x - world->inflow.x) + abs(last_y - world->inflow.y);
    // any edge works as a shipping exit as long as it is not right next to the entry
    int exit = is_downstream_edge(world, last_x, last_y) ||
        (is_edge(world, last_x, last_y) && distance_from_entry > (world->width + world->height) / 4) ?
        last : route_to_edge(world, last, scratch);
    if (exit >= 0) {
        world->river_exit.x = exit % world->width;
        world->river_exit.y = exit / world->width;
    }
}

static int touches_land(const worldgen_world *world, int x, int y)
{
    for (int d = 0; d < 4; d++) {
        int nx = x + DIRECTIONS[d][0];
        int ny = y + DIRECTIONS[d][1];
        if (worldgen_world_contains(world, nx, ny) &&
            world->water[worldgen_world_index(world, nx, ny)] == WORLDGEN_WATER_NONE) {
            return 1;
        }
    }
    return 0;
}

void worldgen_generate_water(worldgen_world *world)
{
    worldgen_rng rng;
    worldgen_rng_init(&rng, world->settings.seed, WORLDGEN_STAGE_WATER);
    worldgen_noise noise;
    worldgen_noise_init(&noise, &rng);
    int tiles = world->width * world->height;

    drainage result;
    result.sea = malloc(tiles);
    result.filled = malloc(tiles * sizeof(double));
    result.downstream = malloc(tiles * sizeof(int));
    result.order = malloc(tiles * sizeof(int));
    result.flow = malloc(tiles * sizeof(double));
    if (result.sea && result.filled && result.downstream && result.order && result.flow) {
        find_sea(world, result.sea, result.order);
        if (flood(world, &noise, &result)) {
            accumulate_flow(world, &result, tiles * 0.08);

            double lake_depth = 0.035 - (world->settings.water - 50) * 0.0003;
            if (world->settings.template_type == WORLDGEN_TEMPLATE_LAKES) {
                lake_depth *= 0.4;
            }
            for (int i = 0; i < tiles; i++) {
                if (result.sea[i] || result.filled[i] - world->elevation[i] > lake_depth) {
                    world->water[i] = WORLDGEN_WATER_DEEP;
                }
            }
            // the flood order is no longer needed, so reuse it as a queue
            place_rivers(world, &result, result.order);
        }
    }
    free(result.sea);
    free(result.filled);
    free(result.downstream);
    free(result.order);
    free(result.flow);

    // water is always at the lowest level; lowering it cuts stepped banks and gorges into higher ground
    for (int i = 0; i < tiles; i++) {
        if (world->water[i] != WORLDGEN_WATER_NONE) {
            world->level[i] = 0;
        }
    }
    worldgen_enforce_single_steps(world);

    // shallows along every shore; boats need deep water
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            int index = worldgen_world_index(world, x, y);
            if (world->water[index] == WORLDGEN_WATER_DEEP && touches_land(world, x, y)) {
                world->water[index] = WORLDGEN_WATER_SHALLOW;
            }
        }
    }
}
