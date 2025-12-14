#pragma once
#include <cstdint>
#include <optional>
#include <vector>

#include "msim/events.hpp"
#include "msim/rng.hpp"
#include "msim/types.hpp"

namespace msim {

struct FlowParams {
  // arrival intensities (events per second)
  double lambda_limit{50.0};
  double lambda_market{5.0};
  double lambda_cancel{10.0};

  // price placement around mid (in ticks)
  int32_t max_offset_ticks{20};

  // quantity range
  Qty min_qty{1};
  Qty max_qty{20};
};

class OrderFlowGenerator {
public:
  OrderFlowGenerator(uint64_t seed, FlowParams p);

  // Generate events in [t0, t0 + horizon_seconds)
  std::vector<Event> generate(Ts t0_ns, double horizon_seconds);

private:
  Rng rng_;
  FlowParams p_;
  OrderId next_id_{1};

  Side sample_side();
  Qty sample_qty();
  int32_t sample_offset();

  // Choose limit price around a reference mid
  Price limit_price_around(Price mid, Side side);

  // (MVP) we don’t track all live ids yet; cancels will be “best effort”
  std::optional<OrderId> sample_cancel_id();
};

} // namespace msim
