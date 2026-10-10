#include "frontier_setup.h"

#include "core/time.h"
#include "frontier/frontier_game.h"
#include "frontier/worldgen/generator.h"
#include "frontier/worldgen/preview.h"
#include "graphics/button.h"
#include "graphics/generic_button.h"
#include "graphics/graphics.h"
#include "graphics/image.h"
#include "graphics/panel.h"
#include "graphics/renderer.h"
#include "graphics/screen.h"
#include "graphics/text.h"
#include "graphics/window.h"
#include "input/input.h"
#include "window/main_menu.h"
#include "window/select_list.h"

#include <stdio.h>
#include <string.h>

// Pre-game setup for a frontier world: settings plus a live preview that regenerates on every change

#define PREVIEW_X 48
#define PREVIEW_Y 52
#define PREVIEW_WIDTH 544
#define PREVIEW_HEIGHT 272

#define ROW_Y 340
#define ROW_SPACING 32
#define LEFT_LABEL_X 32
#define LEFT_BUTTON_X 120
#define RIGHT_LABEL_X 336
#define RIGHT_BUTTON_X 424
#define BUTTON_WIDTH 176
#define BUTTON_HEIGHT 25
#define BOTTOM_Y 440

#define LEVEL_COUNT 3

typedef enum {
    SETTING_TEMPLATE = 0,
    SETTING_CLIMATE,
    SETTING_SIZE,
    SETTING_WATER,
    SETTING_RELIEF,
    SETTING_FOREST,
    SETTING_SEED,
    SETTING_BACK,
    SETTING_START,
    SETTING_MAX
} setting_button;

static const uint8_t *TEMPLATE_LABELS[WORLDGEN_TEMPLATE_MAX] = {
    (const uint8_t *) "River valley",
    (const uint8_t *) "Coastal",
    (const uint8_t *) "Lakes",
    (const uint8_t *) "Highlands",
    (const uint8_t *) "Island"
};

static const uint8_t *CLIMATE_LABELS[] = {
    (const uint8_t *) "Central",
    (const uint8_t *) "Northern",
    (const uint8_t *) "Desert"
};

static const int SIZES[] = { 40, 60, 80, 100, 120, 160 };
static const uint8_t *SIZE_LABELS[] = {
    (const uint8_t *) "40 x 40",
    (const uint8_t *) "60 x 60",
    (const uint8_t *) "80 x 80",
    (const uint8_t *) "100 x 100",
    (const uint8_t *) "120 x 120",
    (const uint8_t *) "160 x 160"
};

static const int LEVEL_VALUES[LEVEL_COUNT] = { 20, 50, 80 };
static const uint8_t *WATER_LABELS[LEVEL_COUNT] = {
    (const uint8_t *) "Dry", (const uint8_t *) "Normal", (const uint8_t *) "Wet"
};
static const uint8_t *RELIEF_LABELS[LEVEL_COUNT] = {
    (const uint8_t *) "Flat", (const uint8_t *) "Rolling", (const uint8_t *) "Rugged"
};
static const uint8_t *FOREST_LABELS[LEVEL_COUNT] = {
    (const uint8_t *) "Sparse", (const uint8_t *) "Normal", (const uint8_t *) "Dense"
};

static void button_click(const generic_button *button);

static generic_button buttons[] = {
    {LEFT_BUTTON_X, ROW_Y, BUTTON_WIDTH, BUTTON_HEIGHT, button_click, 0, SETTING_TEMPLATE},
    {LEFT_BUTTON_X, ROW_Y + ROW_SPACING, BUTTON_WIDTH, BUTTON_HEIGHT, button_click, 0, SETTING_CLIMATE},
    {LEFT_BUTTON_X, ROW_Y + 2 * ROW_SPACING, BUTTON_WIDTH, BUTTON_HEIGHT, button_click, 0, SETTING_SIZE},
    {RIGHT_BUTTON_X, ROW_Y, BUTTON_WIDTH, BUTTON_HEIGHT, button_click, 0, SETTING_WATER},
    {RIGHT_BUTTON_X, ROW_Y + ROW_SPACING, BUTTON_WIDTH, BUTTON_HEIGHT, button_click, 0, SETTING_RELIEF},
    {RIGHT_BUTTON_X, ROW_Y + 2 * ROW_SPACING, BUTTON_WIDTH, BUTTON_HEIGHT, button_click, 0, SETTING_FOREST},
    {LEFT_BUTTON_X, BOTTOM_Y, BUTTON_WIDTH, BUTTON_HEIGHT, button_click, 0, SETTING_SEED},
    {RIGHT_BUTTON_X - 88, BOTTOM_Y, 112, BUTTON_HEIGHT, button_click, 0, SETTING_BACK},
    {RIGHT_BUTTON_X + 40, BOTTOM_Y, 136, BUTTON_HEIGHT, button_click, 0, SETTING_START},
};

