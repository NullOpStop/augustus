#include "rng.h"

// PCG32 (XSH RR), see https://www.pcg-random.org

uint32_t worldgen_rng_next(worldgen_rng *rng)
{
    uint64_t old_state = rng->state;
    rng->state = old_state * 6364136223846793005ULL + rng->inc;
    uint32_t xor_shifted = (uint32_t) (((old_state >> 18u) ^ old_state) >> 27u);
    uint32_t rotation = (uint32_t) (old_state >> 59u);
    return (xor_shifted >> rotation) | (xor_shifted << ((-rotation) & 31u));
}

void worldgen_rng_init(worldgen_rng *rng, uint32_t seed, uint32_t stream)
{
    rng->state = 0;
    rng->inc = ((uint64_t) stream << 1u) | 1u;
    worldgen_rng_next(rng);
    rng->state += seed;
    worldgen_rng_next(rng);
}

int worldgen_rng_between(worldgen_rng *rng, int min, int max)
{
    if (max <= min) {
        return min;
    }
    uint32_t range = (uint32_t) (max - min);
    // reject values that would bias the modulo
    uint32_t threshold = (0u - range) % range;
    uint32_t value;
    do {
        value = worldgen_rng_next(rng);
    } while (value < threshold);
    return min + (int) (value % range);
}

double worldgen_rng_unit(worldgen_rng *rng)
{
    return worldgen_rng_next(rng) / 4294967296.0;
}
