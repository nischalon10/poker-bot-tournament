// Self-contained test runner (no framework dependency). Exit code 0 = all passed.
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "field.hpp"
#include "fourway.hpp"
#include "search.hpp"

using namespace blotto;
using namespace blotto::fourway;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

#define CHECK_NEAR(a, b, tol) CHECK(std::abs((a) - (b)) <= (tol))

Alloc random_alloc(Rng& rng) {
  if (rng.bernoulli(0.5)) return sample_archetype(rng.below(kArchetypeCount), rng);
  Alloc x{};
  for (int t = 0; t < kTroops; ++t) ++x[rng.below(kCastles)];
  return x;
}

void test_split_table() {
  CHECK(kSplit[0][0] == 9);
  CHECK(kSplit[0][1] == 6.5);   // two tied for 1st split 9 + 4
  CHECK(kSplit[0][2] == 5);     // three tied for 1st split 9 + 4 + 2
  CHECK(kSplit[0][3] == 3.75);  // four-way tie
  CHECK(kSplit[1][0] == 4);
  CHECK(kSplit[1][1] == 3);
  CHECK(kSplit[1][2] == 2);
  CHECK(kSplit[2][0] == 2);
  CHECK(kSplit[2][1] == 1);
  CHECK(kSplit[3][0] == 0);
}

void test_hand_examples() {
  // Spread beats three identical all-in-on-5 players: 10 pts vs 5/3 each.
  Outcome o = play_group({Alloc{20, 20, 20, 20, 20}, Alloc{0, 0, 0, 0, 100}, Alloc{0, 0, 0, 0, 100},
                          Alloc{0, 0, 0, 0, 100}});
  CHECK(o.points[0] == 10);
  CHECK_NEAR(o.points[1], 5.0 / 3, 1e-12);
  CHECK(o.payout[0] == 9);
  CHECK(o.payout[1] == 2 && o.payout[2] == 2 && o.payout[3] == 2);  // tie for 2nd-4th: (4+2+0)/3

  // Two tied for 1st with 4.5 pts each, two tied for 3rd with 3 pts each.
  o = play_group({Alloc{0, 0, 0, 50, 50}, Alloc{0, 0, 0, 50, 50}, Alloc{25, 25, 25, 25, 0},
                  Alloc{25, 25, 25, 25, 0}});
  CHECK(o.points[0] == 4.5 && o.points[2] == 3);
  CHECK(o.payout[0] == 6.5 && o.payout[1] == 6.5);
  CHECK(o.payout[2] == 1 && o.payout[3] == 1);

  // All identical: everyone splits evenly.
  const Alloc a{0, 13, 21, 30, 36};
  o = play_group({a, a, a, a});
  for (int i = 0; i < kPlayers; ++i) CHECK(o.payout[i] == 3.75 && o.points[i] == 3.75);
}

void test_identities_and_fast_path() {
  Rng rng(17);
  bool sums_ok = true, points_ok = true, fast_ok = true;
  for (int i = 0; i < 300000; ++i) {
    const std::array<Alloc, kPlayers> p = {random_alloc(rng), random_alloc(rng), random_alloc(rng),
                                           random_alloc(rng)};
    const Outcome o = play_group(p);
    double pay = 0, pts = 0;
    for (int k = 0; k < kPlayers; ++k) pay += o.payout[k], pts += o.points[k];
    sums_ok &= pay == 15;
    points_ok &= std::abs(pts - 15) < 1e-9;
    const Group g = make_group(p[1], p[2], p[3]);
    const Tally t = tally(p[0], g);
    fast_ok &= group_payout(p[0], g) == o.payout[0];
    fast_ok &= std::abs(t.me / double(kUnit) - o.points[0]) < 1e-12;
    for (int k = 0; k < kOpponents; ++k) fast_ok &= std::abs(t.opp[k] / double(kUnit) - o.points[k + 1]) < 1e-12;
  }
  CHECK(sums_ok);    // payouts always sum to 9 + 4 + 2 + 0
  CHECK(points_ok);  // castle points always sum to 15
  CHECK(fast_ok);    // precomputed-group evaluator matches the reference rules
}