static struct {
    worldgen_settings settings;
    int size_index;
    int water_index;
    int relief_index;
    int forest_index;
    worldgen_world *world;
    int preview_width;
    int preview_height;
    unsigned int focus_button_id;
    int initialized;
} data;

static uint32_t new_seed(void)
{
    // mix the clock so repeated clicks give unrelated seeds
    uint32_t value = (uint32_t) time_get_millis() * 2654435761u ^ data.settings.seed * 40503u;
    value ^= value >> 15;
    return value % 1000000;
}

static void apply_choices(void)
{
    data.settings.size = SIZES[data.size_index];
    data.settings.water = LEVEL_VALUES[data.water_index];
    data.settings.relief = LEVEL_VALUES[data.relief_index];
    data.settings.forest = LEVEL_VALUES[data.forest_index];
}

// Projects the world onto a 2:1 diamond, matching how the game shows the map
static void update_preview_image(void)
{
    const worldgen_world *world = data.world;
    int width = world->width * 2;
    int height = world->height;
    if (width != data.preview_width || height != data.preview_height ||
        !graphics_renderer()->has_custom_image(CUSTOM_IMAGE_WORLD_PREVIEW)) {
        graphics_renderer()->create_custom_image(CUSTOM_IMAGE_WORLD_PREVIEW, width, height, 0);
        data.preview_width = width;
        data.preview_height = height;
    }
    int stride;
    color_t *pixels = graphics_renderer()->get_custom_image_buffer(CUSTOM_IMAGE_WORLD_PREVIEW, &stride);
    if (!pixels) {
        return;
    }
    double half = world->width / 2.0;
    for (int v = 0; v < height; v++) {
        for (int u = 0; u < width; u++) {
            double across = (u + 0.5) / 2.0 - half;
            double down = v + 0.5;
            int x = (int) (down + across);
            int y = (int) (down - across);
            color_t color = 0;
            if (down + across >= 0 && down - across >= 0 && worldgen_world_contains(world, x, y)) {
                color = worldgen_preview_color(world, x, y);
            }
            pixels[v * stride + u] = color;
        }
    }
    graphics_renderer()->update_custom_image(CUSTOM_IMAGE_WORLD_PREVIEW);
}

static void regenerate(void)
{
    apply_choices();
    worldgen_world_free(data.world);
    data.world = worldgen_generate(&data.settings);
    if (data.world) {
        update_preview_image();
    }
    window_invalidate();
}

static void init(void)
{
    if (!data.initialized) {
        worldgen_settings_default(&data.settings);
        data.size_index = 5;
        data.water_index = 1;
        data.relief_index = 1;
        data.forest_index = 1;
        data.settings.seed = new_seed();
        data.initialized = 1;
    }
    data.preview_width = 0;
    data.preview_height = 0;
    regenerate();
}

static void draw_preview(void)
{
    if (!data.world || !data.preview_width) {
        return;
    }
    float scale = (float) data.preview_width / PREVIEW_WIDTH;
    int x = screen_dialog_offset_x() + PREVIEW_X;
    int y = screen_dialog_offset_y() + PREVIEW_Y;
    graphics_renderer()->draw_custom_image(CUSTOM_IMAGE_WORLD_PREVIEW,
        (int) (x * scale), (int) (y * scale), scale, 1);
}

static void draw_background(void)
{
    graphics_reset_dialog();
    graphics_reset_clip_rectangle();
    image_draw_fullscreen_background(image_group(GROUP_INTERMEZZO_BACKGROUND));
    graphics_in_dialog();
    outer_panel_draw(0, 0, 40, 30);
    text_draw_centered((const uint8_t *) "New Frontier", 0, 14, 640, FONT_LARGE_BLACK, 0);
    inner_panel_draw(PREVIEW_X - 16, PREVIEW_Y - 8, (PREVIEW_WIDTH + 32) / 16, (PREVIEW_HEIGHT + 16) / 16);

    text_draw((const uint8_t *) "World", LEFT_LABEL_X, ROW_Y + 7, FONT_NORMAL_BLACK, 0);
    text_draw((const uint8_t *) "Climate", LEFT_LABEL_X, ROW_Y + ROW_SPACING + 7, FONT_NORMAL_BLACK, 0);
    text_draw((const uint8_t *) "Size", LEFT_LABEL_X, ROW_Y + 2 * ROW_SPACING + 7, FONT_NORMAL_BLACK, 0);
    text_draw((const uint8_t *) "Water", RIGHT_LABEL_X, ROW_Y + 7, FONT_NORMAL_BLACK, 0);
    text_draw((const uint8_t *) "Relief", RIGHT_LABEL_X, ROW_Y + ROW_SPACING + 7, FONT_NORMAL_BLACK, 0);
    text_draw((const uint8_t *) "Forest", RIGHT_LABEL_X, ROW_Y + 2 * ROW_SPACING + 7, FONT_NORMAL_BLACK, 0);
    text_draw((const uint8_t *) "Seed", LEFT_LABEL_X, BOTTOM_Y + 7, FONT_NORMAL_BLACK, 0);
    graphics_reset_dialog();
}

