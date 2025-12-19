#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "httplib.h"

#include "msim/live_world.hpp"
#include "msim/matching_engine.hpp"
#include "msim/order.hpp"
#include "msim/rules.hpp"
#include "msim/types.hpp"

// ---------------- Small helpers ----------------
static long long get_ll(const httplib::Request& req, const char* key, long long def = 0) {
  if (!req.has_param(key)) return def;
  try {
    return std::stoll(req.get_param_value(key));
  } catch (...) {
    return def;
  }
}

static std::string json_bool(bool v) { return v ? "true" : "false"; }

static std::string read_text_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// ---------------- Compile-time detection helpers ----------------
template <class T>
static bool ack_accepted_(const T& a) {
  if constexpr (requires { a.accepted; }) {
    return static_cast<bool>(a.accepted);
  } else if constexpr (requires { a.ok; }) {
    return static_cast<bool>(a.ok);
  } else if constexpr (requires { a.status; }) {
    return static_cast<int>(a.status) == 0;
  } else {
    return true;
  }
}

template <class T>
static int ack_reason_(const T& a) {
  if constexpr (requires { a.reject_reason; }) {
    return static_cast<int>(a.reject_reason);
  } else if constexpr (requires { a.reason; }) {
    return static_cast<int>(a.reason);
  } else if constexpr (requires { a.reject; }) {
    return static_cast<int>(a.reject);
  } else {
    return 0;
  }
}

template <class T>
static long long ack_order_id_(const T& a) {
  if constexpr (requires { a.order_id; }) {
    return static_cast<long long>(a.order_id);
  } else if constexpr (requires { a.id; }) {
    return static_cast<long long>(a.id);
  } else if constexpr (requires { a.oid; }) {
    return static_cast<long long>(a.oid);
  } else {
    return 0;
  }
}

template <class LiveW>
static bool live_cancel_(LiveW& w, msim::OrderId id) {
  if constexpr (requires { w.cancel(id); }) {
    return w.cancel(id);
  } else if constexpr (requires { w.cancel_order(id); }) {
    return w.cancel_order(id);
  } else if constexpr (requires { w.cancel_order_id(id); }) {
    return w.cancel_order_id(id);
  } else {
    return false;
  }
}

template <class LiveW>
static bool live_modify_(LiveW& w, msim::OrderId id, msim::Qty q) {
  if constexpr (requires { w.modify_qty(id, q); }) {
    return w.modify_qty(id, q);
  } else if constexpr (requires { w.modify(id, q); }) {
    return w.modify(id, q);
  } else if constexpr (requires { w.modify_order_qty(id, q); }) {
    return w.modify_order_qty(id, q);
  } else {
    return false;
  }
}

// Depth level accessors (supports several layouts)
template <class L>
static long long level_price_(const L& x) {
  if constexpr (requires { x.price; }) return static_cast<long long>(x.price);
  else if constexpr (requires { x.px; }) return static_cast<long long>(x.px);
  else return 0;
}

template <class L>
static long long level_qty_(const L& x) {
  if constexpr (requires { x.qty; }) return static_cast<long long>(x.qty);
  else if constexpr (requires { x.total_qty; }) return static_cast<long long>(x.total_qty);
  else if constexpr (requires { x.size; }) return static_cast<long long>(x.size);
  else return 0;
}

template <class L>
static long long level_orders_(const L& x) {
  if constexpr (requires { x.order_count; }) return static_cast<long long>(x.order_count);
  else if constexpr (requires { x.orders; }) return static_cast<long long>(x.orders);
  else return 0;
}

static void set_no_cache(httplib::Response& res) {
  res.set_header("Cache-Control", "no-store, max-age=0");
  res.set_header("Pragma", "no-cache");
}

