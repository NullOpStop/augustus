// Frontier world generation preview: generates a world and writes it as a top-down PNG.
// Usage: worldgen_preview [--seed N] [--size N] [--template NAME] [--water N] [--relief N] [--forest N]
//                         [--scale N] [--out FILE]

#include "frontier/worldgen/generator.h"
#include "frontier/worldgen/preview.h"

#define MINIZ_IMPLEMENTATION
#include "miniz/miniz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
            uint32_t argb = worldgen_preview_color(world, x, y);
            unsigned char color[3] = {
                (unsigned char) (argb >> 16), (unsigned char) (argb >> 8), (unsigned char) argb
            };
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

// Same rule as the engine's boat routing: deep water whose 4 neighbours are all water (off-map counts as water)
static int is_boat_tile(const worldgen_world *world, int x, int y)
{
    static const int directions[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
    if (world->water[worldgen_world_index(world, x, y)] != WORLDGEN_WATER_DEEP) {
        return 0;
    }
    for (int d = 0; d < 4; d++) {
        int nx = x + directions[d][0];
        int ny = y + directions[d][1];
        if (worldgen_world_contains(world, nx, ny) &&
            world->water[worldgen_world_index(world, nx, ny)] == WORLDGEN_WATER_NONE) {
            return 0;
        }
    }
    return 1;
}

static int river_is_navigable(const worldgen_world *world)
{
    static const int directions[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
    worldgen_point from = world->river_entry;
    worldgen_point to = world->river_exit;
    if (from.x < 0 || to.x < 0 || !is_boat_tile(world, from.x, from.y)) {
        return 0;
    }
    int tiles = world->width * world->height;
    int *queue = malloc(tiles * sizeof(int));
    unsigned char *seen = calloc(tiles, 1);
    int found = 0;
    if (queue && seen) {
        int head = 0;
        int tail = 0;
        queue[tail++] = worldgen_world_index(world, from.x, from.y);
        seen[queue[0]] = 1;
        while (head < tail && !found) {
            int x = queue[head] % world->width;
            int y = queue[head++] / world->width;
            found = x == to.x && y == to.y;
            for (int d = 0; d < 4; d++) {
                int nx = x + directions[d][0];
                int ny = y + directions[d][1];
                if (worldgen_world_contains(world, nx, ny) && !seen[worldgen_world_index(world, nx, ny)] &&
                    is_boat_tile(world, nx, ny)) {
                    seen[worldgen_world_index(world, nx, ny)] = 1;
                    queue[tail++] = worldgen_world_index(world, nx, ny);
                }
            }
        }
    }
    free(queue);
    free(seen);
    return found;
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
    if (world->river_entry.x >= 0) {
        printf("river entry (%d, %d), exit (%d, %d), navigable: %s\n", world->river_entry.x, world->river_entry.y,
            world->river_exit.x, world->river_exit.y, river_is_navigable(world) ? "yes" : "NO");
    }
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
