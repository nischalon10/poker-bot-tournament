#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "blotto.hpp"
#include "opponents.hpp"
#include "rng.hpp"

namespace blotto::fourway {

constexpr int kPlayers = 4;
constexpr int kOpponents = kPlayers - 1;
constexpr double kPlacePayout[kPlayers] = {9, 4, 2, 0};
constexpr double kMeanPayout = 15.0 / kPlayers;  // payouts in a group always sum to 15

// Castle points are tracked in twelfths so splits among 1-4 players stay exact integers.
constexpr int kUnit = 12;

// Payout for a player with `higher` opponents strictly ahead and `equal` opponents level:
// the average payout of places higher+1 .. higher+1+equal.
constexpr double split_payout(int higher, int equal) {
  double sum = 0;
  for (int p = higher; p <= higher + equal; ++p) sum += kPlacePayout[p];
  return sum / (equal + 1);
}

inline constexpr auto kSplit = [] {
  std::array<std::array<double, kPlayers>, kPlayers> t{};
  for (int h = 0; h < kPlayers; ++h)
    for (int e = 0; h + e < kPlayers; ++e) t[h][e] = split_payout(h, e);
  return t;
}();

struct Outcome {
  std::array<double, kPlayers> points;  // castle points
  std::array<double, kPlayers> payout;  // tournament payout from the finish
};

// Reference implementation of the rules for one group; the spec the fast path is tested against.
Outcome play_group(const std::array<Alloc, kPlayers>& players);

// One group seen from our seat: per castle, the opponents' highest troop count and a
// bitmask of which opponents sent it. Everything our payout depends on, in 10 bytes.
struct Group {
  uint8_t max[kCastles];
  uint8_t mask[kCastles];
};
Group make_group(const Alloc& a, const Alloc& b, const Alloc& c);

// Running castle-point totals (twelfths) for us and the three opponents.
struct Tally {
  int me = 0;
  int opp[kOpponents] = {0, 0, 0};
};

inline constexpr int kPopcount[8] = {0, 1, 1, 2, 1, 2, 2, 3};

inline void add_castle(Tally& t, const Group& g, int castle, int troops) {
  const int value = kUnit * (castle + 1);
  const int top = g.max[castle];
  if (troops > top) {
    t.me += value;
    return;
  }
  const int mask = g.mask[castle];
  const bool tie = troops == top;
  const int share = value / (kPopcount[mask] + tie);
  if (tie) t.me += share;
  if (mask & 1) t.opp[0] += share;
  if (mask & 2) t.opp[1] += share;
  if (mask & 4) t.opp[2] += share;
}

inline void rank(const Tally& t, int& higher, int& equal) {
  higher = (t.opp[0] > t.me) + (t.opp[1] > t.me) + (t.opp[2] > t.me);
  equal = (t.opp[0] == t.me) + (t.opp[1] == t.me) + (t.opp[2] == t.me);
}

inline double finish(const Tally& t) {
  int higher, equal;
  rank(t, higher, equal);
  return kSplit[higher][equal];
}

inline Tally tally(const Alloc& x, const Group& g) {
  Tally t;
  for (int c = 0; c < kCastles; ++c) add_castle(t, g, c, x[c]);
  return t;
}

inline double group_payout(const Alloc& x, const Group& g) { return finish(tally(x, g)); }

// Shuffles a pool of 3n opponents and seats them three to a group (the shuffle keeps a
// stratified pool from putting same-archetype opponents together).
std::vector<Group> group_up(std::vector<Alloc> opponents, Rng& rng);

}  // namespace blotto::fourway
