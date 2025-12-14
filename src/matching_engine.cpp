#include "msim/matching_engine.hpp"
#include <algorithm>

namespace msim {

static Price mul_div_bps(Price x, int32_t num_bps, int32_t den_bps) noexcept {
  // helper for integer price math: x * num_bps / den_bps
  return static_cast<Price>((static_cast<int64_t>(x) * num_bps) / den_bps);
}

Trade MatchingEngine::make_trade(Ts ts, Price px, Qty q, OrderId maker, OrderId taker) {
  Trade t{};
  t.id = next_trade_id_++;
  t.ts = ts;
  t.price = px;
  t.qty = q;
  t.maker_order_id = maker;
  t.taker_order_id = taker;
  return t;
}

Qty MatchingEngine::available_liquidity(const Order& taker) const noexcept {
  Qty avail = 0;

  if (taker.side == Side::Buy) {
    for (auto it = book_.asks_.begin(); it != book_.asks_.end(); ++it) {
      const Price px = it->first;
      if (taker.type == OrderType::Limit && px > taker.price) break;
      for (const auto& o : it->second.q) {
        avail += o.qty;
        if (avail >= taker.qty) return avail;
      }
    }
  } else {
    for (auto it = book_.bids_.begin(); it != book_.bids_.end(); ++it) {
      const Price px = it->first;
      if (taker.type == OrderType::Limit && px < taker.price) break;
      for (const auto& o : it->second.q) {
        avail += o.qty;
        if (avail >= taker.qty) return avail;
      }
    }
  }

  return avail;
}

std::optional<Price> MatchingEngine::reference_price() const noexcept {
  // Prefer last trade. If none, fall back to mid.
  if (auto lt = rules_.last_trade_price(); lt) return lt;
  return midprice(book_.best_bid(), book_.best_ask());
}

std::optional<Price> MatchingEngine::first_execution_price(const Order& incoming) const noexcept {
  // Returns the first price this order would execute at (if it would execute at all).
  if (incoming.side == Side::Buy) {
    auto ba = book_.best_ask();
    if (!ba) return std::nullopt;

    if (incoming.type == OrderType::Market) return ba;
    // limit buy executes if it crosses the ask
    if (incoming.price >= *ba) return ba;
    return std::nullopt;
  } else {
    auto bb = book_.best_bid();
    if (!bb) return std::nullopt;

    if (incoming.type == OrderType::Market) return bb;
    // limit sell executes if it crosses the bid
    if (incoming.price <= *bb) return bb;
    return std::nullopt;
  }
}

bool MatchingEngine::breaches_price_band(Price exec_px, Price ref_px) const noexcept {
  const auto& cfg = rules_.config();
  if (!cfg.enable_price_bands) return false;

  // bounds: ref*(1±band_bps/10000)
  const int32_t B = cfg.band_bps;
  const Price lower = mul_div_bps(ref_px, 10000 - B, 10000);
  const Price upper = mul_div_bps(ref_px, 10000 + B, 10000);

  return (exec_px < lower) || (exec_px > upper);
}

bool MatchingEngine::should_trigger_volatility_auction(const Order& incoming) const noexcept {
  const auto& cfg = rules_.config();

  // Only in continuous; if already auction/halted, do not trigger again here.
  if (!cfg.enable_volatility_interruption) return false;
  if (replaying_auction_) return false;
  if (rules_.phase() != MarketPhase::Continuous) return false;

  // Only meaningful if the order would actually execute immediately.
  auto exec_px = first_execution_price(incoming);
  if (!exec_px) return false;

  auto ref_px = reference_price();
  if (!ref_px) return false; // no reference => do not block trading

  return breaches_price_band(*exec_px, *ref_px);
}

MatchResult MatchingEngine::queue_in_auction(Order incoming) {
  MatchResult out{};
  auction_queue_.push_back(std::move(incoming));
  return out; // no trades while in auction
}

std::vector<Trade> MatchingEngine::replay_auction_queue(Ts replay_ts) {
  // For Step 10 MVP we "replay" queued orders at the reopen timestamp.
  // (Step 11 will replace this with a proper call auction uncross.)
  std::vector<Trade> all;

  if (auction_queue_.empty()) return all;

  replaying_auction_ = true;
  auto queued = std::move(auction_queue_);
  auction_queue_.clear();

  for (auto& o : queued) {
    o.ts = replay_ts; // stamp to reopen time for deterministic replay
    auto r = process(o); // band checks disabled via replaying_auction_ flag
    all.insert(all.end(), r.trades.begin(), r.trades.end());
  }

  replaying_auction_ = false;
  return all;
}

MatchResult MatchingEngine::process(Order incoming) {
  MatchResult out{};

  // Base admission checks (tick/lot/halt)
  const auto decision = rules_.pre_accept(incoming);
  if (!decision.accept) {
    out.status = OrderStatus::Rejected;
    out.reject_reason = decision.reason;
    return out;
  }

  // Step 10: if we are in auction, queue until end, then reopen + replay
  if (rules_.phase() == MarketPhase::Auction) {
    if (incoming.ts < auction_end_ts_) {
      return queue_in_auction(std::move(incoming));
    }

    // reopen: switch to continuous and replay queued orders first
    rules_.set_phase(MarketPhase::Continuous);
    auto replay_trades = replay_auction_queue(auction_end_ts_);
    out.trades.insert(out.trades.end(), replay_trades.begin(), replay_trades.end());
  }

  // Step 10: check volatility interruption trigger before matching
  if (should_trigger_volatility_auction(incoming)) {
    rules_.set_phase(MarketPhase::Auction);
    auction_end_ts_ = incoming.ts + rules_.config().vol_auction_duration_ns;
    return queue_in_auction(std::move(incoming));
  }

  // Step 8: FOK pre-check (atomic)
  if (incoming.tif == TimeInForce::FOK) {
    const Qty avail = available_liquidity(incoming);
    if (avail < incoming.qty) {
      rules_.on_trades(out.trades);
      return out;
    }
  }

  // normal processing
  MatchResult r{};
  if (incoming.type == OrderType::Market) r = process_market(std::move(incoming));
  else r = process_limit(std::move(incoming));

  out.trades.insert(out.trades.end(), r.trades.begin(), r.trades.end());

  // In this step we keep out.resting as "resting from current order only"
  out.resting = r.resting;

  // filled qty is sum of returned trades
  out.filled_qty = 0;
  for (const auto& tr : out.trades) out.filled_qty += tr.qty;

  rules_.on_trades(out.trades);
  return out;
}

MatchResult MatchingEngine::process_market(Order incoming) {
  MatchResult out{};
  if (incoming.qty <= 0) return out;

  if (incoming.side == Side::Buy) match_buy(out, incoming);
  else match_sell(out, incoming);

  out.filled_qty = 0;
  for (const auto& tr : out.trades) out.filled_qty += tr.qty;

  // Step 8: Market-to-Limit
  if (incoming.mkt_style == MarketStyle::MarketToLimit && incoming.qty > 0 && !out.trades.empty()) {
    Order rest = incoming;
    rest.type = OrderType::Limit;
    rest.price = out.trades.back().price;
    rest.tif = TimeInForce::GTC;
    rest.mkt_style = MarketStyle::PureMarket;

    if (book_.add_resting_limit(rest)) out.resting = rest;
  }

  return out;
}

MatchResult MatchingEngine::process_limit(Order incoming) {
  MatchResult out{};
  if (incoming.qty <= 0) return out;

  if (incoming.side == Side::Buy) match_buy(out, incoming);
  else match_sell(out, incoming);

  out.filled_qty = 0;
  for (const auto& tr : out.trades) out.filled_qty += tr.qty;

  // IOC never rests remainder
  if (incoming.tif == TimeInForce::IOC) return out;

  // GTC rests remainder
  if (incoming.qty > 0) {
    if (book_.add_resting_limit(incoming)) out.resting = incoming;
  }

  return out;
}

void MatchingEngine::match_buy(MatchResult& out, Order& taker) {
  while (taker.qty > 0 && !book_.asks_.empty()) {
    auto best_it = book_.asks_.begin();
    const Price best_ask_px = best_it->first;

    if (taker.type == OrderType::Limit && best_ask_px > taker.price) break;

    auto& lvl = best_it->second;
    if (lvl.q.empty()) {
      book_.asks_.erase(best_it);
      continue;
    }

    // Step 9: STP
    if (rules_.config().stp != StpMode::None) {
      const OwnerId maker_owner = lvl.q.front().owner;
      if (maker_owner == taker.owner) {
        if (rules_.config().stp == StpMode::CancelTaker) {
          taker.qty = 0;
          return;
        }
        const OrderId maker_id = lvl.q.front().id;
        (void)book_.cancel(maker_id);
        continue;
      }
    }

    auto& maker = lvl.q.front();
    const Qty q = std::min(taker.qty, maker.qty);

    out.trades.push_back(make_trade(taker.ts, best_ask_px, q, maker.id, taker.id));

    taker.qty -= q;
    maker.qty -= q;
    lvl.total_qty -= q;

    if (maker.qty == 0) lvl.q.pop_front();
    if (lvl.total_qty == 0) book_.asks_.erase(best_it);
  }
}

void MatchingEngine::match_sell(MatchResult& out, Order& taker) {
  while (taker.qty > 0 && !book_.bids_.empty()) {
    auto best_it = book_.bids_.begin();
    const Price best_bid_px = best_it->first;

    if (taker.type == OrderType::Limit && best_bid_px < taker.price) break;

    auto& lvl = best_it->second;
    if (lvl.q.empty()) {
      book_.bids_.erase(best_it);
      continue;
    }

    // Step 9: STP
    if (rules_.config().stp != StpMode::None) {
      const OwnerId maker_owner = lvl.q.front().owner;
      if (maker_owner == taker.owner) {
        if (rules_.config().stp == StpMode::CancelTaker) {
          taker.qty = 0;
          return;
        }
        const OrderId maker_id = lvl.q.front().id;
        (void)book_.cancel(maker_id);
        continue;
      }
    }

    auto& maker = lvl.q.front();
    const Qty q = std::min(taker.qty, maker.qty);

    out.trades.push_back(make_trade(taker.ts, best_bid_px, q, maker.id, taker.id));

    taker.qty -= q;
    maker.qty -= q;
    lvl.total_qty -= q;

    if (maker.qty == 0) lvl.q.pop_front();
    if (lvl.total_qty == 0) book_.bids_.erase(best_it);
  }
}

} // namespace msim
