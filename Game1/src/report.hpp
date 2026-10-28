#pragma once

#include <string>
#include <vector>

#include "simulate.hpp"

namespace blotto {

// Terminal tables and the final recommendation.
void print_report(const Results& res);

// CSV tables and SVG charts in res.cfg.output; returns the files written.
std::vector<std::string> write_outputs(const Results& res);

}  // namespace blotto
