#include <gtest/gtest.h>

#include "msim/matching_engine.hpp"
#include "msim/rules.hpp"
#include "msim/order.hpp"

TEST(Rules, RejectInvalidOrdersWithReason) {
  msim::MatchingEngine eng;

  msim::Order o{};
  o.id = 1;
  o.ts = 1;
  o.side = msim::Side::Buy;
  o.type = msim::OrderType::Limit;
  o.price = 100;
  o.qty = 0; // invalid

  auto res = eng.process(o);
  EXPECT_EQ(res.status, msim::OrderStatus::Rejected);
  EXPECT_EQ(res.reject_reason, msim::RejectReason::InvalidOrder);
}

TEST(Rules, HaltQueuesOrdersWhenConfigured) {
  msim::RulesConfig cfg;
  cfg.enforce_halt = true;
  cfg.queue_orders_during_halt = true;

  // IMPORTANT: braces avoid “most vexing parse”
  msim::MatchingEngine eng{ msim::RuleSet{cfg} };
  eng.rules_mut().set_phase(msim::MarketPhase::Halted);

  msim::Order o{};
  o.id = 1;
  o.ts = 1;
  o.side = msim::Side::Buy;
  o.type = msim::OrderType::Limit;
  o.price = 100;
  o.qty = 10;
  o.owner = 1;

  auto res = eng.process(o);

  // Accepted because we queue during halts
  EXPECT_EQ(res.status, msim::OrderStatus::Accepted);
  EXPECT_EQ(res.reject_reason, msim::RejectReason::None);
}

TEST(Rules, RejectOrdersWhenMarketHaltedIfQueueDisabled) {
  msim::RulesConfig cfg;
  cfg.enforce_halt = true;
  cfg.queue_orders_during_halt = false;

  // IMPORTANT: braces avoid “most vexing parse”
  msim::MatchingEngine eng{ msim::RuleSet{cfg} };
  eng.rules_mut().set_phase(msim::MarketPhase::Halted);

  msim::Order o{};
  o.id = 1;
  o.ts = 1;
  o.side = msim::Side::Buy;
  o.type = msim::OrderType::Limit;
  o.price = 100;
  o.qty = 10;
  o.owner = 1;

  auto res = eng.process(o);

  EXPECT_EQ(res.status, msim::OrderStatus::Rejected);
  EXPECT_EQ(res.reject_reason, msim::RejectReason::MarketHalted);
}
