#include "msim/simulator.hpp"
#include "msim/invariants.hpp"

#include <algorithm>
#include <utility>

namespace msim {

namespace {
struct TimedEvent {
  Ts ts{};
  uint64_t seq{};
  const Event* ev{};
};

inline BookTop make_top(Ts ts, const OrderBook& b) {
  BookTop t{};
  t.ts = ts;
  t.best_bid = b.best_bid();
  t.best_ask = b.best_ask();
  t.mid = midprice(t.best_bid, t.best_ask);
  return t;
}
} // namespace

SimulationResult Simulator::run(const std::vector<Event>& events) {
  SimulationResult out{};

  std::vector<TimedEvent> sorted;
  sorted.reserve(events.size());
  for (uint64_t i = 0; i < events.size(); ++i) {
    const auto& e = events[i];
    Ts ts = 0;
    std::visit([&](const auto& x) { ts = x.ts; }, e);
    sorted.push_back(TimedEvent{ts, i, &e});
  }

  std::stable_sort(sorted.begin(), sorted.end(),
                   [](const TimedEvent& a, const TimedEvent& b) {
                     if (a.ts != b.ts) return a.ts < b.ts;
                     return a.seq < b.seq;
                   });

  for (const auto& te : sorted) {
    const auto& e = *te.ev;

    std::visit([&](const auto& x) {
      using T = std::decay_t<decltype(x)>;

      if constexpr (std::is_same_v<T, AddLimit>) {
        Order o{x.id, x.ts, x.side, OrderType::Limit, x.price, x.qty, x.owner};
        auto res = engine_.process(o);
        out.trades.insert(out.trades.end(), res.trades.begin(), res.trades.end());
        out.tops.push_back(make_top(x.ts, engine_.book()));
      } else if constexpr (std::is_same_v<T, AddMarket>) {
        Order o{x.id, x.ts, x.side, OrderType::Market, 0, x.qty, x.owner};
        auto res = engine_.process(o);
        out.trades.insert(out.trades.end(), res.trades.begin(), res.trades.end());
        out.tops.push_back(make_top(x.ts, engine_.book()));
      } else if constexpr (std::is_same_v<T, Cancel>) {
        // cancel is book-level (resting orders)
        if (!engine_.book_mut().cancel(x.id)) out.cancel_failures++;
        out.tops.push_back(make_top(x.ts, engine_.book()));
      } else if constexpr (std::is_same_v<T, Modify>) {
        if (!engine_.book_mut().modify(x.id, x.new_qty)) out.modify_failures++;
        out.tops.push_back(make_top(x.ts, engine_.book()));
      }
    }, e);
  }

  return out;
}

} // namespace msim
