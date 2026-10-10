#ifndef FRONTIER_WORLDGEN_WORLD_H
#define FRONTIER_WORLDGEN_WORLD_H

#include <stdint.h>

/**
 * @file
 * The world model: everything the generator decides, independent of the engine's map grids.
 * The exporter turns this into terrain flags, elevation and scenario data.
 */

#define WORLDGEN_MAX_LEVEL 5
#define WORLDGEN_MIN_SIZE 40
#define WORLDGEN_MAX_SIZE 1024

typedef enum {
    WORLDGEN_TEMPLATE_RIVER_VALLEY = 0,
    WORLDGEN_TEMPLATE_COASTAL,
    WORLDGEN_TEMPLATE_LAKES,
    WORLDGEN_TEMPLATE_HIGHLANDS,
    WORLDGEN_TEMPLATE_ISLAND,
    WORLDGEN_TEMPLATE_MAX
} worldgen_template;

typedef enum {
    WORLDGEN_WATER_NONE = 0,
    WORLDGEN_WATER_SHALLOW,
    WORLDGEN_WATER_DEEP
} worldgen_water;

typedef enum {
    WORLDGEN_SURFACE_GRASS = 0,
    WORLDGEN_SURFACE_MEADOW,
    WORLDGEN_SURFACE_SHRUB,
    WORLDGEN_SURFACE_FOREST,
    WORLDGEN_SURFACE_ROCK,
    WORLDGEN_SURFACE_MARSH
} worldgen_surface;

typedef enum {
    WORLDGEN_DEPOSIT_NONE = 0,
    WORLDGEN_DEPOSIT_STONE,
    WORLDGEN_DEPOSIT_MARBLE,
    WORLDGEN_DEPOSIT_IRON,
    WORLDGEN_DEPOSIT_GOLD
} worldgen_deposit;

typedef struct {
    int x;
    int y;
} worldgen_point;

// Ramp tile flags; the low two bits hold the direction up the ramp
#define WORLDGEN_RAMP_DIRECTION_MASK 0x03
#define WORLDGEN_RAMP_LOWER 0x10         // ramp tile on the lower level
#define WORLDGEN_RAMP_UPPER 0x20         // ramp tile on the upper level, drawn over the cliff
#define WORLDGEN_RAMP_ORIGIN 0x40        // top-left tile of the 2x2 ramp, where the engine places it
#define WORLDGEN_RAMP_RESERVED 0x80      // used while placing ramps only

typedef struct {
    uint32_t seed;
    int size;                       // world is size x size tiles
    int climate;                    // same values as the engine: 0 central, 1 northern, 2 desert
    worldgen_template template_type;
    int water;                      // 0..100, how much sea and lake
    int relief;                     // 0..100, how much raised land and how high it goes
    int forest;                     // 0..100
    int richness;                   // 0..100, deposit count and size
    int natives;                    // 0..100
} worldgen_settings;

typedef struct {
    int width;
    int height;
    worldgen_settings settings;
    double sea_level;               // threshold on elevation below which the template wants sea
    worldgen_point inflow;          // map edge tile where a river flows in from off the map, x < 0 if none
    double drain_x;                 // downhill direction across the world; water leaves through edges
    double drain_y;                 // on that side. Both zero means it may leave through any edge
    worldgen_point river_entry;     // navigable river ends on the map edge, used by trade ships; x < 0 if none
    worldgen_point river_exit;
    worldgen_point start;           // where the player's settlement begins, x < 0 if none
    worldgen_point entry;           // land entry and exit on the map edge, joined to the start by the road
    worldgen_point exit;
    double *elevation;              // continuous height, 0..1
    uint8_t *level;                 // engine elevation, 0..WORLDGEN_MAX_LEVEL
    uint8_t *water;                 // worldgen_water
    uint8_t *surface;               // worldgen_surface
    uint8_t *deposit;               // worldgen_deposit
    uint8_t *variant;               // random byte per tile; the engine uses it to pick image variants
    uint8_t *road;                  // 1 where the road runs
    uint8_t *ramp;                  // WORLDGEN_RAMP_* flags
} worldgen_world;

void worldgen_settings_default(worldgen_settings *settings);

const char *worldgen_template_name(worldgen_template template_type);

/**
 * @return The template with this name, or -1 if there is none
 */
int worldgen_template_from_name(const char *name);

/**
 * Allocates a blank world: all land, level 0, grass.
 * @return The world, or 0 when out of memory or the size is invalid
 */
worldgen_world *worldgen_world_create(const worldgen_settings *settings);

void worldgen_world_free(worldgen_world *world);

int worldgen_world_index(const worldgen_world *world, int x, int y);

int worldgen_world_contains(const worldgen_world *world, int x, int y);

/**
 * @return 1 if any neighbour, including diagonals, is lower: the engine draws such tiles as cliffs,
 *         which cannot be walked or built on
 */
int worldgen_world_is_slope(const worldgen_world *world, int x, int y);

#endif // FRONTIER_WORLDGEN_WORLD_H
