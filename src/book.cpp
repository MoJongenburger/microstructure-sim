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

