#ifndef FRONTIER_WORLDGEN_RNG_H
#define FRONTIER_WORLDGEN_RNG_H

#include <stdint.h>

/**
 * @file
 * Seeded PCG32 random number generator for world generation.
 * Never uses rand() or the game's global RNG, so a seed produces the same world on every platform.
 */

typedef struct {
    uint64_t state;
    uint64_t inc;
} worldgen_rng;

/**
 * Initializes a generator for one stream of a seed.
 * Each generation stage uses its own stream so tweaking one stage does not reshuffle the others.
 * @param rng Generator to initialize
 * @param seed World seed
 * @param stream Stream identifier, usually a stage id
 */
void worldgen_rng_init(worldgen_rng *rng, uint32_t seed, uint32_t stream);

/**
 * @return The next 32 random bits
 */
uint32_t worldgen_rng_next(worldgen_rng *rng);

/**
 * @return A uniformly distributed integer in [min, max)
 */
int worldgen_rng_between(worldgen_rng *rng, int min, int max);

/**
 * @return A uniformly distributed double in [0, 1)
 */
double worldgen_rng_unit(worldgen_rng *rng);

#endif // FRONTIER_WORLDGEN_RNG_H
