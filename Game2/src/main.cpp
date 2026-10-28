#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>

#include "experiment.hpp"

namespace {

void usage() {
  std::printf(
      "Usage: fourway [options]\n"
      "  --seed S              RNG seed (default 42)\n"
      "  --scenario SPEC       all | game1 | aware | pure | mixes | name[,name...] (default all)\n"
      "                        aware-25 / aware-50 / aware-only add Game-2-aware concentrators\n"
      "  --screen-groups N     stage 1 groups for the exhaustive screen (default 512)\n"
      "  --screen-keep N       survivors of stage 1 (default 20000)\n"
      "  --refine-groups N     stage 2 groups (default 8192)\n"
      "  --refine-keep N       survivors of stage 2 (default 500)\n"
      "  --select-groups N     stage 3 / hill-climb / robust-selection groups (default 131072)\n"
      "  --holdout-groups N    independent groups for reported scores (default 200000)\n"
      "  --climb-starts N      hill-climb from this many stage-3 leaders (default 5)\n"
      "  --objective O         regret | maximin (default regret)\n"
      "  --threads T           worker threads, 0 = all cores (default 0)\n"
      "  --output DIR          CSV output directory (default outputs)\n"
      "  --candidate A         extra allocation to evaluate, e.g. 0,0,0,48,52 (repeatable)\n");
}

}  // namespace

int main(int argc, char** argv) {
  blotto::fourway::Config cfg;
  try {
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i], value;
      if (arg == "--help" || arg == "-h") {
        usage();
        return 0;
      }
      if (const auto eq = arg.find('='); eq != std::string::npos) {
        value = arg.substr(eq + 1);
        arg = arg.substr(0, eq);
      } else if (i + 1 < argc) {
        value = argv[++i];
      } else {
        throw std::invalid_argument("missing value for " + arg);
      }

      if (arg == "--seed") cfg.seed = std::stoull(value);
      else if (arg == "--scenario") cfg.scenario = value;
      else if (arg == "--screen-groups") cfg.screen_groups = std::stoull(value);
      else if (arg == "--screen-keep") cfg.screen_keep = std::stoull(value);
      else if (arg == "--refine-groups") cfg.refine_groups = std::stoull(value);
      else if (arg == "--refine-keep") cfg.refine_keep = std::stoull(value);
      else if (arg == "--select-groups") cfg.select_groups = std::stoull(value);
      else if (arg == "--holdout-groups") cfg.holdout_groups = std::stoull(value);
      else if (arg == "--climb-starts") cfg.climb_starts = std::stoi(value);
      else if (arg == "--objective") cfg.objective = value;
      else if (arg == "--threads") cfg.threads = std::stoi(value);
      else if (arg == "--output") cfg.output = value;
      else if (arg == "--candidate") {
        const auto x = blotto::parse_alloc(value);
        if (!x) throw std::invalid_argument("invalid allocation (need 5 nonnegative ints summing to 100): " + value);
        cfg.extra_candidates.push_back({"custom " + blotto::format(*x), *x});
      } else {
        throw std::invalid_argument("unknown option " + arg);
      }
    }
    return blotto::fourway::run(cfg);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n\n", e.what());
    usage();
    return 1;
  }
}
