#include "msim/rules.hpp"

namespace msim {

RuleDecision RuleSet::pre_accept(const Order& incoming) const {
  RuleDecision d{};

  if (incoming.qty <= 0) {
    d.accept = false;
    d.reason = RejectReason::InvalidOrder;
    return d;
  }

  // Halt behavior: either reject, or allow engine to queue during halt
  if (phase_ == MarketPhase::Halted && cfg_.enforce_halt && !cfg_.queue_orders_during_halt) {
    d.accept = false;
    d.reason = RejectReason::MarketHalted;
    return d;
  }

  // Tick rule: only apply to LIMIT orders (market price ignored)
  if (incoming.type == OrderType::Limit) {
    if (cfg_.tick_size_ticks > 0 && (incoming.price % cfg_.tick_size_ticks) != 0) {
      d.accept = false;
      d.reason = RejectReason::PriceNotOnTick;
      return d;
    }
  }

  // Lot/min qty rules
  if (cfg_.min_qty > 0 && incoming.qty < cfg_.min_qty) {
    d.accept = false;
    d.reason = RejectReason::QtyBelowMinimum;
    return d;
  }
  if (cfg_.lot_size > 0 && (incoming.qty % cfg_.lot_size) != 0) {
    d.accept = false;
    d.reason = RejectReason::QtyNotOnLot;
    return d;
  }

  return d;
}

void RuleSet::on_trades(std::span<const Trade> trades) {
  if (!trades.empty()) last_trade_price_ = trades.back().price;
}

} // namespace msim
