#include "msim/live_world.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace msim {

static constexpr std::size_t kMaxTrades = 300;
static constexpr std::size_t kMaxTops   = 2000;

uint64_t LiveWorld::splitmix64(uint64_t& x) noexcept {
  uint64_t z = (x += 0x9e3779b97f4a7c15ull);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return z ^ (z >> 31);
}

LiveWorld::LiveWorld(MatchingEngine engine, WorldConfig cfg, uint64_t seed, double horizon_seconds)
  : cfg_(cfg), seed_(seed), engine_(std::move(engine)) {
  t_end_ = static_cast<Ts>(std::llround(horizon_seconds * 1'000'000'000.0));

  // Default agents for “live tape”
  // NoiseTrader (msim::agents) + MarketMaker (msim)
  {
    msim::agents::NoiseTraderConfig nt{};
    agents_.push_back(std::make_unique<msim::agents::NoiseTrader>(OwnerId{1}, nt));
  }
  {
    RulesConfig rcfg{}; // default matches the engine defaults in your gateway
    MarketMakerParams mp{};
    agents_.push_back(std::make_unique<msim::MarketMaker>(OwnerId{2}, rcfg, mp));
  }

  // Seed agents deterministically
  uint64_t sm = seed_;
  for (std::size_t i = 0; i < agents_.size(); ++i) {
    const uint64_t s = splitmix64(sm) ^ (static_cast<uint64_t>(i) + 1ull);
    agents_[i]->seed(s);
  }

  // seed initial top point
  BookTop top{};
  top.ts = 0;
  top.best_bid = engine_.book().best_bid();
  top.best_ask = engine_.book().best_ask();
  top.mid = midprice(top.best_bid, top.best_ask);
  tops_.push_back(top);
}

LiveWorld::~LiveWorld() {
  stop();
}

void LiveWorld::start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true)) return;
  worker_ = std::thread([this]() { loop_(); });
}

void LiveWorld::stop() {
  running_.store(false);
  if (worker_.joinable()) worker_.join();
}

LiveWorld::Snapshot LiveWorld::snapshot() const {
  std::lock_guard<std::mutex> lk(mu_);
  Snapshot s{};
  s.ts = ts_;
  s.best_bid = engine_.book().best_bid();
  s.best_ask = engine_.book().best_ask();
  s.mid = midprice(s.best_bid, s.best_ask);
  s.last_trade = engine_.rules().last_trade_price();
  return s;
}

std::vector<Trade> LiveWorld::snapshot_recent_trades(std::size_t limit) const {
  std::lock_guard<std::mutex> lk(mu_);
  const std::size_t n = std::min(limit, trades_.size());
  std::vector<Trade> out;
  out.reserve(n);
  for (std::size_t i = 0; i < n; ++i) out.push_back(trades_[i]);
  return out; // newest-first
}

std::vector<BookTop> LiveWorld::snapshot_top_points(std::size_t points) const {
  std::lock_guard<std::mutex> lk(mu_);
  const std::size_t n = std::min(points, tops_.size());
  std::vector<BookTop> out;
  out.reserve(n);

  // take last n points, but return oldest-first
  const std::size_t start = tops_.size() - n;
  for (std::size_t i = start; i < tops_.size(); ++i) out.push_back(tops_[i]);
  return out;
}

LiveWorld::BookDepth LiveWorld::snapshot_depth(std::size_t levels) const {
  std::lock_guard<std::mutex> lk(mu_);
  BookDepth bd{};
  bd.bids = extract_depth_(engine_.book(), Side::Buy, levels);
  bd.asks = extract_depth_(engine_.book(), Side::Sell, levels);
  return bd;
}

