#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "report.hpp"
#include "svg.hpp"

namespace blotto {

namespace {

using namespace svg;

// CSV field: quote when needed (labels and allocations contain commas).
std::string q(const std::string& s) {
  if (s.find_first_of(",\"\n") == std::string::npos) return s;
  std::string out = "\"";
  for (char c : s) out += c == '"' ? std::string("\"\"") : std::string(1, c);
  return out + "\"";
}

std::string series_label(const Results& res, int c) {
  return res.candidates[c].label + " " + format(res.candidates[c].alloc);
}

std::vector<int> chart_candidates(const Results& res) {
  std::vector<int> out;
  for (int c : {res.recommended, res.maximin, res.reference, 1, 2, 3})
    if (std::find(out.begin(), out.end(), c) == out.end()) out.push_back(c);
  return out;
}

bool archetype_chart(const Results& res, const std::string& path) {
  std::vector<size_t> cols;
  for (size_t s = 0; s < res.scenarios.size(); ++s)
    if (res.scenarios[s].pure()) cols.push_back(s);
  if (cols.empty())
    for (size_t s = 0; s < res.scenarios.size(); ++s) cols.push_back(s);
  std::vector<std::string> groups, series;
  for (size_t s : cols) groups.push_back(res.scenarios[s].name);
  std::vector<std::vector<double>> values;
  for (int c : chart_candidates(res)) {
    series.push_back(series_label(res, c));
    std::vector<double> row;
    for (size_t s : cols) row.push_back(res.stats[c][s].holdout);
    values.push_back(row);
  }
  return grouped_bars(path, "Score by opponent archetype",
                      "Holdout points per matchup (out of 15) against each pure modelled archetype",
                      "points per matchup", groups, series, values, 15, 2.5, "%.1f");
}

bool allocation_chart(const Results& res, const std::string& path) {
  std::vector<int> cands;
  for (int c : {res.best_under[res.focus], res.recommended, res.maximin, res.reference})
    if (std::find(cands.begin(), cands.end(), c) == cands.end()) cands.push_back(c);
  std::vector<std::string> groups, series;
  for (int i = 0; i < kCastles; ++i) groups.push_back("castle " + std::to_string(i + 1) + " (" + std::to_string(i + 1) + " pt)");
  std::vector<std::vector<double>> values;
  int ymax = 0;
  for (int c : cands) {
    series.push_back(series_label(res, c));
    std::vector<double> row;
    for (int v : res.candidates[c].alloc) {
      row.push_back(v);
      ymax = std::max(ymax, v);
    }
    values.push_back(row);
  }
  const double top = std::ceil((ymax + 5) / 10.0) * 10;
  return grouped_bars(path, "Allocation comparison",
                      "Troops per castle: best under '" + res.scenarios[res.focus].name +
                          "', robust picks, and the current candidate",
                      "troops", groups, series, values, top, 10, "%.0f");
}

bool scenario_heatmap(const Results& res, const std::string& path) {
  const auto& scen = res.scenarios;
  const size_t C = res.candidates.size(), S = scen.size();
  const double label_w = 380, cell_w = 96, cell_h = 30, top = 120, left = 24;
  Svg svg(left + label_w + cell_w * S + 30, top + cell_h * C + 50);
  svg.title("Candidates x scenarios", "Holdout points per matchup; colour is normalised within each column");
  for (size_t s = 0; s < S; ++s) {
    const double x = left + label_w + s * cell_w + cell_w / 2;
    svg.text(x, top - 12, scen[s].name, 11, "start", kInk,
             "transform=\"rotate(-35 " + fmt("%g", x) + " " + fmt("%g", top - 12) + ")\"");
  }
  for (size_t s = 0; s < S; ++s) {
    double lo = 1e9, hi = -1e9;
    for (size_t c = 0; c < C; ++c) {
      lo = std::min(lo, res.stats[c][s].holdout);
      hi = std::max(hi, res.stats[c][s].holdout);
    }
    for (size_t c = 0; c < C; ++c) {
      const double v = res.stats[c][s].holdout;
      const double t = hi > lo ? (v - lo) / (hi - lo) : 0.5;
      const double x = left + label_w + s * cell_w, y = top + c * cell_h;
      svg.rect(x + 1, y + 1, cell_w - 2, cell_h - 2, blend("#f1f5fb", "#1f4e9c", t), "rx=\"3\"");
      svg.text(x + cell_w / 2, y + cell_h / 2 + 4, fmt("%.3f", v), 11, "middle", t > 0.55 ? "#ffffff" : kInk);
    }
  }
  for (size_t c = 0; c < C; ++c) {
    const bool rec = static_cast<int>(c) == res.recommended;
    std::string label = format(res.candidates[c].alloc) + "  " + res.candidates[c].label;
    if (label.size() > 58) label = label.substr(0, 57) + "~";
    svg.text(left + label_w - 10, top + c * cell_h + cell_h / 2 + 4, label, 11, "end", kInk,
             rec ? "font-weight=\"700\"" : "");
  }
  return svg.save(path);
}

bool histogram_chart(const Results& res, const std::string& path) {
  const double left = 70, top = 80, plot_w = 640, plot_h = 340, W = left + plot_w + 30 + 330, H = top + plot_h + 70;
  const auto& pool_name = res.scenarios[res.focus].name;
  double total = 0, ymax = 0;
  for (int v : res.histograms[0]) total += v;
  for (const auto& h : res.histograms)
    for (int v : h) ymax = std::max(ymax, v / total);
  ymax = std::ceil(ymax * 20) / 20;
  Svg svg(W, H);
  svg.title("Head-to-head points distribution",
            "Share of '" + pool_name + "' holdout opponents by points earned in the matchup (0-15)");
  for (double v = 0; v <= ymax + 1e-9; v += 0.05) {
    const double y = top + plot_h - v / ymax * plot_h;
    svg.line(left, y, left + plot_w, y, kGrid);
    svg.text(left - 8, y + 4, fmt("%.0f%%", v * 100), 11, "end", kMuted);
  }
  for (int p = 0; p <= 15; p += 3) {
    const double x = left + p / 15.0 * plot_w;
    svg.text(x, top + plot_h + 20, std::to_string(p), 11, "middle", kMuted);
  }
  svg.text(left + plot_w / 2, top + plot_h + 44, "points earned in the matchup", 12, "middle", kMuted);
  std::vector<std::string> series;
  for (size_t i = 0; i < res.histograms.size(); ++i) {
    std::vector<std::pair<double, double>> pts;
    for (int b = 0; b <= 30; ++b) {
      pts.push_back({left + b / 30.0 * plot_w, top + plot_h - res.histograms[i][b] / total / ymax * plot_h});
    }
    svg.polyline(pts, kPalette[i % kPaletteSize]);
    for (const auto& [x, y] : pts) svg.circle(x, y, 3, kPalette[i % kPaletteSize]);
    series.push_back(series_label(res, res.hist_candidates[i]));
  }
  svg.line(left, top + plot_h, left + plot_w, top + plot_h, kInk);
  legend(svg, left + plot_w + 30, top + 10, series);
  return svg.save(path);
}

bool neighborhood_chart(const Results& res, const std::string& path) {
  const size_t D = res.neighborhood.size();
  const double cell = 64, gap = 60, top = 110, left = 90;
  double vmax = 1e-9;
  for (const auto& g : res.neighborhood)
    for (const auto& row : g)
      for (double v : row)
        if (!std::isnan(v)) vmax = std::max(vmax, std::abs(v));
  const double panel = cell * kCastles;
  Svg svg(left + D * (panel + gap) + 20, top + panel + 70);
  svg.title("Neighbourhood of " + format(res.candidates[res.recommended].alloc),
            "Change in '" + res.scenarios[res.focus].name +
                "' holdout score after moving d troops from row castle to column castle");
  for (size_t d = 0; d < D; ++d) {
    const double x0 = left + d * (panel + gap);
    svg.text(x0 + panel / 2, top - 30, "d = " + std::to_string(d + 1), 13, "middle", kInk, "font-weight=\"600\"");
    for (int j = 0; j < kCastles; ++j)
      svg.text(x0 + j * cell + cell / 2, top - 8, "to " + std::to_string(j + 1), 10, "middle", kMuted);
    for (int i = 0; i < kCastles; ++i) {
      if (d == 0) svg.text(x0 - 8, top + i * cell + cell / 2 + 4, "from " + std::to_string(i + 1), 10, "end", kMuted);
      for (int j = 0; j < kCastles; ++j) {
        const double v = res.neighborhood[d][i][j];
        const double x = x0 + j * cell, y = top + i * cell;
        if (std::isnan(v)) {
          svg.rect(x + 1, y + 1, cell - 2, cell - 2, "#f3f4f6", "rx=\"3\"");
          continue;
        }
        const double t = std::abs(v) / vmax;
        svg.rect(x + 1, y + 1, cell - 2, cell - 2, blend("#f7f7f7", v < 0 ? "#c2413b" : "#2f6bd8", t), "rx=\"3\"");
        svg.text(x + cell / 2, y + cell / 2 + 4, fmt("%+.3f", v), 10, "middle", t > 0.6 ? "#ffffff" : kInk);
      }
    }
  }
  svg.text(left, top + panel + 30, "Red = worse, blue = better; grey = not a legal move. All-red means a local optimum.",
           11, "start", kMuted);
  return svg.save(path);
}

}  // namespace

std::vector<std::string> write_outputs(const Results& res) {
  namespace fs = std::filesystem;
  const fs::path dir(res.cfg.output);
  fs::create_directories(dir);
  std::vector<std::string> written;
  auto out = [&](const std::string& name) {
    written.push_back((dir / name).string());
    return std::ofstream(dir / name);
  };
  const auto& scen = res.scenarios;
  const auto& cands = res.candidates;

  {
    auto f = out("scenario_results.csv");
    f << "scenario,label,allocation,train,holdout,holdout_ci95,diff_vs_reference,diff_ci95,repeat_mean,repeat_sd,"
         "holdout_optimum\n";
    for (size_t s = 0; s < scen.size(); ++s)
      for (size_t c = 0; c < cands.size(); ++c) {
        const auto& st = res.stats[c][s];
        f << scen[s].name << "," << q(cands[c].label) << "," << q(format(cands[c].alloc)) << "," << st.train << ","
          << st.holdout << "," << st.ci_half << "," << st.diff_ref << "," << st.diff_ci_half << "," << st.rep_mean
          << "," << st.rep_sd << "," << res.holdout_best[s] << "\n";
      }
  }
  {
    auto f = out("candidate_matrix.csv");
    f << "label,allocation";
    for (const auto& s : scen) f << "," << s.name;
    if (!res.fp_scores.empty()) f << ",fictitious_play_benchmark";
    f << "\n";
    for (size_t c = 0; c < cands.size(); ++c) {
      f << q(cands[c].label) << "," << q(format(cands[c].alloc));
      for (size_t s = 0; s < scen.size(); ++s) f << "," << res.stats[c][s].holdout;
      if (!res.fp_scores.empty()) f << "," << res.fp_scores[c];
      f << "\n";
    }
  }
  {
    auto f = out("repeats.csv");
    f << "scenario,repeat,dp_pick,train,holdout,robust_pick\n";
    for (size_t s = 0; s < scen.size(); ++s)
      for (size_t r = 0; r < res.repeats[s].size(); ++r) {
        const auto& rep = res.repeats[s][r];
        f << scen[s].name << "," << r << "," << q(format(rep.dp_pick)) << "," << rep.train << "," << rep.holdout
          << "," << q(format(res.robust_picks[r])) << "\n";
      }
  }
  {
    auto f = out("robust_top.csv");
    f << "objective,rank,allocation,value\n";
    for (size_t i = 0; i < res.robust.maximin.size(); ++i)
      f << "maximin_worst_score," << i + 1 << "," << q(format(res.robust.maximin[i].alloc)) << ","
        << res.robust.maximin[i].value << "\n";
    for (size_t i = 0; i < res.robust.minimax_regret.size(); ++i)
      f << "minimax_regret," << i + 1 << "," << q(format(res.robust.minimax_regret[i].alloc)) << ","
        << res.robust.minimax_regret[i].value << "\n";
  }
  {
    auto f = out("adversarial.csv");
    f << "label,allocation,direct_counter,score_vs_direct_counter,direct_counter_focus_score,crowd_counter,"
         "score_vs_crowd_counter,crowd_counter_focus_score\n";
    for (const auto& a : res.adversarial)
      f << q(cands[a.candidate].label) << "," << q(format(cands[a.candidate].alloc)) << "," << q(format(a.counter))
        << "," << a.score_vs_counter << "," << a.counter_focus << "," << q(format(a.crowd_counter)) << ","
        << a.score_vs_crowd_counter << "," << a.crowd_counter_focus << "\n";
  }
  {
    auto f = out("neighborhood.csv");
    f << "d,from_castle,to_castle,delta\n";
    for (size_t d = 0; d < res.neighborhood.size(); ++d)
      for (int i = 0; i < kCastles; ++i)
        for (int j = 0; j < kCastles; ++j)
          if (!std::isnan(res.neighborhood[d][i][j]))
            f << d + 1 << "," << i + 1 << "," << j + 1 << "," << res.neighborhood[d][i][j] << "\n";
  }
  {
    auto f = out("headtohead_hist.csv");
    f << "label,allocation,points,count\n";
    for (size_t i = 0; i < res.histograms.size(); ++i)
      for (int b = 0; b <= 30; ++b)
        f << q(cands[res.hist_candidates[i]].label) << "," << q(format(cands[res.hist_candidates[i]].alloc)) << ","
          << b / 2.0 << "," << res.histograms[i][b] << "\n";
  }
  {
    auto f = out("ties.csv");
    f << "scenario,round_number_share";
    for (int c : res.tie_candidates) f << "," << q("tie_points " + format(cands[c].alloc));
    f << "\n";
    for (size_t s = 0; s < scen.size(); ++s) {
      f << scen[s].name << "," << res.round_share[s];
      for (size_t t = 0; t < res.tie_candidates.size(); ++t) f << "," << res.tie_points[t][s];
      f << "\n";
    }
  }
  if (res.cfg.fp_iterations > 0) {
    auto f = out("fictitious_play.csv");
    f << "kind,iteration_or_rank,allocation,value\n";
    for (const auto& [it, e] : res.fp.trace) f << "exploitability," << it << ",," << e << "\n";
    for (size_t i = 0; i < res.fp.support.size(); ++i)
      f << "support_probability," << i + 1 << "," << q(format(res.fp.support[i].alloc)) << ","
        << res.fp.support[i].value << "\n";
  }

  auto svg = [&](const std::string& name, auto&& draw) {
    if (draw(res, (dir / name).string())) written.push_back((dir / name).string());
  };
  svg("archetype_scores.svg", archetype_chart);
  svg("scenario_heatmap.svg", scenario_heatmap);
  svg("allocation_comparison.svg", allocation_chart);
  svg("headtohead_hist.svg", histogram_chart);
  if (!res.neighborhood.empty()) svg("neighborhood.svg", neighborhood_chart);
  return written;
}

}  // namespace blotto
