#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

#include "msim/ledger.hpp"
#include "msim/matching_engine.hpp"
#include "msim/simulator.hpp" // BookTop
#include "msim/world.hpp"     // IAgent, Action, MarketView, AgentState, WorldConfig

namespace msim {

struct LiveSnapshot {
  Ts ts{0};
  std::optional<Price> best_bid{};
  std::optional<Price> best_ask{};
  std::optional<Price> mid{};
  std::optional<Price> last_trade{};
  std::vector<Trade> recent_trades{};
};

struct LiveSeriesPoint {
  Ts ts{0};
  std::optional<Price> mid{};
};

class LiveWorld {
public:
  explicit LiveWorld(MatchingEngine engine);
  ~LiveWorld();

  LiveWorld(const LiveWorld&) = delete;
  LiveWorld& operator=(const LiveWorld&) = delete;

  void add_agent(std::unique_ptr<IAgent> a);

  // Starts a background thread that advances "exchange time" in dt_ns steps.
  void start(uint64_t seed, double horizon_seconds, WorldConfig cfg = {});
  void stop();
  bool running() const noexcept { return running_.load(std::memory_order_relaxed); }

  // Manual order entry (from UI). These are queued and applied on the simulation thread.
  // If id==0, LiveWorld assigns a deterministic unique id for the owner.
  struct SubmitAck {
    OrderId id{0};
    OrderStatus status{OrderStatus::Accepted};
    RejectReason reject_reason{RejectReason::None};
  };

  SubmitAck submit_order(Order o);
  bool cancel_order(OrderId id);
  bool modify_qty(OrderId id, Qty new_qty);

  LiveSnapshot snapshot(std::size_t max_trades = 200) const;
  std::vector<LiveSeriesPoint> mid_series(Ts window_ns) const;

private:
  enum class CmdType : uint8_t { Submit = 0, Cancel = 1, ModifyQty = 2 };

  struct Cmd {
    CmdType type{CmdType::Submit};
    Order order{};
    OrderId id{0};
    Qty new_qty{0};
  };

  void loop_(uint64_t seed, double horizon_seconds, WorldConfig cfg);

  static uint64_t splitmix64_(uint64_t& x) noexcept;
  OrderId next_manual_id_(OwnerId owner) noexcept;

  // --- mutable shared state guarded by m_ ---
  mutable std::mutex m_;

  MatchingEngine engine_;
  std::vector<std::unique_ptr<IAgent>> agents_;

  std::unordered_map<OrderId, OrderMeta> order_meta_;
  std::unordered_map<OwnerId, Account> accounts_;

  std::deque<Trade> trades_buf_;
  std::deque<BookTop> tops_buf_;

  std::deque<Cmd> pending_;

  Ts current_ts_{0};
  uint32_t manual_seq_{1};

  // --- thread control ---
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_req_{false};
  std::thread th_;
};

} // namespace msim
