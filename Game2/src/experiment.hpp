#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "blotto.hpp"

namespace blotto::fourway {

struct Config {
  uint64_t seed = 42;
  std::string scenario = "all";      // all | game1 | aware | pure | mixes | name[,name...]
  size_t screen_groups = 512;        // stage 1: every legal allocation on this many groups
  size_t screen_keep = 20000;
  size_t refine_groups = 8192;       // stage 2
  size_t refine_keep = 500;
  size_t select_groups = 131072;     // stage 3 + hill climbing + robust selection
  size_t holdout_groups = 200000;    // independent groups for the reported scores
  int climb_starts = 5;
  int threads = 0;
  std::string objective = "regret";  // regret | maximin
  std::string output = "outputs";
  std::vector<std::pair<std::string, Alloc>> extra_candidates;
};

// Runs the full search, prints the report and writes CSVs. Returns a process exit code.
int run(const Config& cfg);

}  // namespace blotto::fourway
