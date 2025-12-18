#include "msim/live_world.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace msim {

static constexpr std::size_t kMaxTradesBuf = 50'000;
static constexpr std::size_t kMaxTopsBuf   = 2'000'000; // enough for long-ish runs at 1ms

LiveWorld::LiveWorld(MatchingEngine engine)
  : engine_(std::move(engine)) {}

LiveWorld::~LiveWorld() {
  stop();
}

void LiveWorld::add_agent(std::unique_ptr<IAgent> a) {
  std::lock_guard<std::mutex> lk(m_);
  agents_.push_back(std::move(a));
}

void LiveWorld::start(uint64_t seed, double horizon_seconds, WorldConfig cfg) {
  if (running()) return;
  stop_req_.store(false, std::memory_order_relaxed);
  running_.store(true, std::memory_order_relaxed);
  th_ = std::thread([this, seed, horizon_seconds, cfg]() { loop_(seed, horizon_seconds, cfg); });
}

void LiveWorld::stop() {
  if (!running()) return;
  stop_req_.store(true, std::memory_order_relaxed);
  if (th_.joinable()) th_.join();
  running_.store(false, std::memory_order_relaxed);
}

uint64_t LiveWorld::splitmix64_(uint64_t& x) noexcept {
  uint64_t z = (x += 0x9e3779b97f4a7c15ull);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return z ^ (z >> 31);
}

