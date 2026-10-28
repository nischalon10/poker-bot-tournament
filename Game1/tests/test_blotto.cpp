// Self-contained test runner (no framework dependency). Exit code 0 = all passed.
#include <cmath>
#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "../src/blotto.hpp"
#include "../src/opponents.hpp"
#include "../src/optimize.hpp"

using namespace blotto;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    ++g_checks;                                                              \
    if (!(cond)) {                                                           \
      ++g_failures;                                                          \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
    }                                                                        \
  } while (0)

#define CHECK_NEAR(a, b, tol) CHECK(std::abs((a) - (b)) <= (tol))

Alloc random_alloc(Rng& rng) {
  Alloc x{};
  for (int t = 0; t < kTroops; ++t) ++x[rng.below(kCastles)];
  if (rng.bernoulli(0.5)) x = sample_archetype(rng.below(kArchetypeCount), rng);
  return x;
}

void test_sanity_checks() {
  CHECK(payoff({0, 13, 21, 30, 36}, {0, 14, 22, 29, 35}) == 9.5);
  CHECK(payoff({0, 13, 21, 30, 36}, {7, 13, 20, 27, 33}) == 13.0);
  CHECK(payoff({0, 14, 22, 29, 35}, {7, 13, 20, 27, 33}) == 14.0);
  CHECK(payoff({0, 13, 21, 30, 36}, {0, 13, 21, 30, 36}) == 7.5);
}

void test_payoff_identities() {
  Rng rng(7);
  bool ok_sum = true, ok_self = true;
  for (int i = 0; i < 200000; ++i) {
    const Alloc x = random_alloc(rng), y = random_alloc(rng);
    ok_sum &= payoff(x, y) + payoff(y, x) == kTotalPoints;
    ok_self &= payoff(x, x) == 7.5;
  }
  CHECK(ok_sum);
  CHECK(ok_self);
}

void test_validation_and_parsing() {
  CHECK(is_valid({20, 20, 20, 20, 20}));
  CHECK(!is_valid({20, 20, 20, 20, 21}));
  CHECK(!is_valid({-1, 21, 20, 30, 30}));
  CHECK(parse_alloc("0,13,21,30,36") == (Alloc{0, 13, 21, 30, 36}));
  CHECK(parse_alloc("(0, 13, 21, 30, 36)") == (Alloc{0, 13, 21, 30, 36}));
  CHECK(!parse_alloc("0,13,21,30"));
  CHECK(!parse_alloc("0,13,21,30,37"));
  CHECK(!parse_alloc("-1,14,21,30,36"));
  CHECK(format({0, 13, 21, 30, 36}) == "(0,13,21,30,36)");
}

void test_generators_valid() {
  for (int a = 0; a < kArchetypeCount; ++a) {
    Rng rng = Rng::derive(42, a);
    bool ok = true;
    std::set<Alloc> distinct;
    for (int i = 0; i < 50000; ++i) {
      const Alloc x = sample_archetype(a, rng);
      ok &= is_valid(x);
      distinct.insert(x);
    }
    if (!ok) std::printf("  archetype %s produced an invalid allocation\n", archetype_name(a));
    CHECK(ok);
    CHECK(distinct.size() > 5);  // every archetype has some spread
  }
  for (const auto& [x, w] : strategic_bases()) CHECK(is_valid(x) && w > 0);
}

void test_pools() {
  for (const auto& s : scenario_catalog()) {
    Rng rng = Rng::derive(1, s.id);
    const auto pool = sample_pool(s, 9999, rng);
    CHECK(pool.size() == 9999);
    bool ok = true;
    for (const auto& x : pool) ok &= is_valid(x);
    CHECK(ok);
  }
  // Reproducible streams.
  Rng a = Rng::derive(42, 3, 0, 1), b = Rng::derive(42, 3, 0, 1), c = Rng::derive(43, 3, 0, 1);
  const auto& sc = scenario_catalog()[9];
  CHECK(sample_pool(sc, 500, a) == sample_pool(sc, 500, b));
  CHECK(sample_pool(sc, 500, a) != sample_pool(sc, 500, c));
  CHECK(select_scenarios("pure").size() == 6);
  CHECK(select_scenarios("mixes").size() == 4);
  CHECK(select_scenarios("equal,uniform-blend").size() == 2);
}

void test_helpers() {
  Rng rng(3);
  for (int t = 0; t < 1000; ++t) {
    double w[kCastles];
    for (double& v : w) v = rng.uniform(0.01, 5);
    int out[kCastles];
    const int total = rng.below(200);
    round_to_total(w, kCastles, total, out);
    int sum = 0;
    for (int v : out) sum += v;
    CHECK(sum == total);

    int x[kCastles];
    for (int& v : x) v = rng.below(60) - 10;
    repair_sum(x, kCastles, kTroops, rng);
    sum = 0;
    bool nonneg = true;
    for (int v : x) sum += v, nonneg &= v >= 0;
    CHECK(sum == kTroops && nonneg);
  }
}

