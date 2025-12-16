#pragma once
#include <optional>
#include <vector>

#include "msim/book.hpp"
#include "msim/invariants.hpp"
#include "msim/types.hpp"

namespace msim::agents {

struct MarketView {
  Ts ts{0};

  std::optional<Price> best_bid;
  std::optional<Price> best_ask;
  std::optional<Price> mid;

  // Optional depth snapshot (keep small for performance)
  std::vector<LevelSummary> bid_depth;
  std::vector<LevelSummary> ask_depth;
};

inline MarketView make_view(const msim::OrderBook& book, Ts ts, std::size_t depth_levels = 0) {
  MarketView v{};
  v.ts = ts;
  v.best_bid = book.best_bid();
  v.best_ask = book.best_ask();
  v.mid = msim::midprice(v.best_bid, v.best_ask);

  if (depth_levels > 0) {
    v.bid_depth = book.depth(msim::Side::Buy, depth_levels);
    v.ask_depth = book.depth(msim::Side::Sell, depth_levels);
  }
  return v;
}

} // namespace msim::agents
