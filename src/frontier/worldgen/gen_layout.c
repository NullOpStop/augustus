#include "frontier/worldgen/noise.h"
#include "frontier/worldgen/rng.h"
#include "frontier/worldgen/stages.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// The big picture of a world, composed from a few deliberate features rather than noise everywhere:
// sea or island coast, one main river with tributaries, lakes and ponds, and a handful of raised plateaus.
// Hand-made Caesar III maps are almost entirely flat open land with elevation used as landmarks;
// cliffs drawn everywhere look like scribbles, so raised ground is rare and has smooth outlines.

#define PI 3.14159265358979323846

#define MAX_RIVERS 4
#define MAX_PLATEAUS 12
#define SAMPLE_STEP 0.5
#define RIVER_CLEARANCE 6       // plateaus keep this far from water
#define PLATEAU_GAP 5
#define MIN_TERRACE_AREA 40

typedef struct {
    double x;
    double y;
    double radius;
} river_sample;

typedef struct {
    river_sample *samples;
    int count;
} river_path;

typedef struct {
    worldgen_world *world;
    worldgen_rng rng;
    worldgen_noise noise;
    double scale;               // world size relative to the largest classic map (160)
    uint8_t *corridor;          // 1 near water: plateaus stay out
    river_path rivers[MAX_RIVERS];
    int river_count;
} layout;

static const int DIRECTIONS[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };

static double clamp(double value, double min, double max)
{
    return value < min ? min : value > max ? max : value;
}

static double random_between(layout *l, double min, double max)
{
    return min + (max - min) * worldgen_rng_unit(&l->rng);
}

// A point on a map side (0 top, 1 right, 2 bottom, 3 left), at a fraction along it
static void edge_point(const worldgen_world *world, int side, double fraction, double *x, double *y)
{
    double along_x = fraction * (world->width - 1);
    double along_y = fraction * (world->height - 1);
    switch (side) {
        case 0: *x = along_x; *y = 0; break;
        case 1: *x = world->width - 1; *y = along_y; break;
        case 2: *x = along_x; *y = world->height - 1; break;
        default: *x = 0; *y = along_y; break;
    }
}

// Organic but smooth shape: a circle whose radius wobbles with low-frequency noise
static int in_blob(const layout *l, int x, int y, double cx, double cy, double radius, double roughness, int salt)
{
    double dx = x - cx;
    double dy = y - cy;
    double distance = sqrt(dx * dx + dy * dy);
    if (distance > radius * (1 + roughness)) {
        return 0;
    }
    double wobble = worldgen_noise_fbm(&l->noise, x / (radius * 1.1) + salt * 7.31, y / (radius * 1.1) - salt * 3.17,
        2, 2.0, 0.5);
    return distance < radius * (1 + roughness * wobble);
}

static void stamp_disc(layout *l, double cx, double cy, double radius, uint8_t water_value)
{
    worldgen_world *world = l->world;
    int min_x = (int) floor(cx - radius);
    int max_x = (int) ceil(cx + radius);
    int min_y = (int) floor(cy - radius);
    int max_y = (int) ceil(cy + radius);
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            if (!worldgen_world_contains(world, x, y)) {
                continue;
            }
            double dx = x - cx;
            double dy = y - cy;
            if (dx * dx + dy * dy <= radius * radius) {
                world->water[worldgen_world_index(world, x, y)] = water_value;
            }
        }
    }
}

// ---- sea and islands ----

static void place_coast(layout *l, int sea_side)
{
    worldgen_world *world = l->world;
    double phase = random_between(l, 0, 2 * PI);
    double depth = world->width * (0.14 + 0.06 * world->settings.water / 100.0);
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            double along = (sea_side == 0 || sea_side == 2) ? (double) x / world->width : (double) y / world->height;
            double from_edge = sea_side == 0 ? y : sea_side == 1 ? world->width - 1 - x :
                sea_side == 2 ? world->height - 1 - y : x;
            // broad bays and headlands, then gentle irregularity
            double coast = depth * (1 + 0.35 * sin(along * 2 * PI * 1.3 + phase)) +
                world->width * 0.03 * worldgen_noise_fbm(&l->noise, along * 5, phase, 3, 2.0, 0.5);
            if (from_edge < coast) {
                world->water[worldgen_world_index(world, x, y)] = WORLDGEN_WATER_DEEP;
            }
        }
    }
}

