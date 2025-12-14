#pragma once
#include <cstdint>
#include <optional>
#include <vector>

#include "msim/events.hpp"
#include "msim/matching_engine.hpp"

namespace msim {

struct BookTop {
  Ts ts{};
  std::optional<Price> best_bid;
  std::optional<Price> best_ask;
  std::optional<Price> mid;
};

struct SimulationResult {
  std::vector<Trade> trades;
  std::vector<BookTop> tops;        // top-of-book snapshot after each event
  uint32_t cancel_failures{0};
  uint32_t modify_failures{0};
};

class Simulator {
public:
  explicit Simulator(MatchingEngine engine = {}) : engine_(std::move(engine)) {}

  MatchingEngine& engine_mut() noexcept { return engine_; }
  const MatchingEngine& engine() const noexcept { return engine_; }

  // Deterministic replay: stable ordering by (ts, insertion order)
  SimulationResult run(const std::vector<Event>& events);

private:
  MatchingEngine engine_;
};

} // namespace msim
