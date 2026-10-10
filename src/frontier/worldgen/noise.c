#include "noise.h"

#include <math.h>

// 2D simplex noise, after Stefan Gustavson's public domain reference implementation

#define F2 0.36602540378443864676 // (sqrt(3) - 1) / 2
#define G2 0.21132486540518711775 // (3 - sqrt(3)) / 6

static const int GRADIENTS[12][2] = {
    {1, 1}, {-1, 1}, {1, -1}, {-1, -1},
    {1, 0}, {-1, 0}, {1, 0}, {-1, 0},
    {0, 1}, {0, -1}, {0, 1}, {0, -1}
};

void worldgen_noise_init(worldgen_noise *noise, worldgen_rng *rng)
{
    for (int i = 0; i < 256; i++) {
        noise->perm[i] = (uint8_t) i;
    }
    for (int i = 255; i > 0; i--) {
        int j = worldgen_rng_between(rng, 0, i + 1);
        uint8_t swap = noise->perm[i];
        noise->perm[i] = noise->perm[j];
        noise->perm[j] = swap;
    }
    for (int i = 0; i < 256; i++) {
        noise->perm[i + 256] = noise->perm[i];
    }
}

static double corner(const worldgen_noise *noise, int gradient_index, double x, double y)
{
    double t = 0.5 - x * x - y * y;
    if (t < 0) {
        return 0;
    }
    const int *gradient = GRADIENTS[gradient_index % 12];
    t *= t;
    return t * t * (gradient[0] * x + gradient[1] * y);
}

double worldgen_noise_2d(const worldgen_noise *noise, double x, double y)
{
    double skew = (x + y) * F2;
    int i = (int) floor(x + skew);
    int j = (int) floor(y + skew);
    double unskew = (i + j) * G2;
    double x0 = x - (i - unskew);
    double y0 = y - (j - unskew);

    int i1 = x0 > y0 ? 1 : 0;
    int j1 = 1 - i1;

    double x1 = x0 - i1 + G2;
    double y1 = y0 - j1 + G2;
    double x2 = x0 - 1.0 + 2.0 * G2;
    double y2 = y0 - 1.0 + 2.0 * G2;

    int ii = i & 255;
    int jj = j & 255;
    const uint8_t *p = noise->perm;

    double n = corner(noise, p[ii + p[jj]], x0, y0) +
        corner(noise, p[ii + i1 + p[jj + j1]], x1, y1) +
        corner(noise, p[ii + 1 + p[jj + 1]], x2, y2);
    return 70.0 * n;
}

double worldgen_noise_fbm(const worldgen_noise *noise, double x, double y, int octaves, double lacunarity, double gain)
{
    double total = 0;
    double amplitude = 1;
    double frequency = 1;
    double normalizer = 0;
    for (int octave = 0; octave < octaves; octave++) {
        // offset each octave so the layers do not line up at the origin
        total += amplitude * worldgen_noise_2d(noise, x * frequency + octave * 17.31, y * frequency - octave * 11.97);
        normalizer += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }
    return normalizer > 0 ? total / normalizer : 0;
}

void worldgen_noise_warp(const worldgen_noise *noise, double *x, double *y, double strength, double frequency)
{
    double dx = worldgen_noise_fbm(noise, *x * frequency + 31.7, *y * frequency + 47.2, 3, 2.0, 0.5);
    double dy = worldgen_noise_fbm(noise, *x * frequency - 83.1, *y * frequency + 12.9, 3, 2.0, 0.5);
    *x += dx * strength;
    *y += dy * strength;
}
