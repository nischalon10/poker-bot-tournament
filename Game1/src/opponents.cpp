#include "opponents.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace blotto {

namespace {

constexpr Alloc kProportionalBase{7, 13, 20, 27, 33};
constexpr Alloc kEqualBase{20, 20, 20, 20, 20};

void add_gaussian_noise(int* x, int n, double sigma, Rng& rng) {
  for (int i = 0; i < n; ++i) x[i] = static_cast<int>(std::lround(x[i] + sigma * rng.normal()));
}

// Move `step` troops from a random castle holding at least `step` to a different castle.
void transfer(Alloc& x, int step, Rng& rng) {
  int from;
  do from = rng.below(kCastles);
  while (x[from] < step);
  int to;
  do to = rng.below(kCastles);
  while (to == from);
  x[from] -= step;
  x[to] += step;
}

Alloc sample_proportional(Rng& rng) {
  Alloc x = kProportionalBase;
  if (rng.bernoulli(0.3)) return x;  // exact proportional entries
  add_gaussian_noise(x.data(), kCastles, rng.uniform(0.5, 3.0), rng);
  repair_sum(x.data(), kCastles, kTroops, rng);
  return x;
}

Alloc sample_equal(Rng& rng) {
  Alloc x = kEqualBase;
  const double u = rng.uniform();
  if (u < 0.4) return x;
  if (u < 0.75) {  // round-number variants: shift blocks of 5 or 10
    const int step = rng.bernoulli(0.6) ? 5 : 10;
    for (int m = 1 + rng.below(3); m > 0; --m) transfer(x, step, rng);
    return x;
  }
  add_gaussian_noise(x.data(), kCastles, rng.uniform(1.0, 4.0), rng);
  repair_sum(x.data(), kCastles, kTroops, rng);
  return x;
}

Alloc sample_value_focused(Rng& rng) {
  const double alpha = rng.uniform(1.5, 3.0);
  double w[kCastles];
  for (int i = 0; i < kCastles; ++i) w[i] = std::pow(i + 1.0, alpha);
  Alloc x;
  if (rng.bernoulli(0.5)) {
    round_to_total(w, kCastles, kTroops, x.data());
    jitter(x, rng.below(4), rng);
  } else {
    multinomial(w, kCastles, kTroops, x.data(), rng);
  }
  return x;
}

Alloc sample_sacrifice_low(Rng& rng) {
  Alloc x{};
  x[0] = rng.bernoulli(0.6) ? 0 : 1 + rng.below(5);
  const int rest = kTroops - x[0];
  const double alpha = rng.uniform(0.8, 2.0);
  double w[kCastles - 1];
  for (int i = 0; i < kCastles - 1; ++i) w[i] = std::pow(i + 2.0, alpha);
  round_to_total(w, kCastles - 1, rest, x.data() + 1);
  add_gaussian_noise(x.data() + 1, kCastles - 1, rng.uniform(0.0, 3.0), rng);
  repair_sum(x.data() + 1, kCastles - 1, rest, rng);
  return x;
}

Alloc sample_dispersed(Rng& rng) {
  // Log-uniform concentration: small values give spiky allocations, large ones broad.
  const double concentration = std::exp(rng.uniform(std::log(0.3), std::log(10.0)));
  double p[kCastles];
  double sum = 0;
  for (double& v : p) sum += (v = rng.gamma(concentration));
  if (!(sum > 0)) std::fill(std::begin(p), std::end(p), 1.0);
  Alloc x;
  multinomial(p, kCastles, kTroops, x.data(), rng);
  return x;
}

Alloc sample_strategic(Rng& rng) {
  const auto& bases = strategic_bases();
  double total = 0;
  for (const auto& b : bases) total += b.second;
  double u = rng.uniform() * total;
  size_t pick = 0;
  while (pick + 1 < bases.size() && u >= bases[pick].second) u -= bases[pick++].second;
  Alloc x = bases[pick].first;
  jitter(x, rng.below(5), rng);
  return x;
}

}  // namespace

const char* archetype_name(int archetype) {
  static const char* names[kArchetypeCount] = {"proportional", "equal",     "value",
                                               "sacrifice",    "dispersed", "strategic"};
  return names[archetype];
}

Alloc sample_archetype(int archetype, Rng& rng) {
  switch (archetype) {
    case kProportional: return sample_proportional(rng);
    case kEqual: return sample_equal(rng);
    case kValueFocused: return sample_value_focused(rng);
    case kSacrificeLow: return sample_sacrifice_low(rng);
    case kDispersed: return sample_dispersed(rng);
    case kStrategic: return sample_strategic(rng);
    default: throw std::invalid_argument("unknown archetype");
  }
}

