#include "field.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace blotto::fourway {

namespace {

constexpr int kAwareIdBase = 100;

// Fills the castles with nonzero weight by largest-remainder rounding, then jitters
// troops among those castles only.
Alloc concentrate(const std::array<double, kCastles>& weights, int moves, Rng& rng) {
  Alloc x;
  round_to_total(weights.data(), kCastles, kTroops, x.data());
  int targets[kCastles], n = 0;
  for (int c = 0; c < kCastles; ++c)
    if (weights[c] > 0) targets[n++] = c;
  for (; moves > 0; --moves) {
    const int from = targets[rng.below(n)], to = targets[rng.below(n)];
    if (from != to && x[from] > 0) --x[from], ++x[to];
  }
  return x;
}

}  // namespace

Alloc sample_aware(Rng& rng) {
  Alloc x{};
  const double u = rng.uniform();
  if (u < 0.45) {  // castles 4 + 5 = 9 points
    x[3] = 30 + rng.below(21);
    x[4] = kTroops - x[3];
  } else if (u < 0.70) {  // castles 3 + 5 = 8 points
    x[2] = 25 + rng.below(21);
    x[4] = kTroops - x[2];
  } else if (u < 0.85) {  // castles 2 + 3 + 4 = 9 points
    x = concentrate({0, 2, 3, 4, 0}, rng.below(12), rng);
  } else {  // castles 2 + 3 + 5 = 10 points
    x = concentrate({0, 2, 3, 0, 5}, rng.below(12), rng);
  }
  if (rng.bernoulli(0.3)) {  // hedge: a few troops onto an otherwise empty castle
    const int from = static_cast<int>(std::max_element(x.begin(), x.end()) - x.begin());
    int to;
    do to = rng.below(kCastles);
    while (x[to] != 0);
    const int h = 1 + rng.below(3);
    x[from] -= h;
    x[to] += h;
  }
  return x;
}

const std::vector<Field>& field_catalog() {
  static const std::vector<Field> catalog = [] {
    std::vector<Field> out;
    for (const auto& s : scenario_catalog()) out.push_back({s.id, s.name, s.description, s, 0.0});
    const Scenario& blend = scenario_catalog().back();  // uniform-blend
    out.push_back({kAwareIdBase, "aware-25", "uniform-blend + 25% Game-2-aware concentrators", blend, 0.25});
    out.push_back({kAwareIdBase + 1, "aware-50", "uniform-blend + 50% Game-2-aware concentrators", blend, 0.50});
    out.push_back({kAwareIdBase + 2, "aware-only", "100% Game-2-aware concentrators", blend, 1.0});
    return out;
  }();
  return catalog;
}

std::vector<Field> select_fields(const std::string& spec) {
  const auto& catalog = field_catalog();
  if (spec == "all") return catalog;
  std::vector<Field> out;
  if (spec == "game1" || spec == "aware" || spec == "pure" || spec == "mixes") {
    for (const auto& f : catalog) {
      const bool keep = spec == "game1"      ? f.aware == 0
                        : spec == "aware" ? f.aware > 0
                        : spec == "pure"  ? f.aware == 0 && f.base.pure()
                                          : f.aware > 0 || !f.base.pure();
      if (keep) out.push_back(f);
    }
    return out;
  }
  std::stringstream ss(spec);
  for (std::string name; std::getline(ss, name, ',');) {
    auto it = std::find_if(catalog.begin(), catalog.end(), [&](const Field& f) { return f.name == name; });
    if (it == catalog.end()) throw std::invalid_argument("unknown scenario: " + name);
    out.push_back(*it);
  }
  return out;
}

std::vector<Group> sample_groups(const Field& field, size_t n, Rng& rng) {
  const size_t total = kOpponents * n;
  const size_t aware = static_cast<size_t>(std::llround(field.aware * total));
  std::vector<Alloc> pool = sample_pool(field.base, total - aware, rng);
  for (size_t i = 0; i < aware; ++i) pool.push_back(sample_aware(rng));
  return group_up(std::move(pool), rng);
}

}  // namespace blotto::fourway
