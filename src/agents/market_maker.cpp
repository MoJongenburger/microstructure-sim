#include "msim/agents/market_maker.hpp"
#include <algorithm>
#include <cstdint>

namespace msim {

static Price clamp_price(Price px) noexcept {
  return (px < 1) ? 1 : px;
}

MarketMaker::MarketMaker(OwnerId owner, RulesConfig rules_cfg, MarketMakerParams p)
  : owner_(owner), rules_cfg_(rules_cfg), p_(p) {}

OrderId MarketMaker::next_id_() noexcept {
  // avoid collisions across agents
  const uint64_t hi = (static_cast<uint64_t>(owner_) & 0xFFFF'FFFFull) << 32;
  const uint64_t lo = static_cast<uint64_t>(local_seq_++);
  return static_cast<OrderId>(hi | lo);
}

void MarketMaker::step(Ts ts, const MarketView& view, const AgentState& self, std::vector<Action>& out) {
  const Price tick = std::max<Price>(1, rules_cfg_.tick_size_ticks);
  const Qty lot = std::max<Qty>(1, rules_cfg_.lot_size);
  const Qty minq = std::max<Qty>(1, rules_cfg_.min_qty);

  if (ts < next_refresh_ts_) return;
  next_refresh_ts_ = ts + p_.refresh_ns;

  // cancel old quotes (if any)
  if (bid_id_ != 0) out.push_back(Action::cancel(bid_id_));
  if (ask_id_ != 0) out.push_back(Action::cancel(ask_id_));

  const Price ref = view.mid.value_or(view.last_trade.value_or(100 * tick));

  // inventory skew: if long -> move quotes down, if short -> move quotes up
  const int64_t inv = self.position;
  int64_t skew = inv * static_cast<int64_t>(p_.skew_per_unit);
  const int64_t clamp = static_cast<int64_t>(p_.max_skew_ticks);
  if (skew > clamp) skew = clamp;
  if (skew < -clamp) skew = -clamp;

  const Price half = static_cast<Price>(p_.spread_ticks / 2);
  const Price rem  = static_cast<Price>(p_.spread_ticks - half);

  Price bid_px = clamp_price(static_cast<Price>(ref - half - static_cast<Price>(skew)));
  Price ask_px = clamp_price(static_cast<Price>(ref + rem - static_cast<Price>(skew)));

  // snap to tick grid
  bid_px = static_cast<Price>((bid_px / tick) * tick);
  ask_px = static_cast<Price>(((ask_px + tick - 1) / tick) * tick);

  if (ask_px <= bid_px) ask_px = static_cast<Price>(bid_px + tick);

  Qty q = p_.quote_qty;
  if (q < minq) q = minq;
  // round to lot
  if (q % lot != 0) q = static_cast<Qty>(((q / lot) + 1) * lot);

  // submit new bid/ask
  {
    Order b{};
    b.id = next_id_();
    b.ts = ts;
    b.side = Side::Buy;
    b.type = OrderType::Limit;
    b.price = bid_px;
    b.qty = q;
    b.owner = owner_;
    b.tif = TimeInForce::GTC;

    bid_id_ = b.id;
    out.push_back(Action::submit(b));
  }
  {
    Order a{};
    a.id = next_id_();
    a.ts = ts;
    a.side = Side::Sell;
    a.type = OrderType::Limit;
    a.price = ask_px;
    a.qty = q;
    a.owner = owner_;
    a.tif = TimeInForce::GTC;

    ask_id_ = a.id;
    out.push_back(Action::submit(a));
  }
}

} // namespace msim
