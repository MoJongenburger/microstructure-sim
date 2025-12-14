#include "msim/rules.hpp"

namespace msim {

RuleDecision RuleSet::pre_accept(const Order& incoming) const {
  // Basic validation (kept here so all rejections become structured)
  if (!is_valid_order(incoming)) {
    return RuleDecision{false, RejectReason::InvalidOrder};
  }

  // If market is halted, reject new aggressive entry.
  // (Later we’ll allow some order types in some phases, and allow auctions.)
  if (cfg_.enforce_halt && phase_ == MarketPhase::Halted) {
    return RuleDecision{false, RejectReason::MarketHalted};
  }

  return RuleDecision{true, RejectReason::None};
}

void RuleSet::on_trades(std::span<const Trade> trades) {
  // Update reference info for later rules (bands, halts, stops, etc.)
  for (const auto& t : trades) {
    last_trade_price_ = t.price;
  }
}

} // namespace msim