void test_marginal_identity() {
  Rng rng(11);
  std::vector<Alloc> pool;
  for (int i = 0; i < 3000; ++i) pool.push_back(random_alloc(rng));
  const MarginalTable t = build_marginals(pool);
  for (int k = 0; k < 200; ++k) {
    const Alloc x = random_alloc(rng);
    double direct = 0;
    for (const auto& y : pool) direct += payoff(x, y);
    CHECK_NEAR(t.score(x), direct / pool.size(), 1e-9);
  }
}

// DP vs brute force on reduced toy instances (3 castles, 8 troops) and others.
void test_dp_vs_brute_force() {
  Rng rng(5);
  for (const auto [castles, total] : std::vector<std::pair<int, int>>{{3, 8}, {4, 10}, {5, 12}, {2, 7}}) {
    for (int trial = 0; trial < 200; ++trial) {
      const int n = 1 + rng.below(40);
      std::vector<int> pool(n * castles);
      for (int j = 0; j < n; ++j) {
        std::vector<int> y(castles, 0);
        for (int t = 0; t < total; ++t) ++y[rng.below(castles)];
        for (int c = 0; c < castles; ++c) pool[j * castles + c] = y[c];
      }
      const MarginalTable t = build_marginals(pool.data(), n, castles, total);
      const Best dp = dp_best(t), bf = brute_force_best(t);
      CHECK_NEAR(dp.score, bf.score, 1e-12);
      int sum = 0;
      for (int v : dp.alloc) sum += v;
      CHECK(sum == total);
      CHECK_NEAR(t.score(dp.alloc.data()), dp.score, 1e-12);
    }
  }
}

// Exhaustive enumeration agrees with the DP on the full 5-castle, 100-troop game.
void test_robust_search() {
  Rng rng = Rng::derive(42, 9);
  const auto pool_a = sample_pool(scenario_catalog()[9], 5000, rng);
  const auto pool_b = sample_pool(scenario_catalog()[1], 5000, rng);
  const MarginalTable a = build_marginals(pool_a), b = build_marginals(pool_b);

  const RobustResult single = robust_search({&a}, 0);
  CHECK(single.visited == 4598126);  // C(104, 4)
  CHECK_NEAR(single.maximin[0].value, dp_best(a).score, 1e-12);
  CHECK_NEAR(single.minimax_regret[0].value, 0.0, 1e-12);

  const RobustResult two = robust_search({&a, &b}, 0);
  const Alloc m = two.maximin[0].alloc;
  CHECK(is_valid(m));
  CHECK_NEAR(two.maximin[0].value, std::min(a.score(m), b.score(m)), 1e-12);
  const Alloc r = two.minimax_regret[0].alloc;
  CHECK_NEAR(two.minimax_regret[0].value, std::max(two.best[0] - a.score(r), two.best[1] - b.score(r)), 1e-12);
  // Thread count must not change the answer.
  const RobustResult serial = robust_search({&a, &b}, 1);
  CHECK(serial.maximin[0].alloc == m && serial.minimax_regret[0].alloc == r);
}

void test_fictitious_play() {
  const FictitiousPlay fp = fictitious_play(400, {0, 13, 21, 30, 36});
  CHECK(fp.exploitability >= -1e-12);
  CHECK(!fp.support.empty());
  double total = 0;
  for (const auto& s : fp.support) {
    CHECK(is_valid(s.alloc));
    total += s.value;
  }
  CHECK(total <= 1.0 + 1e-12);
  // Each pure strategy is beaten by its exact best response.
  const MarginalTable single = build_marginals(std::vector<Alloc>{{0, 13, 21, 30, 36}});
  CHECK(dp_best(single).score > 7.5);
}

}  // namespace

int main() {
  const std::vector<std::pair<const char*, std::function<void()>>> tests = {
      {"sanity checks", test_sanity_checks},
      {"payoff identities", test_payoff_identities},
      {"validation and parsing", test_validation_and_parsing},
      {"generators produce valid allocations", test_generators_valid},
      {"pools and scenario selection", test_pools},
      {"rounding and repair helpers", test_helpers},
      {"marginal table equals direct average", test_marginal_identity},
      {"DP agrees with brute force (toy instances)", test_dp_vs_brute_force},
      {"exhaustive robust search", test_robust_search},
      {"fictitious play", test_fictitious_play},
  };
  for (const auto& [name, fn] : tests) {
    const int before = g_failures;
    fn();
    std::printf("%s %s\n", g_failures == before ? "[ok]  " : "[FAIL]", name);
  }
  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
