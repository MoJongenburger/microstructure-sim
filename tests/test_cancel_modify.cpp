#include <gtest/gtest.h>
#include "msim/book.hpp"

TEST(OrderBook, CancelRemovesOrderAndUpdatesDepth) {
  msim::OrderBook ob;

  EXPECT_TRUE(ob.add_resting_limit(msim::Order{1, 10, msim::Side::Buy, msim::OrderType::Limit, 100, 5, 1}));
  EXPECT_TRUE(ob.add_resting_limit(msim::Order{2, 11, msim::Side::Buy, msim::OrderType::Limit, 100, 7, 1}));

  auto d0 = ob.depth(msim::Side::Buy, 1);
  ASSERT_EQ(d0.size(), 1u);
  EXPECT_EQ(d0[0].total_qty, 12);

  EXPECT_TRUE(ob.cancel(1));

  auto d1 = ob.depth(msim::Side::Buy, 1);
  ASSERT_EQ(d1.size(), 1u);
  EXPECT_EQ(d1[0].total_qty, 7);

  EXPECT_FALSE(ob.cancel(9999));
}

TEST(OrderBook, ModifyOnlyReducesQuantity) {
  msim::OrderBook ob;

  EXPECT_TRUE(ob.add_resting_limit(msim::Order{1, 10, msim::Side::Sell, msim::OrderType::Limit, 110, 10, 2}));

  EXPECT_TRUE(ob.modify(1, 6));   // reduce
  auto d = ob.depth(msim::Side::Sell, 1);
  ASSERT_EQ(d.size(), 1u);
  EXPECT_EQ(d[0].total_qty, 6);

  EXPECT_FALSE(ob.modify(1, 12)); // increase not allowed
  EXPECT_FALSE(ob.modify(9999, 1));
}
