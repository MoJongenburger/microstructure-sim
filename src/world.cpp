#include "msim/world.hpp"
#include <algorithm>

namespace msim {

void World::add_agent(std::unique_ptr<msim::agents::Agent> a) {
  agents_.push_back(std::move(a));
}

WorldStats World::run(Ts start_ts, Ts horizon_ns, Ts step_ns, std::size_t depth_levels) {
  WorldStats stats{};
  if (step_ns <= 0) step_ns = 1;

  // deterministic RNG per run: derive seed from start_ts
  std::mt19937_64 rng(static_cast<uint64_t>(start_ts) ^ 0x9E3779B97F4A7C15ULL);

  const Ts end_ts = start_ts + horizon_ns;
  for (Ts ts = start_ts; ts <= end_ts; ts += step_ns) {
    stats.steps++;

    const auto view = msim::agents::make_view(engine_.book(), ts, depth_levels);

    // Agents generate actions at this timestamp
    for (auto& ap : agents_) {
      auto actions = ap->generate_actions(view, rng);
      stats.actions_sent += static_cast<uint64_t>(actions.size());

      // Apply actions sequentially (deterministic ordering by agent registration)
      for (auto& act : actions) {
        if (act.type == msim::agents::ActionType::Place) {
          stats.orders_sent++;
          act.place.ts = ts;      // world timestamp is authoritative for now
          act.place.owner = ap->owner_id();

          auto res = engine_.process(act.place);
          if (res.status == msim::OrderStatus::Rejected) stats.rejects++;

          stats.trades += static_cast<uint64_t>(res.trades.size());

          msim::agents::MarketEvent ev{};
          ev.ts = ts;
          ev.phase = engine_.rules().phase();
          ev.trades = std::move(res.trades);

          // Broadcast to all agents
          for (auto& bp : agents_) bp->on_market_event(ev);
        }
        else if (act.type == msim::agents::ActionType::Cancel) {
          stats.cancels_sent++;
          (void)engine_.book_mut().cancel(act.cancel_id);
        }
        else if (act.type == msim::agents::ActionType::ModifyQty) {
          stats.modifies_sent++;
          (void)engine_.book_mut().modify_qty(act.cancel_id, act.new_qty);
        }
      }
    }
  }

  return stats;
}

} // namespace msim
