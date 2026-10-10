#include "dev_render.h"

#include "core/log.h"
#include "frontier/frontier_game.h"
#include "graphics/screenshot.h"
#include "graphics/window.h"
#include "window/frontier_setup.h"
#include "window/main_menu.h"

#include <stdlib.h>
#include <string.h>

#define MAX_SPEC_LENGTH 256
// the city needs a few drawn frames before the full city screenshot renders anything
#define FRAMES_BEFORE_SCREENSHOT 10

static int frames_drawn;
static int show_setup;

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
    if (!parse_spec(spec, &settings)) {
        return 0;
    }
    frames_drawn = 0;
    if (show_setup == 2) {
        window_main_menu_show(0);
        return 1;
    } else if (show_setup) {
        window_frontier_setup_show();
        return 1;
    }
    return frontier_game_start(&settings);
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
    graphics_save_screenshot(SCREENSHOT_FULL_CITY);
    graphics_save_screenshot(SCREENSHOT_MINIMAP);
    log_info("Frontier render done", 0, 0);
    return 1;
}
