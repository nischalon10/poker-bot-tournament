#include "optimize.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <utility>

#include "parallel.hpp"

namespace blotto {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

// Keeps `top` sorted best-first with at most k entries. `better` must be a strict
// total order (value, then allocation) so the merged result is thread-independent.
template <class Better>
void offer(std::vector<Ranked>& top, const Ranked& r, size_t k, Better better) {
  if (top.size() == k && !better(r, top.back())) return;
  auto pos = std::upper_bound(top.begin(), top.end(), r, better);
  top.insert(pos, r);
  if (top.size() > k) top.pop_back();
}

bool higher_first(const Ranked& a, const Ranked& b) {
  return a.value != b.value ? a.value > b.value : a.alloc < b.alloc;
}

bool lower_first(const Ranked& a, const Ranked& b) {
  return a.value != b.value ? a.value < b.value : a.alloc < b.alloc;
}

void brute_force(const MarginalTable& t, int castle, int left, double acc, std::vector<int>& cur,
                 Best& best) {
  if (castle == t.castles - 1) {
    cur[castle] = left;
    const double v = acc + t.at(castle, left);
    if (v > best.score) best = {cur, v};
    return;
  }
  for (int k = 0; k <= left; ++k) {
    cur[castle] = k;
    brute_force(t, castle + 1, left - k, acc + t.at(castle, k), cur, best);
  }
}

}  // namespace

double MarginalTable::score(const int* x) const {
  double s = 0;
  for (int i = 0; i < castles; ++i) s += at(i, x[i]);
  return s;
}

MarginalTable marginals_from_counts(const std::vector<double>& counts, double n, int castles,
                                    int total) {
  const int K = total + 1;
  MarginalTable t{castles, total, std::vector<double>(static_cast<size_t>(castles) * K)};
  for (int c = 0; c < castles; ++c) {
    double below = 0;
    for (int k = 0; k < K; ++k) {
      const double equal = counts[c * K + k];
      t.m[c * K + k] = (c + 1) * (below + 0.5 * equal) / n;
      below += equal;
    }
  }
  return t;
}

MarginalTable build_marginals(const int* pool, size_t n, int castles, int total) {
  const int K = total + 1;
  std::vector<double> counts(static_cast<size_t>(castles) * K, 0.0);
  for (size_t j = 0; j < n; ++j)
    for (int c = 0; c < castles; ++c) counts[c * K + pool[j * castles + c]] += 1.0;
  return marginals_from_counts(counts, static_cast<double>(n), castles, total);
}

MarginalTable build_marginals(const std::vector<Alloc>& pool) {
  return build_marginals(pool.empty() ? nullptr : pool.front().data(), pool.size(), kCastles,
                         kTroops);
}

Best dp_best(const MarginalTable& t) {
  const int C = t.castles, T = t.total, K = T + 1;
  // value[c][u]: best score from castles 0..c using exactly u troops;
  // choice[c][u]: troops placed on castle c in that optimum.
  std::vector<double> value(static_cast<size_t>(C) * K, -kInf);
  std::vector<int> choice(static_cast<size_t>(C) * K, 0);
  for (int u = 0; u <= T; ++u) {
    value[u] = t.at(0, u);
    choice[u] = u;
  }
  for (int c = 1; c < C; ++c) {
    for (int u = 0; u <= T; ++u) {
      double& best = value[c * K + u];
      for (int k = 0; k <= u; ++k) {
        const double v = value[(c - 1) * K + (u - k)] + t.at(c, k);
        if (v > best) {
          best = v;
          choice[c * K + u] = k;
        }
      }
    }
  }
  Best out{std::vector<int>(C), value[(C - 1) * K + T]};
  for (int c = C - 1, u = T; c >= 0; --c) {
    out.alloc[c] = choice[c * K + u];
    u -= out.alloc[c];
  }
  return out;
}

Alloc dp_best_alloc(const MarginalTable& t) {
  const Best b = dp_best(t);
  Alloc x;
  std::copy(b.alloc.begin(), b.alloc.end(), x.begin());
  return x;
}

Best brute_force_best(const MarginalTable& t) {
  Best best{{}, -kInf};
  std::vector<int> cur(t.castles);
  brute_force(t, 0, t.total, 0.0, cur, best);
  return best;
}

