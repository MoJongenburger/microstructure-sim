#pragma once
#include <variant>
#include "msim/types.hpp"

namespace msim {

struct AddLimit {
  OrderId id{};
  Ts      ts{};
  Side    side{Side::Buy};
  Price   price{};
  Qty     qty{};
  uint32_t owner{};
};

struct AddMarket {
  OrderId id{};
  Ts      ts{};
  Side    side{Side::Buy};
  Qty     qty{};
  uint32_t owner{};
};

struct Cancel {
  OrderId id{};
  Ts      ts{};
};

struct Modify {
  OrderId id{};
  Ts      ts{};
  Qty     new_qty{};
};

using Event = std::variant<AddLimit, AddMarket, Cancel, Modify>;

enum class EventType : uint8_t { AddLimit, AddMarket, Cancel, Modify };

inline EventType type_of(const Event& e) noexcept {
  return static_cast<EventType>(e.index()); // relies on variant order above
}

} // namespace msim