static void place_island(layout *l)
{
    worldgen_world *world = l->world;
    double cx = world->width * random_between(l, 0.45, 0.55);
    double cy = world->height * random_between(l, 0.45, 0.55);
    double radius = world->width * (0.40 - 0.08 * world->settings.water / 100.0);
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            if (!in_blob(l, x, y, cx, cy, radius, 0.3, 11)) {
                world->water[worldgen_world_index(world, x, y)] = WORLDGEN_WATER_DEEP;
            }
        }
    }
}

// ---- rivers ----

static int add_river(layout *l, double sx, double sy, double ex, double ey, double width_radius, double meander)
{
    if (l->river_count >= MAX_RIVERS) {
        return -1;
    }
    double dx = ex - sx;
    double dy = ey - sy;
    double length = sqrt(dx * dx + dy * dy);
    if (length < 1) {
        return -1;
    }
    double nx = -dy / length;
    double ny = dx / length;
    int count = (int) (length * 1.4 / SAMPLE_STEP) + 2;
    river_path *path = &l->rivers[l->river_count];
    path->samples = malloc(count * sizeof(river_sample));
    if (!path->samples) {
        return -1;
    }
    double amplitude = length * meander;
    double f1 = random_between(l, 1.0, 2.0);
    double f2 = random_between(l, 2.5, 4.0);
    double p1 = random_between(l, 0, 2 * PI);
    double p2 = random_between(l, 0, 2 * PI);
    double width_phase = random_between(l, 0, 100);
    path->count = 0;
    for (int i = 0; i < count; i++) {
        double t = (double) i / (count - 1);
        // meanders fade out at the ends so the river meets the edge squarely
        double envelope = pow(sin(PI * t), 0.6);
        double offset = envelope * amplitude * (sin(2 * PI * f1 * t + p1) + 0.35 * sin(2 * PI * f2 * t + p2) +
            0.3 * worldgen_noise_fbm(&l->noise, t * 3, width_phase, 2, 2.0, 0.5));
        river_sample *sample = &path->samples[path->count++];
        sample->x = sx + dx * t + nx * offset;
        sample->y = sy + dy * t + ny * offset;
        sample->radius = width_radius * (1 + 0.2 * worldgen_noise_fbm(&l->noise, t * 6, width_phase + 50, 2, 2.0, 0.5));
        // a meander can swing off the map; the river ends where it first leaves
        int outside = sample->x < 0 || sample->y < 0 || sample->x > l->world->width - 1 ||
            sample->y > l->world->height - 1;
        if (outside && i > 0) {
            break;
        }
    }
    return l->river_count++;
}

static void clamp_to_map(const worldgen_world *world, const river_sample *sample, worldgen_point *point)
{
    point->x = (int) round(clamp(sample->x, 0, world->width - 1));
    point->y = (int) round(clamp(sample->y, 0, world->height - 1));
}

// Splits a stretch of river into two channels around an island
static void add_island_fork(layout *l, river_path *path)
{
    int start = (int) (path->count * random_between(l, 0.3, 0.55));
    int end = start + (int) (path->count * random_between(l, 0.08, 0.14));
    if (end >= path->count - 1) {
        return;
    }
    double gap = random_between(l, 2.5, 4.5) * sqrt(l->scale);
    for (int i = start; i < end; i++) {
        river_sample *a = &path->samples[i];
        river_sample *b = &path->samples[i + 1];
        double dx = b->x - a->x;
        double dy = b->y - a->y;
        double length = sqrt(dx * dx + dy * dy);
        if (length <= 0) {
            continue;
        }
        double t = (double) (i - start) / (end - start);
        double spread = gap * sin(PI * t) + a->radius * 0.5;
        double channel = a->radius * 0.75 < 1.6 ? 1.6 : a->radius * 0.75;
        stamp_disc(l, a->x - dy / length * spread, a->y + dx / length * spread, channel, WORLDGEN_WATER_DEEP);
        stamp_disc(l, a->x + dy / length * spread, a->y - dx / length * spread, channel, WORLDGEN_WATER_DEEP);
        // mark the middle so the plain stamping leaves the island alone
        a->radius = -a->radius;
    }
}