void test_groups() {
  const auto& sc = field_catalog()[9];
  Rng a = Rng::derive(1, 2), b = Rng::derive(1, 2);
  const auto ga = sample_groups(sc, 1000, a), gb = sample_groups(sc, 1000, b);
  CHECK(ga.size() == 1000);
  bool same = true, masks_ok = true;
  for (size_t i = 0; i < ga.size(); ++i)
    for (int c = 0; c < kCastles; ++c) {
      same &= ga[i].max[c] == gb[i].max[c] && ga[i].mask[c] == gb[i].mask[c];
      masks_ok &= ga[i].mask[c] >= 1 && ga[i].mask[c] <= 7 && ga[i].max[c] <= kTroops;
    }
  CHECK(same);
  CHECK(masks_ok);

  Rng rng(21);
  bool aware_ok = true;
  for (int i = 0; i < 100000; ++i) aware_ok &= is_valid(sample_aware(rng));
  CHECK(aware_ok);
  CHECK(select_fields("all").size() == 13);
  CHECK(select_fields("game1").size() == 10);
  CHECK(select_fields("aware").size() == 3);
  for (const auto& f : select_fields("aware")) {
    Rng r = Rng::derive(4, f.id);
    CHECK(sample_groups(f, 777, r).size() == 777);
  }
}

void test_evaluate() {
  Rng rng(3);
  const auto groups = sample_groups(field_catalog()[9], 5000, rng);
  const Alloc x{1, 0, 21, 31, 47};
  const Evaluation e = evaluate(x, groups);
  CHECK_NEAR(e.payout, mean_payout(x, groups), 1e-12);
  CHECK_NEAR(e.place[0] + e.place[1] + e.place[2] + e.place[3], 1.0, 1e-9);
  CHECK_NEAR(9 * e.place[0] + 4 * e.place[1] + 2 * e.place[2], e.payout, 1e-9);
}

void test_screen() {
  Rng rng(5);
  const auto groups = sample_groups(field_catalog()[9], 40, rng);
  size_t visited = 0;
  const auto top = screen_all(groups, 50, 0, &visited);
  CHECK(visited == 4598126);  // C(104, 4)
  CHECK(top.size() == 50);
  for (size_t i = 0; i < top.size(); ++i) {
    CHECK(is_valid(top[i].alloc));
    CHECK_NEAR(top[i].value, mean_payout(top[i].alloc, groups), 1e-12);
    if (i) CHECK(!better(top[i], top[i - 1]));
  }
  // Nothing outside the screen beats its winner.
  Rng r2(9);
  bool ok = true;
  for (int i = 0; i < 20000; ++i) ok &= mean_payout(random_alloc(r2), groups) <= top[0].value;
  CHECK(ok);
  // Thread count must not change the result.
  const auto serial = screen_all(groups, 50, 1);
  CHECK(serial[0].alloc == top[0].alloc && serial[49].alloc == top[49].alloc);
  // Hill climbing never gets worse and stays legal.
  const Scored climbed = hill_climb({20, 20, 20, 20, 20}, [&](const Alloc& x) { return mean_payout(x, groups); }, 0);
  CHECK(is_valid(climbed.alloc) && climbed.value >= mean_payout({20, 20, 20, 20, 20}, groups));
}

}  // namespace

int main() {
  const std::vector<std::pair<const char*, std::function<void()>>> tests = {
      {"split payout table", test_split_table},
      {"hand-worked groups", test_hand_examples},
      {"identities and fast evaluator vs reference", test_identities_and_fast_path},
      {"group sampling", test_groups},
      {"evaluation summaries", test_evaluate},
      {"exhaustive screen and hill climbing", test_screen},
  };
  for (const auto& [name, fn] : tests) {
    const int before = g_failures;
    fn();
    std::printf("%s %s\n", g_failures == before ? "[ok]  " : "[FAIL]", name);
  }
  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
