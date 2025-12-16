#pragma once
#include <random>
#include <vector>

#include "msim/agents/agent.hpp"
#include "msim/types.hpp"

namespace msim::agents {

struct NoiseTraderConfig {
  // Probability of sending an order on each timestep
  double intensity_per_step{0.20};

  // Market vs limit
  double prob_market{0.15};

  // If limit: how far from mid in ticks
  int32_t max_offset_ticks{5};

  // Quantity distribution (uniform in [min_qty, max_qty])
  Qty min_qty{1};
  Qty max_qty{10};

  // Grid assumptions for generated orders (exchange will enforce its own rules too)
  Price tick_size{1};
  Qty   lot_size{1};

  // If book has no mid yet, use this
  Price default_mid{100};
};

class NoiseTrader final : public Agent {
public:
  NoiseTrader(OwnerId owner, NoiseTraderConfig cfg)
    : owner_(owner), cfg_(cfg) {}

  OwnerId owner_id() const noexcept override { return owner_; }

  std::vector<Action> generate_actions(const MarketView& view, std::mt19937_64& rng) override;

private:
  OwnerId owner_{0};
  NoiseTraderConfig cfg_{};
  OrderId next_order_id_{1};

  Price snap_to_tick(Price p) const noexcept;
  Qty   snap_to_lot(Qty q) const noexcept;
};

} // namespace msim::agents
