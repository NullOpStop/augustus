#include "preview.h"

#include <stdlib.h>

static const uint8_t LEVEL_COLORS[WORLDGEN_MAX_LEVEL + 1][3] = {
    {150, 190, 100},
    {112, 156, 72},
    {150, 152, 84},
    {164, 132, 92},
    {136, 112, 92},
    {224, 220, 210}
};

static const uint8_t SURFACE_COLORS[][3] = {
    {0, 0, 0},       // grass: use the level colour
    {196, 200, 96},  // meadow
    {96, 128, 64},   // shrub
    {52, 92, 48},    // forest
    {120, 116, 110}, // rock
    {92, 120, 96}    // marsh
};

static const uint8_t ROAD[3] = {214, 196, 150};
static const uint8_t START[3] = {214, 40, 40};
static const uint8_t RAMP[3] = {240, 160, 40};
static const uint8_t DEEP_WATER[3] = {38, 78, 122};
static const uint8_t SHALLOW_WATER[3] = {66, 118, 166};

static uint32_t pack(const uint8_t *rgb)
{
    return 0xff000000u | ((uint32_t) rgb[0] << 16) | ((uint32_t) rgb[1] << 8) | rgb[2];
}

static int is_cliff(const worldgen_world *world, int x, int y)
{
    static const int directions[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
    int level = world->level[worldgen_world_index(world, x, y)];
    for (int d = 0; d < 4; d++) {
        int nx = x + directions[d][0];
        int ny = y + directions[d][1];
        if (worldgen_world_contains(world, nx, ny) && world->level[worldgen_world_index(world, nx, ny)] < level) {
            return 1;
        }
    }
    return 0;
}

uint32_t worldgen_preview_color(const worldgen_world *world, int x, int y)
{
    int index = worldgen_world_index(world, x, y);
    if (world->start.x >= 0 && abs(x - world->start.x) <= 2 && abs(y - world->start.y) <= 2) {
        return pack(START);
    }
    if (world->ramp[index]) {
        return pack(RAMP);
    }
    if (world->road[index]) {
        return pack(ROAD);
    }
    if (world->water[index] == WORLDGEN_WATER_DEEP) {
        return pack(DEEP_WATER);
    }
    if (world->water[index] == WORLDGEN_WATER_SHALLOW) {
        return pack(SHALLOW_WATER);
    }
    uint8_t color[3] = {
        LEVEL_COLORS[world->level[index]][0],
        LEVEL_COLORS[world->level[index]][1],
        LEVEL_COLORS[world->level[index]][2]
    };
    int surface = world->surface[index];
    if (surface != WORLDGEN_SURFACE_GRASS) {
        // blend the surface over the level colour so terraces stay readable
        for (int c = 0; c < 3; c++) {
            color[c] = (uint8_t) ((SURFACE_COLORS[surface][c] * 3 + color[c]) / 4);
        }
    }
    if (is_cliff(world, x, y)) {
        for (int c = 0; c < 3; c++) {
            color[c] = (uint8_t) (color[c] * 7 / 10);
        }
    }
    return pack(color);
}
