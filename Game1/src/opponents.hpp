#pragma once

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "blotto.hpp"
#include "rng.hpp"

namespace blotto {

// Opponent archetypes. These are modelling hypotheses, not observed applicant data.
enum Archetype : int {
  kProportional,  // near (7,13,20,27,33)
  kEqual,         // near (20,20,20,20,20), often round numbers
  kValueFocused,  // weights value^alpha, alpha in [1.5, 3]
  kSacrificeLow,  // castle 1 at 0..5, rest weighted by value
  kDispersed,     // Dirichlet probabilities + multinomial draw
  kStrategic,     // jittered copies of plausible strong picks (crowding)
  kArchetypeCount
};

const char* archetype_name(int archetype);
Alloc sample_archetype(int archetype, Rng& rng);

// A hypothesised applicant population: a weighted mix of archetypes.
struct Scenario {
  int id;  // stable RNG stream id, independent of which scenarios are selected
  std::string name;
  std::string description;
  std::array<double, kArchetypeCount> weights;

  bool pure() const;
};

const std::vector<Scenario>& scenario_catalog();

// "all" | "pure" | "mixes" | comma-separated scenario names. Throws on unknown names.
std::vector<Scenario> select_scenarios(const std::string& spec);

// Stratified sample: archetype counts are fixed by largest-remainder rounding of
// n * weights, so pool-to-pool noise comes only from within-archetype draws.
std::vector<Alloc> sample_pool(const Scenario& scenario, size_t n, Rng& rng);

// The "known strategies" the strategic archetype imitates, with sampling weights.
const std::vector<std::pair<Alloc, double>>& strategic_bases();

// Low-level helpers, exposed for tests.
// Largest-remainder rounding of total * w / sum(w); the result sums to `total`.
void round_to_total(const double* weights, int n, int total, int* out);
// Clips negatives to 0, then adds/removes single troops at random until sum == total.
void repair_sum(int* x, int n, int total, Rng& rng);
// `moves` random single-troop transfers between castles (keeps the sum).
void jitter(Alloc& x, int moves, Rng& rng);
// `trials` categorical draws with (unnormalised) probabilities p[0..n).
void multinomial(const double* p, int n, int trials, int* out, Rng& rng);

}  // namespace blotto
