#include "frontier/worldgen/heap.h"
#include "frontier/worldgen/rng.h"
#include "frontier/worldgen/stages.h"

#include <stdlib.h>

// Start site, land entry and exit, and the road joining them

#define MAX_DISTANCE 255
#define SITE_STEP 3
#define SITE_RADIUS 7
#define RESOURCE_RADIUS 14
#define WATER_REACH 8

// road costs: stay in the valleys, keep clear of forest and rock, avoid cutting through hills
#define COST_GRASS 1.0
#define COST_MEADOW 1.5
#define COST_SHRUB 2.0
#define COST_FOREST 3.0
#define COST_ROCK 6.0
#define COST_RAMP 4.0
#define COST_TURN 1.5

static const int DIRECTIONS[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };

typedef struct {
    int *component;     // land region id per tile, 0 for water and marsh
    uint8_t *water_distance;
    int *queue;
    int component_count;
    int home_component; // the landmass the player starts on
    int use_coast;      // the home landmass never reaches the map edge: arrive by its coast instead
} site_data;

// Dry land that is not a cliff; the upper half of a ramp sits on the cliff but can be walked
static int is_walkable(const worldgen_world *world, int x, int y)
{
    int index = worldgen_world_index(world, x, y);
    return world->water[index] == WORLDGEN_WATER_NONE && world->surface[index] != WORLDGEN_SURFACE_MARSH &&
        ((world->ramp[index] & WORLDGEN_RAMP_UPPER) || !worldgen_world_is_slope(world, x, y));
}

// Moving between levels is only possible along a ramp, from its lower row to its upper row or back
static int can_step(const worldgen_world *world, int x, int y, int direction)
{
    int nx = x + DIRECTIONS[direction][0];
    int ny = y + DIRECTIONS[direction][1];
    if (!worldgen_world_contains(world, nx, ny) || !is_walkable(world, nx, ny)) {
        return 0;
    }
    int from = worldgen_world_index(world, x, y);
    int to = worldgen_world_index(world, nx, ny);
    if (world->level[from] == world->level[to]) {
        return 1;
    }
    int ramp_direction = world->ramp[from] & WORLDGEN_RAMP_DIRECTION_MASK;
    if ((world->ramp[from] & WORLDGEN_RAMP_LOWER) && (world->ramp[to] & WORLDGEN_RAMP_UPPER)) {
        return direction == ramp_direction;
    }
    if ((world->ramp[from] & WORLDGEN_RAMP_UPPER) && (world->ramp[to] & WORLDGEN_RAMP_LOWER)) {
        // directions come in opposite pairs: 0 and 1, 2 and 3
        return direction == (ramp_direction ^ 1);
    }
    return 0;
}

static int is_edge(const worldgen_world *world, int x, int y)
{
    return x == 0 || y == 0 || x == world->width - 1 || y == world->height - 1;
}

static int is_coast(const worldgen_world *world, int x, int y)
{
    for (int d = 0; d < 4; d++) {
        int nx = x + DIRECTIONS[d][0];
        int ny = y + DIRECTIONS[d][1];
        if (worldgen_world_contains(world, nx, ny) &&
            world->water[worldgen_world_index(world, nx, ny)] != WORLDGEN_WATER_NONE) {
            return 1;
        }
    }
    return 0;
}

// Where travellers arrive: the map edge, or the coast for a landmass that does not reach the edge
static int is_arrival_point(const worldgen_world *world, const site_data *data, int x, int y)
{
    int index = worldgen_world_index(world, x, y);
    if (data->component[index] != data->home_component || world->surface[index] == WORLDGEN_SURFACE_ROCK ||
        world->ramp[index]) {
        return 0;
    }
    return is_edge(world, x, y) || (data->use_coast && is_coast(world, x, y));
}

// Land regions joined by walkable ground and ramps
static void find_components(const worldgen_world *world, site_data *data)
{
    int tiles = world->width * world->height;
    int next_id = 0;
    for (int i = 0; i < tiles; i++) {
        data->component[i] = 0;
    }
    for (int start = 0; start < tiles; start++) {
        if (data->component[start] || !is_walkable(world, start % world->width, start / world->width)) {
            continue;
        }
        int id = ++next_id;
        int head = 0;
        int tail = 0;
        data->queue[tail++] = start;
        data->component[start] = id;
        while (head < tail) {
            int x = data->queue[head] % world->width;
            int y = data->queue[head++] / world->width;
            for (int d = 0; d < 4; d++) {
                if (!can_step(world, x, y, d)) {
                    continue;
                }
                int neighbour = worldgen_world_index(world, x + DIRECTIONS[d][0], y + DIRECTIONS[d][1]);
                if (!data->component[neighbour]) {
                    data->component[neighbour] = id;
                    data->queue[tail++] = neighbour;
                }
            }
        }
    }
    data->component_count = next_id;
}

