#include "frontier_game.h"

#include "city/view.h"
#include "core/dir.h"
#include "core/image.h"
#include "core/lang.h"
#include "core/log.h"
#include "frontier/world_export.h"
#include "frontier/worldgen/generator.h"
#include "game/file.h"
#include "game/file_editor.h"
#include "map/grid.h"
#include "scenario/property.h"
#include "sound/music.h"
#include "window/city.h"

#define SCENARIO_NAME "frontier_world"

int frontier_game_start(const worldgen_settings *settings)
{
    int size_index = frontier_world_export_size_index(settings->size);
    if (size_index < 0) {
        log_error("Frontier: size must be 40, 60, 80, 100, 120 or 160, got", 0, settings->size);
        return 0;
    }
    worldgen_world *world = worldgen_generate(settings);
    if (!world) {
        log_error("Frontier: world generation failed", 0, 0);
        return 0;
    }

    // Build the map through the editor, the same way a hand-made map is created, then save and load it
    // as a scenario so the game initializes everything exactly as it would for any custom map
    game_file_editor_create_scenario(size_index);
    scenario_change_climate(settings->climate);
    int exported = frontier_world_export(world);
    worldgen_point start = world->start;
    worldgen_world_free(world);
    if (!exported) {
        log_error("Frontier: export failed", 0, 0);
        return 0;
    }

    scenario_set_name((const uint8_t *) SCENARIO_NAME);
    scenario_set_custom(2);
    scenario_set_player_name(lang_get_string(9, 5));
    const char *path = dir_append_location(SCENARIO_NAME ".mapx", PATH_LOCATION_SCENARIO);
    if (!path || !game_file_editor_write_scenario(path)) {
        log_error("Frontier: could not write scenario", path, 0);
        return 0;
    }

    // the editor loaded its own graphics; switch back to the game's or the city draws black
    image_load_climate(settings->climate, 0, 1, 0);
    sound_music_stop();
    window_city_show();
    if (!game_file_start_scenario_by_name((const uint8_t *) SCENARIO_NAME)) {
        log_error("Frontier: could not start scenario", path, 0);
        return 0;
    }
    if (start.x >= 0) {
        city_view_go_to_grid_offset(map_grid_offset(start.x, start.y));
    }
    sound_music_update(1);
    return 1;
}
