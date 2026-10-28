#pragma once

// Minimal dependency-free SVG charting shared by the Game 1 and Game 2 reports.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace blotto::svg {

inline const char* const kPalette[] = {"#2f6bd8", "#e8833a", "#2a9d6f", "#c2416b", "#7b5cc4", "#8c939d"};
inline constexpr int kPaletteSize = 6;
inline const char* const kInk = "#1f2328";
inline const char* const kMuted = "#6b7280";
inline const char* const kGrid = "#e5e7eb";

inline std::string fmt(const char* f, double v) {
  char buf[64];
  std::snprintf(buf, sizeof buf, f, v);
  return buf;
}

inline std::string escape(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else out += c;
  }
  return out;
}

// Minimal SVG writer: white background, system font.
class Svg {
 public:
  Svg(double w, double h) : w_(w), h_(h) {}

  void rect(double x, double y, double w, double h, const std::string& fill, const std::string& extra = "") {
    body_ << "<rect x=\"" << x << "\" y=\"" << y << "\" width=\"" << std::max(0.0, w) << "\" height=\""
          << std::max(0.0, h) << "\" fill=\"" << fill << "\" " << extra << "/>\n";
  }
  void line(double x1, double y1, double x2, double y2, const std::string& stroke, double width = 1) {
    body_ << "<line x1=\"" << x1 << "\" y1=\"" << y1 << "\" x2=\"" << x2 << "\" y2=\"" << y2 << "\" stroke=\""
          << stroke << "\" stroke-width=\"" << width << "\"/>\n";
  }
  void polyline(const std::vector<std::pair<double, double>>& pts, const std::string& stroke) {
    body_ << "<polyline fill=\"none\" stroke=\"" << stroke << "\" stroke-width=\"2\" points=\"";
    for (const auto& [x, y] : pts) body_ << x << "," << y << " ";
    body_ << "\"/>\n";
  }
  void circle(double x, double y, double r, const std::string& fill) {
    body_ << "<circle cx=\"" << x << "\" cy=\"" << y << "\" r=\"" << r << "\" fill=\"" << fill << "\"/>\n";
  }
  void text(double x, double y, const std::string& s, double size = 12, const std::string& anchor = "start",
            const std::string& fill = kInk, const std::string& extra = "") {
    body_ << "<text x=\"" << x << "\" y=\"" << y << "\" font-size=\"" << size << "\" text-anchor=\"" << anchor
          << "\" fill=\"" << fill << "\" " << extra << ">" << escape(s) << "</text>\n";
  }
  void title(const std::string& t, const std::string& subtitle) {
    text(24, 32, t, 18, "start", kInk, "font-weight=\"600\"");
    text(24, 52, subtitle, 12, "start", kMuted);
  }
  bool save(const std::string& path) const {
    std::ofstream f(path);
    f << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << w_ << "\" height=\"" << h_ << "\" viewBox=\"0 0 "
      << w_ << " " << h_ << "\" font-family=\"-apple-system, Segoe UI, Helvetica, Arial, sans-serif\">\n"
      << "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>\n"
      << body_.str() << "</svg>\n";
    return static_cast<bool>(f);
  }

 private:
  double w_, h_;
  std::ostringstream body_;
};

// Linear blend between two #rrggbb colours.
inline std::string blend(const std::string& a, const std::string& b, double t) {
  t = std::clamp(t, 0.0, 1.0);
  auto channel = [](const std::string& c, int i) { return std::stoi(c.substr(1 + 2 * i, 2), nullptr, 16); };
  char buf[8];
  std::snprintf(buf, sizeof buf, "#%02x%02x%02x",
                static_cast<int>(std::lround(channel(a, 0) + t * (channel(b, 0) - channel(a, 0)))),
                static_cast<int>(std::lround(channel(a, 1) + t * (channel(b, 1) - channel(a, 1)))),
                static_cast<int>(std::lround(channel(a, 2) + t * (channel(b, 2) - channel(a, 2)))));
  return buf;
}

