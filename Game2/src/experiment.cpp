#include "experiment.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

#include "field.hpp"
#include "parallel.hpp"
#include "search.hpp"
#include "svg.hpp"

namespace blotto::fourway {

namespace {

// RNG stream ids per scenario: one independent set of groups per stage.
enum Stage : uint64_t { kScreen, kRefine, kSelect, kHoldout };

constexpr int kFinalistsPerScenario = 5;

struct Candidate {
  std::string label;
  Alloc alloc;
};

int add_candidate(std::vector<Candidate>& cands, const std::string& label, const Alloc& x) {
  for (size_t i = 0; i < cands.size(); ++i) {
    if (cands[i].alloc == x) {
      cands[i].label += " / " + label;
      return static_cast<int>(i);
    }
  }
  cands.push_back({label, x});
  return static_cast<int>(cands.size() - 1);
}

std::string clip(const std::string& s, size_t width) {
  return s.size() <= width ? s : s.substr(0, width - 1) + "~";
}

double seconds_since(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

std::string q(const std::string& s) {
  if (s.find_first_of(",\"") == std::string::npos) return s;
  std::string out = "\"";
  for (char c : s) out += c == '"' ? std::string("\"\"") : std::string(1, c);
  return out + "\"";
}

}  // namespace

int run(const Config& cfg) {
  const auto t0 = std::chrono::steady_clock::now();
  if (cfg.objective != "regret" && cfg.objective != "maximin")
    throw std::invalid_argument("--objective must be regret or maximin");
  if (cfg.screen_keep < 1 || cfg.refine_keep < 1 || cfg.refine_keep > cfg.screen_keep)
    throw std::invalid_argument("need 1 <= refine-keep <= screen-keep");
  const int threads = resolve_threads(cfg.threads);
  const std::vector<Field> scen = select_fields(cfg.scenario);
  const size_t S = scen.size();

  std::printf("Four-way Blotto (Game 2) - payout 9/4/2/0 by finish, ties split\n");
  std::printf("seed %llu | threads %d | scenarios %zu | break-even payout %.2f per group\n",
              static_cast<unsigned long long>(cfg.seed), threads, S, kMeanPayout);
  std::printf("Opponent populations are modelled hypotheses (Game 1's scenarios plus Game-2-aware mixes),\n"
              "not observed applicant data.\n");
  std::printf("Search per scenario: all legal allocations on %zu groups -> top %zu on %zu -> top %zu on %zu\n"
              "  -> hill climbing from the top %d -> independent holdout of %zu groups.\n\n",
              cfg.screen_groups, cfg.screen_keep, cfg.refine_groups, cfg.refine_keep, cfg.select_groups,
              cfg.climb_starts, cfg.holdout_groups);

  // ---- Per-scenario successive-halving search.
  std::vector<std::vector<Group>> select(S), holdout(S);
  std::vector<std::vector<Scored>> finalists(S);
  for (size_t s = 0; s < S; ++s) {
    const auto ts = std::chrono::steady_clock::now();
    auto groups_for = [&](Stage stage, size_t n) {
      Rng rng = Rng::derive(cfg.seed, scen[s].id, stage);
      return sample_groups(scen[s], n, rng);
    };
    size_t visited = 0;
    const auto screened = screen_all(groups_for(kScreen, cfg.screen_groups), cfg.screen_keep, threads, &visited);

    std::vector<Alloc> pool;
    for (const auto& x : screened) pool.push_back(x.alloc);
    const auto refined = score_all(pool, groups_for(kRefine, cfg.refine_groups), threads);

    select[s] = groups_for(kSelect, cfg.select_groups);
    pool.clear();
    for (size_t i = 0; i < std::min(cfg.refine_keep, refined.size()); ++i) pool.push_back(refined[i].alloc);
    std::vector<Scored> ranked = score_all(pool, select[s], threads);

    // Polish: local search can recover allocations the noisy screen dropped.
    const auto& groups = select[s];
    for (int i = 0; i < std::min<int>(cfg.climb_starts, static_cast<int>(ranked.size())); ++i) {
      const Scored climbed = hill_climb(
          ranked[i].alloc, [&](const Alloc& x) { return mean_payout(x, groups); }, threads);
      if (std::none_of(ranked.begin(), ranked.end(), [&](const Scored& r) { return r.alloc == climbed.alloc; }))
        ranked.push_back(climbed);
    }
    std::sort(ranked.begin(), ranked.end(), better);
    ranked.resize(std::min<size_t>(kFinalistsPerScenario, ranked.size()));
    finalists[s] = ranked;
    holdout[s] = groups_for(kHoldout, cfg.holdout_groups);

    std::printf("  searched %-16s %zu allocations, best %s (%.3f on selection groups) in %.1fs\n",
                scen[s].name.c_str(), visited, format(ranked[0].alloc).c_str(), ranked[0].value,
                seconds_since(ts));
    std::fflush(stdout);
  }

  // ---- Candidate pool: Game 1 answers for comparison, then every scenario's finalists.
  std::vector<Candidate> cands;
  const int g1_pick = add_candidate(cands, "Game 1 pick", {1, 0, 21, 31, 47});
  const int g1_alt = add_candidate(cands, "Game 1 alt (castle 3)", {0, 0, 22, 31, 47});
  const int g1_ours = add_candidate(cands, "Game 1 our candidate", {0, 13, 21, 30, 36});
  const int g1_earlier = add_candidate(cands, "Game 1 earlier candidate", {0, 14, 22, 29, 35});
  add_candidate(cands, "proportional baseline", {7, 13, 20, 27, 33});
  add_candidate(cands, "equal baseline", {20, 20, 20, 20, 20});
  for (const auto& [label, x] : cfg.extra_candidates) add_candidate(cands, label, x);
  std::vector<int> best_under(S);
  for (size_t s = 0; s < S; ++s)
    for (size_t i = 0; i < finalists[s].size(); ++i) {
      const int c = add_candidate(cands, "#" + std::to_string(i + 1) + " " + scen[s].name, finalists[s][i].alloc);
      if (i == 0) best_under[s] = c;
    }

  // ---- Selection-group matrix, robust objectives, robust hill climbing.
  std::vector<std::vector<double>> sel;
  auto score_new_candidates = [&] {
    const size_t first = sel.size();
    sel.resize(cands.size(), std::vector<double>(S));
    parallel_for((cands.size() - first) * S, threads, [&](size_t task, int) {
      const size_t c = first + task / S, s = task % S;
      sel[c][s] = mean_payout(cands[c].alloc, select[s]);
    });
  };
  score_new_candidates();
  std::vector<double> best(S, -1);
  for (size_t s = 0; s < S; ++s)
    for (const auto& row : sel) best[s] = std::max(best[s], row[s]);

  auto worst_of = [&](const std::vector<double>& row) { return *std::min_element(row.begin(), row.end()); };
  auto regret_of = [&](const std::vector<double>& row) {
    double r = 0;
    for (size_t s = 0; s < S; ++s) r = std::max(r, best[s] - row[s]);
    return r;
  };
  auto objective = [&](const Alloc& x, bool maximin) {
    double worst = std::numeric_limits<double>::infinity(), regret = 0;
    for (size_t s = 0; s < S; ++s) {
      const double v = mean_payout(x, select[s]);
      worst = std::min(worst, v);
      regret = std::max(regret, best[s] - v);
    }
    return maximin ? worst : -regret;
  };
  auto robust_climb = [&](bool maximin) {
    std::vector<Scored> starts;
    for (size_t c = 0; c < cands.size(); ++c)
      starts.push_back({cands[c].alloc, maximin ? worst_of(sel[c]) : -regret_of(sel[c])});
    std::sort(starts.begin(), starts.end(), better);
    Scored top{starts[0].alloc, -std::numeric_limits<double>::infinity()};
    for (size_t i = 0; i < std::min<size_t>(3, starts.size()); ++i) {
      const Scored r = hill_climb(starts[i].alloc, [&](const Alloc& x) { return objective(x, maximin); }, threads);
      if (better(r, top)) top = r;
    }
    return top.alloc;
  };
  const int maximin = add_candidate(cands, "robust maximin", robust_climb(true));
  score_new_candidates();  // robust_climb reads a score row for every candidate
  const int regret = add_candidate(cands, "minimax regret", robust_climb(false));
  score_new_candidates();
  for (size_t s = 0; s < S; ++s)
    for (const auto& row : sel) best[s] = std::max(best[s], row[s]);
  const int recommended = cfg.objective == "maximin" ? maximin : regret;
  const size_t C = cands.size();

  // ---- Independent holdout evaluation (what the report shows).
  std::vector<std::vector<Evaluation>> hold(C, std::vector<Evaluation>(S));
  parallel_for(C * S, threads, [&](size_t task, int) {
    const size_t c = task / S, s = task % S;
    hold[c][s] = evaluate(cands[c].alloc, holdout[s]);
  });
  std::vector<double> hold_best(S, -1);
  for (size_t s = 0; s < S; ++s)
    for (size_t c = 0; c < C; ++c) hold_best[s] = std::max(hold_best[s], hold[c][s].payout);

  // ---- Report.
  auto row = [&](int c, size_t s) {
    const auto& e = hold[c][s];
    std::printf("  %-34s %-17s %7.3f %7.3f %6.3f %6.1f%% %6.1f%% %6.1f%% %6.1f%% %7.2f\n",
                clip(cands[c].label, 34).c_str(), format(cands[c].alloc).c_str(), sel[c][s], e.payout, e.ci_half,
                100 * e.place[0], 100 * e.place[1], 100 * e.place[2], 100 * e.place[3], e.castle_points);
  };
  for (size_t s = 0; s < S; ++s) {
    std::printf("\n== Scenario: %s - %s ==\n", scen[s].name.c_str(), scen[s].description.c_str());
    std::printf("  %-34s %-17s %7s %7s %6s %7s %7s %7s %7s %7s\n", "label", "allocation", "select", "holdout",
                "+-CI", "1st", "2nd", "3rd", "4th", "castles");
    std::vector<int> rows;
    auto add = [&](int c) {
      if (std::find(rows.begin(), rows.end(), c) == rows.end()) rows.push_back(c);
    };
    for (const auto& f : finalists[s])
      for (size_t c = 0; c < C; ++c)
        if (cands[c].alloc == f.alloc) add(static_cast<int>(c));
    for (int c : {recommended, maximin, regret, g1_pick, g1_alt, g1_ours, g1_earlier}) add(c);
    for (int c : rows) row(c, s);
  }

  std::vector<int> leaders;
  for (int c : {recommended, maximin, regret, g1_pick, g1_alt, g1_ours, g1_earlier})
    if (std::find(leaders.begin(), leaders.end(), c) == leaders.end()) leaders.push_back(c);
  std::printf("\n== Holdout expected payout per group (break-even %.2f, max 9) ==\n", kMeanPayout);
  std::printf("  %-16s", "scenario");
  for (int c : leaders) std::printf(" %17s", format(cands[c].alloc).c_str());
  std::printf(" %9s\n", "best");
  for (size_t s = 0; s < S; ++s) {
    std::printf("  %-16s", scen[s].name.c_str());
    for (int c : leaders) std::printf(" %17.3f", hold[c][s].payout);
    std::printf(" %9.3f\n", hold_best[s]);
  }
  std::printf("  %-16s", "mean");
  for (int c : leaders) {
    double m = 0;
    for (size_t s = 0; s < S; ++s) m += hold[c][s].payout / S;
    std::printf(" %17.3f", m);
  }
  std::printf("\n  %-16s", "worst case");
  for (int c : leaders) {
    double w = 1e9;
    for (size_t s = 0; s < S; ++s) w = std::min(w, hold[c][s].payout);
    std::printf(" %17.3f", w);
  }
  std::printf("\n  %-16s", "max regret");
  for (int c : leaders) {
    double r = 0;
    for (size_t s = 0; s < S; ++s) r = std::max(r, hold_best[s] - hold[c][s].payout);
    std::printf(" %17.3f", r);
  }
  std::printf("\n  Columns: ");
  for (size_t i = 0; i < leaders.size(); ++i)
    std::printf("%s%s = %s", i ? "; " : "", format(cands[leaders[i]].alloc).c_str(), cands[leaders[i]].label.c_str());
  std::printf("\n  'best' = best candidate found for that scenario (searched on separate groups).\n");

  const auto& rec = hold[recommended];
  size_t worst_s = 0, best_s = 0;
  double mean = 0, p1 = 0, vs_g1 = 0;
  int ahead_g1 = 0;
  for (size_t s = 0; s < S; ++s) {
    if (rec[s].payout < rec[worst_s].payout) worst_s = s;
    if (rec[s].payout > rec[best_s].payout) best_s = s;
    mean += rec[s].payout / S;
    p1 += rec[s].place[0] / S;
    vs_g1 += (rec[s].payout - hold[g1_pick][s].payout) / S;
    ahead_g1 += rec[s].payout > hold[g1_pick][s].payout;
  }
  std::printf("\n==> Recommended Game 2 allocation: %s\n", format(cands[recommended].alloc).c_str());
  std::printf("  Objective: %s across %zu modelled scenarios (selection max regret %.3f, worst %.3f).\n",
              cfg.objective == "regret" ? "minimax regret" : "maximin payout", S, regret_of(sel[recommended]),
              worst_of(sel[recommended]));
  std::printf("  Holdout payout: mean %.3f per group, from %.3f ('%s') to %.3f ('%s'); finishes 1st %.1f%% of the time.\n",
              mean, rec[worst_s].payout, scen[worst_s].name.c_str(), rec[best_s].payout, scen[best_s].name.c_str(),
              100 * p1);
  if (recommended != g1_pick)
    std::printf("  Versus the Game 1 pick %s: ahead in %d/%zu scenarios, mean %+.3f payout per group.\n",
                format(cands[g1_pick].alloc).c_str(), ahead_g1, S, vs_g1);
  std::printf("  Holdout CIs (~%.3f) are sampling error only; the spread across scenarios is model uncertainty.\n",
              rec[0].ci_half);

  // ---- CSV.
  namespace fs = std::filesystem;
  fs::create_directories(cfg.output);
  {
    std::ofstream f(fs::path(cfg.output) / "results.csv");
    f << "label,allocation,scenario,selection_payout,holdout_payout,holdout_ci95,p_first,p_second,p_third,p_fourth,"
         "castle_points\n";
    for (size_t c = 0; c < C; ++c)
      for (size_t s = 0; s < S; ++s) {
        const auto& e = hold[c][s];
        f << q(cands[c].label) << "," << q(format(cands[c].alloc)) << "," << scen[s].name << "," << sel[c][s] << ","
          << e.payout << "," << e.ci_half << "," << e.place[0] << "," << e.place[1] << "," << e.place[2] << ","
          << e.place[3] << "," << e.castle_points << "\n";
      }
  }
  {
    std::ofstream f(fs::path(cfg.output) / "finalists.csv");
    f << "scenario,rank,allocation,selection_payout\n";
    for (size_t s = 0; s < S; ++s)
      for (size_t i = 0; i < finalists[s].size(); ++i)
        f << scen[s].name << "," << i + 1 << "," << q(format(finalists[s][i].alloc)) << "," << finalists[s][i].value
          << "\n";
  }
  // ---- Charts (SVG). Leaders get role names; candidate labels are too long for legends.
  auto field_index = [&](const std::string& name) {
    for (size_t s = 0; s < S; ++s)
      if (scen[s].name == name) return static_cast<int>(s);
    return -1;
  };
  std::vector<std::pair<int, std::string>> leads;
  auto lead = [&](int c, const std::string& role) {
    if (c < 0 || leads.size() == static_cast<size_t>(svg::kPaletteSize)) return;
    for (const auto& l : leads)
      if (l.first == c) return;
    leads.push_back({c, role + " " + format(cands[c].alloc)});
  };
  lead(recommended, "recommended");
  lead(maximin, "robust maximin");
  for (const char* name : {"aware-only", "uniform-blend", "proportional"})
    if (const int s = field_index(name); s >= 0) lead(best_under[s], std::string("best vs ") + name);
  lead(g1_pick, "Game 1 pick");
  lead(g1_ours, "Game 1 candidate");

  std::vector<std::string> lead_labels, field_names;
  for (const auto& l : leads) lead_labels.push_back(l.second);
  for (const auto& f : scen) field_names.push_back(f.name);
  const std::string dir = cfg.output + "/";
  {
    std::vector<std::vector<double>> v;
    for (const auto& l : leads) {
      v.emplace_back();
      for (size_t s = 0; s < S; ++s) v.back().push_back(hold[l.first][s].payout);
    }
    svg::heatmap(dir + "payout_heatmap.svg", "Expected payout by opponent field",
                 "Holdout payout per group (0-9, break-even 3.75); colour on a fixed 0-9 scale", lead_labels,
                 field_names, v, 0, 9, 0, "%.2f");
  }
  {
    std::vector<std::string> castles;
    for (int i = 0; i < kCastles; ++i) castles.push_back("castle " + std::to_string(i + 1));
    std::vector<std::vector<double>> v;
    for (const auto& l : leads) v.emplace_back(cands[l.first].alloc.begin(), cands[l.first].alloc.end());
    svg::grouped_bars(dir + "allocation_comparison.svg", "Allocation comparison",
                      "Troops per castle for the leading Game 2 allocations and the Game 1 answers", "troops",
                      castles, lead_labels, v, 70, 10, "%.0f");
  }
  const int blend = field_index("uniform-blend");
  const std::vector<int> aware = {blend, field_index("aware-25"), field_index("aware-50"), field_index("aware-only")};
  if (std::none_of(aware.begin(), aware.end(), [](int s) { return s < 0; })) {
    std::vector<std::vector<double>> v;
    for (const auto& l : leads) {
      v.emplace_back();
      for (int s : aware) v.back().push_back(hold[l.first][s].payout);
    }
    svg::line_chart(dir + "aware_sensitivity.svg", "What happens as the field wises up",
                    "Holdout payout as Game-2-aware concentrators replace uniform-blend applicants",
                    "share of Game-2-aware applicants", "payout per group", {"0%", "25%", "50%", "100%"},
                    lead_labels, v, 9, 1);
  }
  for (const char* name : {"uniform-blend", "aware-50"}) {
    const int s = field_index(name);
    if (s < 0) continue;
    std::vector<std::vector<double>> v;
    for (const auto& l : leads) v.push_back({hold[l.first][s].place.begin(), hold[l.first][s].place.end()});
    svg::stacked_bars(dir + "finish_places_" + name + ".svg", std::string("Finishing places in '") + name + "'",
                      "Share of groups finishing 1st / 2nd / 3rd / 4th (ties split evenly)", lead_labels,
                      {"1st (9)", "2nd (4)", "3rd (2)", "4th (0)"}, {"#2a9d6f", "#2f6bd8", "#e8833a", "#c2416b"},
                      v);
  }

  std::printf("\nWrote CSVs and SVG charts to %s (%.1fs total)\n", cfg.output.c_str(), seconds_since(t0));
  return 0;
}

}  // namespace blotto::fourway