static void draw_button_label(setting_button id, const uint8_t *label)
{
    const generic_button *button = &buttons[id];
    text_draw_centered(label, button->x, button->y + 7, button->width, FONT_NORMAL_BLACK, 0);
}

static void draw_foreground(void)
{
    graphics_in_dialog();
    for (unsigned int i = 0; i < SETTING_MAX; i++) {
        button_border_draw(buttons[i].x, buttons[i].y, buttons[i].width, buttons[i].height,
            data.focus_button_id == i + 1);
    }
    draw_button_label(SETTING_TEMPLATE, TEMPLATE_LABELS[data.settings.template_type]);
    draw_button_label(SETTING_CLIMATE, CLIMATE_LABELS[data.settings.climate]);
    draw_button_label(SETTING_SIZE, SIZE_LABELS[data.size_index]);
    draw_button_label(SETTING_WATER, WATER_LABELS[data.water_index]);
    draw_button_label(SETTING_RELIEF, RELIEF_LABELS[data.relief_index]);
    draw_button_label(SETTING_FOREST, FOREST_LABELS[data.forest_index]);
    char seed_text[32];
    snprintf(seed_text, sizeof(seed_text), "%u  (new)", data.settings.seed);
    draw_button_label(SETTING_SEED, (const uint8_t *) seed_text);
    draw_button_label(SETTING_BACK, (const uint8_t *) "Back");
    draw_button_label(SETTING_START, (const uint8_t *) "Start");
    graphics_reset_dialog();

    draw_preview();
}

static void close_window(void)
{
    worldgen_world_free(data.world);
    data.world = 0;
}

static void handle_input(const mouse *m, const hotkeys *h)
{
    const mouse *m_dialog = mouse_in_dialog(m);
    if (generic_buttons_handle_mouse(m_dialog, 0, 0, buttons, SETTING_MAX, &data.focus_button_id)) {
        return;
    }
    if (input_go_back_requested(m, h)) {
        close_window();
        window_main_menu_show(0);
    }
}

static void template_selected(int index)
{
    data.settings.template_type = index;
    regenerate();
}

static void climate_selected(int index)
{
    data.settings.climate = index;
    regenerate();
}

static void size_selected(int index)
{
    data.size_index = index;
    regenerate();
}

static void show_list(const generic_button *button, const uint8_t **labels, int count, void (*callback)(int))
{
    window_select_list_show_text(screen_dialog_offset_x(), screen_dialog_offset_y(), button, labels, count, callback);
}

static void button_click(const generic_button *button)
{
    switch (button->parameter1) {
        case SETTING_TEMPLATE:
            show_list(button, TEMPLATE_LABELS, WORLDGEN_TEMPLATE_MAX, template_selected);
            break;
        case SETTING_CLIMATE:
            show_list(button, CLIMATE_LABELS, 3, climate_selected);
            break;
        case SETTING_SIZE:
            show_list(button, SIZE_LABELS, 6, size_selected);
            break;
        case SETTING_WATER:
            data.water_index = (data.water_index + 1) % LEVEL_COUNT;
            regenerate();
            break;
        case SETTING_RELIEF:
            data.relief_index = (data.relief_index + 1) % LEVEL_COUNT;
            regenerate();
            break;
        case SETTING_FOREST:
            data.forest_index = (data.forest_index + 1) % LEVEL_COUNT;
            regenerate();
            break;
        case SETTING_SEED:
            data.settings.seed = new_seed();
            regenerate();
            break;
        case SETTING_BACK:
            close_window();
            window_main_menu_show(0);
            break;
        case SETTING_START:
            close_window();
            apply_choices();
            if (!frontier_game_start(&data.settings)) {
                window_main_menu_show(0);
            }
            break;
        default:
            break;
    }
}

void window_frontier_setup_show(void)
{
    window_type window = {
        WINDOW_FRONTIER_SETUP,
        draw_background,
        draw_foreground,
        handle_input
    };
    init();
    window_show(&window);
}
