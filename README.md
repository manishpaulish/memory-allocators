# memory-allocators

O(1) object pool and arena allocator in C++ — zero heap allocation in the hot path.

---

## The Problem with malloc

Standard `new`/`delete` has non-deterministic latency:

```
new/delete (1M calls):
  p50: 41 ns
  p99: 42 ns
  max: 35,500 ns  ← 850x the median
```

Three causes:
1. **Fragmentation** — after many alloc/free cycles, malloc searches a fragmented free list
2. **Lock contention** — default allocator uses a global lock across threads
3. **OS involvement** — when free list is empty, `sbrk()`/`mmap()` system call adds 200-10,000ns

---

## Solution

Pre-allocate everything upfront. Hot path never calls `malloc`/`free`.

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

// O(1), no malloc:
Order* o = pool.acquire(15000.0, 100);
// ...
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

This triggers all OS page faults at startup so the hot path never sees them.

---

## ArenaAllocator

Bump-pointer arena for temporary per-batch allocations.

```cpp
#include "include/arena_allocator.hpp"

ArenaAllocator arena(1024 * 1024);  // 1MB, warmed up in constructor

auto* price = arena.allocate<double>();
auto* qty   = arena.allocate<int32_t>();
auto* sym   = arena.allocate_array<char>(8);

// ... process ...

arena.reset();  // O(1) — ready for next batch, same buffer reused
```

### How it works

```
buffer: [_________________________ 1MB _________________________]
         ↑ offset = 0

allocate<double>():
buffer: [dddddddd________________________________________________]
                 ↑ offset = 8

allocate<int32_t>():
buffer: [ddddddddIIII____________________________________________]
                     ↑ offset = 12

reset():
buffer: [ddddddddIIII____________________________________________]
         ↑ offset = 0  ← one assignment, nothing freed
```

`reset()` is O(1) — sets `offset = 0`. No individual frees. Previous data overwritten on next use.

---

## Benchmark

```bash
g++ -std=c++20 -O2 -I./include benchmarks/pool_benchmark.cpp -o pool_benchmark
./pool_benchmark
```

Results (Apple M-series, 1M allocations):

| Allocator | min | p50 | p99 | max |
|---|---|---|---|---|
| new/delete | 0 ns | 41 ns | 42 ns | 35,500 ns |
| ObjectPool (warmed up) | 0 ns | 41 ns | 42 ns | 5,042 ns |
| ArenaAllocator (warmed up) | 0 ns | 0 ns | 41 ns | 4,200 ns |

Pool max is **7x lower** than new/delete max.

---

## Project Structure

```
memory-allocators/
├── include/
│   ├── object_pool.hpp      — O(1) fixed-size object pool
│   └── arena_allocator.hpp  — O(1) bump-pointer arena
├── benchmarks/
│   └── pool_benchmark.cpp   — latency distribution comparison
└── README.md
```

---

## Key Techniques

**Placement new** — construct at a specific address without allocating:
```cpp
void* mem = raw_slot();
Order* o = new(mem) Order(15000, 100);  // no malloc
o->~Order();                             // explicit destructor — not delete
```

**Aligned storage** — correct alignment for any type T:
```cpp
alignas(T) char storage_[N * sizeof(T)];
```

**Page fault warmup** — touch every cache line at startup so the hot path never stalls:
```cpp
for (size_t i = 0; i < sizeof(storage_); i += 64)
    storage_[i] = 0;
```
