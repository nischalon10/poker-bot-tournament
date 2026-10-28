# Game 1 simulator: Weighted Colonel Blotto (C++)

Technical reference for the Game 1 code. The write-up of how I approached the problem is in
[game1.md](game1.md).

A fast C++20 simulator for Game 1. It picks a single allocation of exactly 100
integer soldiers over five castles worth `[1,2,3,4,5]`. It does this by simulating many
hypothesised applicant populations and scoring candidates by **expected castle points per matchup**.

The opponent populations are **modelled hypotheses, not observed applicant data**. The output is a
model-based recommendation, not a provably optimal pure strategy.

## Build and run

You need a C++20 compiler (clang or gcc) and `make`. There are no other dependencies.

```sh
make                 # builds build/blotto and build/test_blotto
make test            # runs the test suite
./build/blotto --samples 50000 --seed 42 --scenario all --output outputs/
```

On a 10-core laptop, the default run takes about 0.7 s. That covers 100 pools of 50k opponents,
five exhaustive robust searches over all 4,598,126 legal allocations, and 5000 rounds of
fictitious play.

### Options

| Flag | Default | Meaning |
| --- | --- | --- |
| `--samples N` | 50000 | opponents per pool |
| `--seed S` | 42 | RNG seed (results are identical for any thread count) |
| `--repeats R` | 5 | independent train/holdout pool pairs per scenario |
| `--scenario SPEC` | all | `all`, `pure`, `mixes`, or comma-separated names |
| `--output DIR` | outputs | where CSV/SVG files go |
| `--threads T` | 0 (all cores) | worker threads |
| `--objective O` | regret | `regret` (minimax regret) or `maximin` (best worst-case score) |
| `--focus NAME` | uniform-blend | scenario used for the histogram and neighbourhood charts |
| `--fp-iters N` | 5000 | fictitious-play iterations (0 disables) |
| `--neighborhood D` | 3 | largest transfer size in the neighbourhood heatmap |
| `--candidate A` | — | extra allocation to track, e.g. `--candidate 0,12,22,30,36` (repeatable) |

## How it works

| File | Contents |
| --- | --- |
| [src/blotto.hpp](src/blotto.hpp) | rules: validation, payoff (exact, in half-points), parsing |
| [src/rng.hpp](src/rng.hpp) | xoshiro256** with portable normal/gamma, giving reproducible streams per (seed, scenario, repeat, pool) |
| [src/opponents.cpp](src/opponents.cpp) | six archetype generators, scenario catalog, stratified pool sampling |
| [src/optimize.cpp](src/optimize.cpp) | marginal-payoff table, DP optimizer, exhaustive robust search, fictitious play |
| [src/simulate.cpp](src/simulate.cpp) | train/holdout experiments, repeats, CIs, stress tests, neighbourhood, ties |
| [src/report.cpp](src/report.cpp) | terminal tables and the final recommendation |
| [src/plots.cpp](src/plots.cpp) | CSV export and dependency-free SVG charts |
| [tests/test_blotto.cpp](tests/test_blotto.cpp) | rules, identities, generator validity, DP vs brute force |

**Marginal table.** For a pool, `M[i][k] = i * (count(y_i < k) + 0.5 * count(y_i == k)) / N`. The
pool-average payoff of `x` is then exactly `sum_i M[i][x_i]`. Scoring any allocation costs five
lookups.

**DP optimizer.** This is the exact best pure allocation for a pool, with `sum == 100` enforced.

**Exhaustive robust search.** C++ makes it cheap to stream through every legal allocation
(C(104,4) = 4.6M) against every scenario's table without storing them. That makes the **maximin**
(best worst-case score) and **minimax-regret** (smallest worst-case shortfall from each scenario's
own optimum) picks exact for the training pools.

**Overfitting checks.** Each pick is chosen on a training pool, then scored on an independent
holdout pool, over `--repeats` independent pool pairs.

## Scenarios

There are six pure archetypes: `proportional`, `equal`, `value`, `sacrifice`, `dispersed` and
`strategic`. There are also four labelled mixes: `naive-heavy`, `strategic-heavy`, `value-heavy`
and `uniform-blend`. The weights are in
[src/opponents.cpp](src/opponents.cpp) (`scenario_catalog`).

Adversarial best responses are reported in their own table and are **never** mixed into a
population. Each candidate gets two of them:
- the direct counter to the candidate;
- the best response to a crowd of jittered copies of the candidate.

## Reading the output

- **Per-scenario tables:**
  - train score, holdout score and its 95% CI (sampling uncertainty only);
  - paired difference vs `(0,13,21,30,36)` on the same opponents;
  - mean and SD across repeats;
  - how stable the re-optimised DP pick is.
- **Cross-scenario matrix:** holdout scores for the leading allocations, with each scenario's
  optimum, worst case and max regret. The spread across scenarios is the *model* uncertainty, and
  it is much larger than the sampling CIs.
- **Fictitious play:** an approximate mixed-strategy equilibrium. It is a game-theoretic benchmark,
  not a prediction of the field.
- **Recommendation:** one legal 5-tuple, the objective that chose it, its worst and best scenarios,
  and its stability across repeats.

The files written to `outputs/` are:

| File | Contents |
| --- | --- |
| `scenario_results.csv` | every candidate × scenario stat |
| `candidate_matrix.csv` | heatmap data, plus the fictitious-play benchmark column |
| `repeats.csv` | DP and robust picks for each repeat |
| `robust_top.csv` | top-5 maximin and minimax-regret allocations |
| `adversarial.csv` | stress-test results |
| `neighborhood.csv` | score changes after moving troops between castles |
| `headtohead_hist.csv` | head-to-head points distribution |
| `ties.csv` | tie exposure and round-number share |
| `fictitious_play.csv` | exploitability trace and mixture support |
| `archetype_scores.svg` | score by archetype for the leading allocations |
| `scenario_heatmap.svg` | candidates × scenarios |
| `allocation_comparison.svg` | troops per castle for best-under-focus, robust picks and the current candidate |
| `headtohead_hist.svg` | points-per-matchup distribution |
| `neighborhood.svg` | transfer-`d` heatmap around the recommendation |

The charts are SVG rather than PNG so the build needs no image library. Open them in any browser.

## Caveats

- Every result depends on the hypothesised populations. Change the archetype weights in
  [src/opponents.cpp](src/opponents.cpp) to test your own beliefs about the field.
- Any fixed allocation can be exploited by a targeted counter (see the stress-test table).
- No historical or leaderboard data is used, because none exists here.
