#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "msim/world.hpp"
#include "msim/rules.hpp"
#include "msim/agents/noise_trader.hpp"
#include "msim/agents/market_maker.hpp"

static void write_trades_csv(const std::string& path, const std::vector<msim::Trade>& trades) {
  std::ofstream f(path);
  f << "trade_id,ts,price,qty,maker_id,taker_id\n";
  for (const auto& t : trades) {
    f << t.id << "," << t.ts << "," << t.price << "," << t.qty << ","
      << t.maker_order_id << "," << t.taker_order_id << "\n";
  }
}

static void write_top_csv(const std::string& path, const std::vector<msim::BookTop>& tops) {
  std::ofstream f(path);
  f << "ts,best_bid,best_ask,mid\n";
  for (const auto& x : tops) {
    f << x.ts << ",";
    if (x.best_bid) f << *x.best_bid;
    f << ",";
    if (x.best_ask) f << *x.best_ask;
    f << ",";
    if (x.mid) f << *x.mid;
    f << "\n";
  }
}

static void write_accounts_csv(const std::string& path, const std::vector<msim::AccountSnapshot>& accts) {
  std::ofstream f(path);
  f << "ts,owner,cash_ticks,position,mtm_ticks\n";
  for (const auto& a : accts) {
    f << a.ts << "," << a.owner << "," << a.cash_ticks << "," << a.position << "," << a.mtm_ticks << "\n";
  }
}

int main(int argc, char** argv) {
  uint64_t seed = 1;
  double horizon = 2.0; // seconds

  if (argc >= 2) seed = static_cast<uint64_t>(std::stoull(argv[1]));
  if (argc >= 3) horizon = std::stod(argv[2]);

  msim::RulesConfig rcfg{};
  rcfg.tick_size_ticks = 1;
  rcfg.lot_size = 1;
  rcfg.min_qty = 1;

  msim::MatchingEngine eng{ msim::RuleSet(rcfg) };
  msim::World w(std::move(eng));

  // Agents
  msim::NoiseTraderParams np{};
  w.add_agent(std::make_unique<msim::NoiseTrader>(msim::OwnerId{1}, rcfg, np));

  msim::MarketMakerParams mp{};
  w.add_agent(std::make_unique<msim::MarketMaker>(msim::OwnerId{2}, rcfg, mp));

  msim::WorldConfig wcfg{};
  wcfg.dt_ns = 1'000'000; // 1ms

  auto res = w.run(seed, horizon, wcfg);

  write_trades_csv("trades.csv", res.trades);
  write_top_csv("top.csv", res.tops);
  write_accounts_csv("accounts.csv", res.accounts);

  std::cout << "trades=" << res.trades.size()
            << " tops=" << res.tops.size()
            << " accounts=" << res.accounts.size()
            << " cancel_failures=" << res.cancel_failures
            << " modify_failures=" << res.modify_failures
            << "\n";
  return 0;
}