static void stamp_rivers(layout *l)
{
    for (int r = 0; r < l->river_count; r++) {
        river_path *path = &l->rivers[r];
        for (int i = 0; i < path->count; i++) {
            if (path->samples[i].radius > 0) {
                stamp_disc(l, path->samples[i].x, path->samples[i].y, path->samples[i].radius, WORLDGEN_WATER_DEEP);
            }
        }
    }
}

static void place_lake_part(layout *l, double cx, double cy, double radius)
{
    worldgen_world *world = l->world;
    int salt = worldgen_rng_between(&l->rng, 0, 1000);
    for (int y = (int) (cy - radius * 1.4); y <= (int) (cy + radius * 1.4); y++) {
        for (int x = (int) (cx - radius * 1.4); x <= (int) (cx + radius * 1.4); x++) {
            if (worldgen_world_contains(world, x, y) && in_blob(l, x, y, cx, cy, radius, 0.35, salt)) {
                world->water[worldgen_world_index(world, x, y)] = WORLDGEN_WATER_DEEP;
            }
        }
    }
}

// A few overlapping parts give a lake bays and arms instead of a round outline
static void place_lake(layout *l, double cx, double cy, double radius)
{
    int parts = worldgen_rng_between(&l->rng, 2, 5);
    for (int p = 0; p < parts; p++) {
        double angle = random_between(l, 0, 2 * PI);
        double offset = p == 0 ? 0 : radius * random_between(l, 0.5, 0.9);
        place_lake_part(l, cx + cos(angle) * offset, cy + sin(angle) * offset,
            radius * (p == 0 ? 1.0 : random_between(l, 0.4, 0.7)));
    }
}

static int opposite_side(layout *l, int side)
{
    // mostly straight across, sometimes turning to a neighbouring side
    if (worldgen_rng_unit(&l->rng) < 0.7) {
        return (side + 2) % 4;
    }
    return (side + (worldgen_rng_unit(&l->rng) < 0.5 ? 1 : 3)) % 4;
}

static void place_rivers(layout *l, int sea_side)
{
    worldgen_world *world = l->world;
    worldgen_template type = world->settings.template_type;
    double main_radius = random_between(l, 2.6, 3.4) * sqrt(l->scale);
    double sx, sy, ex, ey;
    int main_river = -1;

    if (type == WORLDGEN_TEMPLATE_ISLAND) {
        // a short stream from the hills to the shore
        double angle = random_between(l, 0, 2 * PI);
        sx = world->width * 0.5 + cos(angle) * world->width * 0.08;
        sy = world->height * 0.5 + sin(angle) * world->height * 0.08;
        ex = world->width * 0.5 + cos(angle) * world->width * 0.5;
        ey = world->height * 0.5 + sin(angle) * world->height * 0.5;
        add_river(l, sx, sy, ex, ey, 1.0, 0.12);
        return;
    }
    int start_side = sea_side >= 0 ? (sea_side + 2) % 4 : worldgen_rng_between(&l->rng, 0, 4);
    int end_side = sea_side >= 0 ? sea_side : opposite_side(l, start_side);
    edge_point(world, start_side, random_between(l, 0.25, 0.75), &sx, &sy);
    edge_point(world, end_side, random_between(l, 0.25, 0.75), &ex, &ey);
    main_river = add_river(l, sx, sy, ex, ey, main_radius, random_between(l, 0.08, 0.14));
    if (main_river < 0) {
        return;
    }
    river_path *main_path = &l->rivers[main_river];
    if (sea_side < 0) {
        clamp_to_map(world, &main_path->samples[0], &world->river_entry);
        clamp_to_map(world, &main_path->samples[main_path->count - 1], &world->river_exit);
    }

    // tributaries join the main river from the sides
    int tributaries = (int) (world->settings.water / 40.0 + random_between(l, 0, 1.2));
    for (int i = 0; i < tributaries; i++) {
        river_sample *join = &main_path->samples[(int) (main_path->count * random_between(l, 0.25, 0.75))];
        int side = worldgen_rng_between(&l->rng, 0, 4);
        if (side == start_side || side == end_side) {
            side = (side + 1) % 4;
        }
        double tx, ty;
        edge_point(world, side, random_between(l, 0.2, 0.8), &tx, &ty);
        add_river(l, tx, ty, join->x, join->y, random_between(l, 1.6, 2.0) * sqrt(l->scale), random_between(l, 0.06, 0.12));
    }
    if (worldgen_rng_unit(&l->rng) < 0.6) {
        add_island_fork(l, main_path);
    }
    // lakes strung along the river; the lakes world gets several, others sometimes one
    int lakes = type == WORLDGEN_TEMPLATE_LAKES ? worldgen_rng_between(&l->rng, 2, 4) :
        (worldgen_rng_unit(&l->rng) < 0.25 + world->settings.water / 250.0 ? 1 : 0);
    for (int i = 0; i < lakes; i++) {
        river_sample *centre = &main_path->samples[(int) (main_path->count * random_between(l, 0.2, 0.8))];
        double radius = world->width * (type == WORLDGEN_TEMPLATE_LAKES ? random_between(l, 0.06, 0.10) :
            random_between(l, 0.04, 0.07));
        place_lake(l, centre->x, centre->y, radius);
    }
}

