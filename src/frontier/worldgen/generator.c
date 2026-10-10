#include "generator.h"

#include "frontier/worldgen/rng.h"
#include "frontier/worldgen/stages.h"

static void generate_variants(worldgen_world *world)
{
    worldgen_rng rng;
    worldgen_rng_init(&rng, world->settings.seed, WORLDGEN_STAGE_VARIANT);
    int tiles = world->width * world->height;
    for (int i = 0; i < tiles; i++) {
        world->variant[i] = (uint8_t) worldgen_rng_next(&rng);
    }
}

worldgen_world *worldgen_generate(const worldgen_settings *settings)
{
    worldgen_world *world = worldgen_world_create(settings);
    if (!world) {
        return 0;
    }
    worldgen_generate_layout(world);
    worldgen_generate_surface(world);
    worldgen_generate_ramps(world);
    worldgen_generate_sites(world);
    generate_variants(world);
    return world;
}
