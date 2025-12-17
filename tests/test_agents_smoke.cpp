#include <gtest/gtest.h>

#include <memory>
#include <utility>

#include "msim/matching_engine.hpp"
#include "msim/rules.hpp"
#include "msim/world.hpp"

#include "msim/agents/noise_trader.hpp"
#include "msim/agents/market_maker.hpp"

TEST(Agents, NoiseTraderWorldRunsDeterministically) {
  const uint64_t seed = 42;
  const double horizon = 2.0;

  msim::RulesConfig cfg{};

  // --- World 1 ---
  msim::MatchingEngine eng1{msim::RuleSet(cfg)};
  msim::World w1{std::move(eng1)};

  msim::agents::NoiseTraderConfig ntcfg{};
  w1.add_agent(std::make_unique<msim::agents::NoiseTrader>(msim::OwnerId{1}, ntcfg));

  msim::MarketMakerParams mp{};
  w1.add_agent(std::make_unique<msim::MarketMaker>(msim::OwnerId{2}, cfg, mp));

  auto r1 = w1.run(seed, horizon);

  // --- World 2 (same config + same seed) ---
  msim::MatchingEngine eng2{msim::RuleSet(cfg)};
  msim::World w2{std::move(eng2)};

  w2.add_agent(std::make_unique<msim::agents::NoiseTrader>(msim::OwnerId{1}, ntcfg));
  w2.add_agent(std::make_unique<msim::MarketMaker>(msim::OwnerId{2}, cfg, mp));

  auto r2 = w2.run(seed, horizon);

  EXPECT_EQ(r1.trades.size(), r2.trades.size());
  EXPECT_EQ(r1.tops.size(), r2.tops.size());
}