inline void legend(Svg& svg, double x, double y, const std::vector<std::string>& series) {
  for (size_t i = 0; i < series.size(); ++i) {
    svg.rect(x, y + i * 22 - 10, 12, 12, kPalette[i % kPaletteSize], "rx=\"2\"");
    svg.text(x + 18, y + i * 22, series[i], 12);
  }
}

// values[series][group]; y axis from 0 to ymax.
inline bool grouped_bars(const std::string& path, const std::string& title, const std::string& subtitle,
                         const std::string& ylabel, const std::vector<std::string>& groups,
                         const std::vector<std::string>& series, const std::vector<std::vector<double>>& values,
                         double ymax, double ytick, const char* value_fmt) {
  const double left = 70, top = 80, plot_w = std::max(560.0, 120.0 * groups.size()), plot_h = 360;
  const double legend_w = 330, W = left + plot_w + 30 + legend_w, H = top + plot_h + 70;
  Svg svg(W, H);
  svg.title(title, subtitle);
  for (double v = 0; v <= ymax + 1e-9; v += ytick) {
    const double y = top + plot_h - v / ymax * plot_h;
    svg.line(left, y, left + plot_w, y, kGrid);
    svg.text(left - 8, y + 4, fmt("%g", v), 11, "end", kMuted);
  }
  svg.text(18, top + plot_h / 2, ylabel, 12, "middle", kMuted,
           "transform=\"rotate(-90 18 " + fmt("%g", top + plot_h / 2) + ")\"");
  const double group_w = plot_w / groups.size();
  const double bar_w = std::min(28.0, (group_w - 16) / series.size());
  for (size_t g = 0; g < groups.size(); ++g) {
    const double gx = left + g * group_w + (group_w - bar_w * series.size()) / 2;
    for (size_t s = 0; s < series.size(); ++s) {
      const double v = values[s][g];
      const double h = v / ymax * plot_h;
      svg.rect(gx + s * bar_w, top + plot_h - h, bar_w - 2, h, kPalette[s % kPaletteSize]);
      if (series.size() * groups.size() <= 40)
        svg.text(gx + s * bar_w + bar_w / 2 - 1, top + plot_h - h - 4, fmt(value_fmt, v), 9, "middle", kMuted);
    }
    svg.text(left + g * group_w + group_w / 2, top + plot_h + 20, groups[g], 12, "middle");
  }
  svg.line(left, top + plot_h, left + plot_w, top + plot_h, kInk);
  legend(svg, left + plot_w + 30, top + 10, series);
  return svg.save(path);
}

// values[row][col] coloured on a fixed [lo, hi] scale; `bold_row` (or -1) is emphasised.
inline bool heatmap(const std::string& path, const std::string& title, const std::string& subtitle,
                    const std::vector<std::string>& rows, const std::vector<std::string>& cols,
                    const std::vector<std::vector<double>>& values, double lo, double hi, int bold_row,
                    const char* value_fmt) {
  const double label_w = 300, cell_w = 84, cell_h = 30, top = 130, left = 24;
  Svg svg(left + label_w + cell_w * cols.size() + 60, top + cell_h * rows.size() + 40);
  svg.title(title, subtitle);
  for (size_t c = 0; c < cols.size(); ++c) {
    const double x = left + label_w + c * cell_w + cell_w / 2;
    svg.text(x, top - 12, cols[c], 11, "start", kInk,
             "transform=\"rotate(-35 " + fmt("%g", x) + " " + fmt("%g", top - 12) + ")\"");
  }
  for (size_t r = 0; r < rows.size(); ++r) {
    const double y = top + r * cell_h;
    svg.text(left + label_w - 10, y + cell_h / 2 + 4, rows[r], 11, "end", kInk,
             static_cast<int>(r) == bold_row ? "font-weight=\"700\"" : "");
    for (size_t c = 0; c < cols.size(); ++c) {
      const double t = std::clamp((values[r][c] - lo) / (hi - lo), 0.0, 1.0);
      const double x = left + label_w + c * cell_w;
      svg.rect(x + 1, y + 1, cell_w - 2, cell_h - 2, blend("#f1f5fb", "#1f4e9c", t), "rx=\"3\"");
      svg.text(x + cell_w / 2, y + cell_h / 2 + 4, fmt(value_fmt, values[r][c]), 11, "middle",
               t > 0.55 ? "#ffffff" : kInk);
    }
  }
  return svg.save(path);
}

