#include "nexus/sim/advanced/predictor.hpp"

#include <algorithm>

namespace nexus::sim::advanced {

std::string_view predictor_name(PredictorKind predictor) {
  switch (predictor) {
    case PredictorKind::StaticNotTaken:
      return "static-not-taken";
    case PredictorKind::TwoBit:
      return "2bit";
  }

  return "unknown";
}

bool TwoBitPredictor::predict(std::size_t pc) const {
  return counter(pc) >= 2U;
}

void TwoBitPredictor::update(std::size_t pc, bool taken) {
  auto [it, inserted] = table_.emplace(pc, 1U);
  std::uint8_t& value = it->second;

  if (taken) {
    value = std::min<std::uint8_t>(3U, static_cast<std::uint8_t>(value + 1U));
  } else if (value > 0U) {
    value = static_cast<std::uint8_t>(value - 1U);
  }
}

std::uint8_t TwoBitPredictor::counter(std::size_t pc) const {
  const auto found = table_.find(pc);
  if (found == table_.end()) {
    return 1U;
  }
  return found->second;
}

}  // namespace nexus::sim::advanced