static int farthest_edge_tile(const worldgen_world *world, const site_data *data, int component, int from,
    int *distance)
{
    int best = -1;
    *distance = -1;
    int from_x = from % world->width;
    int from_y = from / world->width;
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            if (!is_edge(world, x, y) || data->component[worldgen_world_index(world, x, y)] != component) {
                continue;
            }
            int d = abs(x - from_x) + abs(y - from_y);
            if (d > *distance) {
                *distance = d;
                best = worldgen_world_index(world, x, y);
            }
        }
    }
    return best;
}

// The home landmass is big and reaches the map edge at points far apart, so the road in and out is long.
// Without such a landmass, the biggest one is used and travellers land on its coast.
static void choose_home_component(const worldgen_world *world, site_data *data)
{
    int tiles = world->width * world->height;
    int *area = calloc(data->component_count + 1, sizeof(int));
    int *edge_tile = malloc((data->component_count + 1) * sizeof(int));
    if (!area || !edge_tile) {
        free(area);
        free(edge_tile);
        return;
    }
    for (int i = 0; i <= data->component_count; i++) {
        edge_tile[i] = -1;
    }
    for (int i = 0; i < tiles; i++) {
        int component = data->component[i];
        area[component]++;
        if (is_edge(world, i % world->width, i / world->width)) {
            edge_tile[component] = i;
        }
    }
    double best_score = -1;
    int largest = 0;
    int wanted_span = (world->width + world->height) / 2;
    for (int component = 1; component <= data->component_count; component++) {
        if (!largest || area[component] > area[largest]) {
            largest = component;
        }
        if (edge_tile[component] < 0) {
            continue;
        }
        // two sweeps: the tile farthest from any edge tile, then the tile farthest from that one
        int span;
        int far_tile = farthest_edge_tile(world, data, component, edge_tile[component], &span);
        farthest_edge_tile(world, data, component, far_tile, &span);
        double reach = span >= wanted_span ? 1.0 : (double) span / wanted_span;
        double score = area[component] * reach * reach;
        if (score > best_score) {
            best_score = score;
            data->home_component = component;
        }
    }
    // a landmass that barely touches the edge is worse than arriving by sea
    if (best_score < area[largest] * 0.25) {
        data->home_component = largest;
        data->use_coast = 1;
    }
    free(area);
    free(edge_tile);
}

static void compute_water_distance(const worldgen_world *world, site_data *data)
{
    int tiles = world->width * world->height;
    int head = 0;
    int tail = 0;
    for (int i = 0; i < tiles; i++) {
        if (world->water[i] != WORLDGEN_WATER_NONE) {
            data->water_distance[i] = 0;
            data->queue[tail++] = i;
        } else {
            data->water_distance[i] = MAX_DISTANCE;
        }
    }
    while (head < tail) {
        int index = data->queue[head++];
        int x = index % world->width;
        int y = index / world->width;
        for (int d = 0; d < 4; d++) {
            int nx = x + DIRECTIONS[d][0];
            int ny = y + DIRECTIONS[d][1];
            if (!worldgen_world_contains(world, nx, ny)) {
                continue;
            }
            int neighbour = worldgen_world_index(world, nx, ny);
            if (data->water_distance[neighbour] > data->water_distance[index] + 1 &&
                data->water_distance[index] + 1 < MAX_DISTANCE) {
                data->water_distance[neighbour] = (uint8_t) (data->water_distance[index] + 1);
                data->queue[tail++] = neighbour;
            }
        }
    }
}

static int has_surface_near(const worldgen_world *world, int x, int y, int radius, worldgen_surface surface)
{
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (worldgen_world_contains(world, x + dx, y + dy) &&
                world->surface[worldgen_world_index(world, x + dx, y + dy)] == surface) {
                return 1;
            }
        }
    }
    return 0;
}

