#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_map>

namespace nexus::sim::advanced {

enum class PredictorKind {
  StaticNotTaken,
  TwoBit,
};

std::string_view predictor_name(PredictorKind predictor);

class TwoBitPredictor {
 public:
  bool predict(std::size_t pc) const;
  void update(std::size_t pc, bool taken);
  std::uint8_t counter(std::size_t pc) const;

 private:
  std::unordered_map<std::size_t, std::uint8_t> table_;
};

}  // namespace nexus::sim::advanced
