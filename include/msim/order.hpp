#pragma once
#include "msim/types.hpp"

namespace msim {

struct Order {
  OrderId   id{};
  Ts        ts{};
  Side      side{Side::Buy};
  OrderType type{OrderType::Limit};

  Price     price{};   // only meaningful for limit orders
  Qty       qty{};     // remaining qty (must be > 0)

  uint32_t  owner{};   // agent/trader id
};

inline constexpr bool is_valid_order(const Order& o) noexcept {
  if (o.qty <= 0) return false;
  if (o.type == OrderType::Market) return true;
  return o.price >= 0;
}

} // namespace msim

