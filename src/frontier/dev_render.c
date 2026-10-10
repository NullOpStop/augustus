#include "dev_render.h"

#include "building/construction.h"
#include "core/dir.h"
#include "core/log.h"
#include "frontier/frontier_game.h"
#include "frontier/map_stats.h"
#include "graphics/screenshot.h"
#include "game/file.h"
#include "game/file_io.h"
#include "game/tick.h"
#include "graphics/window.h"
#include "map/fog.h"
#include "map/grid.h"
#include "map/terrain.h"
#include "map/tiles.h"
#include "scenario/property.h"
#include "window/city.h"
#include "window/frontier_setup.h"
#include "window/main_menu.h"

#include <stdlib.h>
#include <string.h>

#define MAX_SPEC_LENGTH 256
// the city needs a few drawn frames before the full city screenshot renders anything
#define FRAMES_BEFORE_SCREENSHOT 10

static int frames_drawn;
static int show_setup;
static int simulate_ticks;
static int save_test;
static char load_name[64];
static int show_fog;

static int parse_spec(const char *spec, worldgen_settings *settings)
{
    char buffer[MAX_SPEC_LENGTH];
    strncpy(buffer, spec, MAX_SPEC_LENGTH - 1);
    buffer[MAX_SPEC_LENGTH - 1] = 0;

    for (char *pair = strtok(buffer, ","); pair; pair = strtok(0, ",")) {
        char *value = strchr(pair, '=');
        if (!value) {
            log_error("Frontier render: expected key=value, got", pair, 0);
            return 0;
        }
        *value++ = 0;
        if (strcmp(pair, "seed") == 0) {
            settings->seed = (uint32_t) strtoul(value, 0, 10);
        } else if (strcmp(pair, "size") == 0) {
            settings->size = atoi(value);
        } else if (strcmp(pair, "template") == 0) {
            int template_type = worldgen_template_from_name(value);
            if (template_type < 0) {
                log_error("Frontier render: unknown template", value, 0);
                return 0;
            }
            settings->template_type = template_type;
        } else if (strcmp(pair, "climate") == 0) {
            settings->climate = atoi(value);
        } else if (strcmp(pair, "water") == 0) {
            settings->water = atoi(value);
        } else if (strcmp(pair, "relief") == 0) {
            settings->relief = atoi(value);
        } else if (strcmp(pair, "forest") == 0) {
            settings->forest = atoi(value);
        } else if (strcmp(pair, "setup") == 0) {
            show_setup = atoi(value);
        } else if (strcmp(pair, "simulate") == 0) {
            simulate_ticks = atoi(value);
        } else if (strcmp(pair, "savetest") == 0) {
            save_test = atoi(value);
        } else if (strcmp(pair, "fog") == 0) {
            show_fog = atoi(value);
        } else if (strcmp(pair, "load") == 0) {
            strncpy(load_name, value, sizeof(load_name) - 1);
            load_name[sizeof(load_name) - 1] = 0;
        } else {
            log_error("Frontier render: unknown setting", pair, 0);
            return 0;
        }
    }
    return 1;
}

int frontier_dev_render_start(const char *spec)
{
    worldgen_settings settings;
    worldgen_settings_default(&settings);
    show_setup = 0;
    simulate_ticks = 0;
    save_test = 0;
    load_name[0] = 0;
    show_fog = 1;
    if (!parse_spec(spec, &settings)) {
        return 0;
    }
    if (load_name[0]) {
        // an existing scenario, for comparing generated maps with hand-made ones
        scenario_set_custom(2);
        window_city_show();
        if (!game_file_start_scenario_by_name((const uint8_t *) load_name)) {
            log_error("Frontier render: could not load", load_name, 0);
            return 0;
        }
        frontier_map_stats_log(load_name);
        frames_drawn = 0;
        return 1;
    }
    frames_drawn = 0;
    if (show_setup == 2) {
        window_main_menu_show(0);
        return 1;
    } else if (show_setup) {
        window_frontier_setup_show();
        return 1;
    }
    if (!frontier_game_start(&settings)) {
        return 0;
    }
    frontier_map_stats_log("generated");
    if (!show_fog) {
        map_fog_clear();
    }
    return 1;
}

static int is_near_road(int x, int y)
{
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            if (map_grid_is_inside(x + dx, y + dy, 1) && map_terrain_is(map_grid_offset(x + dx, y + dy), TERRAIN_ROAD)) {
                return 1;
            }
        }
    }
    return 0;
}

// A few house plots by the road near the start, so immigrants walk in and explore
static void place_houses(void)
{
    worldgen_point start = frontier_game_start_point();
    int placed = 0;
    for (int radius = 1; radius <= 10 && placed < 12; radius++) {
        for (int y = start.y - radius; y <= start.y + radius && placed < 12; y++) {
            for (int x = start.x - radius; x <= start.x + radius && placed < 12; x++) {
                if (!map_grid_is_inside(x, y, 1) || !is_near_road(x, y) ||
                    !map_tiles_are_clear(x, y, 1, TERRAIN_ALL, 1)) {
                    continue;
                }
                int grid_offset = map_grid_offset(x, y);
                building_construction_set_type(BUILDING_HOUSE_VACANT_LOT, 0);
                building_construction_start(x, y, grid_offset);
                building_construction_update(x, y, grid_offset);
                building_construction_place();
                placed += map_terrain_is(grid_offset, TERRAIN_BUILDING);
            }
        }
    }
    log_info("Frontier render: house plots placed", 0, placed);
}

static int run_test_steps(void)
{
    if (simulate_ticks) {
        place_houses();
        for (int i = 0; i < simulate_ticks; i++) {
            game_tick_run();
        }
    }
    if (save_test) {
        const char *path = dir_append_location("frontier_save_test.svx", PATH_LOCATION_SAVEGAME);
        if (!path || !game_file_write_saved_game(path) || game_file_load_saved_game(path) != FILE_LOAD_SUCCESS) {
            log_error("Frontier render: save test failed", path, 0);
            return 0;
        }
        log_info("Frontier render: saved and reloaded", path, 0);
    }
    return 1;
}

int frontier_dev_render_update(void)
{
    if (++frames_drawn < FRAMES_BEFORE_SCREENSHOT) {
        return 0;
    }
    if (show_setup) {
        graphics_save_screenshot(SCREENSHOT_DISPLAY);
        return 1;
    }
    if (!window_is(WINDOW_CITY)) {
        log_error("Frontier render: city window is not showing", 0, 0);
        return -1;
    }
    if (frames_drawn == FRAMES_BEFORE_SCREENSHOT) {
        if (!run_test_steps()) {
            return -1;
        }
        // draw a few frames of the new state before the screenshot
        return 0;
    }
    if (frames_drawn < FRAMES_BEFORE_SCREENSHOT * 2) {
        return 0;
    }
    graphics_save_screenshot(SCREENSHOT_FULL_CITY);
    graphics_save_screenshot(SCREENSHOT_MINIMAP);
    log_info("Frontier render done", 0, 0);
    return 1;
}
