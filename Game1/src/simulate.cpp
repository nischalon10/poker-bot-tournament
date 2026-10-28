#include "simulate.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>

#include "parallel.hpp"

namespace blotto {

namespace {

constexpr uint64_t kTrainPool = 0;
constexpr uint64_t kHoldoutPool = 1;
constexpr uint64_t kCrowdStream = 0xC0FFEE;
constexpr double kZ95 = 1.959963984540054;

// Adds a candidate, merging labels when the allocation is already present.
int add_candidate(std::vector<Candidate>& candidates, const std::string& label, const Alloc& x) {
  for (size_t i = 0; i < candidates.size(); ++i) {
    if (candidates[i].alloc == x) {
      candidates[i].label += " / " + label;
      return static_cast<int>(i);
    }
  }
  candidates.push_back({label, x});
  return static_cast<int>(candidates.size() - 1);
}

// Mean and 95% half-width of a sample summarised by its sum and sum of squares.
std::pair<double, double> mean_ci(double sum, double sum_sq, size_t n) {
  const double mean = sum / n;
  const double var = n > 1 ? std::max(0.0, (sum_sq - n * mean * mean) / (n - 1)) : 0.0;
  return {mean, kZ95 * std::sqrt(var / n)};
}

}  // namespace

Results simulate(const Config& cfg) {
  const auto t0 = std::chrono::steady_clock::now();
  if (cfg.samples == 0 || cfg.samples > 50'000'000) throw std::invalid_argument("--samples must be 1..50000000");
  if (cfg.repeats < 1) throw std::invalid_argument("--repeats must be >= 1");
  if (cfg.objective != "regret" && cfg.objective != "maximin")
    throw std::invalid_argument("--objective must be regret or maximin");

  Results res;
  res.cfg = cfg;
  res.threads = resolve_threads(cfg.threads);
  res.scenarios = select_scenarios(cfg.scenario);
  const auto& scen = res.scenarios;
  const size_t S = scen.size(), R = static_cast<size_t>(cfg.repeats), N = cfg.samples;
  if (S == 0) throw std::invalid_argument("no scenarios selected");

  res.focus = static_cast<int>(S - 1);
  for (size_t s = 0; s < S; ++s)
    if (scen[s].name == cfg.focus) res.focus = static_cast<int>(s);

  // ---- Opponent pools. Each (scenario, repeat, train/holdout) has its own RNG stream,
  // so results are identical for any thread count or scenario selection.
  std::vector<MarginalTable> train(S * R), hold(S * R);
  std::vector<std::vector<Alloc>> hold_pool(S);  // repeat 0 only, for CIs and histograms
  parallel_for(S * R, res.threads, [&](size_t task, int) {
    const size_t r = task / S, s = task % S;
    Rng train_rng = Rng::derive(cfg.seed, scen[s].id, r, kTrainPool);
    train[task] = build_marginals(sample_pool(scen[s], N, train_rng));
    Rng hold_rng = Rng::derive(cfg.seed, scen[s].id, r, kHoldoutPool);
    std::vector<Alloc> pool = sample_pool(scen[s], N, hold_rng);
    hold[task] = build_marginals(pool);
    if (r == 0) hold_pool[s] = std::move(pool);
  });
  auto train_at = [&](size_t s, size_t r) -> const MarginalTable& { return train[r * S + s]; };
  auto hold_at = [&](size_t s, size_t r) -> const MarginalTable& { return hold[r * S + s]; };

  // ---- Exact best response to each training pool, scored on its holdout pool.
  res.repeats.assign(S, std::vector<ScenarioRepeat>(R));
  for (size_t s = 0; s < S; ++s) {
    for (size_t r = 0; r < R; ++r) {
      const Alloc pick = dp_best_alloc(train_at(s, r));
      res.repeats[s][r] = {pick, train_at(s, r).score(pick), hold_at(s, r).score(pick)};
    }
    res.holdout_best.push_back(dp_best(hold_at(s, 0)).score);
  }

  // ---- Robust selection across scenarios, repeated on every independent training set.
  for (size_t r = 0; r < R; ++r) {
    std::vector<const MarginalTable*> tables;
    for (size_t s = 0; s < S; ++s) tables.push_back(&train_at(s, r));
    RobustResult rr = robust_search(tables, res.threads);
    res.robust_picks.push_back(cfg.objective == "maximin" ? rr.maximin[0].alloc
                                                          : rr.minimax_regret[0].alloc);
    if (r == 0) res.robust = std::move(rr);
  }

  // ---- Candidate set.
  auto& cands = res.candidates;
  res.reference = add_candidate(cands, "our candidate", {0, 13, 21, 30, 36});
  add_candidate(cands, "earlier candidate", {0, 14, 22, 29, 35});
  add_candidate(cands, "proportional baseline", {7, 13, 20, 27, 33});
  add_candidate(cands, "equal baseline", {20, 20, 20, 20, 20});
  for (const auto& [label, x] : cfg.extra_candidates) add_candidate(cands, label, x);
  res.regret = add_candidate(cands, "minimax regret", res.robust.minimax_regret[0].alloc);
  res.maximin = add_candidate(cands, "robust maximin", res.robust.maximin[0].alloc);
  for (size_t s = 0; s < S; ++s)
    res.best_under.push_back(add_candidate(cands, "best: " + scen[s].name, res.repeats[s][0].dp_pick));
  res.recommended = cfg.objective == "maximin" ? res.maximin : res.regret;
  const size_t C = cands.size();

  // ---- Candidate x scenario statistics. Every candidate faces the same opponents.
  res.stats.assign(C, std::vector<CandidateStats>(S));
  parallel_for(S, res.threads, [&](size_t s, int) {
    const auto& pool = hold_pool[s];
    const Alloc& ref = cands[res.reference].alloc;
    for (size_t c = 0; c < C; ++c) {
      const Alloc& x = cands[c].alloc;
      CandidateStats& st = res.stats[c][s];
      st.train = train_at(s, 0).score(x);
      st.holdout = hold_at(s, 0).score(x);

      double sum = 0, sum_sq = 0, dsum = 0, dsum_sq = 0;
      for (const Alloc& y : pool) {
        const double p = payoff(x, y);
        const double d = p - payoff(ref, y);
        sum += p;
        sum_sq += p * p;
        dsum += d;
        dsum_sq += d * d;
      }
      st.ci_half = mean_ci(sum, sum_sq, pool.size()).second;
      std::tie(st.diff_ref, st.diff_ci_half) = mean_ci(dsum, dsum_sq, pool.size());

      double rsum = 0, rsum_sq = 0;
      for (size_t r = 0; r < R; ++r) {
        const double h = hold_at(s, r).score(x);
        rsum += h;
        rsum_sq += h * h;
      }
      st.rep_mean = rsum / R;
      st.rep_sd = R > 1 ? std::sqrt(std::max(0.0, (rsum_sq - R * st.rep_mean * st.rep_mean) / (R - 1))) : 0.0;
    }
  });

  // ---- Adversarial stress tests (reported separately, never mixed into populations).
  const MarginalTable& focus_hold = hold_at(res.focus, 0);
  std::vector<int> leaders = {res.recommended, res.maximin, res.reference, 1, res.best_under[res.focus]};
  std::vector<int> seen;
  for (int c : leaders) {
    if (std::find(seen.begin(), seen.end(), c) != seen.end()) continue;
    seen.push_back(c);
    const Alloc& x = cands[c].alloc;
    AdversarialRow row{};
    row.candidate = c;
    row.counter = dp_best_alloc(build_marginals(std::vector<Alloc>{x}));
    row.score_vs_counter = payoff(x, row.counter);
    row.counter_focus = focus_hold.score(row.counter);

    Rng crowd_rng = Rng::derive(cfg.seed, kCrowdStream, static_cast<uint64_t>(c));
    std::vector<Alloc> crowd(5000, x);
    for (auto& y : crowd) jitter(y, crowd_rng.below(7), crowd_rng);
    row.crowd_counter = dp_best_alloc(build_marginals(crowd));
    row.score_vs_crowd_counter = payoff(x, row.crowd_counter);
    row.crowd_counter_focus = focus_hold.score(row.crowd_counter);
    res.adversarial.push_back(row);
  }

  // ---- Fictitious-play mixed-strategy benchmark.
  if (cfg.fp_iterations > 0) {
    res.fp = fictitious_play(cfg.fp_iterations, cands[res.reference].alloc);
    for (const auto& c : cands) res.fp_scores.push_back(res.fp.mixture.score(c.alloc));
  }

  // ---- Neighbourhood of the recommended allocation under the focus scenario.
  const Alloc rec = cands[res.recommended].alloc;
  const double rec_focus = focus_hold.score(rec);
  for (int d = 1; d <= cfg.neighborhood_max; ++d) {
    std::array<std::array<double, kCastles>, kCastles> grid;
    for (int from = 0; from < kCastles; ++from) {
      for (int to = 0; to < kCastles; ++to) {
        grid[from][to] = std::numeric_limits<double>::quiet_NaN();
        if (from == to || rec[from] < d) continue;
        Alloc y = rec;
        y[from] -= d;
        y[to] += d;
        grid[from][to] = focus_hold.score(y) - rec_focus;
      }
    }
    res.neighborhood.push_back(grid);
  }

  // ---- Head-to-head point distributions on the focus holdout pool.
  for (int c : {res.recommended, res.reference, res.best_under[res.focus]}) {
    if (std::find(res.hist_candidates.begin(), res.hist_candidates.end(), c) != res.hist_candidates.end()) continue;
    res.hist_candidates.push_back(c);
    std::array<int, 31> h{};
    for (const Alloc& y : hold_pool[res.focus]) ++h[payoff2(cands[c].alloc, y)];
    res.histograms.push_back(h);
  }

  // ---- Tie exposure and round-number clustering.
  res.tie_candidates = {res.recommended};
  if (res.reference != res.recommended) res.tie_candidates.push_back(res.reference);
  for (int c : res.tie_candidates) {
    std::vector<double> row;
    for (size_t s = 0; s < S; ++s) {
      double ties = 0;
      for (const Alloc& y : hold_pool[s])
        for (int i = 0; i < kCastles; ++i) ties += (cands[c].alloc[i] == y[i]) * 0.5 * (i + 1);
      row.push_back(ties / hold_pool[s].size());
    }
    res.tie_points.push_back(row);
  }
  for (size_t s = 0; s < S; ++s) {
    size_t round = 0;
    for (const Alloc& y : hold_pool[s])
      round += std::all_of(y.begin(), y.end(), [](int v) { return v % 5 == 0; });
    res.round_share.push_back(static_cast<double>(round) / hold_pool[s].size());
  }

  res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  return res;
}

}  // namespace blotto
