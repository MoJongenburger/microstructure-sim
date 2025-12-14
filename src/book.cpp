#include "msim/book.hpp"

namespace msim {

bool OrderBook::would_cross(const Order& o) const noexcept {
  if (o.type != OrderType::Limit) return true; // book only stores resting limits
  if (o.qty <= 0) return true;

  const auto bb = best_bid();
  const auto ba = best_ask();

  if (o.side == Side::Buy) {
    // Crosses if buy price >= best ask
    if (ba && o.price >= *ba) return true;
  } else {
    // Crosses if sell price <= best bid
    if (bb && o.price <= *bb) return true;
  }
  return false;
}

bool OrderBook::add_resting_limit(Order o) {
  if (!is_valid_order(o)) return false;
  if (o.type != OrderType::Limit) return false;
  if (would_cross(o)) return false;

  if (o.side == Side::Buy) {
    auto& lvl = bids_[o.price];
    lvl.total_qty += o.qty;
    lvl.q.push_back(std::move(o));
  } else {
    auto& lvl = asks_[o.price];
    lvl.total_qty += o.qty;
    lvl.q.push_back(std::move(o));
  }
  return true;
}

std::optional<Price> OrderBook::best_bid() const noexcept {
  if (bids_.empty()) return std::nullopt;
  return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const noexcept {
  if (asks_.empty()) return std::nullopt;
  return asks_.begin()->first;
}

bool OrderBook::is_crossed() const noexcept {
  return is_book_crossed(best_bid(), best_ask());
}

bool OrderBook::empty(Side side) const noexcept {
  return (side == Side::Buy) ? bids_.empty() : asks_.empty();
}

std::size_t OrderBook::level_count(Side side) const noexcept {
  return (side == Side::Buy) ? bids_.size() : asks_.size();
}

std::vector<LevelSummary> OrderBook::depth(Side side, std::size_t levels) const {
  std::vector<LevelSummary> out;
  out.reserve(levels);

  if (levels == 0) return out;

  if (side == Side::Buy) {
    for (auto it = bids_.begin(); it != bids_.end() && out.size() < levels; ++it) {
      const auto& price = it->first;
      const auto& lvl   = it->second;
      out.push_back(LevelSummary{price, lvl.total_qty, static_cast<uint32_t>(lvl.q.size())});
    }
  } else {
    for (auto it = asks_.begin(); it != asks_.end() && out.size() < levels; ++it) {
      const auto& price = it->first;
      const auto& lvl   = it->second;
      out.push_back(LevelSummary{price, lvl.total_qty, static_cast<uint32_t>(lvl.q.size())});
    }
  }

  return out;
}

} // namespace msim

namespace msim {

bool OrderBook::cancel(OrderId id) {
  if (cancel_in_bids_(id)) return true;
  return cancel_in_asks_(id);
}

bool OrderBook::modify(OrderId id, Qty new_qty) {
  if (new_qty <= 0) return false; // use cancel() explicitly
  if (modify_in_bids_(id, new_qty)) return true;
  return modify_in_asks_(id, new_qty);
}

bool OrderBook::cancel_in_bids_(OrderId id) {
  for (auto it = bids_.begin(); it != bids_.end(); ) {
    auto& lvl = it->second;
    for (auto qit = lvl.q.begin(); qit != lvl.q.end(); ++qit) {
      if (qit->id == id) {
        lvl.total_qty -= qit->qty;
        lvl.q.erase(qit);
        if (lvl.total_qty == 0) {
          it = bids_.erase(it);
        } else {
          ++it;
        }
        return true;
      }
    }
    if (lvl.total_qty == 0) it = bids_.erase(it);
    else ++it;
  }
  return false;
}

bool OrderBook::cancel_in_asks_(OrderId id) {
  for (auto it = asks_.begin(); it != asks_.end(); ) {
    auto& lvl = it->second;
    for (auto qit = lvl.q.begin(); qit != lvl.q.end(); ++qit) {
      if (qit->id == id) {
        lvl.total_qty -= qit->qty;
        lvl.q.erase(qit);
        if (lvl.total_qty == 0) {
          it = asks_.erase(it);
        } else {
          ++it;
        }
        return true;
      }
    }
    if (lvl.total_qty == 0) it = asks_.erase(it);
    else ++it;
  }
  return false;
}

bool OrderBook::modify_in_bids_(OrderId id, Qty new_qty) {
  for (auto it = bids_.begin(); it != bids_.end(); ++it) {
    auto& lvl = it->second;
    for (auto& o : lvl.q) {
      if (o.id == id) {
        if (new_qty > o.qty) return false; // only reduce
        const Qty delta = o.qty - new_qty;
        o.qty = new_qty;
        lvl.total_qty -= delta;
        if (lvl.total_qty == 0) bids_.erase(it);
        return true;
      }
    }
  }
  return false;
}

bool OrderBook::modify_in_asks_(OrderId id, Qty new_qty) {
  for (auto it = asks_.begin(); it != asks_.end(); ++it) {
    auto& lvl = it->second;
    for (auto& o : lvl.q) {
      if (o.id == id) {
        if (new_qty > o.qty) return false; // only reduce
        const Qty delta = o.qty - new_qty;
        o.qty = new_qty;
        lvl.total_qty -= delta;
        if (lvl.total_qty == 0) asks_.erase(it);
        return true;
      }
    }
  }
  return false;
}

} // namespace msim

