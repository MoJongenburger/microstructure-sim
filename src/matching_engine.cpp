#include "msim/matching_engine.hpp"
#include <algorithm>

namespace msim {

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

std::optional<OwnerId> MatchingEngine::next_maker_owner_for(const Order& taker) const noexcept {
  if (taker.side == Side::Buy) {
    if (book_.asks_.empty()) return std::nullopt;
    auto it = book_.asks_.begin();
    const Price px = it->first;
    if (taker.type == OrderType::Limit && px > taker.price) return std::nullopt;
    if (it->second.q.empty()) return std::nullopt;
    return it->second.q.front().owner;
  } else {
    if (book_.bids_.empty()) return std::nullopt;
    auto it = book_.bids_.begin();
    const Price px = it->first;
    if (taker.type == OrderType::Limit && px < taker.price) return std::nullopt;
    if (it->second.q.empty()) return std::nullopt;
    return it->second.q.front().owner;
  }
}

bool MatchingEngine::cancel_next_maker_if_self_trade(const Order& taker) {
  // Cancel the *front* maker order at the next executable price level.
  if (taker.side == Side::Buy) {
    if (book_.asks_.empty()) return false;
    auto it = book_.asks_.begin();
    const Price px = it->first;
    if (taker.type == OrderType::Limit && px > taker.price) return false;
    auto& lvl = it->second;
    if (lvl.q.empty()) return false;

    const OrderId maker_id = lvl.q.front().id;
    return book_.cancel(maker_id);
  } else {
    if (book_.bids_.empty()) return false;
    auto it = book_.bids_.begin();
    const Price px = it->first;
    if (taker.type == OrderType::Limit && px < taker.price) return false;
    auto& lvl = it->second;
    if (lvl.q.empty()) return false;

    const OrderId maker_id = lvl.q.front().id;
    return book_.cancel(maker_id);
  }
}

MatchResult MatchingEngine::process(Order incoming) {
  MatchResult out{};

  const auto decision = rules_.pre_accept(incoming);
  if (!decision.accept) {
    out.status = OrderStatus::Rejected;
    out.reject_reason = decision.reason;
    return out;
  }

  // FOK pre-check (atomic)
  if (incoming.tif == TimeInForce::FOK) {
    const Qty avail = available_liquidity(incoming);
    if (avail < incoming.qty) {
      rules_.on_trades(out.trades);
      return out;
    }
  }

  if (incoming.type == OrderType::Market) out = process_market(std::move(incoming));
  else out = process_limit(std::move(incoming));

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

    // Step 9: self-trade prevention
    if (rules_.config().stp != StpMode::None) {
      const OwnerId maker_owner = lvl.q.front().owner;
      if (maker_owner == taker.owner) {
        if (rules_.config().stp == StpMode::CancelTaker) {
          // Kill taker immediately
          taker.qty = 0;
          return;
        }
        // CancelMaker: remove maker and continue
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

    // Step 9: self-trade prevention
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
