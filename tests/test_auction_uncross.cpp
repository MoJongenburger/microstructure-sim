#include <gtest/gtest.h>
#include "msim/matching_engine.hpp"

TEST(AuctionUncross, ComputesClearingPriceAndExecutesAtSinglePrice) {
  msim::MatchingEngine eng;

  // Force short auction
  eng.rules_mut().config_mut().enable_price_bands = true;
  eng.rules_mut().config_mut().enable_volatility_interruption = true;
  eng.rules_mut().config_mut().band_bps = 100;               // 1%
  eng.rules_mut().config_mut().vol_auction_duration_ns = 5;  // 5 ns

  // Seed reference trade at 10000
  EXPECT_TRUE(eng.book_mut().add_resting_limit(msim::Order{1, 1, msim::Side::Sell, msim::OrderType::Limit, 10000, 1, 2}));
  (void)eng.process(msim::Order{2, 2, msim::Side::Buy, msim::OrderType::Market, 0, 1, 3});

  // Put ask far away so a market buy triggers auction
  EXPECT_TRUE(eng.book_mut().add_resting_limit(msim::Order{3, 3, msim::Side::Sell, msim::OrderType::Limit, 12000, 10, 9}));

  // Trigger auction with market buy (queued)
  auto r0 = eng.process(msim::Order{10, 10, msim::Side::Buy, msim::OrderType::Market, 0, 5, 7});
  EXPECT_TRUE(r0.trades.empty());
  EXPECT_EQ(eng.rules().phase(), msim::MarketPhase::Auction);

  // During auction, queue two-sided interest around two limit prices
  // Buys: 11800 x5, 11900 x5
  (void)eng.process(msim::Order{11, 11, msim::Side::Buy, msim::OrderType::Limit, 11800, 5, 1});
  (void)eng.process(msim::Order{12, 12, msim::Side::Buy, msim::OrderType::Limit, 11900, 5, 1});
  // Sells: 11800 x6, 11900 x2
  (void)eng.process(msim::Order{13, 13, msim::Side::Sell, msim::OrderType::Limit, 11800, 6, 2});
  (void)eng.process(msim::Order{14, 14, msim::Side::Sell, msim::OrderType::Limit, 11900, 2, 2});

  // At ts >= end, uncross happens before processing this benign order
  auto r1 = eng.process(msim::Order{15, 20, msim::Side::Buy, msim::OrderType::Limit, 1, 1, 8});

  EXPECT_EQ(eng.rules().phase(), msim::MarketPhase::Continuous);
  EXPECT_FALSE(r1.trades.empty());

  // All auction trades should print at a single clearing price
  const auto px0 = r1.trades.front().price;
  for (const auto& t : r1.trades) {
    EXPECT_EQ(t.price, px0);
  }
}
