#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "httplib.h"
#include "nlohmann/json.hpp"

#include "msim/matching_engine.hpp"
#include "msim/rules.hpp"
#include "msim/order.hpp"

using json = nlohmann::json;

namespace {

struct SharedState {
  std::mutex m;

  msim::MatchingEngine engine;

  std::vector<msim::Trade> trades;     // append-only
  msim::Ts ts{0};

  msim::OrderId next_order_id{1};
  std::optional<msim::Price> last_trade_price;

  explicit SharedState(msim::MatchingEngine eng) : engine(std::move(eng)) {}
};

static std::string slurp_file(const std::string& path) {
  std::ifstream f(path);
  if (!f.is_open()) return {};
  return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// ---- helpers: parse enums from JSON ----
static msim::Side parse_side(const std::string& s) {
  if (s == "buy") return msim::Side::Buy;
  return msim::Side::Sell;
}

static msim::OrderType parse_type(const std::string& s) {
  if (s == "market") return msim::OrderType::Market;
  return msim::OrderType::Limit;
}

static msim::TimeInForce parse_tif(const std::string& s) {
  if (s == "IOC") return msim::TimeInForce::IOC;
  if (s == "FOK") return msim::TimeInForce::FOK;
  return msim::TimeInForce::GTC;
}

static msim::MarketStyle parse_mkt_style(const std::string& s) {
  if (s == "MTL") return msim::MarketStyle::MarketToLimit;
  return msim::MarketStyle::PureMarket;
}

// ---- serialize a small state snapshot (top + L2 ladder) ----
static json snapshot_json(const SharedState& S, int depth_levels = 10) {
  json out;

  const auto& book = S.engine.book(); // assumes you expose book() const; if not, use book_mut() carefully.
  const auto bb = book.best_bid();
  const auto ba = book.best_ask();
  const auto mid = msim::midprice(bb, ba);

  out["ts"] = S.ts;
  out["best_bid"] = bb ? json(*bb) : json(nullptr);
  out["best_ask"] = ba ? json(*ba) : json(nullptr);
  out["mid"]      = mid ? json(*mid) : json(nullptr);
  out["last_trade_price"] = S.last_trade_price ? json(*S.last_trade_price) : json(nullptr);

  // Depth: rely on the book exposing bids_/asks_ maps (your engine already uses them internally).
  // If your OrderBook doesn't expose these publicly, tell me and I’ll adjust to your actual API.
  json bids = json::array();
  json asks = json::array();

  int c = 0;
  for (auto it = book.bids_.begin(); it != book.bids_.end() && c < depth_levels; ++it, ++c) {
    bids.push_back({{"px", it->first}, {"qty", it->second.total_qty}});
  }
  c = 0;
  for (auto it = book.asks_.begin(); it != book.asks_.end() && c < depth_levels; ++it, ++c) {
    asks.push_back({{"px", it->first}, {"qty", it->second.total_qty}});
  }

  out["bids"] = bids;
  out["asks"] = asks;
  out["trades_total"] = S.trades.size();
  return out;
}

static json trades_since_json(const SharedState& S, std::uint64_t since_trade_id) {
  json arr = json::array();
  for (const auto& t : S.trades) {
    if (t.id <= since_trade_id) continue;
    arr.push_back({
      {"id", t.id},
      {"ts", t.ts},
      {"price", t.price},
      {"qty", t.qty},
      {"maker", t.maker_order_id},
      {"taker", t.taker_order_id},
    });
  }
  return arr;
}

} // namespace

int main(int argc, char** argv) {
  const int port = (argc >= 2) ? std::stoi(argv[1]) : 8080;

  // Exchange rules config (keep defaults; your Rules layer enforces tick/lot/min qty etc).
  msim::RulesConfig rcfg{};
  msim::MatchingEngine eng{msim::RuleSet(rcfg)};
  SharedState S{std::move(eng)};

  // --- background “market” thread (tiny noise bot to generate activity) ---
  std::atomic<bool> running{true};
  std::thread market_thread([&] {
    std::mt19937_64 rng(12345);
    std::uniform_real_distribution<double> U(0.0, 1.0);
    std::uniform_int_distribution<int> side01(0, 1);
    std::uniform_int_distribution<int> offset(0, 5);
    std::uniform_int_distribution<int> qtyd(1, 5);

    const msim::Ts dt = 1'000'000; // 1ms
    msim::OrderId bot_next_id = 1'000'000;

    while (running.load()) {
      {
        std::lock_guard<std::mutex> lk(S.m);
        S.ts += dt;

        // Flush phase transitions if needed
        auto flushed = S.engine.flush(S.ts);
        for (auto& t : flushed) {
          S.last_trade_price = t.price;
          S.trades.push_back(t);
        }

        // Bot: occasionally submit a limit near mid, sometimes market
        if (U(rng) < 0.25) {
          msim::Order o{};
          o.id = bot_next_id++;
          o.ts = S.ts;
          o.owner = msim::OwnerId{777};

          o.side = (side01(rng) == 0) ? msim::Side::Buy : msim::Side::Sell;
          o.qty = static_cast<msim::Qty>(qtyd(rng));

          const auto snap = snapshot_json(S, 1);
          msim::Price ref = 100;
          if (!snap["mid"].is_null()) ref = snap["mid"].get<msim::Price>();

          // Small random behavior
          if (U(rng) < 0.15) {
            o.type = msim::OrderType::Market;
          } else {
            o.type = msim::OrderType::Limit;
            const int off = offset(rng);
            if (o.side == msim::Side::Buy)  o.price = ref - off;
            else                           o.price = ref + off;
            if (o.price < 1) o.price = 1;
          }

          auto res = S.engine.process(o);
          for (auto& t : res.trades) {
            S.last_trade_price = t.price;
            S.trades.push_back(t);
          }
        }
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  httplib::Server svr;

  // Serve the UI
  svr.Get("/", [&](const httplib::Request&, httplib::Response& res) {
    const std::string html = slurp_file("web/index.html");
    if (html.empty()) {
      res.set_content("web/index.html not found. Run from repo root.", "text/plain");
      return;
    }
    res.set_content(html, "text/html");
  });

  // Health
  svr.Get("/health", [&](const httplib::Request&, httplib::Response& res) {
    res.set_content("ok", "text/plain");
  });

  // Snapshot: top + depth
  svr.Get("/state", [&](const httplib::Request& req, httplib::Response& res) {
    const int depth = req.has_param("depth") ? std::stoi(req.get_param_value("depth")) : 10;
    json out;
    {
      std::lock_guard<std::mutex> lk(S.m);
      out = snapshot_json(S, depth);
    }
    res.set_content(out.dump(), "application/json");
  });

  // Trades since id
  svr.Get("/trades", [&](const httplib::Request& req, httplib::Response& res) {
    std::uint64_t since = 0;
    if (req.has_param("since")) since = std::stoull(req.get_param_value("since"));
    json out;
    {
      std::lock_guard<std::mutex> lk(S.m);
      out = trades_since_json(S, since);
    }
    res.set_content(out.dump(), "application/json");
  });

  // Submit order
  svr.Post("/order", [&](const httplib::Request& req, httplib::Response& res) {
    json j;
    try { j = json::parse(req.body); }
    catch (...) {
      res.status = 400;
      res.set_content(R"({"error":"invalid json"})", "application/json");
      return;
    }

    json out;
    {
      std::lock_guard<std::mutex> lk(S.m);

      msim::Order o{};
      o.id = S.next_order_id++;
      o.ts = S.ts; // server-time; next step we’ll make this deterministic with seq-stamping
      o.owner = msim::OwnerId{999};

      o.side = parse_side(j.value("side", "buy"));
      o.type = parse_type(j.value("type", "limit"));
      o.tif  = parse_tif(j.value("tif", "GTC"));
      o.mkt_style = parse_mkt_style(j.value("mkt_style", "PURE"));

      o.qty = static_cast<msim::Qty>(j.value("qty", 1));
      if (o.type == msim::OrderType::Limit) {
        if (!j.contains("price")) {
          res.status = 400;
          res.set_content(R"({"error":"limit order requires price"})", "application/json");
          return;
        }
        o.price = static_cast<msim::Price>(j["price"].get<int64_t>());
      }

      auto r = S.engine.process(o);
      for (auto& t : r.trades) {
        S.last_trade_price = t.price;
        S.trades.push_back(t);
      }

      out["order_id"] = o.id;
      out["status"] = static_cast<int>(r.status);
      out["reject_reason"] = static_cast<int>(r.reject_reason);
      out["filled_qty"] = r.filled_qty;
      out["trades_count"] = r.trades.size();
    }

    res.set_content(out.dump(), "application/json");
  });

  // Cancel
  svr.Post("/cancel", [&](const httplib::Request& req, httplib::Response& res) {
    json j;
    try { j = json::parse(req.body); }
    catch (...) {
      res.status = 400;
      res.set_content(R"({"error":"invalid json"})", "application/json");
      return;
    }

    const auto id = static_cast<msim::OrderId>(j.value("order_id", 0));
    bool ok = false;
    {
      std::lock_guard<std::mutex> lk(S.m);
      ok = S.engine.book_mut().cancel(id);
    }
    res.set_content(json({{"ok", ok}}).dump(), "application/json");
  });

  // Modify (reduce-only)
  svr.Post("/modify", [&](const httplib::Request& req, httplib::Response& res) {
    json j;
    try { j = json::parse(req.body); }
    catch (...) {
      res.status = 400;
      res.set_content(R"({"error":"invalid json"})", "application/json");
      return;
    }

    const auto id = static_cast<msim::OrderId>(j.value("order_id", 0));
    const auto new_qty = static_cast<msim::Qty>(j.value("new_qty", 0));
    bool ok = false;
    {
      std::lock_guard<std::mutex> lk(S.m);
      ok = S.engine.book_mut().modify_qty(id, new_qty);
    }
    res.set_content(json({{"ok", ok}}).dump(), "application/json");
  });

  std::cout << "msim_gateway listening on http://localhost:" << port << "\n";
  svr.listen("0.0.0.0", port);

  running.store(false);
  if (market_thread.joinable()) market_thread.join();
  return 0;
}
