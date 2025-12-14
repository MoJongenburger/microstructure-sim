#pragma once
#include <optional>
#include <vector>

#include "msim/book.hpp"
#include "msim/order.hpp"
#include "msim/trade.hpp"

namespace msim {

struct MatchResult {
  std::vector<Trade> trades;
  std::optional<Order> resting; // remainder that becomes resting (limit only)
  Qty filled_qty{0};
};

class MatchingEngine {
public:
  MatchingEngine() = default;

  // Access to book (for tests + later simulator wiring)
  const OrderBook& book() const noexcept { return book_; }
  OrderBook& book_mut() noexcept { return book_; }

  // Main entry: process an incoming order
  MatchResult process(Order incoming);

private:
  OrderBook book_{};
  TradeId next_trade_id_{1};

  MatchResult process_market(Order incoming);
  MatchResult process_limit(Order incoming);

  void match_buy(MatchResult& out, Order& taker);
  void match_sell(MatchResult& out, Order& taker);

  Trade make_trade(Ts ts, Price px, Qty q, OrderId maker, OrderId taker);
};

} // namespace msim
