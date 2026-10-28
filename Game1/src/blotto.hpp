#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace blotto {

constexpr int kCastles = 5;
constexpr int kTroops = 100;
constexpr double kTotalPoints = 15.0;  // 1 + 2 + 3 + 4 + 5

// Castle i (0-based) is worth i + 1 points.
using Alloc = std::array<int, kCastles>;
static_assert(sizeof(Alloc) == kCastles * sizeof(int), "pools are read as flat int arrays");

// Five nonnegative integers summing to exactly 100.
bool is_valid(const Alloc& x);

// Twice the matchup payoff P(x, y), so every payoff is an exact integer in [0, 30].
inline int payoff2(const Alloc& x, const Alloc& y) {
  int p = 0;
  for (int i = 0; i < kCastles; ++i) {
    const int value = i + 1;
    p += x[i] > y[i] ? 2 * value : (x[i] == y[i] ? value : 0);
  }
  return p;
}

// P(x, y): i points per castle won, i/2 per castle tied.
inline double payoff(const Alloc& x, const Alloc& y) { return 0.5 * payoff2(x, y); }

std::string format(const Alloc& x);  // "(0,13,21,30,36)"

// Accepts "0,13,21,30,36" with optional parentheses/spaces; nullopt unless valid.
std::optional<Alloc> parse_alloc(std::string_view text);

}  // namespace blotto
