#include "search.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "parallel.hpp"

namespace blotto::fourway {

namespace {

constexpr int kSteps[] = {1, 2, 3, 5, 10};

// Bounded heap of the best `keep` entries; front() is the worst kept entry.
void offer(std::vector<Scored>& heap, const Scored& s, size_t keep) {
  if (heap.size() < keep) {
    heap.push_back(s);
    std::push_heap(heap.begin(), heap.end(), better);
  } else if (better(s, heap.front())) {
    std::pop_heap(heap.begin(), heap.end(), better);
    heap.back() = s;
    std::push_heap(heap.begin(), heap.end(), better);
  }
}

}  // namespace

double mean_payout(const Alloc& x, const std::vector<Group>& groups) {
  double sum = 0;
  for (const Group& g : groups) sum += group_payout(x, g);
  return sum / groups.size();
}

Evaluation evaluate(const Alloc& x, const std::vector<Group>& groups) {
  Evaluation e;
  double sum = 0, sum_sq = 0, points = 0;
  for (const Group& g : groups) {
    const Tally t = tally(x, g);
    int higher, equal;
    rank(t, higher, equal);
    const double p = kSplit[higher][equal];
    sum += p;
    sum_sq += p * p;
    points += t.me;
    for (int place = higher; place <= higher + equal; ++place) e.place[place] += 1.0 / (equal + 1);
  }
  const double n = static_cast<double>(groups.size());
  e.payout = sum / n;
  e.ci_half = 1.959963984540054 * std::sqrt(std::max(0.0, (sum_sq - n * e.payout * e.payout) / (n - 1)) / n);
  e.castle_points = points / n / kUnit;
  for (double& p : e.place) p /= n;
  return e;
}

std::vector<Scored> screen_all(const std::vector<Group>& groups, size_t keep, int threads, size_t* visited) {
  const size_t G = groups.size();
  std::vector<std::pair<int, int>> prefixes;
  for (int x1 = 0; x1 <= kTroops; ++x1)
    for (int x2 = 0; x1 + x2 <= kTroops; ++x2) prefixes.push_back({x1, x2});

  struct Local {
    std::vector<Scored> heap;
    std::vector<Tally> t12, t123;
    size_t visited = 0;
  };
  threads = resolve_threads(threads);
  std::vector<Local> locals(threads);
  for (auto& l : locals) l.t12.resize(G), l.t123.resize(G);

  // Castles 1-3 are tallied once per prefix and reused for every (x4, x5) completion.
  parallel_for(prefixes.size(), threads, [&](size_t task, int worker) {
    Local& L = locals[worker];
    const auto [x1, x2] = prefixes[task];
    for (size_t g = 0; g < G; ++g) {
      L.t12[g] = Tally{};
      add_castle(L.t12[g], groups[g], 0, x1);
      add_castle(L.t12[g], groups[g], 1, x2);
    }
    const int rest12 = kTroops - x1 - x2;
    for (int x3 = 0; x3 <= rest12; ++x3) {
      for (size_t g = 0; g < G; ++g) {
        L.t123[g] = L.t12[g];
        add_castle(L.t123[g], groups[g], 2, x3);
      }
      const int rest123 = rest12 - x3;
      for (int x4 = 0; x4 <= rest123; ++x4) {
        const int x5 = rest123 - x4;
        double sum = 0;
        for (size_t g = 0; g < G; ++g) {
          Tally t = L.t123[g];
          add_castle(t, groups[g], 3, x4);
          add_castle(t, groups[g], 4, x5);
          sum += finish(t);
        }
        ++L.visited;
        offer(L.heap, {{x1, x2, x3, x4, x5}, sum / G}, keep);
      }
    }
  });

  std::vector<Scored> out;
  size_t total = 0;
  for (auto& l : locals) {
    total += l.visited;
    out.insert(out.end(), l.heap.begin(), l.heap.end());
  }
  std::sort(out.begin(), out.end(), better);
  if (out.size() > keep) out.resize(keep);
  if (visited) *visited = total;
  return out;
}

std::vector<Scored> score_all(const std::vector<Alloc>& xs, const std::vector<Group>& groups, int threads) {
  std::vector<Scored> out(xs.size());
  parallel_for(xs.size(), resolve_threads(threads),
               [&](size_t i, int) { out[i] = {xs[i], mean_payout(xs[i], groups)}; });
  std::sort(out.begin(), out.end(), better);
  return out;
}

Scored hill_climb(const Alloc& start, const std::function<double(const Alloc&)>& objective, int threads) {
  Scored cur{start, objective(start)};
  for (;;) {
    std::vector<Alloc> neighbours;
    for (int d : kSteps)
      for (int from = 0; from < kCastles; ++from)
        for (int to = 0; to < kCastles; ++to) {
          if (from == to || cur.alloc[from] < d) continue;
          Alloc y = cur.alloc;
          y[from] -= d;
          y[to] += d;
          neighbours.push_back(y);
        }
    std::vector<Scored> scored(neighbours.size());
    parallel_for(neighbours.size(), resolve_threads(threads),
                 [&](size_t i, int) { scored[i] = {neighbours[i], objective(neighbours[i])}; });
    const auto best = std::min_element(scored.begin(), scored.end(), better);
    if (best == scored.end() || best->value <= cur.value) return cur;
    cur = *best;
  }
}

}  // namespace blotto::fourway
