# HFT Memory Allocators

High-frequency trading memory allocation primitives — O(1) object pool and arena allocator with zero heap allocation in the hot path.

Built as part of a structured 125-day HFT SWE curriculum targeting roles at Jane Street, Optiver, HRT, and Flow Traders.

---

## The Problem

Standard `new`/`delete` is non-deterministic in latency:

```
new/delete (1M calls):
  p50: 41 ns    ← typical
  p99: 42 ns    ← still fast
  max: 35,500 ns ← 35 MICROSECONDS — 850x the median
```

Three sources of non-determinism:
1. **Fragmentation** — after hours of trading, malloc searches a fragmented free list
2. **Lock contention** — default allocator uses a global lock across threads
3. **OS involvement** — when free list is empty, `sbrk()`/`mmap()` system call adds 200-10,000ns

In HFT where trading decisions must happen in under 1 microsecond, a 35μs allocation spike means missing 35,000 potential opportunities.

---

## Solution

Pre-allocate everything at startup. Hot path never calls `malloc`/`free`.

```
ObjectPool (warmed up):
  p50: 41 ns
  p99: 42 ns
  max: 5,042 ns  ← 7x lower max than new/delete
```

---

## ObjectPool

Fixed-size object pool with O(1) acquire and release.

```cpp
#include "include/object_pool.hpp"

ObjectPool<Order, 4096> pool;  // pre-allocates 4096 Orders at startup
                                // warmup happens in constructor

// Hot path — O(1), no malloc:
Order* o = pool.acquire(15000.0, 100);
// ... process order ...
pool.release(o);  // O(1), no free
```

### How it works

```
Storage: [raw bytes for N objects — no constructors yet]
Freelist: [0][1][2]...[N-1]  ← stack of available slot indices

acquire():
  pop slot from freelist      O(1)
  placement new at slot       no malloc
  return pointer

release():
  call ~T() explicitly        destructor only, no free
  push slot back to freelist  O(1)
```

### Warmup

The constructor touches every 64 bytes of storage:
```cpp
for (size_t i = 0; i < sizeof(storage_); i += 64)
    storage_[i] = 0;
```

This triggers all OS page faults at startup. Without warmup, first access to each page during trading causes a 10,000ns+ spike. After warmup, every acquire() is guaranteed to hit already-mapped physical memory.

---

## ArenaAllocator

Bump-pointer arena for per-message temporary allocations.

```cpp
#include "include/arena_allocator.hpp"

ArenaAllocator arena(1024 * 1024);  // 1MB, warmed up in constructor

// Per market data message:
auto* price = arena.allocate<double>();
auto* qty   = arena.allocate<int32_t>();
auto* sym   = arena.allocate_array<char>(8);

*price = 15000.0;
*qty   = 100;

// ... process all fields ...

arena.reset();  // O(1) — ready for next message, same buffer reused
```

### How it works

```
buffer: [_________________________ 1MB _________________________]
         ↑
        offset = 0

After allocate<double>():
buffer: [dddddddd________________________________________________]
                 ↑
                offset = 8

After allocate<int32_t>():
buffer: [ddddddddIIII____________________________________________]
                     ↑
                    offset = 12

After reset():
buffer: [ddddddddIIII____________________________________________]
         ↑
        offset = 0  ← just moved the pointer, nothing freed
```

**Why reset() is O(1):** just sets `offset = 0`. No individual frees. All data considered overwritten on next use.

**Why individual free is not supported:** no metadata tracked per allocation — only the bump pointer exists.

---

## Benchmark

```
Compile:
  g++ -std=c++20 -O2 -I./include benchmarks/pool_benchmark.cpp -o pool_benchmark

Run:
  ./pool_benchmark
```

**Results on Apple M-series (1M allocations):**

| Allocator | min | p50 | p99 | max |
|---|---|---|---|---|
| new/delete (no warmup) | 0 ns | 41 ns | 42 ns | 35,500 ns |
| ObjectPool (warmed up) | 0 ns | 41 ns | 42 ns | 5,042 ns |
| ArenaAllocator (warmed up) | 0 ns | 0 ns | 41 ns | 4,200 ns |

Key result: **pool max is 7x lower than new/delete max** — the tail latency that matters most in HFT.

---

## Project Structure

```
hft-curriculum/
├── include/
│   ├── object_pool.hpp      — O(1) fixed-size object pool
│   └── arena_allocator.hpp  — O(1) bump-pointer arena
├── benchmarks/
│   └── pool_benchmark.cpp   — latency distribution comparison
└── README.md
```

---

## Key Concepts

**Placement new:** construct object at a specific address without allocating memory.
```cpp
void* mem = pool.get_raw_slot();
Order* o = new(mem) Order(15000, 100);  // no malloc
o->~Order();                             // explicit destructor — not delete
```

**Aligned storage:** pool uses `alignas(T)` to ensure correct alignment for any T.
```cpp
alignas(T) char storage_[N * sizeof(T)];
```

**Page faults:** OS allocates physical RAM lazily on first touch. Warmup forces this at startup, not during trading.

---

## Part of HFT SWE Curriculum

This is Day 8 of a 125-day structured curriculum covering:
- Phase 0 (Days 1-10): C++ fundamentals — types, pointers, memory, OOP, templates
- Phase 1 (Days 11-20): Systems — cache, atomics, SIMD, profiling
- Phase 2 (Days 21-40): HFT patterns — order book, risk, SPSC queues, allocators
- Phase 3 (Days 41-60): OS internals — scheduler, huge pages, NUMA, kernel bypass
- Phase 4 (Days 61-80): Networking — multicast, ITCH, FIX protocol
- Phase 5 (Days 81-100): Complete system — feed handler, execution engine
- Phase 6 (Days 101-120): Interview preparation — mock interviews, system design

Target: HFT SWE roles at Jane Street, Optiver, HRT, Flow Traders via MS CS (Harvard SM CSE / ETH MSc CSE).
