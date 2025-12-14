#pragma once
#include <deque>
#include <map>
#include <optional>
#include <vector>

#include "msim/order.hpp"
#include "msim/invariants.hpp"
#include "msim/types.hpp"

namespace msim {

class MatchingEngine;

struct LevelSummary {
  Price    price{};
  Qty      total_qty{};
  uint32_t order_count{};
};

class OrderBook {
public:
  bool add_resting_limit(Order o);

  // Cancel/remove a resting order by id (O(total orders) for now)
  bool cancel(OrderId id);

  // Modify only allows REDUCE quantity (common exchange rule). Returns false otherwise.
  bool modify(OrderId id, Qty new_qty);

  std::optional<Price> best_bid() const noexcept;
  std::optional<Price> best_ask() const noexcept;

  bool is_crossed() const noexcept;

  std::vector<LevelSummary> depth(Side side, std::size_t levels) const;

  bool empty(Side side) const noexcept;
  std::size_t level_count(Side side) const noexcept;

private:
  friend class MatchingEngine;

  struct Level {
    std::deque<Order> q;
    Qty total_qty{0};
  };

  using BidMap = std::map<Price, Level, std::greater<Price>>;
  using AskMap = std::map<Price, Level, std::less<Price>>;

  BidMap bids_;
  AskMap asks_;

  BidMap& bids() noexcept { return bids_; }
  AskMap& asks() noexcept { return asks_; }
  const BidMap& bids() const noexcept { return bids_; }
  const AskMap& asks() const noexcept { return asks_; }

  bool would_cross(const Order& o) const noexcept;

  // Helpers (scan-based for MVP)
  bool cancel_in_bids_(OrderId id);
  bool cancel_in_asks_(OrderId id);
  bool modify_in_bids_(OrderId id, Qty new_qty);
  bool modify_in_asks_(OrderId id, Qty new_qty);
};

} // namespace msim