// values[series][x] over categorical x positions.
inline bool line_chart(const std::string& path, const std::string& title, const std::string& subtitle,
                       const std::string& xlabel, const std::string& ylabel, const std::vector<std::string>& xs,
                       const std::vector<std::string>& series, const std::vector<std::vector<double>>& values,
                       double ymax, double ytick) {
  const double left = 70, top = 80, plot_w = 560, plot_h = 340, W = left + plot_w + 30 + 330,
               H = top + plot_h + 70;
  Svg svg(W, H);
  svg.title(title, subtitle);
  for (double v = 0; v <= ymax + 1e-9; v += ytick) {
    const double y = top + plot_h - v / ymax * plot_h;
    svg.line(left, y, left + plot_w, y, kGrid);
    svg.text(left - 8, y + 4, fmt("%g", v), 11, "end", kMuted);
  }
  svg.text(18, top + plot_h / 2, ylabel, 12, "middle", kMuted,
           "transform=\"rotate(-90 18 " + fmt("%g", top + plot_h / 2) + ")\"");
  auto x_at = [&](size_t i) { return left + 30 + i * (plot_w - 60) / std::max<size_t>(1, xs.size() - 1); };
  for (size_t i = 0; i < xs.size(); ++i) svg.text(x_at(i), top + plot_h + 20, xs[i], 12, "middle");
  svg.text(left + plot_w / 2, top + plot_h + 44, xlabel, 12, "middle", kMuted);
  for (size_t s = 0; s < series.size(); ++s) {
    std::vector<std::pair<double, double>> pts;
    for (size_t i = 0; i < xs.size(); ++i) pts.push_back({x_at(i), top + plot_h - values[s][i] / ymax * plot_h});
    svg.polyline(pts, kPalette[s % kPaletteSize]);
    for (const auto& [x, y] : pts) svg.circle(x, y, 4, kPalette[s % kPaletteSize]);
  }
  svg.line(left, top + plot_h, left + plot_w, top + plot_h, kInk);
  legend(svg, left + plot_w + 30, top + 10, series);
  return svg.save(path);
}

// Horizontal 100% stacked bars: values[row][segment] are shares summing to 1.
inline bool stacked_bars(const std::string& path, const std::string& title, const std::string& subtitle,
                         const std::vector<std::string>& rows, const std::vector<std::string>& segments,
                         const std::vector<std::string>& colors, const std::vector<std::vector<double>>& values) {
  const double label_w = 300, bar_w = 560, bar_h = 26, gap = 10, top = 100, left = 24;
  Svg svg(left + label_w + bar_w + 40, top + rows.size() * (bar_h + gap) + 30);
  svg.title(title, subtitle);
  for (size_t k = 0; k < segments.size(); ++k) {
    const double x = left + label_w + k * 110;
    svg.rect(x, 70, 12, 12, colors[k], "rx=\"2\"");
    svg.text(x + 18, 80, segments[k], 12);
  }
  for (size_t r = 0; r < rows.size(); ++r) {
    const double y = top + r * (bar_h + gap);
    svg.text(left + label_w - 10, y + bar_h / 2 + 4, rows[r], 11, "end");
    double x = left + label_w;
    for (size_t k = 0; k < segments.size(); ++k) {
      const double w = values[r][k] * bar_w;
      svg.rect(x, y, w, bar_h, colors[k]);
      if (w > 34) svg.text(x + w / 2, y + bar_h / 2 + 4, fmt("%.0f%%", 100 * values[r][k]), 10, "middle", "#ffffff");
      x += w;
    }
  }
  return svg.save(path);
}

}  // namespace blotto::svg
