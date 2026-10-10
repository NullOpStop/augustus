// Frontier world generation preview: generates a world and writes it as a top-down PNG.
// Usage: worldgen_preview [--seed N] [--size N] [--template NAME] [--water N] [--relief N] [--forest N]
//                         [--scale N] [--out FILE]

#include "frontier/worldgen/generator.h"

#define MINIZ_IMPLEMENTATION
#include "miniz/miniz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const unsigned char LEVEL_COLORS[WORLDGEN_MAX_LEVEL + 1][3] = {
    {150, 190, 100},
    {112, 156, 72},
    {150, 152, 84},
    {164, 132, 92},
    {136, 112, 92},
    {224, 220, 210}
};

static const unsigned char SURFACE_TINT[][3] = {
    {0, 0, 0},       // grass: use the level colour
    {196, 200, 96},  // meadow
    {96, 128, 64},   // shrub
    {52, 92, 48},    // forest
    {120, 116, 110}, // rock
    {92, 120, 96}    // marsh
};

static const unsigned char DEEP_WATER[3] = {38, 78, 122};
static const unsigned char SHALLOW_WATER[3] = {66, 118, 166};

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

static unsigned char *render(const worldgen_world *world, int scale)
{
    int width = world->width * scale;
    int height = world->height * scale;
    unsigned char *pixels = malloc((size_t) width * height * 3);
    if (!pixels) {
        return 0;
    }
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            int index = worldgen_world_index(world, x, y);
            unsigned char color[3];
            if (world->water[index] == WORLDGEN_WATER_DEEP) {
                memcpy(color, DEEP_WATER, 3);
            } else if (world->water[index] == WORLDGEN_WATER_SHALLOW) {
                memcpy(color, SHALLOW_WATER, 3);
            } else {
                memcpy(color, LEVEL_COLORS[world->level[index]], 3);
                int surface = world->surface[index];
                if (surface != WORLDGEN_SURFACE_GRASS) {
                    // blend the surface over the level colour so terraces stay readable
                    for (int c = 0; c < 3; c++) {
                        color[c] = (unsigned char) ((SURFACE_TINT[surface][c] * 3 + color[c]) / 4);
                    }
                }
                if (is_cliff(world, x, y)) {
                    for (int c = 0; c < 3; c++) {
                        color[c] = (unsigned char) (color[c] * 7 / 10);
                    }
                }
            }
            for (int py = 0; py < scale; py++) {
                unsigned char *row = pixels + ((size_t) (y * scale + py) * width + x * scale) * 3;
                for (int px = 0; px < scale; px++) {
                    memcpy(row + px * 3, color, 3);
                }
            }
        }
    }
    return pixels;
}

static void print_stats(const worldgen_world *world, double milliseconds)
{
    int tiles = world->width * world->height;
    int water = 0;
    int levels[WORLDGEN_MAX_LEVEL + 1] = { 0 };
    for (int i = 0; i < tiles; i++) {
        if (world->water[i] != WORLDGEN_WATER_NONE) {
            water++;
        } else {
            levels[world->level[i]]++;
        }
    }
    printf("%dx%d, seed %u, template %s, generated in %.0f ms\n", world->width, world->height,
        world->settings.seed, worldgen_template_name(world->settings.template_type), milliseconds);
    printf("water %.1f%%", 100.0 * water / tiles);
    for (int level = 0; level <= WORLDGEN_MAX_LEVEL; level++) {
        printf(" | level %d %.1f%%", level, 100.0 * levels[level] / tiles);
    }
    printf("\n");
}

int main(int argc, char **argv)
{
    worldgen_settings settings;
    worldgen_settings_default(&settings);
    int scale = 4;
    const char *out = "world.png";

    for (int i = 1; i + 1 < argc; i += 2) {
        const char *key = argv[i];
        const char *value = argv[i + 1];
        if (strcmp(key, "--seed") == 0) {
            settings.seed = (uint32_t) strtoul(value, 0, 10);
        } else if (strcmp(key, "--size") == 0) {
            settings.size = atoi(value);
        } else if (strcmp(key, "--template") == 0) {
            settings.template_type = worldgen_template_from_name(value);
        } else if (strcmp(key, "--water") == 0) {
            settings.water = atoi(value);
        } else if (strcmp(key, "--relief") == 0) {
            settings.relief = atoi(value);
        } else if (strcmp(key, "--forest") == 0) {
            settings.forest = atoi(value);
        } else if (strcmp(key, "--scale") == 0) {
            scale = atoi(value);
        } else if (strcmp(key, "--out") == 0) {
            out = value;
        } else {
            fprintf(stderr, "Unknown option %s\n", key);
            return 1;
        }
    }
    if (settings.template_type < 0 || settings.template_type >= WORLDGEN_TEMPLATE_MAX || scale < 1) {
        fprintf(stderr, "Invalid template or scale\n");
        return 1;
    }

    clock_t start = clock();
    worldgen_world *world = worldgen_generate(&settings);
    double milliseconds = 1000.0 * (clock() - start) / CLOCKS_PER_SEC;
    if (!world) {
        fprintf(stderr, "World generation failed (size must be %d..%d)\n", WORLDGEN_MIN_SIZE, WORLDGEN_MAX_SIZE);
        return 1;
    }
    print_stats(world, milliseconds);

    unsigned char *pixels = render(world, scale);
    size_t png_size = 0;
    void *png = pixels ?
        tdefl_write_image_to_png_file_in_memory_ex(pixels, world->width * scale, world->height * scale, 3,
            &png_size, 6, MZ_FALSE) : 0;
    int result = 1;
    FILE *file = png ? fopen(out, "wb") : 0;
    if (file) {
        result = fwrite(png, 1, png_size, file) == png_size ? 0 : 1;
        fclose(file);
    }
    if (result) {
        fprintf(stderr, "Could not write %s\n", out);
    }
    mz_free(png);
    free(pixels);
    worldgen_world_free(world);
    return result;
}