static void place_ponds(layout *l)
{
    worldgen_world *world = l->world;
    int ponds = (int) random_between(l, 0, 1 + world->settings.water / 40.0);
    for (int i = 0; i < ponds; i++) {
        double cx = random_between(l, 0.1, 0.9) * world->width;
        double cy = random_between(l, 0.1, 0.9) * world->height;
        place_lake_part(l, cx, cy, random_between(l, 3.0, 5.0) * sqrt(l->scale));
    }
}

// ---- raised ground ----

static void mark_corridor(layout *l)
{
    worldgen_world *world = l->world;
    int tiles = world->width * world->height;
    int *queue = malloc(tiles * sizeof(int));
    uint8_t *distance = malloc(tiles);
    if (!queue || !distance) {
        free(queue);
        free(distance);
        return;
    }
    int head = 0;
    int tail = 0;
    for (int i = 0; i < tiles; i++) {
        distance[i] = 255;
        if (world->water[i] != WORLDGEN_WATER_NONE) {
            distance[i] = 0;
            queue[tail++] = i;
        }
    }
    while (head < tail) {
        int index = queue[head++];
        if (distance[index] >= RIVER_CLEARANCE) {
            continue;
        }
        int x = index % world->width;
        int y = index / world->width;
        for (int d = 0; d < 4; d++) {
            int nx = x + DIRECTIONS[d][0];
            int ny = y + DIRECTIONS[d][1];
            if (worldgen_world_contains(world, nx, ny) && distance[worldgen_world_index(world, nx, ny)] == 255) {
                distance[worldgen_world_index(world, nx, ny)] = (uint8_t) (distance[index] + 1);
                queue[tail++] = worldgen_world_index(world, nx, ny);
            }
        }
    }
    for (int i = 0; i < tiles; i++) {
        l->corridor[i] = distance[i] <= RIVER_CLEARANCE;
    }
    free(queue);
    free(distance);
}

typedef struct {
    double x;
    double y;
    double radius;
} plateau;

