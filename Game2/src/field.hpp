#pragma once

#include <string>
#include <vector>

#include "fourway.hpp"
#include "opponents.hpp"
#include "rng.hpp"

namespace blotto::fourway {

// An applicant population for Game 2: one of Game 1's scenarios, optionally with a share of
// "Game-2-aware" applicants who concentrate on a few castles worth 8+ points. All of it is
// a modelling hypothesis, not observed applicant data.
struct Field {
  int id;  // stable RNG stream id
  std::string name;
  std::string description;
  Scenario base;
  double aware;  // share of Game-2-aware concentrators
};

const std::vector<Field>& field_catalog();

// "all" | "game1" (Game 1's ten scenarios) | "aware" | "pure" | "mixes" | comma-separated names.
std::vector<Field> select_fields(const std::string& spec);

// A Game-2-aware applicant: wins a castle set worth 8+ points by concentrating on it
// ({4,5}, {3,5}, {2,3,4} or {2,3,5}), sometimes with a small hedge elsewhere.
Alloc sample_aware(Rng& rng);

std::vector<Group> sample_groups(const Field& field, size_t n, Rng& rng);

}  // namespace blotto::fourway
