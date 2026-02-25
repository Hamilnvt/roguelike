#include "game.h"

static inline uint64_t rotl(const uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

static uint64_t splitmix64(uint64_t *x)
{
    uint64_t z = (*x += 0x9e3779b97f4a7c15);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
    z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
    return z ^ (z >> 31);
}

void rng_init(RNG *rng, uint64_t seed)
{
    uint64_t sm_state = seed;
    rng->state[0] = splitmix64(&sm_state);
    rng->state[1] = splitmix64(&sm_state);
    rng->state[2] = splitmix64(&sm_state);
    rng->state[3] = splitmix64(&sm_state);
}

uint64_t rng_generate(RNG *rng)
{
    const uint64_t result = rotl(rng->state[1] * 5, 7) * 9;
    const uint64_t t = rng->state[1] << 17;
    rng->state[2] ^= rng->state[0];
    rng->state[3] ^= rng->state[1];
    rng->state[1] ^= rng->state[2];
    rng->state[0] ^= rng->state[3];
    rng->state[2] ^= t;
    rng->state[3] = rotl(rng->state[3], 45);
    return result;
}

double rng_generate_double(RNG *rng) { return rng_generate(rng) / ((double)UINT64_MAX + 1.0); }

bool rng_bernoulli(RNG *rng, double p) { return rng_generate_double(rng) < p; }

int64_t rng_range(RNG *rng, int64_t begin, int64_t end)
{
    assert(end >= begin);

    uint64_t range = (uint64_t)end - (uint64_t)begin;
    
    if (range == UINT64_MAX) return (int64_t)rng_generate(rng);

    uint64_t range_size = range + 1;
    
    uint64_t max_acceptable = UINT64_MAX - (UINT64_MAX % range_size);
    
    uint64_t r;
    // Rejection sampling: keep rolling until we get an unbiased number
    do {
        r = rng_generate(rng);
    } while (r == UINT64_MAX || r > max_acceptable);

    return begin + (int64_t)(r % range_size);
}

void shuffle_indices(size_t *indices, size_t count, RNG *rng)
{
    size_t tmp;
    for (size_t i = count-1; i >= 1; i--) {
        size_t j = rng_generate(rng) % i;
        tmp = indices[i];
        indices[i] = indices[j];
        indices[j] = tmp;
    }
}

