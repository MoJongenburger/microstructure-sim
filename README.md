# microstructure-sim

Event-driven **limit order book** + **matching engine** simulator in modern C++20.

This repository is built as a **research-grade microstructure sandbox**:
- deterministic replay of event streams
- realistic order instructions (IOC/FOK/Market-to-Limit)
- exchange-style admission rules (tick size + lot size)
- self-trade prevention (STP)
- CI-tested across Linux/Windows/macOS with sanitizers

> Goal: provide a clean, extensible foundation for later additions such as auctions (closing/volatility), price bands, circuit breakers, latency models, rate limits, and iceberg orders.

---

## Features implemented

### Core matching + book
- Price–time priority **limit order book** (FIFO per price level)
- **Market** and **Limit** orders
- Partial fills and multi-level sweeps
- L2 depth snapshots (top-N levels)
- Cancel and reduce-only modify (MVP scan-based implementation)

### Order instructions 
- **Time-in-Force:** GTC / IOC / FOK  
  - IOC: immediate execution, remainder canceled  
  - FOK: atomic — fills fully or does nothing
- **Market-to-Limit:** remainder becomes a resting limit at last execution price (if any fill occurred)

### Rule / policy layer 
- Market phase support (Continuous / Halted / Auction foundation)
- Structured rejects:
  - invalid order
  - market halted
  - tick size violations
  - lot size / min quantity violations
- Tracks last trade price (reference foundation for price bands / halts later)

### Self-Trade Prevention 
- STP modes:
  - None
  - CancelTaker
  - CancelMaker

### Experiment harness
- Deterministic **Simulator** (stable ordering by timestamp + insertion order)
- Stochastic **OrderFlowGenerator** (seeded RNG)
- CLI tool emits CSV:
  - `trades.csv`
  - `top.csv`

---

## Build & Test

### Configure + build
```bash
cmake -S . -B build
cmake --build build
````

### Run tests

```bash
ctest --test-dir build --output-on-failure
```

### Run the CLI simulator

```bash
# args: <seed> <horizon_seconds>
./build/msim_cli 1 2.0
```

Outputs:

* `trades.csv` — trade prints (id, timestamp, price, qty, maker/taker ids)
* `top.csv` — top-of-book evolution (timestamp, best bid/ask, mid)

---

## Project structure

```text
include/msim/        # public headers (order book, engine, rules, simulator, flow)
src/                 # implementations
tests/               # gtests
.github/workflows/   # CI
```

---

## Roadmap (next)

* O(1) cancel/modify via order-id index (locator map)
* Price bands + volatility interruption (volatility auction)
* Closing auction + trading-at-last phase
* Circuit breaker halts (e.g., large downward move) + reopen auction
* Latency model + message rate limits
* Stop/stop-limit orders
* Iceberg orders
* Benchmarks + profiling + performance charts

```
```
