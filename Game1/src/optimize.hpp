#pragma once

#include <cstddef>
#include <vector>

#include "blotto.hpp"

namespace blotto {

// Marginal-payoff table for an opponent pool:
//   M[i][k] = value_i * (count(y_i < k) + 0.5 * count(y_i == k)) / N
// The empirical mean payoff of x against the pool is then exactly sum_i M[i][x_i],
// so scoring a candidate costs 5 lookups instead of N matchups.
struct MarginalTable {
  int castles = 0;
  int total = 0;
  std::vector<double> m;  // row-major, castles x (total + 1)

  double at(int castle, int troops) const { return m[castle * (total + 1) + troops]; }
  double score(const int* x) const;
  double score(const Alloc& x) const { return score(x.data()); }
};

// `pool` holds n allocations of `castles` ints each, back to back.
MarginalTable build_marginals(const int* pool, size_t n, int castles, int total);
MarginalTable build_marginals(const std::vector<Alloc>& pool);
// From per-castle troop-count histograms: counts[i * (total + 1) + k] over n opponents.
MarginalTable marginals_from_counts(const std::vector<double>& counts, double n, int castles,
                                    int total);

struct Best {
  std::vector<int> alloc;
  double score = 0;
};

// Exact best pure allocation (summing to t.total) against the table, by dynamic programming.
Best dp_best(const MarginalTable& t);
Alloc dp_best_alloc(const MarginalTable& t);
// Exhaustive enumeration; only for checking the DP on small instances.
Best brute_force_best(const MarginalTable& t);

struct Ranked {
  Alloc alloc;
  double value;
};

struct RobustResult {
  std::vector<double> best;            // per-table optimum (from the DP)
  std::vector<Ranked> maximin;         // highest worst-case score first
  std::vector<Ranked> minimax_regret;  // lowest worst-case regret first
  size_t visited = 0;                  // legal allocations enumerated
};

// Streams through all C(104,4) = 4,598,126 legal allocations (none stored) and ranks
// them by worst-case score and by worst-case regret (best_s - score_s) over `tables`.
RobustResult robust_search(const std::vector<const MarginalTable*>& tables, int threads,
                           int top_k = 5);

struct FictitiousPlay {
  int iterations = 0;
  MarginalTable mixture;           // marginals of the empirical mixed strategy
  std::vector<Ranked> support;     // most frequent pure strategies (value = probability)
  double exploitability = 0;       // best-response score vs the mixture, minus 7.5
  std::vector<std::pair<int, double>> trace;  // (iteration, exploitability)
};

// Symmetric fictitious play: each round adds an exact DP best response to the
// empirical mixture of all previous rounds. A benchmark, not a field prediction.
FictitiousPlay fictitious_play(int iterations, const Alloc& start);

}  // namespace blotto
