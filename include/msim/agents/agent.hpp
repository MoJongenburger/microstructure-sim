#pragma once

#include <random>
#include <vector>

#include "msim/world.hpp"              // msim::IAgent, msim::MarketView, msim::AgentState, msim::Action, msim::OwnerId, msim::Ts
#include "msim/agents/actions.hpp"     // msim::agents::Action, ActionType::Place/Cancel/ModifyQty
#include "msim/agents/market_event.hpp"
#include "msim/agents/market_view.hpp" // msim::agents::MarketView

namespace msim::agents {

class Agent : public msim::IAgent {
public:
  virtual ~Agent() = default;

  // ---- Your existing interface ----
  virtual OwnerId owner_id() const noexcept = 0;

  // Called after exchange processes something and produces an event
  virtual void on_market_event(const MarketEvent& ev) { (void)ev; }

  // Called at each timestep (deterministic schedule)
  virtual std::vector<msim::agents::Action> generate_actions(
      const msim::agents::MarketView& view,
      std::mt19937_64& rng) = 0;

  // ---- msim::IAgent interface (adapter layer) ----
  OwnerId owner() const noexcept override { return owner_id(); }

  void seed(uint64_t s) override {
    seed_ = s;
    rng_.seed(seed_);
  }

  void step(msim::Ts ts,
            const msim::MarketView& view,
            const msim::AgentState& /*self*/,
            std::vector<msim::Action>& out) override {
    // Convert msim::MarketView -> msim::agents::MarketView (depth left empty here)
    msim::agents::MarketView av{};
    av.ts = ts;
    av.best_bid = view.best_bid;
    av.best_ask = view.best_ask;
    av.mid = view.mid;
    // av.bid_depth / av.ask_depth remain empty unless World chooses to populate them via make_view()

    // Let the agent generate its native actions
    auto acts = generate_actions(av, rng_);

    // Convert msim::agents::Action -> msim::Action (World’s execution format)
    for (const auto& a : acts) {
      switch (a.type) {
        case msim::agents::ActionType::Place: {
          msim::Order o = a.place;

          // Ensure order has correct identity & timestamp (don’t trust caller)
          o.owner = owner_id();
          if (a.ts != 0) {
            o.ts = a.ts; // if your msim::Order has ts (it does in your project)
          } else {
            o.ts = ts;
          }

          out.push_back(msim::Action::submit(o));
          break;
        }
        case msim::agents::ActionType::Cancel: {
          out.push_back(msim::Action::cancel(a.cancel_id));
          break;
        }
        case msim::agents::ActionType::ModifyQty: {
          // Reuse cancel_id as "target order id" (your Action struct uses cancel_id field)
          out.push_back(msim::Action::modify_qty(a.cancel_id, a.new_qty));
          break;
        }
        default:
          break;
      }
    }
  }

protected:
  uint64_t seed_{0};
  std::mt19937_64 rng_{0};
};

} // namespace msim::agents