// Share of the tiles around a site that are flat, dry and open enough to build on at the site's level
static double buildable_share(const worldgen_world *world, int x, int y, int level)
{
    int buildable = 0;
    int total = 0;
    for (int dy = -SITE_RADIUS; dy <= SITE_RADIUS; dy++) {
        for (int dx = -SITE_RADIUS; dx <= SITE_RADIUS; dx++) {
            if (dx * dx + dy * dy > SITE_RADIUS * SITE_RADIUS) {
                continue;
            }
            total++;
            if (!worldgen_world_contains(world, x + dx, y + dy)) {
                continue;
            }
            int index = worldgen_world_index(world, x + dx, y + dy);
            int surface = world->surface[index];
            if (world->water[index] == WORLDGEN_WATER_NONE && world->level[index] == level &&
                (surface == WORLDGEN_SURFACE_GRASS || surface == WORLDGEN_SURFACE_MEADOW) &&
                !worldgen_world_is_slope(world, x + dx, y + dy)) {
                buildable++;
            }
        }
    }
    return (double) buildable / total;
}

// Open ground on the main landmass with room to build, farmland, water, timber and stone nearby,
// kept away from the edges so the road in is long. Lower ground scores a little better.
static void find_start(worldgen_world *world, site_data *data, worldgen_rng *rng, int margin, double min_flat)
{
    double best_score = -1e9;
    for (int y = margin; y < world->height - margin; y += SITE_STEP) {
        for (int x = margin; x < world->width - margin; x += SITE_STEP) {
            int index = worldgen_world_index(world, x, y);
            if (data->component[index] != data->home_component || world->ramp[index]) {
                continue;
            }
            int level = world->level[index];
            double flat = buildable_share(world, x, y, level);
            if (flat < min_flat) {
                continue;
            }
            double from_centre_x = (double) x / world->width - 0.5;
            double from_centre_y = (double) y / world->height - 0.5;
            double score = flat * 4 +
                (data->water_distance[index] <= WATER_REACH ? 2 : 0) +
                (has_surface_near(world, x, y, RESOURCE_RADIUS, WORLDGEN_SURFACE_MEADOW) ? 2 : 0) +
                (has_surface_near(world, x, y, RESOURCE_RADIUS, WORLDGEN_SURFACE_FOREST) ? 1 : 0) +
                (has_surface_near(world, x, y, RESOURCE_RADIUS, WORLDGEN_SURFACE_ROCK) ? 1 : 0) -
                3 * (from_centre_x * from_centre_x + from_centre_y * from_centre_y) -
                0.3 * level +
                worldgen_rng_unit(rng) * 0.5;
            if (score > best_score) {
                best_score = score;
                world->start.x = x;
                world->start.y = y;
            }
        }
    }
}

static void choose_start(worldgen_world *world, site_data *data, worldgen_rng *rng)
{
    // relax the requirements until somewhere fits
    static const struct {
        int margin_divisor;
        double min_flat;
    } attempts[] = { {5, 0.5}, {5, 0.3}, {16, 0.3}, {40, 0.1} };
    for (int i = 0; i < (int) (sizeof(attempts) / sizeof(attempts[0])) && world->start.x < 0; i++) {
        find_start(world, data, rng, world->width / attempts[i].margin_divisor, attempts[i].min_flat);
    }
}

// Which side of the map a point is on; coastal arrival points use the nearest side
static int edge_side(const worldgen_world *world, int x, int y)
{
    int to_side[4] = { y, world->width - 1 - x, world->height - 1 - y, x };
    int side = 0;
    for (int i = 1; i < 4; i++) {
        if (to_side[i] < to_side[side]) {
            side = i;
        }
    }
    return side;
}

// Picks an arrival point on the home landmass, as far as possible from the given points.
// Points off avoid_side are preferred, so the road usually crosses the map.
static worldgen_point choose_edge_point(const worldgen_world *world, const site_data *data, worldgen_rng *rng,
    worldgen_point far_from_a, worldgen_point far_from_b, int avoid_side)
{
    worldgen_point best = { -1, -1 };
    double best_score = -1;
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            if (!is_arrival_point(world, data, x, y)) {
                continue;
            }
            double distance = abs(x - far_from_a.x) + abs(y - far_from_a.y);
            if (far_from_b.x >= 0) {
                distance += 0.5 * (abs(x - far_from_b.x) + abs(y - far_from_b.y));
            }
            double score = distance * (0.85 + 0.3 * worldgen_rng_unit(rng)) *
                (edge_side(world, x, y) == avoid_side ? 1.0 : 1.2);
            if (score > best_score) {
                best_score = score;
                best.x = x;
                best.y = y;
            }
        }
    }
    return best;
}

