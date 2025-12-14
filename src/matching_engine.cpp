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

MatchResult MatchingEngine::process(Order incoming) {
  MatchResult out{};
  if (!is_valid_order(incoming)) return out;

  if (incoming.type == OrderType::Market) return process_market(std::move(incoming));
  return process_limit(std::move(incoming));
}

MatchResult MatchingEngine::process_market(Order incoming) {
  MatchResult out{};
  if (incoming.qty <= 0) return out;

  if (incoming.side == Side::Buy) match_buy(out, incoming);
  else match_sell(out, incoming);

  for (const auto& tr : out.trades) out.filled_qty += tr.qty;
  return out;
}

MatchResult MatchingEngine::process_limit(Order incoming) {
  MatchResult out{};
  if (incoming.qty <= 0) return out;

  if (incoming.side == Side::Buy) match_buy(out, incoming);
  else match_sell(out, incoming);

  for (const auto& tr : out.trades) out.filled_qty += tr.qty;

  // Remainder becomes resting, but ONLY if it no longer crosses
  if (incoming.qty > 0) {
    if (book_.add_resting_limit(incoming)) out.resting = incoming;
  }
  return out;
}

void MatchingEngine::match_buy(MatchResult& out, Order& taker) {
  // Match BUY taker vs best ASKs (lowest price first, FIFO within price)
  while (taker.qty > 0 && !book_.asks_.empty()) {
    auto best_it = book_.asks_.begin();
    const Price best_ask_px = best_it->first;

    // For a LIMIT buy, only match while ask <= limit
    if (taker.type == OrderType::Limit && best_ask_px > taker.price) break;

    auto& lvl = best_it->second;

    // Defensive: should never happen, but keeps structure consistent
    if (lvl.q.empty()) {
      book_.asks_.erase(best_it);
      continue;
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
  // Match SELL taker vs best BIDs (highest price first, FIFO within price)
  while (taker.qty > 0 && !book_.bids_.empty()) {
    auto best_it = book_.bids_.begin();
    const Price best_bid_px = best_it->first;

    // For a LIMIT sell, only match while bid >= limit
    if (taker.type == OrderType::Limit && best_bid_px < taker.price) break;

    auto& lvl = best_it->second;

    if (lvl.q.empty()) {
      book_.bids_.erase(best_it);
      continue;
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
