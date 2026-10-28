#include "report.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

namespace blotto {

namespace {

std::string clip(const std::string& s, size_t width) {
  return s.size() <= width ? s : s.substr(0, width - 1) + "~";
}

void heading(const std::string& title) { std::printf("\n== %s ==\n", title.c_str()); }

// Candidates worth showing in compact tables: recommendation first, then references.
std::vector<int> leaders(const Results& res) {
  std::vector<int> out;
  for (int c : {res.recommended, res.maximin, res.regret, res.reference, 1, 2, 3}) {
    if (std::find(out.begin(), out.end(), c) == out.end()) out.push_back(c);
  }
  return out;
}

void print_scenario(const Results& res, size_t s) {
  const auto& sc = res.scenarios[s];
  heading("Scenario: " + sc.name + " - " + sc.description);

  std::vector<int> rows;
  auto add = [&](int c) {
    if (std::find(rows.begin(), rows.end(), c) == rows.end()) rows.push_back(c);
  };
  for (int c = 0; c < 4 + static_cast<int>(res.cfg.extra_candidates.size()); ++c) add(c);
  add(res.regret);
  add(res.maximin);
  add(res.best_under[s]);

  std::printf("  %-36s %-17s %7s %7s %7s %16s %8s %7s\n", "label", "allocation", "train", "holdout",
              "+-95%CI", "d vs ours (CI)", "rep mean", "rep sd");
  for (int c : rows) {
    const auto& st = res.stats[c][s];
    char diff_buf[32];
    if (c == res.reference)
      std::snprintf(diff_buf, sizeof diff_buf, "%s", "-");
    else
      std::snprintf(diff_buf, sizeof diff_buf, "%+.3f (%.3f)", st.diff_ref, st.diff_ci_half);
    std::printf("  %-36s %-17s %7.3f %7.3f %7.3f %16s %8.3f %7.3f\n",
                clip(res.candidates[c].label, 36).c_str(), format(res.candidates[c].alloc).c_str(),
                st.train, st.holdout, st.ci_half, diff_buf, st.rep_mean, st.rep_sd);
  }

  const auto& reps = res.repeats[s];
  std::map<Alloc, int> distinct;
  double hsum = 0, hsum_sq = 0, gap = 0;
  for (const auto& r : reps) {
    ++distinct[r.dp_pick];
    hsum += r.holdout;
    hsum_sq += r.holdout * r.holdout;
    gap += r.train - r.holdout;
  }
  const double n = static_cast<double>(reps.size());
  const double mean = hsum / n;
  const double sd = n > 1 ? std::sqrt(std::max(0.0, (hsum_sq - n * mean * mean) / (n - 1))) : 0.0;
  std::printf(
      "  Re-optimised DP pick over %zu repeats: %zu distinct allocation(s); holdout %.3f (sd %.3f); "
      "train-holdout optimism %+.4f\n",
      reps.size(), distinct.size(), mean, sd, gap / n);
}

void print_matrix(const Results& res) {
  heading("Holdout points (out of 15) by scenario - same opponents for every column");
  const auto cols = leaders(res);
  std::printf("  %-16s", "scenario");
  for (int c : cols) std::printf(" %17s", format(res.candidates[c].alloc).c_str());
  std::printf(" %9s\n", "optimum");
  for (size_t s = 0; s < res.scenarios.size(); ++s) {
    std::printf("  %-16s", res.scenarios[s].name.c_str());
    for (int c : cols) std::printf(" %17.3f", res.stats[c][s].holdout);
    std::printf(" %9.3f\n", res.holdout_best[s]);
  }
  std::printf("  %-16s", "worst case");
  for (int c : cols) {
    double worst = 1e9;
    for (const auto& st : res.stats[c]) worst = std::min(worst, st.holdout);
    std::printf(" %17.3f", worst);
  }
  std::printf("\n  %-16s", "max regret");
  for (int c : cols) {
    double regret = 0;
    for (size_t s = 0; s < res.scenarios.size(); ++s)
      regret = std::max(regret, res.holdout_best[s] - res.stats[c][s].holdout);
    std::printf(" %17.3f", regret);
  }
  std::printf("\n  Columns: ");
  for (size_t i = 0; i < cols.size(); ++i)
    std::printf("%s%s = %s", i ? "; " : "", format(res.candidates[cols[i]].alloc).c_str(),
                res.candidates[cols[i]].label.c_str());
  std::printf("\n  'optimum' is the exact best pure allocation for that holdout pool (in-sample, so optimistic).\n");
}

void print_robust(const Results& res) {
  heading("Robust selection over " + std::to_string(res.scenarios.size()) +
          " modelled scenarios (training pools, exhaustive)");
  std::printf("  Enumerated all %zu legal allocations.\n", res.robust.visited);
  std::printf("  %-4s %-20s %12s   %-20s %12s\n", "rank", "maximin", "worst score", "minimax regret",
              "max regret");
  for (size_t i = 0; i < std::max(res.robust.maximin.size(), res.robust.minimax_regret.size()); ++i) {
    std::printf("  %-4zu", i + 1);
    if (i < res.robust.maximin.size())
      std::printf(" %-20s %12.4f", format(res.robust.maximin[i].alloc).c_str(), res.robust.maximin[i].value);
    if (i < res.robust.minimax_regret.size())
      std::printf("   %-20s %12.4f", format(res.robust.minimax_regret[i].alloc).c_str(),
                  res.robust.minimax_regret[i].value);
    std::printf("\n");
  }
  std::map<Alloc, int> picks;
  for (const auto& a : res.robust_picks) ++picks[a];
  std::printf("  %s pick across %zu independent repeats:", res.cfg.objective.c_str(), res.robust_picks.size());
  for (const auto& [a, n] : picks) std::printf(" %s x%d", format(a).c_str(), n);
  std::printf("\n");
}

void print_adversarial(const Results& res) {
  heading("Adversarial stress tests (NOT part of any modelled population)");
  const std::string& focus = res.scenarios[res.focus].name;
  std::printf("  %-36s %-17s %-17s %6s %8s   %-17s %6s %8s\n", "candidate", "allocation", "direct counter",
              "pts", "ctr@foc", "crowd counter", "pts", "ctr@foc");
  for (const auto& a : res.adversarial) {
    const auto& c = res.candidates[a.candidate];
    std::printf("  %-36s %-17s %-17s %6.1f %8.3f   %-17s %6.1f %8.3f\n", clip(c.label, 36).c_str(),
                format(c.alloc).c_str(), format(a.counter).c_str(), a.score_vs_counter, a.counter_focus,
                format(a.crowd_counter).c_str(), a.score_vs_crowd_counter, a.crowd_counter_focus);
  }
  std::printf("  pts = candidate's points vs that counter; ctr@foc = the counter's own holdout score in '%s'.\n",
              focus.c_str());
  std::printf("  Crowd counter = best response to 5000 jittered copies of the candidate (models copycats).\n");
}

void print_fictitious_play(const Results& res) {
  if (res.cfg.fp_iterations <= 0) return;
  heading("Mixed-strategy benchmark: fictitious play (" + std::to_string(res.fp.iterations) + " iterations)");
  std::printf("  Exploitability of the empirical mixture: %.4f pts above 7.5 (0 = exact equilibrium)\n",
              res.fp.exploitability);
  std::printf("  Most frequent support:");
  for (size_t i = 0; i < std::min<size_t>(5, res.fp.support.size()); ++i)
    std::printf(" %s %.1f%%", format(res.fp.support[i].alloc).c_str(), 100 * res.fp.support[i].value);
  std::printf("\n  Score vs the mixture:");
  for (int c : leaders(res))
    std::printf(" %s %.3f;", format(res.candidates[c].alloc).c_str(), res.fp_scores[c]);
  std::printf("\n  This is a game-theoretic reference point, not a forecast of the applicant field.\n");
}

void print_neighborhood(const Results& res) {
  if (res.neighborhood.empty()) return;
  const auto& rec = res.candidates[res.recommended].alloc;
  heading("Neighbourhood of " + format(rec) + " in '" + res.scenarios[res.focus].name +
          "': holdout change after moving 1 troop (row -> column)");
  std::printf("  %-10s", "from\\to");
  for (int j = 0; j < kCastles; ++j) std::printf(" %9s", ("castle " + std::to_string(j + 1)).c_str());
  std::printf("\n");
  for (int i = 0; i < kCastles; ++i) {
    std::printf("  castle %-3d", i + 1);
    for (int j = 0; j < kCastles; ++j) {
      const double d = res.neighborhood[0][i][j];
      if (std::isnan(d))
        std::printf(" %9s", "-");
      else
        std::printf(" %+9.4f", d);
    }
    std::printf("\n");
  }
}

void print_ties(const Results& res) {
  heading("Tie exposure and round-number clustering (holdout)");
  std::printf("  %-16s %12s", "scenario", "all %5 share");
  for (int c : res.tie_candidates)
    std::printf(" %22s", ("tie pts " + format(res.candidates[c].alloc)).c_str());
  std::printf("\n");
  for (size_t s = 0; s < res.scenarios.size(); ++s) {
    std::printf("  %-16s %11.1f%%", res.scenarios[s].name.c_str(), 100 * res.round_share[s]);
    for (size_t t = 0; t < res.tie_candidates.size(); ++t) std::printf(" %22.3f", res.tie_points[t][s]);
    std::printf("\n");
  }
  std::printf("  Tie pts = expected points per matchup earned through exact ties (half-value castles).\n");
}

void print_recommendation(const Results& res) {
  const auto& scen = res.scenarios;
  const int rec = res.recommended;
  const auto& x = res.candidates[rec].alloc;
  const auto& st = res.stats[rec];

  size_t worst_s = 0, regret_s = 0, beats = 0;
  double diff_sum = 0;
  for (size_t s = 0; s < scen.size(); ++s) {
    if (st[s].holdout < st[worst_s].holdout) worst_s = s;
    if (res.holdout_best[s] - st[s].holdout > res.holdout_best[regret_s] - st[regret_s].holdout) regret_s = s;
    diff_sum += st[s].diff_ref;
    beats += st[s].diff_ref > 0;
  }
  size_t best_s = 0;
  for (size_t s = 0; s < scen.size(); ++s)
    if (st[s].holdout > st[best_s].holdout) best_s = s;
  const int stable = static_cast<int>(std::count(res.robust_picks.begin(), res.robust_picks.end(), x));

  std::printf("\n==> Recommended allocation: %s\n", format(x).c_str());
  if (res.cfg.objective == "regret")
    std::printf("  Why: minimises the worst-case shortfall from each scenario's optimum across %zu modelled\n"
                "       scenarios (training max regret %.3f pts).\n",
                scen.size(), res.robust.minimax_regret[0].value);
  else
    std::printf("  Why: maximises the worst-case score across %zu modelled scenarios (training worst %.3f pts).\n",
                scen.size(), res.robust.maximin[0].value);
  std::printf("  Holdout: %.3f pts in '%s' (worst) to %.3f in '%s' (best); largest regret %.3f in '%s'.\n",
              st[worst_s].holdout, scen[worst_s].name.c_str(), st[best_s].holdout, scen[best_s].name.c_str(),
              res.holdout_best[regret_s] - st[regret_s].holdout, scen[regret_s].name.c_str());
  if (rec != res.reference)
    std::printf("  Versus %s: ahead in %zu/%zu scenarios, mean difference %+.3f pts per matchup.\n",
                format(res.candidates[res.reference].alloc).c_str(), beats, scen.size(), diff_sum / scen.size());
  std::printf("  Stability: chosen in %d/%zu independent repeats.\n", stable, res.robust_picks.size());
  // Scenarios where it gives up least versus that scenario's own optimum.
  std::vector<size_t> order(scen.size());
  for (size_t s = 0; s < order.size(); ++s) order[s] = s;
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    return res.holdout_best[a] - st[a].holdout < res.holdout_best[b] - st[b].holdout;
  });
  std::printf("  Closest to optimal under:");
  for (size_t i = 0; i < std::min<size_t>(3, order.size()); ++i)
    std::printf("%s %s (-%.3f)", i ? "," : "", scen[order[i]].name.c_str(),
                res.holdout_best[order[i]] - st[order[i]].holdout);
  std::printf(".\n  It is a compromise: no single scenario's best response is this robust across all of them.\n");
  std::printf("  Caveats: every scenario is a hypothesis, not observed applicant data; sampling CIs (~%.3f)\n"
              "  are far smaller than the spread between scenarios, so population assumptions dominate.\n"
              "  Any fixed allocation can be exploited by a targeted counter (see stress tests).\n",
              st[res.focus].ci_half);
}

}  // namespace

void print_report(const Results& res) {
  const auto& cfg = res.cfg;
  std::printf("Weighted Colonel Blotto - Monte Carlo tournament simulator\n");
  std::printf("samples/pool %zu | seed %llu | repeats %d | threads %d | scenarios %zu | %.2fs\n", cfg.samples,
              static_cast<unsigned long long>(cfg.seed), cfg.repeats, res.threads, res.scenarios.size(),
              res.seconds);
  std::printf("Opponent populations are modelled hypotheses, not observed applicant data.\n");
  std::printf("Scores are expected castle points per matchup (out of 15); 7.5 = break-even.\n");

  for (size_t s = 0; s < res.scenarios.size(); ++s) print_scenario(res, s);
  print_matrix(res);
  print_robust(res);
  print_adversarial(res);
  print_fictitious_play(res);
  print_neighborhood(res);
  print_ties(res);
  print_recommendation(res);
}

}  // namespace blotto
