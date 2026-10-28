#include <cstdio>
#include <exception>
#include <string>

#include "report.hpp"
#include "simulate.hpp"

namespace {

void usage() {
  std::printf(
      "Usage: blotto [options]\n"
      "  --samples N         opponents per pool (default 50000)\n"
      "  --seed S            RNG seed (default 42)\n"
      "  --repeats R         independent train/holdout pool pairs per scenario (default 5)\n"
      "  --scenario SPEC     all | pure | mixes | name[,name...] (default all)\n"
      "                      names: proportional equal value sacrifice dispersed strategic\n"
      "                             naive-heavy strategic-heavy value-heavy uniform-blend\n"
      "  --output DIR        where CSV/SVG files go (default outputs)\n"
      "  --threads T         worker threads, 0 = all cores (default 0)\n"
      "  --objective O       regret | maximin: robust pick to recommend (default regret)\n"
      "  --focus NAME        scenario for histogram/neighbourhood charts (default uniform-blend)\n"
      "  --fp-iters N        fictitious-play iterations, 0 disables (default 5000)\n"
      "  --neighborhood D    largest transfer size in the neighbourhood heatmap (default 3)\n"
      "  --candidate A       extra candidate to track, e.g. 0,12,22,30,36 (repeatable)\n"
      "  --help\n");
}

}  // namespace

int main(int argc, char** argv) {
  blotto::Config cfg;
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

      if (arg == "--samples") cfg.samples = std::stoull(value);
      else if (arg == "--seed") cfg.seed = std::stoull(value);
      else if (arg == "--repeats") cfg.repeats = std::stoi(value);
      else if (arg == "--scenario") cfg.scenario = value;
      else if (arg == "--output") cfg.output = value;
      else if (arg == "--threads") cfg.threads = std::stoi(value);
      else if (arg == "--objective") cfg.objective = value;
      else if (arg == "--focus") cfg.focus = value;
      else if (arg == "--fp-iters") cfg.fp_iterations = std::stoi(value);
      else if (arg == "--neighborhood") cfg.neighborhood_max = std::stoi(value);
      else if (arg == "--candidate") {
        const auto x = blotto::parse_alloc(value);
        if (!x) throw std::invalid_argument("invalid allocation (need 5 nonnegative ints summing to 100): " + value);
        cfg.extra_candidates.push_back({"custom " + blotto::format(*x), *x});
      } else {
        throw std::invalid_argument("unknown option " + arg);
      }
    }

    const blotto::Results res = blotto::simulate(cfg);
    blotto::print_report(res);
    const auto files = blotto::write_outputs(res);
    std::printf("\nWrote %zu files to %s\n", files.size(), cfg.output.c_str());
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n\n", e.what());
    usage();
    return 1;
  }
  return 0;
}
