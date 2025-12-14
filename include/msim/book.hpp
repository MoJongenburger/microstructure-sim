#pragma once
#include <deque>
#include <map>
#include <optional>
#include <vector>

#include "msim/order.hpp"
#include "msim/invariants.hpp"
#include "msim/types.hpp"

namespace msim {

// A lightweight “Level 2” view: price + total quantity + number of resting orders.
struct LevelSummary {
  Price    price{};
  Qty      total_qty{};
  uint32_t order_count{};
};

class OrderBook {
public:
  // Insert a *resting* limit order. Returns false if it would cross the spread.
  bool add_resting_limit(Order o);

  // Top of book
  std::optional<Price> best_bid() const noexcept;
  std::optional<Price> best_ask() const noexcept;

  // Crossed-book check (should stay false if you only add resting orders correctly)
  bool is_crossed() const noexcept;

  // L2 depth snapshot: top N levels for a side
  std::vector<LevelSummary> depth(Side side, std::size_t levels) const;

  // Quick stats
  bool empty(Side side) const noexcept;
  std::size_t level_count(Side side) const noexcept;

private:
  struct Level {
    std::deque<Order> q;
    Qty total_qty{0};
  };

  using BidMap = std::map<Price, Level, std::greater<Price>>;
  using AskMap = std::map<Price, Level, std::less<Price>>;

  BidMap bids_;
  AskMap asks_;

  // Helper: get references to correct side container
  BidMap& bids() noexcept { return bids_; }
  AskMap& asks() noexcept { return asks_; }
  const BidMap& bids() const noexcept { return bids_; }
  const AskMap& asks() const noexcept { return asks_; }

  bool would_cross(const Order& o) const noexcept;
};

} // namespace msim

