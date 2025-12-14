# MSIM — Market Microstructure Simulator (C++20)

A deterministic, event-driven **limit order book + matching engine** built in modern **C++20**, designed as a **microstructure research sandbox**.

MSIM prioritizes **reproducibility, correctness, and extensibility**. The architecture deliberately separates:
- **Core market mechanics** (order book + matching)
- **Policy/rules layer** (admission checks, market phases, auctions, circuit breakers)

This makes it possible to evolve from a clean “core exchange kernel” into a more realistic venue simulator without rewriting the engine.

---

## Why it’s built this way

- **Deterministic replay**: microstructure experiments must be reproducible (same event stream → same trades).
- **Separation of concerns**: matching logic stays minimal and testable; market rules are configurable.
- **Safety through testing**: CI + unit tests allow aggressive feature iteration without breaking invariants.
- **Phase-aware engine**: realistic venues are session-driven (continuous trading + auctions + halts). MSIM supports this model.

---

## Features implemented

### Core book + matching
- Price–time priority **limit order book** (FIFO per price level)
- **Market** and **Limit** orders
- Partial fills and multi-level sweeps
- L2 depth snapshots (top-N levels)
- Cancel + reduce-only modify

### Order instructions (exchange-style)
- **Time-in-Force:** GTC / IOC / FOK  
  - IOC: execute immediately, remainder canceled  
  - FOK: atomic — fills completely or does nothing
- **Market-to-Limit:** if a market order partially fills, remainder becomes a resting limit at last execution price

### Rule / policy layer
- Structured rejects:
  - invalid order
  - market halted
  - tick size violations
  - lot size / min quantity violations
- Reference price tracking (last trade, mid fallback)

### Market phases + auction mechanics
- Phases supported (foundation integrated in engine):
  - Continuous
  - Auction (uncrossing)
  - Trading-at-Last
  - Closing Auction
  - Halted (circuit breaker)
- **Auction uncrossing** at a single clearing price:
  - candidate prices derived from limit prices
  - maximize executable volume
  - tie-break by closest to reference price

### Stability mechanisms (real-venue inspired)
- **Volatility interruption foundation** (price band breach → enter auction)
- **Circuit breaker halt + reopen auction**
  - large downward move triggers halt
  - transition into reopening auction

### Self-Trade Prevention (STP)
- STP modes:
  - None
  - CancelTaker
  - CancelMaker

### Experiment harness
- Deterministic **Simulator** (stable ordering by timestamp + insertion order)
- Seeded stochastic **OrderFlowGenerator**
- CLI tool emits CSV:
  - `trades.csv`
  - `top.csv`

### Engineering quality
- CMake targets (library + CLI + tests)
- GoogleTest suite
- CI across Linux / Windows / macOS + Linux sanitizers (ASan/UBSan)
- Optional warnings-as-errors builds

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
* Latency model + message rate limits
* Stop / stop-limit orders
* Iceberg orders
* Benchmarks + profiling + performance charts