static int fits(layout *l, const plateau *p, const plateau *placed, int placed_count)
{
    worldgen_world *world = l->world;
    for (int i = 0; i < placed_count; i++) {
        double dx = p->x - placed[i].x;
        double dy = p->y - placed[i].y;
        if (sqrt(dx * dx + dy * dy) < (p->radius + placed[i].radius) * 1.3 + PLATEAU_GAP) {
            return 0;
        }
    }
    int reach = (int) (p->radius * 1.35);
    for (int y = (int) p->y - reach; y <= (int) p->y + reach; y++) {
        for (int x = (int) p->x - reach; x <= (int) p->x + reach; x++) {
            if (worldgen_world_contains(world, x, y) && l->corridor[worldgen_world_index(world, x, y)]) {
                double dx = x - p->x;
                double dy = y - p->y;
                if (sqrt(dx * dx + dy * dy) < p->radius * 1.35) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

static void raise_blob(layout *l, double cx, double cy, double radius, int level, int salt)
{
    worldgen_world *world = l->world;
    int reach = (int) (radius * 1.4) + 1;
    for (int y = (int) cy - reach; y <= (int) cy + reach; y++) {
        for (int x = (int) cx - reach; x <= (int) cx + reach; x++) {
            if (!worldgen_world_contains(world, x, y)) {
                continue;
            }
            int index = worldgen_world_index(world, x, y);
            if (world->water[index] == WORLDGEN_WATER_NONE && !l->corridor[index] &&
                world->level[index] < level && in_blob(l, x, y, cx, cy, radius, 0.3, salt)) {
                world->level[index] = (uint8_t) level;
            }
        }
    }
}

static void place_plateaus(layout *l)
{
    worldgen_world *world = l->world;
    worldgen_template type = world->settings.template_type;
    int relief = world->settings.relief;
    int wanted = relief < 35 ? 1 : relief < 65 ? 3 : 5;
    if (type == WORLDGEN_TEMPLATE_HIGHLANDS) {
        wanted += 4;
    }
    wanted = (int) (wanted * sqrt(l->scale) + 0.5);
    if (wanted > MAX_PLATEAUS) {
        wanted = MAX_PLATEAUS;
    }
    plateau placed[MAX_PLATEAUS];
    int count = 0;
    for (int attempt = 0; attempt < 400 && count < wanted; attempt++) {
        plateau p;
        double size = type == WORLDGEN_TEMPLATE_HIGHLANDS ? random_between(l, 0.09, 0.15) : random_between(l, 0.07, 0.13);
        p.radius = world->width * size;
        // allowed to run off the map edge, like the edge mesas of hand-made maps
        p.x = random_between(l, -0.05, 1.05) * world->width;
        p.y = random_between(l, -0.05, 1.05) * world->height;
        if (type == WORLDGEN_TEMPLATE_ISLAND && count == 0) {
            p.x = world->width * 0.5;
            p.y = world->height * 0.5;
        }
        if (!fits(l, &p, placed, count)) {
            continue;
        }
        int salt = worldgen_rng_between(&l->rng, 0, 1000);
        raise_blob(l, p.x, p.y, p.radius, 1, salt);
        // larger plateaus may rise in steps; highlands climb higher
        if (relief >= 35 && p.radius > world->width * 0.075 && worldgen_rng_unit(&l->rng) < 0.6) {
            raise_blob(l, p.x + random_between(l, -0.15, 0.15) * p.radius,
                p.y + random_between(l, -0.15, 0.15) * p.radius, p.radius * 0.5, 2, salt + 1);
            if (type == WORLDGEN_TEMPLATE_HIGHLANDS && relief >= 65) {
                raise_blob(l, p.x, p.y, p.radius * 0.22, 3, salt + 2);
            }
        }
        placed[count++] = p;
    }
}

// ---- cleanup ----

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

// Smooths outlines of a byte grid: each cell becomes the most common value around it
static void majority_filter(const worldgen_world *world, uint8_t *values, uint8_t *scratch, int max_value)
{
    int counts[8];
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            memset(counts, 0, sizeof(counts));
            for (int dy = -2; dy <= 2; dy++) {
                for (int dx = -2; dx <= 2; dx++) {
                    if (worldgen_world_contains(world, x + dx, y + dy)) {
                        counts[values[worldgen_world_index(world, x + dx, y + dy)]]++;
                    }
                }
            }
            int index = worldgen_world_index(world, x, y);
            int best = values[index];
            for (int v = 0; v <= max_value; v++) {
                if (counts[v] > counts[best]) {
                    best = v;
                }
            }
            scratch[index] = (uint8_t) best;
        }
    }
    memcpy(values, scratch, (size_t) world->width * world->height);
}

// Merges patches smaller than min_area into their most common neighbour value
static void remove_small_patches(const worldgen_world *world, uint8_t *values, int min_area, int *queue,
    uint8_t *visited)
{
    int tiles = world->width * world->height;
    memset(visited, 0, (size_t) tiles);
    for (int start = 0; start < tiles; start++) {
        if (visited[start]) {
            continue;
        }
        int value = values[start];
        int neighbour_counts[8] = { 0 };
        int head = 0;
        int tail = 0;
        queue[tail++] = start;
        visited[start] = 1;
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
                if (values[neighbour] != value) {
                    neighbour_counts[values[neighbour]]++;
                } else if (!visited[neighbour]) {
                    visited[neighbour] = 1;
                    queue[tail++] = neighbour;
                }
            }
        }
        if (tail >= min_area) {
            continue;
        }
        int replacement = -1;
        for (int v = 0; v < 8; v++) {
            if (neighbour_counts[v] && (replacement < 0 || neighbour_counts[v] > neighbour_counts[replacement])) {
                replacement = v;
            }
        }
        if (replacement >= 0) {
            for (int i = 0; i < tail; i++) {
                values[queue[i]] = (uint8_t) replacement;
            }
        }
    }
}

