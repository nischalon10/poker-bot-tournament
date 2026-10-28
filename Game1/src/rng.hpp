#pragma once

#include <cmath>
#include <cstdint>

namespace blotto {

// xoshiro256** with hand-written distributions. std:: distributions differ
// between standard libraries, which would make seeds non-reproducible.
class Rng {
 public:
  explicit Rng(uint64_t seed) {
    for (auto& word : s_) word = splitmix64(seed);
  }

  // Independent stream for a (seed, a, b, c) tuple, e.g. (seed, scenario, repeat, pool).
  static Rng derive(uint64_t seed, uint64_t a, uint64_t b = 0, uint64_t c = 0) {
    uint64_t h = mix(seed);
    h = mix(h ^ mix(a + 0x9E3779B97F4A7C15ULL));
    h = mix(h ^ mix(b + 0xBF58476D1CE4E5B9ULL));
    h = mix(h ^ mix(c + 0x94D049BB133111EBULL));
    return Rng(h);
  }

  uint64_t next() {
    const uint64_t result = rotl(s_[1] * 5, 7) * 9;
    const uint64_t t = s_[1] << 17;
    s_[2] ^= s_[0];
    s_[3] ^= s_[1];
    s_[1] ^= s_[2];
    s_[0] ^= s_[3];
    s_[2] ^= t;
    s_[3] = rotl(s_[3], 45);
    return result;
  }

  double uniform() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }
  double uniform(double lo, double hi) { return lo + (hi - lo) * uniform(); }

  // Uniform integer in [0, n). Multiply-shift; bias < n / 2^32, negligible for n <= 101.
  int below(int n) {
    return static_cast<int>(((next() >> 32) * static_cast<uint64_t>(n)) >> 32);
  }

  bool bernoulli(double p) { return uniform() < p; }

  // Box-Muller; 1 - u avoids log(0).
  double normal() {
    const double u1 = 1.0 - uniform();
    const double u2 = uniform();
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
  }

  // Marsaglia-Tsang Gamma(shape, 1).
  double gamma(double shape) {
    if (shape < 1.0) {
      const double u = 1.0 - uniform();
      return gamma(shape + 1.0) * std::pow(u, 1.0 / shape);
    }
    const double d = shape - 1.0 / 3.0;
    const double c = 1.0 / std::sqrt(9.0 * d);
    for (;;) {
      double x, v;
      do {
        x = normal();
        v = 1.0 + c * x;
      } while (v <= 0.0);
      v = v * v * v;
      const double u = 1.0 - uniform();
      if (std::log(u) < 0.5 * x * x + d - d * v + d * std::log(v)) return d * v;
    }
  }

 private:
  static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
  static uint64_t mix(uint64_t z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }
  static uint64_t splitmix64(uint64_t& state) {
    state += 0x9E3779B97F4A7C15ULL;
    return mix(state);
  }

  uint64_t s_[4];
};

}  // namespace blotto
