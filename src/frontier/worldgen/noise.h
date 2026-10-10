#ifndef FRONTIER_WORLDGEN_NOISE_H
#define FRONTIER_WORLDGEN_NOISE_H

#include "frontier/worldgen/rng.h"

#include <stdint.h>

/**
 * @file
 * Seeded 2D simplex noise with fractal and domain-warp helpers.
 */

typedef struct {
    uint8_t perm[512];
} worldgen_noise;

/**
 * Shuffles the permutation table from the given generator.
 */
void worldgen_noise_init(worldgen_noise *noise, worldgen_rng *rng);

/**
 * @return Simplex noise at (x, y), roughly in [-1, 1]
 */
double worldgen_noise_2d(const worldgen_noise *noise, double x, double y);

/**
 * Fractal Brownian motion: several octaves of simplex noise added together.
 * @param octaves Number of layers
 * @param lacunarity Frequency multiplier per octave, usually 2
 * @param gain Amplitude multiplier per octave, usually 0.5
 * @return Noise roughly in [-1, 1]
 */
double worldgen_noise_fbm(const worldgen_noise *noise, double x, double y, int octaves, double lacunarity, double gain);

/**
 * Offsets (x, y) by a noise field so shapes look natural instead of blobby.
 * @param strength Maximum offset, in the same units as x and y
 * @param frequency Frequency of the warp field
 */
void worldgen_noise_warp(const worldgen_noise *noise, double *x, double *y, double strength, double frequency);

#endif // FRONTIER_WORLDGEN_NOISE_H