static double step_cost(const worldgen_world *world, int to)
{
    if (world->ramp[to]) {
        return COST_RAMP;
    }
    double cost;
    switch (world->surface[to]) {
        case WORLDGEN_SURFACE_MEADOW:
            cost = COST_MEADOW;
            break;
        case WORLDGEN_SURFACE_SHRUB:
            cost = COST_SHRUB;
            break;
        case WORLDGEN_SURFACE_FOREST:
            cost = COST_FOREST;
            break;
        case WORLDGEN_SURFACE_ROCK:
            cost = COST_ROCK;
            break;
        default:
            cost = COST_GRASS;
            break;
    }
    return cost;
}

// A* over walkable land; marks the found path in world->road. Ramps already act as road in the engine.
// Search states are (tile, direction of arrival) so turns can cost extra: Roman roads run straight,
// rather than in the staircase a plain grid search gives on diagonals.
static int lay_road(worldgen_world *world, worldgen_point from, worldgen_point to)
{
    int tiles = world->width * world->height;
    int states = tiles * 4;
    double *cost = malloc(states * sizeof(double));
    int *previous = malloc(states * sizeof(int));
    worldgen_heap heap;
    // a state can be pushed again each time a cheaper route to it is found
    heap.items = malloc(states * 4 * sizeof(worldgen_heap_item));
    heap.size = 0;
    int found = -1;
    if (cost && previous && heap.items) {
        for (int i = 0; i < states; i++) {
            cost[i] = -1;
            previous[i] = -1;
        }
        int start = worldgen_world_index(world, from.x, from.y);
        int goal = worldgen_world_index(world, to.x, to.y);
        for (int d = 0; d < 4; d++) {
            cost[start * 4 + d] = 0;
            worldgen_heap_push(&heap, 0, start * 4 + d);
        }
        while (heap.size > 0 && heap.size < states * 4 - 4) {
            worldgen_heap_item item = worldgen_heap_pop(&heap);
            int state = item.index;
            int index = state / 4;
            int arrived = state % 4;
            if (index == goal) {
                found = state;
                break;
            }
            int x = index % world->width;
            int y = index / world->width;
            for (int d = 0; d < 4; d++) {
                if (!can_step(world, x, y, d)) {
                    continue;
                }
                int nx = x + DIRECTIONS[d][0];
                int ny = y + DIRECTIONS[d][1];
                int next = worldgen_world_index(world, nx, ny) * 4 + d;
                double new_cost = cost[state] + step_cost(world, next / 4) + (d != arrived ? COST_TURN : 0);
                if (cost[next] < 0 || new_cost < cost[next]) {
                    cost[next] = new_cost;
                    previous[next] = state;
                    double estimate = abs(nx - to.x) + abs(ny - to.y);
                    worldgen_heap_push(&heap, new_cost + estimate * COST_GRASS, next);
                }
            }
        }
        for (int state = found; state >= 0; state = previous[state]) {
            int index = state / 4;
            if (!world->ramp[index]) {
                world->road[index] = 1;
                world->surface[index] = WORLDGEN_SURFACE_GRASS;
            }
        }
    }
    free(cost);
    free(previous);
    free(heap.items);
    return found >= 0;
}

void worldgen_generate_sites(worldgen_world *world)
{
    worldgen_rng rng;
    worldgen_rng_init(&rng, world->settings.seed, WORLDGEN_STAGE_SITES);
    int tiles = world->width * world->height;
    site_data data;
    data.component = malloc(tiles * sizeof(int));
    data.water_distance = malloc(tiles);
    data.queue = malloc(tiles * sizeof(int));
    data.home_component = 0;
    data.use_coast = 0;
    if (data.component && data.water_distance && data.queue) {
        find_components(world, &data);
        choose_home_component(world, &data);
        compute_water_distance(world, &data);
        choose_start(world, &data, &rng);
        if (world->start.x >= 0) {
            worldgen_point none = { -1, -1 };
            world->entry = choose_edge_point(world, &data, &rng, world->start, none, -1);
            if (world->entry.x >= 0) {
                world->exit = choose_edge_point(world, &data, &rng, world->entry, world->start,
                    edge_side(world, world->entry.x, world->entry.y));
            }
            if (world->entry.x >= 0 && world->exit.x >= 0) {
                lay_road(world, world->entry, world->start);
                lay_road(world, world->start, world->exit);
            }
        }
    }
    free(data.component);
    free(data.water_distance);
    free(data.queue);
}
