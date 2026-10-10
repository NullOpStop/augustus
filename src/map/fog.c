#include "fog.h"

#include "building/building.h"
#include "editor/editor.h"
#include "figure/figure.h"
#include "map/grid.h"
#include "widget/minimap.h"

#define FOG_EXPLORED 0x01
// Set on a tile once a sight circle has been revealed around it, so standing still costs nothing
#define FOG_SEEN_BY_WALKER 0x02
#define FOG_SEEN_BY_SOLDIER 0x04
#define FOG_SEEN_BY_BUILDING 0x08

#define WALKER_SIGHT 5
#define SOLDIER_SIGHT 9
#define BUILDING_SIGHT 3

static struct {
    int enabled;
    int revealed_new_tiles;
    grid_u8 tiles;
} data;

void map_fog_clear(void)
{
    data.enabled = 0;
    map_grid_clear_u8(data.tiles.items);
}

void map_fog_enable(void)
{
    data.enabled = 1;
    map_grid_clear_u8(data.tiles.items);
}

int map_fog_is_enabled(void)
{
    return data.enabled;
}

int map_fog_is_explored(int grid_offset)
{
    return !data.enabled || editor_is_active() || (data.tiles.items[grid_offset] & FOG_EXPLORED);
}

int map_fog_area_is_explored(int x, int y, int size)
{
    if (!data.enabled || editor_is_active()) {
        return 1;
    }
    for (int dy = 0; dy < size; dy++) {
        for (int dx = 0; dx < size; dx++) {
            if (map_grid_is_inside(x + dx, y + dy, 1) && !map_fog_is_explored(map_grid_offset(x + dx, y + dy))) {
                return 0;
            }
        }
    }
    return 1;
}

void map_fog_reveal(int x, int y, int radius)
{
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy > radius * radius + radius || !map_grid_is_inside(x + dx, y + dy, 1)) {
                continue;
            }
            uint8_t *tile = &data.tiles.items[map_grid_offset(x + dx, y + dy)];
            if (!(*tile & FOG_EXPLORED)) {
                *tile |= FOG_EXPLORED;
                data.revealed_new_tiles = 1;
            }
        }
    }
}

static void reveal_once(int x, int y, int radius, uint8_t seen_flag)
{
    if (!map_grid_is_inside(x, y, 1)) {
        return;
    }
    uint8_t *tile = &data.tiles.items[map_grid_offset(x, y)];
    if (*tile & seen_flag) {
        return;
    }
    *tile |= seen_flag;
    map_fog_reveal(x, y, radius);
}

// The player's own people; traders and wanderers from elsewhere do not explore for the city
static int is_explorer(figure *f)
{
    if (figure_is_dead(f) || f->is_ghost || !figure_is_category(f, FIGURE_CATEGORY_CITIZEN)) {
        return 0;
    }
    return f->type != FIGURE_TRADE_CARAVAN && f->type != FIGURE_TRADE_CARAVAN_DONKEY &&
        f->type != FIGURE_NATIVE_TRADER && f->type != FIGURE_TRADE_SHIP;
}

static int is_native(building_type type)
{
    return type == BUILDING_NATIVE_CROPS || type == BUILDING_NATIVE_HUT || type == BUILDING_NATIVE_HUT_ALT ||
        type == BUILDING_NATIVE_HUT_ALT_2 || type == BUILDING_NATIVE_LARGE_HUT_ALT ||
        type == BUILDING_NATIVE_LARGE_HUT_ALT_2 || type == BUILDING_NATIVE_MEETING ||
        type == BUILDING_NATIVE_MONUMENT || type == BUILDING_NATIVE_DECORATION ||
        type == BUILDING_NATIVE_WATCHTOWER || type == BUILDING_NATIVE_WELL || type == BUILDING_NATIVE_PALISADE;
}

void map_fog_update(void)
{
    if (!data.enabled) {
        return;
    }
    for (unsigned int i = 1; i < figure_count(); i++) {
        figure *f = figure_get(i);
        if (!is_explorer(f)) {
            continue;
        }
        if (figure_is_category(f, FIGURE_CATEGORY_ARMED)) {
            reveal_once(f->x, f->y, SOLDIER_SIGHT, FOG_SEEN_BY_SOLDIER);
        } else {
            reveal_once(f->x, f->y, WALKER_SIGHT, FOG_SEEN_BY_WALKER);
        }
    }
    for (int i = 1; i < building_count(); i++) {
        building *b = building_get(i);
        if ((b->state != BUILDING_STATE_IN_USE && b->state != BUILDING_STATE_CREATED &&
            b->state != BUILDING_STATE_MOTHBALLED) || is_native(b->type)) {
            continue;
        }
        // centred on the building, reaching a little past its edges
        int half = b->size / 2;
        reveal_once(b->x + half, b->y + half, BUILDING_SIGHT + half, FOG_SEEN_BY_BUILDING);
    }
    if (data.revealed_new_tiles) {
        data.revealed_new_tiles = 0;
        widget_minimap_invalidate();
    }
}

void map_fog_save_state(buffer *buf)
{
    buffer_write_i32(buf, data.enabled);
    buffer_write_raw(buf, data.tiles.items, GRID_SIZE * GRID_SIZE);
}

void map_fog_load_state(buffer *buf)
{
    data.enabled = buffer_read_i32(buf);
    buffer_read_raw(buf, data.tiles.items, GRID_SIZE * GRID_SIZE);
}
