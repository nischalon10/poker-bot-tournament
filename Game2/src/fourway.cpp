#include "fourway.hpp"

#include <algorithm>
#include <utility>

namespace blotto::fourway {

Outcome play_group(const std::array<Alloc, kPlayers>& players) {
  std::array<int, kPlayers> points{};  // twelfths
  for (int c = 0; c < kCastles; ++c) {
    int top = 0;
    for (const auto& p : players) top = std::max(top, p[c]);
    int winners = 0;
    for (const auto& p : players) winners += p[c] == top;
    for (int i = 0; i < kPlayers; ++i)
      if (players[i][c] == top) points[i] += kUnit * (c + 1) / winners;
  }
  Outcome out{};
  for (int i = 0; i < kPlayers; ++i) {
    int higher = 0, equal = 0;
    for (int j = 0; j < kPlayers; ++j) {
      if (j == i) continue;
      higher += points[j] > points[i];
      equal += points[j] == points[i];
    }
    out.points[i] = static_cast<double>(points[i]) / kUnit;
    out.payout[i] = split_payout(higher, equal);
  }
  return out;
}

Group make_group(const Alloc& a, const Alloc& b, const Alloc& c) {
  Group g{};
  for (int i = 0; i < kCastles; ++i) {
    const int top = std::max({a[i], b[i], c[i]});
    g.max[i] = static_cast<uint8_t>(top);
    g.mask[i] = static_cast<uint8_t>((a[i] == top) | (b[i] == top) << 1 | (c[i] == top) << 2);
  }
  return g;
}

std::vector<Group> group_up(std::vector<Alloc> pool, Rng& rng) {
  for (size_t i = pool.size(); i > 1; --i) std::swap(pool[i - 1], pool[rng.below(static_cast<int>(i))]);
  std::vector<Group> groups;
  groups.reserve(pool.size() / kOpponents);
  for (size_t g = 0; g + kOpponents <= pool.size(); g += kOpponents)
    groups.push_back(make_group(pool[g], pool[g + 1], pool[g + 2]));
  return groups;
}

}  // namespace blotto::fourway