RobustResult robust_search(const std::vector<const MarginalTable*>& tables, int threads,
                           int top_k) {
  constexpr int K = kTroops + 1;
  const size_t S = tables.size();
  const size_t k = static_cast<size_t>(top_k);
  RobustResult out;
  for (const auto* t : tables) out.best.push_back(dp_best(*t).score);

  // Interleave as [castle][troops][scenario] so the innermost loop is contiguous.
  std::vector<double> inter(kCastles * K * S);
  for (int c = 0; c < kCastles; ++c)
    for (int x = 0; x < K; ++x)
      for (size_t s = 0; s < S; ++s) inter[(c * K + x) * S + s] = tables[s]->at(c, x);
  auto column = [&](int c, int x) { return &inter[(c * K + x) * S]; };

  // One task per (x1, x2) prefix: 5,151 tasks, dynamically balanced across workers.
  std::vector<std::pair<int, int>> prefixes;
  for (int x1 = 0; x1 <= kTroops; ++x1)
    for (int x2 = 0; x1 + x2 <= kTroops; ++x2) prefixes.push_back({x1, x2});

  struct Local {
    std::vector<Ranked> maximin, regret;
    size_t visited = 0;
    std::vector<double> base12, base123;
  };
  threads = resolve_threads(threads);
  std::vector<Local> locals(threads);
  for (auto& l : locals) l.base12.resize(S), l.base123.resize(S);

  parallel_for(prefixes.size(), threads, [&](size_t task, int worker) {
    Local& L = locals[worker];
    const auto [x1, x2] = prefixes[task];
    const double* a1 = column(0, x1);
    const double* a2 = column(1, x2);
    for (size_t s = 0; s < S; ++s) L.base12[s] = a1[s] + a2[s];
    const int rest12 = kTroops - x1 - x2;
    for (int x3 = 0; x3 <= rest12; ++x3) {
      const double* a3 = column(2, x3);
      for (size_t s = 0; s < S; ++s) L.base123[s] = L.base12[s] + a3[s];
      const int rest123 = rest12 - x3;
      for (int x4 = 0; x4 <= rest123; ++x4) {
        const int x5 = rest123 - x4;
        const double* a4 = column(3, x4);
        const double* a5 = column(4, x5);
        double worst = kInf, regret = -kInf;
        for (size_t s = 0; s < S; ++s) {
          const double v = L.base123[s] + a4[s] + a5[s];
          worst = std::min(worst, v);
          regret = std::max(regret, out.best[s] - v);
        }
        ++L.visited;
        const Alloc x{x1, x2, x3, x4, x5};
        offer(L.maximin, {x, worst}, k, higher_first);
        offer(L.regret, {x, regret}, k, lower_first);
      }
    }
  });

  for (const auto& l : locals) {
    out.visited += l.visited;
    for (const auto& r : l.maximin) offer(out.maximin, r, k, higher_first);
    for (const auto& r : l.regret) offer(out.minimax_regret, r, k, lower_first);
  }
  return out;
}

FictitiousPlay fictitious_play(int iterations, const Alloc& start) {
  constexpr int K = kTroops + 1;
  iterations = std::max(iterations, 1);
  std::vector<double> counts(kCastles * K, 0.0);
  std::map<Alloc, int> frequency;
  auto add = [&](const Alloc& x) {
    for (int c = 0; c < kCastles; ++c) counts[c * K + x[c]] += 1.0;
    ++frequency[x];
  };

  FictitiousPlay out;
  out.iterations = iterations;
  add(start);
  const int trace_every = std::max(1, iterations / 20);
  for (int it = 1; it < iterations; ++it) {
    const MarginalTable table = marginals_from_counts(counts, it, kCastles, kTroops);
    const Best br = dp_best(table);
    if (it % trace_every == 0) out.trace.push_back({it, br.score - kTotalPoints / 2});
    Alloc x;
    std::copy(br.alloc.begin(), br.alloc.end(), x.begin());
    add(x);
  }
  out.mixture = marginals_from_counts(counts, iterations, kCastles, kTroops);
  out.exploitability = dp_best(out.mixture).score - kTotalPoints / 2;
  out.trace.push_back({iterations, out.exploitability});

  for (const auto& [x, n] : frequency)
    offer(out.support, {x, static_cast<double>(n) / iterations}, 10, higher_first);
  return out;
}

}  // namespace blotto
