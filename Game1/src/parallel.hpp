#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace blotto {

inline int resolve_threads(int requested) {
  if (requested > 0) return requested;
  const unsigned hw = std::thread::hardware_concurrency();
  return hw ? static_cast<int>(hw) : 1;
}

// Calls f(index, worker) for every index in [0, n) across `threads` workers.
// Indices are handed out dynamically, so f must not depend on which worker runs it
// except to pick per-worker scratch space.
template <class F>
void parallel_for(size_t n, int threads, F&& f) {
  threads = std::max(1, static_cast<int>(std::min<size_t>(threads, n)));
  std::atomic<size_t> next{0};
  auto work = [&](int worker) {
    for (size_t i; (i = next.fetch_add(1)) < n;) f(i, worker);
  };
  std::vector<std::thread> pool;
  for (int w = 1; w < threads; ++w) pool.emplace_back(work, w);
  work(0);
  for (auto& t : pool) t.join();
}

}  // namespace blotto