OrderId LiveWorld::make_scoped_id_(OwnerId owner) {
  // owner in high bits, local seq in low bits
  const uint64_t hi = (static_cast<uint64_t>(owner) & 0xFFFF'FFFFull) << 32;
  const uint64_t lo = static_cast<uint64_t>(local_seq_++);
  return static_cast<OrderId>(hi | lo);
}

OrderAck LiveWorld::submit_order(Order o) {
  std::lock_guard<std::mutex> lk(mu_);

  o.ts = ts_;
  if (o.id == 0) o.id = make_scoped_id_(o.owner);

  order_meta_[o.id] = OrderMeta{o.owner, o.side};

  auto res = engine_.process(o);

  if (!res.trades.empty()) {
    // prepend newest trades
    for (auto it = res.trades.rbegin(); it != res.trades.rend(); ++it) {
      trades_.push_front(*it);
    }
    while (trades_.size() > kMaxTrades) trades_.pop_back();

    const auto bb = engine_.book().best_bid();
    const auto ba = engine_.book().best_ask();
    const auto mid = midprice(bb, ba);
    apply_trades_to_accounts(ts_, res.trades, order_meta_, accounts_, mid);
  }

  return res.ack;
}

bool LiveWorld::cancel_order(OrderId id) {
  std::lock_guard<std::mutex> lk(mu_);
  return engine_.book_mut().cancel(id);
}

bool LiveWorld::modify_qty(OrderId id, Qty new_qty) {
  std::lock_guard<std::mutex> lk(mu_);
  return engine_.book_mut().modify_qty(id, new_qty);
}

void LiveWorld::loop_() {
  using namespace std::chrono;

  const Ts dt = std::max<Ts>(1, cfg_.dt_ns);
  const auto wall_dt = nanoseconds(static_cast<long long>(dt));

  for (;;) {
    if (!running_.load()) break;

    {
      std::lock_guard<std::mutex> lk(mu_);

      // stop at end-of-horizon (but keep serving UI)
      if (ts_ >= t_end_) {
        // just keep last snapshot; no more stepping
      } else {
        // timed flush (auctions/phase transitions)
        {
          auto flushed = engine_.flush(ts_);
          if (!flushed.empty()) {
            // prepend newest
            for (auto it = flushed.rbegin(); it != flushed.rend(); ++it) {
              trades_.push_front(*it);
            }
            while (trades_.size() > kMaxTrades) trades_.pop_back();

            const auto bb = engine_.book().best_bid();
            const auto ba = engine_.book().best_ask();
            const auto mid = midprice(bb, ba);
            apply_trades_to_accounts(ts_, flushed, order_meta_, accounts_, mid);
          }
        }

        const auto bb = engine_.book().best_bid();
        const auto ba = engine_.book().best_ask();
        const auto mid = midprice(bb, ba);

        MarketView view{};
        view.ts = ts_;
        view.best_bid = bb;
        view.best_ask = ba;
        view.mid = mid;
        view.last_trade = engine_.rules().last_trade_price();

        // deterministic per-agent stepping (in insertion order)
        for (auto& ap : agents_) {
          const OwnerId oid = ap->owner();

          AgentState self{};
          self.owner = oid;
          if (const auto it = accounts_.find(oid); it != accounts_.end()) {
            self.cash_ticks = it->second.cash_ticks;
            self.position = it->second.position;
          }

          std::vector<Action> actions;
          actions.reserve(8);
          ap->step(ts_, view, self, actions);

          for (const auto& act : actions) {
            if (act.type == ActionType::Submit) {
              Order oo = act.order;
              oo.ts = ts_;
              if (oo.id == 0) oo.id = make_scoped_id_(oo.owner);

              order_meta_[oo.id] = OrderMeta{oo.owner, oo.side};

              auto res = engine_.process(oo);
              if (!res.trades.empty()) {
                for (auto it = res.trades.rbegin(); it != res.trades.rend(); ++it) {
                  trades_.push_front(*it);
                }
                while (trades_.size() > kMaxTrades) trades_.pop_back();

                const auto bb2 = engine_.book().best_bid();
                const auto ba2 = engine_.book().best_ask();
                const auto mid2 = midprice(bb2, ba2);
                apply_trades_to_accounts(ts_, res.trades, order_meta_, accounts_, mid2);
              }
            } else if (act.type == ActionType::Cancel) {
              (void)engine_.book_mut().cancel(act.id);
            } else if (act.type == ActionType::ModifyQty) {
              (void)engine_.book_mut().modify_qty(act.id, act.new_qty);
            }
          }
        }

        // record top-of-book
        BookTop top{};
        top.ts = ts_;
        top.best_bid = engine_.book().best_bid();
        top.best_ask = engine_.book().best_ask();
        top.mid = midprice(top.best_bid, top.best_ask);
        tops_.push_back(top);
        while (tops_.size() > kMaxTops) tops_.pop_front();

        ts_ += dt;
      }
    }

    std::this_thread::sleep_for(wall_dt);
  }
}

} // namespace msim
