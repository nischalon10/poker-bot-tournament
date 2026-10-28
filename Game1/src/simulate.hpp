#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "blotto.hpp"
#include "opponents.hpp"
#include "optimize.hpp"

namespace blotto {

struct Config {
  size_t samples = 50000;           // opponents per pool
  uint64_t seed = 42;
  int repeats = 5;                  // independent (train, holdout) pool pairs per scenario
  std::string scenario = "all";     // all | pure | mixes | name[,name...]
  std::string output = "outputs";
  int threads = 0;                  // 0 = all hardware threads
  int fp_iterations = 5000;         // 0 disables the fictitious-play benchmark
  std::string objective = "regret"; // regret | maximin: which robust pick to recommend
  std::string focus = "uniform-blend";  // scenario used for histograms / neighbourhood
  int neighborhood_max = 3;         // largest transfer size d in the neighbourhood heatmap
  std::vector<std::pair<std::string, Alloc>> extra_candidates;
};

struct Candidate {
  std::string label;
  Alloc alloc;
};

// One candidate under one scenario.
struct CandidateStats {
  double train = 0;         // repeat-0 training pool (the pool the optimiser saw)
  double holdout = 0;       // repeat-0 independent holdout pool
  double ci_half = 0;       // 95% half-width from per-opponent payoff spread on the holdout
  double diff_ref = 0;      // paired holdout difference vs the reference candidate
  double diff_ci_half = 0;
  double rep_mean = 0;      // holdout score averaged over all repeats
  double rep_sd = 0;        // its standard deviation across repeats
};

struct ScenarioRepeat {
  Alloc dp_pick;            // exact best response to this repeat's training pool
  double train = 0;
  double holdout = 0;
};

struct AdversarialRow {
  int candidate;
  Alloc counter;            // exact best response to the candidate alone
  double score_vs_counter;  // candidate's points against it
  double counter_focus;     // the counter's own holdout score in the focus scenario
  Alloc crowd_counter;      // best response to a crowd of jittered copies of the candidate
  double score_vs_crowd_counter;
  double crowd_counter_focus;
};

struct Results {
  Config cfg;
  int threads = 1;
  double seconds = 0;
  std::vector<Scenario> scenarios;
  int focus = 0;  // index into scenarios

  std::vector<Candidate> candidates;
  int reference = 0;     // (0,13,21,30,36)
  int maximin = 0;
  int regret = 0;
  int recommended = 0;
  std::vector<int> best_under;  // per scenario: candidate index of the repeat-0 DP pick

  std::vector<std::vector<CandidateStats>> stats;     // [candidate][scenario]
  std::vector<std::vector<ScenarioRepeat>> repeats;   // [scenario][repeat]
  std::vector<double> holdout_best;                   // [scenario] exact optimum on the repeat-0 holdout
  RobustResult robust;                                // repeat 0 training pools
  std::vector<Alloc> robust_picks;                    // objective's pick in each repeat

  std::vector<AdversarialRow> adversarial;
  FictitiousPlay fp;
  std::vector<double> fp_scores;  // [candidate] score vs the fictitious-play mixture

  // [d - 1][from][to]: change in focus-scenario holdout score after moving d troops
  // from castle `from` to castle `to` in the recommended allocation (NaN if illegal).
  std::vector<std::array<std::array<double, kCastles>, kCastles>> neighborhood;

  std::vector<int> hist_candidates;                      // candidate indices
  std::vector<std::array<int, 2 * 15 + 1>> histograms;   // counts of 2*P(x,y) on focus holdout

  std::vector<int> tie_candidates;
  std::vector<std::vector<double>> tie_points;  // [tie candidate][scenario]: expected points from ties
  std::vector<double> round_share;              // [scenario]: holdout share with all entries % 5 == 0
};

Results simulate(const Config& cfg);

}  // namespace blotto
