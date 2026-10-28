#pragma once

#include <array>
#include <functional>
#include <vector>

#include "fourway.hpp"

namespace blotto::fourway {

struct Scored {
  Alloc alloc;
  double value;
};

// Strict total order: higher value first, then lexicographic allocation (thread-independent).
inline bool better(const Scored& a, const Scored& b) {
  return a.value != b.value ? a.value > b.value : a.alloc < b.alloc;
}

double mean_payout(const Alloc& x, const std::vector<Group>& groups);

// Payout mean, 95% half-width, castle-point mean and finish-place shares (ties split evenly).
struct Evaluation {
  double payout = 0;
  double ci_half = 0;
  double castle_points = 0;
  std::array<double, kPlayers> place{};
};
Evaluation evaluate(const Alloc& x, const std::vector<Group>& groups);

// Scores every legal allocation (C(104,4) = 4,598,126) on `groups` and returns the best
// `keep`, best first. Nothing but the survivors is stored.
std::vector<Scored> screen_all(const std::vector<Group>& groups, size_t keep, int threads,
                               size_t* visited = nullptr);

// Scores `xs` on `groups`; returns them best first.
std::vector<Scored> score_all(const std::vector<Alloc>& xs, const std::vector<Group>& groups, int threads);

// Best-improvement hill climbing over transfers of d troops between two castles.
// `objective` must be thread-safe; higher is better.
Scored hill_climb(const Alloc& start, const std::function<double(const Alloc&)>& objective, int threads);

}  // namespace blotto::fourway
