#include <gtest/gtest.h>
#include "msim/simulator.hpp"

TEST(Simulator, DeterministicReplayAndTrades) {
  msim::Simulator sim;

  std::vector<msim::Event> events;
  events.push_back(msim::AddLimit{1, 10, msim::Side::Sell, 105, 5, 1});
  events.push_back(msim::AddMarket{2, 11, msim::Side::Buy, 3, 9}); // should trade 3 @105

  auto res = sim.run(events);

  ASSERT_EQ(res.trades.size(), 1u);
  EXPECT_EQ(res.trades[0].price, 105);
  EXPECT_EQ(res.trades[0].qty, 3);
  EXPECT_EQ(res.cancel_failures, 0u);
  EXPECT_EQ(res.modify_failures, 0u);

  ASSERT_EQ(res.tops.size(), 2u);
  EXPECT_FALSE(sim.engine().book().is_crossed());
}