OrderId LiveWorld::next_manual_id_(OwnerId owner) noexcept {
  // same style as MarketMaker to avoid collisions
  const uint64_t hi = (static_cast<uint64_t>(owner) & 0xFFFF'FFFFull) << 32;
  const uint64_t lo = static_cast<uint64_t>(manual_seq_++);
  return static_cast<OrderId>(hi | lo);
}

LiveWorld::SubmitAck LiveWorld::submit_order(Order o) {
  std::lock_guard<std::mutex> lk(m_);

  if (o.id == 0) o.id = next_manual_id_(o.owner);
  // ts will be overwritten at execution time to current_ts_ for determinism
  pending_.push_back(Cmd{CmdType::Submit, o, 0, 0});

  SubmitAck ack{};
  ack.id = o.id;
  return ack;
}

bool LiveWorld::cancel_order(OrderId id) {
  std::lock_guard<std::mutex> lk(m_);
  pending_.push_back(Cmd{CmdType::Cancel, Order{}, id, 0});
  return true;
}

bool LiveWorld::modify_qty(OrderId id, Qty new_qty) {
  std::lock_guard<std::mutex> lk(m_);
  pending_.push_back(Cmd{CmdType::ModifyQty, Order{}, id, new_qty});
  return true;
}

LiveSnapshot LiveWorld::snapshot(std::size_t max_trades) const {
  std::lock_guard<std::mutex> lk(m_);

  LiveSnapshot s{};
  s.ts = current_ts_;
  s.best_bid = engine_.book().best_bid();
  s.best_ask = engine_.book().best_ask();
  s.mid = midprice(s.best_bid, s.best_ask);
  s.last_trade = engine_.rules().last_trade_price();

  const std::size_t n = std::min<std::size_t>(max_trades, trades_buf_.size());
  s.recent_trades.reserve(n);

  // newest last
  const std::size_t start = trades_buf_.size() - n;
  for (std::size_t i = start; i < trades_buf_.size(); ++i) s.recent_trades.push_back(trades_buf_[i]);
  return s;
}

std::vector<LiveSeriesPoint> LiveWorld::mid_series(Ts window_ns) const {
  std::lock_guard<std::mutex> lk(m_);
  std::vector<LiveSeriesPoint> out;

  const Ts now = current_ts_;
  const Ts cutoff = (window_ns > 0 && now > window_ns) ? (now - window_ns) : 0;

  // Keep points with ts >= cutoff
  for (const auto& t : tops_buf_) {
    if (t.ts < cutoff) continue;
    LiveSeriesPoint p{};
    p.ts = t.ts;
    p.mid = t.mid;
    out.push_back(p);
  }
  return out;
}

void LiveWorld::loop_(uint64_t seed, double horizon_seconds, WorldConfig cfg) {
  // local copies to minimize lock time
  const Ts t0 = 0;
  const Ts t_end = static_cast<Ts>(std::llround(horizon_seconds * 1'000'000'000.0));
  const Ts dt = std::max<Ts>(1, cfg.dt_ns);

  // deterministic per-agent seeding (one-time)
  {
    std::lock_guard<std::mutex> lk(m_);
    uint64_t sm = seed;
    for (std::size_t i = 0; i < agents_.size(); ++i) {
      const uint64_t s = splitmix64_(sm) ^ (static_cast<uint64_t>(i) + 1ull);
      agents_[i]->seed(s);
    }
  }

  auto next_wake = std::chrono::steady_clock::now();

  for (Ts ts = t0; ts <= t_end && !stop_req_.load(std::memory_order_relaxed); ts += dt) {
    next_wake += std::chrono::nanoseconds(dt);

    std::lock_guard<std::mutex> lk(m_);
    current_ts_ = ts;

    // 1) flush timed transitions / auctions
    {
      auto flushed = engine_.flush(ts);
      if (!flushed.empty()) {
        for (const auto& tr : flushed) trades_buf_.push_back(tr);
        while (trades_buf_.size() > kMaxTradesBuf) trades_buf_.pop_front();

        const auto bb = engine_.book().best_bid();
        const auto ba = engine_.book().best_ask();
        const auto mid = midprice(bb, ba);
        apply_trades_to_accounts(ts, flushed, order_meta_, accounts_, mid);
      }
    }

    // 2) apply queued manual commands (FIFO)
    while (!pending_.empty()) {
      Cmd c = pending_.front();
      pending_.pop_front();

      if (c.type == CmdType::Submit) {
        Order o = c.order;
        o.ts = ts; // enforce deterministic arrival timestamp

        // record meta before matching
        order_meta_[o.id] = OrderMeta{o.owner, o.side};

        auto res = engine_.process(o);
        // Even if rejected, process() returns status/reason; we still update trades if any.
        if (!res.trades.empty()) {
          for (const auto& tr : res.trades) trades_buf_.push_back(tr);
          while (trades_buf_.size() > kMaxTradesBuf) trades_buf_.pop_front();

          const auto bb2 = engine_.book().best_bid();
          const auto ba2 = engine_.book().best_ask();
          const auto mid2 = midprice(bb2, ba2);
          apply_trades_to_accounts(ts, res.trades, order_meta_, accounts_, mid2);
        }
      } else if (c.type == CmdType::Cancel) {
        (void)engine_.book_mut().cancel(c.id);
      } else {
        (void)engine_.book_mut().modify_qty(c.id, c.new_qty);
      }
    }

    // 3) compute view
    const auto bb = engine_.book().best_bid();
    const auto ba = engine_.book().best_ask();
    const auto mid = midprice(bb, ba);

    MarketView view{};
    view.ts = ts;
    view.best_bid = bb;
    view.best_ask = ba;
    view.mid = mid;
    view.last_trade = engine_.rules().last_trade_price();

    // 4) agent steps (deterministic insertion order)
    for (auto& ap : agents_) {
      const OwnerId oid = ap->owner();

      AgentState self{};
      self.owner = oid;
      const auto it = accounts_.find(oid);
      if (it != accounts_.end()) {
        self.cash_ticks = it->second.cash_ticks;
        self.position = it->second.position;
      }

      std::vector<Action> actions;
      actions.reserve(8);
      ap->step(ts, view, self, actions);

      for (const auto& act : actions) {
        if (act.type == ActionType::Submit) {
          Order o = act.order;
          o.ts = ts;

          order_meta_[o.id] = OrderMeta{o.owner, o.side};

          auto res = engine_.process(o);
          if (!res.trades.empty()) {
            for (const auto& tr : res.trades) trades_buf_.push_back(tr);
            while (trades_buf_.size() > kMaxTradesBuf) trades_buf_.pop_front();

            const auto bb2 = engine_.book().best_bid();
            const auto ba2 = engine_.book().best_ask();
            const auto mid2 = midprice(bb2, ba2);
            apply_trades_to_accounts(ts, res.trades, order_meta_, accounts_, mid2);
          }
        } else if (act.type == ActionType::Cancel) {
          (void)engine_.book_mut().cancel(act.id);
        } else {
          (void)engine_.book_mut().modify_qty(act.id, act.new_qty);
        }
      }
    }

    // 5) record top-of-book series
    BookTop top{};
    top.ts = ts;
    top.best_bid = engine_.book().best_bid();
    top.best_ask = engine_.book().best_ask();
    top.mid = midprice(top.best_bid, top.best_ask);

    tops_buf_.push_back(top);
    while (tops_buf_.size() > kMaxTopsBuf) tops_buf_.pop_front();

    // unlock before sleeping
    // (lock_guard ends here)

    std::this_thread::sleep_until(next_wake);
  }

  running_.store(false, std::memory_order_relaxed);
}

} // namespace msim
