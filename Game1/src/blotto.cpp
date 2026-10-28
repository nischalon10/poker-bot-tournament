#include "blotto.hpp"

#include <cctype>

namespace blotto {

bool is_valid(const Alloc& x) {
  int sum = 0;
  for (int v : x) {
    if (v < 0) return false;
    sum += v;
  }
  return sum == kTroops;
}

std::string format(const Alloc& x) {
  std::string out = "(";
  for (int i = 0; i < kCastles; ++i) {
    if (i) out += ',';
    out += std::to_string(x[i]);
  }
  return out + ")";
}

std::optional<Alloc> parse_alloc(std::string_view text) {
  Alloc x{};
  int count = 0;
  size_t i = 0;
  while (i < text.size()) {
    const char ch = text[i];
    if (std::isdigit(static_cast<unsigned char>(ch))) {
      if (count == kCastles) return std::nullopt;
      long value = 0;
      while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
        value = value * 10 + (text[i] - '0');
        if (value > kTroops) return std::nullopt;
        ++i;
      }
      x[count++] = static_cast<int>(value);
    } else if (ch == ',' || ch == ' ' || ch == '(' || ch == ')') {
      ++i;
    } else {
      return std::nullopt;  // includes '-' so negatives are rejected
    }
  }
  if (count != kCastles || !is_valid(x)) return std::nullopt;
  return x;
}

}  // namespace blotto