int main(int argc, char** argv) {
  // args: [port] [seed]
  int port = 8080;
  uint64_t seed = 1;

  if (argc > 1) port = std::atoi(argv[1]);
  if (argc > 2) seed = static_cast<uint64_t>(std::strtoull(argv[2], nullptr, 10));

  // long horizon to behave "live"
  const double horizon_seconds = 3600.0 * 24.0 * 365.0;

  msim::RulesConfig rcfg{};
  msim::MatchingEngine eng{msim::RuleSet(rcfg)};
  msim::LiveWorld world{std::move(eng)};
  world.start(seed, horizon_seconds);

  httplib::Server svr;

  // ---- Static files from ./web ----
  svr.Get("/", [&](const httplib::Request&, httplib::Response& res) {
    auto html = read_text_file("web/index.html");
    if (html.empty()) {
      res.set_content("<h1>Missing web/index.html</h1><p>Run from repo root.</p>", "text/html");
      return;
    }
    set_no_cache(res);
    res.set_content(html, "text/html; charset=utf-8");
  });

  svr.Get("/styles.css", [&](const httplib::Request&, httplib::Response& res) {
    auto css = read_text_file("web/styles.css");
    if (css.empty()) {
      res.set_content("/* Missing web/styles.css */", "text/css");
      return;
    }
    set_no_cache(res);
    res.set_content(css, "text/css; charset=utf-8");
  });

  svr.Get("/app.js", [&](const httplib::Request&, httplib::Response& res) {
    auto js = read_text_file("web/app.js");
    if (js.empty()) {
      res.set_content("// Missing web/app.js", "application/javascript");
      return;
    }
    set_no_cache(res);
    res.set_content(js, "application/javascript; charset=utf-8");
  });

  // ---- APIs ----
  svr.Get("/api/snapshot", [&](const httplib::Request&, httplib::Response& res) {
    constexpr std::size_t MAX_TRADES = 250;
    auto snap = world.snapshot(MAX_TRADES);

    std::ostringstream oss;
    oss << "{";
    oss << "\"ts\":" << snap.ts << ",";
    oss << "\"best_bid\":" << (snap.best_bid ? std::to_string(*snap.best_bid) : "null") << ",";
    oss << "\"best_ask\":" << (snap.best_ask ? std::to_string(*snap.best_ask) : "null") << ",";
    oss << "\"mid\":" << (snap.mid ? std::to_string(*snap.mid) : "null") << ",";
    oss << "\"last_trade\":" << (snap.last_trade ? std::to_string(*snap.last_trade) : "null") << ",";

    oss << "\"recent_trades\":[";
    for (std::size_t i = 0; i < snap.recent_trades.size(); ++i) {
      const auto& t = snap.recent_trades[i];
      if (i) oss << ",";
      oss << "{";
      oss << "\"id\":" << t.id << ",";
      oss << "\"ts\":" << t.ts << ",";
      oss << "\"price\":" << t.price << ",";
      oss << "\"qty\":" << t.qty << ",";
      oss << "\"maker_order_id\":" << t.maker_order_id << ",";
      oss << "\"taker_order_id\":" << t.taker_order_id;
      oss << "}";
    }
    oss << "]";
    oss << "}";

    set_no_cache(res);
    res.set_content(oss.str(), "application/json");
  });

  svr.Get("/api/depth", [&](const httplib::Request& req, httplib::Response& res) {
    std::size_t levels = 5;
    if (req.has_param("levels")) {
      try {
        const auto v = std::stoull(req.get_param_value("levels"));
        if (v > 0 && v <= 200) levels = static_cast<std::size_t>(v);
      } catch (...) {
      }
    }

    auto d = world.book_depth(levels);

    long long max_cum = 1;
    {
      long long acc = 0;
      for (const auto& x : d.asks) {
        acc += level_qty_(x);
        max_cum = std::max(max_cum, acc);
      }
      acc = 0;
      for (const auto& x : d.bids) {
        acc += level_qty_(x);
        max_cum = std::max(max_cum, acc);
      }
    }

    std::ostringstream oss;
    oss << "{";
    oss << "\"max_cum\":" << max_cum << ",";
    oss << "\"bids\":[";
    for (std::size_t i = 0; i < d.bids.size(); ++i) {
      const auto& x = d.bids[i];
      if (i) oss << ",";
      oss << "{"
          << "\"price\":" << level_price_(x) << ","
          << "\"qty\":" << level_qty_(x) << ","
          << "\"orders\":" << level_orders_(x)
          << "}";
    }
    oss << "],";
    oss << "\"asks\":[";
    for (std::size_t i = 0; i < d.asks.size(); ++i) {
      const auto& x = d.asks[i];
      if (i) oss << ",";
      oss << "{"
          << "\"price\":" << level_price_(x) << ","
          << "\"qty\":" << level_qty_(x) << ","
          << "\"orders\":" << level_orders_(x)
          << "}";
    }
    oss << "]";
    oss << "}";

    set_no_cache(res);
    res.set_content(oss.str(), "application/json");
  });

  svr.Post("/api/order", [&](const httplib::Request& req, httplib::Response& res) {
    const auto side_s = req.has_param("side") ? req.get_param_value("side") : "Buy";
    const auto type_s = req.has_param("type") ? req.get_param_value("type") : "Limit";
    const auto tif_s  = req.has_param("tif") ? req.get_param_value("tif") : "GTC";

    const auto id_ll = get_ll(req, "id", 0);
    const auto price_ll = get_ll(req, "price", 100);
    const auto qty_ll = get_ll(req, "qty", 1);

    msim::Order o{};
    o.owner = static_cast<msim::OwnerId>(999);
    if (id_ll > 0) o.id = static_cast<msim::OrderId>(id_ll);

    o.side = (side_s == "Sell") ? msim::Side::Sell : msim::Side::Buy;
    o.type = (type_s == "Market") ? msim::OrderType::Market : msim::OrderType::Limit;
    o.tif  = (tif_s == "IOC") ? msim::TimeInForce::IOC
           : (tif_s == "FOK") ? msim::TimeInForce::FOK
                              : msim::TimeInForce::GTC;

    o.price = static_cast<msim::Price>(price_ll);
    o.qty   = static_cast<msim::Qty>(qty_ll);

    const auto ack = world.submit_order(o);

    const bool accepted = ack_accepted_(ack);
    const int reason = ack_reason_(ack);
    const long long oid = ack_order_id_(ack);

    std::ostringstream oss;
    oss << "{";
    oss << "\"accepted\":" << json_bool(accepted) << ",";
    oss << "\"reason\":" << reason << ",";
    oss << "\"order_id\":" << oid;
    oss << "}";
    set_no_cache(res);
    res.set_content(oss.str(), "application/json");
  });

  svr.Post("/api/cancel", [&](const httplib::Request& req, httplib::Response& res) {
    const auto id_ll = get_ll(req, "id", 0);
    const bool ok = live_cancel_(world, static_cast<msim::OrderId>(id_ll));
    set_no_cache(res);
    res.set_content(std::string("{\"ok\":") + json_bool(ok) + "}", "application/json");
  });

  svr.Post("/api/modify", [&](const httplib::Request& req, httplib::Response& res) {
    const auto id_ll = get_ll(req, "id", 0);
    const auto q_ll  = get_ll(req, "qty", 0);
    const bool ok = live_modify_(world,
                                static_cast<msim::OrderId>(id_ll),
                                static_cast<msim::Qty>(q_ll));
    set_no_cache(res);
    res.set_content(std::string("{\"ok\":") + json_bool(ok) + "}", "application/json");
  });

  std::cout << "MSIM gateway listening on http://localhost:" << port << "/\n";
  std::cout << "Run from repo root so it can read: web/index.html, web/styles.css, web/app.js\n";

  svr.listen("0.0.0.0", port);

  if constexpr (requires { world.stop(); }) {
    world.stop();
  }
  return 0;
}
