#pragma once

#include <random>
#include <vector>

#include "msim/world.hpp"  // msim::IAgent, msim::MarketView, msim::AgentState, msim::Action, msim::OwnerId, msim::Ts

#include "msim/agents/market_event.hpp" // keep if you use it elsewhere

namespace msim::agents {

// This keeps your *existing* agent interface (owner_id / generate_actions / on_market_event)
// but ALSO makes every agents::Agent usable by msim::World (which expects msim::IAgent).
class Agent : public msim::IAgent {
public:
  virtual ~Agent() = default;

  // ----- Your existing interface -----
  virtual OwnerId owner_id() const noexcept = 0;

  // Called after exchange processes something and produces an event
  virtual void on_market_event(const MarketEvent& ev) { (void)ev; }

  // Called at each timestep (deterministic schedule)
  virtual std::vector<msim::Action> generate_actions(
      const msim::MarketView& view,
      std::mt19937_64& rng) = 0;

  // ----- msim::IAgent bridge -----
  OwnerId owner() const noexcept override { return owner_id(); }

  void seed(uint64_t s) override {
    seed_ = s;
    rng_.seed(seed_);
  }

  void step(Ts /*ts*/,
            const msim::MarketView& view,
            const msim::AgentState& /*self*/,
            std::vector<msim::Action>& out) override {
    // Preserve your existing “generate_actions(view, rng)” model.
    auto acts = generate_actions(view, rng_);
    out.insert(out.end(), acts.begin(), acts.end());
  }

protected:
  uint64_t seed_{0};
  std::mt19937_64 rng_{0};
};

} // namespace msim::agents
