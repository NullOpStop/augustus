#include "dev_render.h"

#include "core/dir.h"
#include "core/image.h"
#include "core/log.h"
#include "frontier/world_export.h"
#include "frontier/worldgen/generator.h"
#include "game/file.h"
#include "game/file_editor.h"
#include "graphics/screenshot.h"
#include "graphics/window.h"
#include "scenario/property.h"
#include "window/city.h"

#include <stdlib.h>
#include <string.h>

#define SCENARIO_NAME "frontier_preview"
#define MAX_SPEC_LENGTH 256
// the city needs a few drawn frames before the full city screenshot renders anything
#define FRAMES_BEFORE_SCREENSHOT 10

static int frames_drawn;

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
    if (!parse_spec(spec, &settings)) {
        return 0;
    }
    int size_index = frontier_world_export_size_index(settings.size);
    if (size_index < 0) {
        log_error("Frontier render: size must be 40, 60, 80, 100, 120 or 160, got", 0, settings.size);
        return 0;
    }
    worldgen_world *world = worldgen_generate(&settings);
    if (!world) {
        log_error("Frontier render: world generation failed", 0, 0);
        return 0;
    }

    game_file_editor_create_scenario(size_index);
    scenario_change_climate(settings.climate);
    int exported = frontier_world_export(world);
    worldgen_world_free(world);
    if (!exported) {
        log_error("Frontier render: export failed", 0, 0);
        return 0;
    }

    scenario_set_name((const uint8_t *) SCENARIO_NAME);
    scenario_set_custom(2);
    const char *path = dir_append_location(SCENARIO_NAME ".mapx", PATH_LOCATION_SCENARIO);
    if (!path || !game_file_editor_write_scenario(path)) {
        log_error("Frontier render: could not write scenario", path, 0);
        return 0;
    }

    // the editor loaded its own graphics; switch back to the game's before showing the city
    image_load_climate(settings.climate, 0, 1, 0);
    window_city_show();
    if (!game_file_start_scenario_by_name((const uint8_t *) SCENARIO_NAME)) {
        log_error("Frontier render: could not start scenario", path, 0);
        return 0;
    }
    frames_drawn = 0;
    return 1;
}

int frontier_dev_render_update(void)
{
    if (!window_is(WINDOW_CITY)) {
        log_error("Frontier render: city window is not showing", 0, 0);
        return -1;
    }
    if (++frames_drawn < FRAMES_BEFORE_SCREENSHOT) {
        return 0;
    }
    graphics_save_screenshot(SCREENSHOT_FULL_CITY);
    graphics_save_screenshot(SCREENSHOT_MINIMAP);
    log_info("Frontier render done", 0, 0);
    return 1;
}
