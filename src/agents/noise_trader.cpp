#include "msim/agents/noise_trader.hpp"
#include <algorithm>

namespace msim::agents {

Price NoiseTrader::snap_to_tick(Price p) const noexcept {
  const Price tick = std::max<Price>(1, rules_cfg_.tick_size);
  // ticks are integers already, but enforce grid:
  return (p / tick) * tick;
}

Qty NoiseTrader::snap_to_lot(Qty q) const noexcept {
  const Qty lot = std::max<Qty>(1, rules_cfg_.lot_size);
  q = std::max(q, rules_cfg_.min_qty);
  // snap down to lot
  return (q / lot) * lot;
}

std::vector<Action> NoiseTrader::generate_actions(const MarketView& view, std::mt19937_64& rng) {
  std::vector<Action> out;

  std::uniform_real_distribution<double> U01(0.0, 1.0);
  if (U01(rng) > cfg_.intensity_per_step) return out;

  // Need a reference price. If no book exists, pick a stable default.
  Price ref = view.mid.value_or(100 * std::max<Price>(1, rules_cfg_.tick_size));

  std::bernoulli_distribution coin_side(0.5);
  const msim::Side side = coin_side(rng) ? msim::Side::Buy : msim::Side::Sell;

  std::uniform_int_distribution<int32_t> qty_dist(cfg_.min_qty, cfg_.max_qty);
  Qty qty = static_cast<Qty>(qty_dist(rng));
  qty = snap_to_lot(qty);
  if (qty <= 0) qty = std::max<Qty>(rules_cfg_.min_qty, rules_cfg_.lot_size);

  const bool is_market = (U01(rng) < cfg_.prob_market);

  msim::Order o{};
  o.id = next_order_id_++;
  o.ts = view.ts;
  o.side = side;
  o.owner = owner_;
  o.qty = qty;

  if (is_market) {
    o.type = msim::OrderType::Market;
    o.price = 0;
    // keep it simple: pure market + IOC effect comes from engine semantics
    o.tif = msim::TimeInForce::IOC;
    o.mkt_style = msim::MarketStyle::PureMarket;
  } else {
    o.type = msim::OrderType::Limit;
    std::uniform_int_distribution<int32_t> off_dist(1, std::max(1, cfg_.max_offset_ticks));
    int32_t off = off_dist(rng);

    // Quotes around ref
    Price px = ref;
    if (side == msim::Side::Buy) px = ref - off;
    else px = ref + off;

    px = snap_to_tick(px);
    if (px <= 0) px = snap_to_tick(ref);

    o.price = px;
    o.tif = msim::TimeInForce::GTC;
  }

  Action a{};
  a.type = ActionType::Place;
  a.ts = view.ts;
  a.owner = owner_;
  a.place = o;

  out.push_back(a);
  return out;
}

} // namespace msim::agents