static void clean_up(layout *l)
{
    worldgen_world *world = l->world;
    int tiles = world->width * world->height;
    uint8_t *scratch = malloc(tiles);
    int *queue = malloc(tiles * sizeof(int));
    if (scratch && queue) {
        // water: smooth shorelines, drop specks, but never close up a river
        uint8_t *is_water = malloc(tiles);
        if (is_water) {
            for (int i = 0; i < tiles; i++) {
                is_water[i] = world->water[i] != WORLDGEN_WATER_NONE;
            }
            remove_small_patches(world, is_water, 6, queue, scratch);
            for (int i = 0; i < tiles; i++) {
                world->water[i] = is_water[i] ? WORLDGEN_WATER_DEEP : WORLDGEN_WATER_NONE;
            }
            free(is_water);
        }
        majority_filter(world, world->level, scratch, WORLDGEN_MAX_LEVEL);
        remove_small_patches(world, world->level, MIN_TERRACE_AREA, queue, scratch);
    }
    free(scratch);
    free(queue);
    for (int i = 0; i < tiles; i++) {
        if (world->water[i] != WORLDGEN_WATER_NONE) {
            world->level[i] = 0;
        }
    }
    worldgen_enforce_single_steps(world);
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

static void mark_shallows(worldgen_world *world)
{
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            int index = worldgen_world_index(world, x, y);
            if (world->water[index] == WORLDGEN_WATER_DEEP && touches_land(world, x, y)) {
                world->water[index] = WORLDGEN_WATER_SHALLOW;
            }
        }
    }
}

// Ships on the sea come and go along the coast, at either end of it
static void set_sea_route(worldgen_world *world, int sea_side)
{
    int found = 0;
    for (int pass = 0; pass < 2; pass++) {
        double fraction = pass == 0 ? 0.08 : 0.92;
        for (int step = 0; step < world->width / 2 && found <= pass; step++) {
            double x, y;
            edge_point(world, sea_side, clamp(fraction + (pass == 0 ? 1 : -1) * step / (double) world->width, 0, 1),
                &x, &y);
            int index = worldgen_world_index(world, (int) x, (int) y);
            if (world->water[index] == WORLDGEN_WATER_DEEP) {
                worldgen_point *point = pass == 0 ? &world->river_entry : &world->river_exit;
                point->x = (int) x;
                point->y = (int) y;
                found++;
            }
        }
    }
}

void worldgen_generate_layout(worldgen_world *world)
{
    layout l;
    memset(&l, 0, sizeof(l));
    l.world = world;
    worldgen_rng_init(&l.rng, world->settings.seed, WORLDGEN_STAGE_LAYOUT);
    worldgen_noise_init(&l.noise, &l.rng);
    l.scale = world->width / 160.0;
    int tiles = world->width * world->height;
    l.corridor = calloc(tiles, 1);
    if (!l.corridor) {
        return;
    }
    worldgen_template type = world->settings.template_type;

    int sea_side = -1;
    if (type == WORLDGEN_TEMPLATE_COASTAL) {
        sea_side = worldgen_rng_between(&l.rng, 0, 4);
        place_coast(&l, sea_side);
    } else if (type == WORLDGEN_TEMPLATE_ISLAND) {
        place_island(&l);
    }
    place_rivers(&l, sea_side);
    stamp_rivers(&l);
    place_ponds(&l);
    mark_corridor(&l);
    place_plateaus(&l);
    clean_up(&l);
    mark_shallows(world);
    if (type == WORLDGEN_TEMPLATE_COASTAL) {
        set_sea_route(world, sea_side);
    } else if (type == WORLDGEN_TEMPLATE_ISLAND) {
        set_sea_route(world, 3);
    }

    // continuous elevation follows the terraces, for anything that wants a height value
    for (int i = 0; i < tiles; i++) {
        world->elevation[i] = world->level[i] / (double) WORLDGEN_MAX_LEVEL;
    }
    for (int r = 0; r < l.river_count; r++) {
        free(l.rivers[r].samples);
    }
    free(l.corridor);
}