const std::vector<std::pair<Alloc, double>>& strategic_bases() {
  static const std::vector<std::pair<Alloc, double>> bases = {
      {{0, 13, 21, 30, 36}, 3.0}, {{0, 14, 22, 29, 35}, 3.0}, {{0, 10, 20, 30, 40}, 2.0},
      {{0, 15, 20, 30, 35}, 2.0}, {{1, 12, 21, 30, 36}, 1.0}, {{0, 12, 22, 31, 35}, 1.0},
      {{2, 11, 21, 31, 35}, 1.0}, {{0, 0, 25, 35, 40}, 1.0},
  };
  return bases;
}

bool Scenario::pure() const {
  return std::count(weights.begin(), weights.end(), 1.0) == 1;
}

const std::vector<Scenario>& scenario_catalog() {
  // Weight order: proportional, equal, value, sacrifice, dispersed, strategic.
  constexpr double k6 = 1.0 / 6.0;
  static const std::vector<Scenario> catalog = {
      {0, "proportional", "pure archetype: near (7,13,20,27,33)", {1, 0, 0, 0, 0, 0}},
      {1, "equal", "pure archetype: near (20,20,20,20,20), round numbers", {0, 1, 0, 0, 0, 0}},
      {2, "value", "pure archetype: value^alpha weights, alpha 1.5-3", {0, 0, 1, 0, 0, 0}},
      {3, "sacrifice", "pure archetype: castle 1 at 0-5, rest by value", {0, 0, 0, 1, 0, 0}},
      {4, "dispersed", "pure archetype: Dirichlet + multinomial", {0, 0, 0, 0, 1, 0}},
      {5, "strategic", "pure archetype: jittered copies of known picks", {0, 0, 0, 0, 0, 1}},
      {6, "naive-heavy", "mix: mostly proportional / equal / random", {.35, .25, .10, .05, .15, .10}},
      {7, "strategic-heavy", "mix: mostly sacrifice / imitation", {.10, .05, .15, .25, .05, .40}},
      {8, "value-heavy", "mix: mostly value-weighted", {.15, .05, .40, .20, .10, .10}},
      {9, "uniform-blend", "mix: equal weight on all six archetypes", {k6, k6, k6, k6, k6, k6}},
  };
  return catalog;
}

std::vector<Scenario> select_scenarios(const std::string& spec) {
  const auto& catalog = scenario_catalog();
  std::vector<Scenario> out;
  if (spec == "all") return catalog;
  if (spec == "pure" || spec == "mixes") {
    for (const auto& s : catalog)
      if (s.pure() == (spec == "pure")) out.push_back(s);
    return out;
  }
  std::stringstream ss(spec);
  for (std::string name; std::getline(ss, name, ',');) {
    auto it = std::find_if(catalog.begin(), catalog.end(),
                           [&](const Scenario& s) { return s.name == name; });
    if (it == catalog.end()) throw std::invalid_argument("unknown scenario: " + name);
    out.push_back(*it);
  }
  return out;
}

std::vector<Alloc> sample_pool(const Scenario& scenario, size_t n, Rng& rng) {
  int counts[kArchetypeCount];
  round_to_total(scenario.weights.data(), kArchetypeCount, static_cast<int>(n), counts);
  std::vector<Alloc> pool;
  pool.reserve(n);
  for (int a = 0; a < kArchetypeCount; ++a)
    for (int k = 0; k < counts[a]; ++k) pool.push_back(sample_archetype(a, rng));
  return pool;
}

void round_to_total(const double* weights, int n, int total, int* out) {
  double sum = 0;
  for (int i = 0; i < n; ++i) sum += weights[i];
  std::vector<std::pair<double, int>> remainders;
  int assigned = 0;
  for (int i = 0; i < n; ++i) {
    const double exact = total * weights[i] / sum;
    out[i] = static_cast<int>(std::floor(exact));
    assigned += out[i];
    remainders.push_back({exact - out[i], i});
  }
  std::stable_sort(remainders.begin(), remainders.end(),
                   [](const auto& a, const auto& b) { return a.first > b.first; });
  for (size_t k = 0; assigned < total; ++k, ++assigned) ++out[remainders[k % n].second];
}

void repair_sum(int* x, int n, int total, Rng& rng) {
  int sum = 0;
  for (int i = 0; i < n; ++i) sum += (x[i] = std::max(x[i], 0));
  for (; sum < total; ++sum) ++x[rng.below(n)];
  while (sum > total) {
    const int i = rng.below(n);
    if (x[i] > 0) {
      --x[i];
      --sum;
    }
  }
}

void jitter(Alloc& x, int moves, Rng& rng) {
  for (; moves > 0; --moves) transfer(x, 1, rng);
}

void multinomial(const double* p, int n, int trials, int* out, Rng& rng) {
  double cumulative[kCastles];
  if (n > kCastles) throw std::invalid_argument("multinomial supports at most kCastles bins");
  double sum = 0;
  for (int i = 0; i < n; ++i) {
    cumulative[i] = (sum += p[i]);
    out[i] = 0;
  }
  for (int t = 0; t < trials; ++t) {
    const double u = rng.uniform() * sum;
    int i = 0;
    while (i < n - 1 && u >= cumulative[i]) ++i;
    ++out[i];
  }
}

}  // namespace blotto
