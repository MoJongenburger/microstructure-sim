#include "msim/order_flow.hpp"
#include <cmath>

namespace msim {

OrderFlowGenerator::OrderFlowGenerator(uint64_t seed, FlowParams p)
  : rng_(seed), p_(p) {}

Side OrderFlowGenerator::sample_side() {
  return (rng_.uniform01() < 0.5) ? Side::Buy : Side::Sell;
}

Qty OrderFlowGenerator::sample_qty() {
  return static_cast<Qty>(rng_.uniform_int(p_.min_qty, p_.max_qty));
}

int32_t OrderFlowGenerator::sample_offset() {
  return rng_.uniform_int(1, p_.max_offset_ticks);
}

Price OrderFlowGenerator::limit_price_around(Price mid, Side side) {
  // Place orders away from mid so book tends to stay non-crossed.
  const int32_t off = sample_offset();
  if (side == Side::Buy) return mid - off;
  return mid + off;
}

std::optional<OrderId> OrderFlowGenerator::sample_cancel_id() {
  // MVP: generate some cancels for small ids; many will fail (and that’s ok).
  if (next_id_ <= 5) return std::nullopt;
  OrderId lo = 1;
  OrderId hi = next_id_ - 1;
  return static_cast<OrderId>(rng_.uniform_int(static_cast<int32_t>(lo), static_cast<int32_t>(hi)));
}

std::vector<Event> OrderFlowGenerator::generate(Ts t0_ns, double horizon_seconds) {
  std::vector<Event> out;
  const double horizon_ns = horizon_seconds * 1e9;

  double t = 0.0;

  // Start around a reference mid (ticks). Later we’ll adapt to book mid.
  Price ref_mid = 10000; // 100.00 if tick=0.01

  while (t < horizon_ns) {
    // Next event time using combined intensity
    const double lambda_total = p_.lambda_limit + p_.lambda_market + p_.lambda_cancel;
    if (lambda_total <= 0.0) break;

    const double dt_sec = rng_.exp(lambda_total); // in seconds
    const double dt_ns = dt_sec * 1e9;
    t += dt_ns;
    if (t >= horizon_ns) break;

    const Ts ts = t0_ns + static_cast<Ts>(t);

    // Choose event type by mixture
    const double u = rng_.uniform01() * lambda_total;

    if (u < p_.lambda_limit) {
      Side side = sample_side();
      Qty qty = sample_qty();
      Price px = limit_price_around(ref_mid, side);
      out.push_back(AddLimit{next_id_++, ts, side, px, qty, /*owner*/ 1});
    } else if (u < p_.lambda_limit + p_.lambda_market) {
      Side side = sample_side();
      Qty qty = sample_qty();
      out.push_back(AddMarket{next_id_++, ts, side, qty, /*owner*/ 2});
    } else {
      auto id = sample_cancel_id();
      if (id) out.push_back(Cancel{*id, ts});
    }
  }

  return out;
}

} // namespace msim
