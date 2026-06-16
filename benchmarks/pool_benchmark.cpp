/**
 * pool_benchmark.cpp
 *
 * Benchmarks: new/delete vs ObjectPool vs ArenaAllocator
 * for 1M allocations, showing latency distribution.
 *
 * Compile:
 *   g++ -std=c++20 -O2 -I../include pool_benchmark.cpp -o pool_benchmark
 *
 * Run:
 *   ./pool_benchmark
 */

#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cstdint>

#include "object_pool.hpp"
#include "arena_allocator.hpp"

// The Order struct used throughout
struct Order {
    double   price     = 0.0;
    int64_t  timestamp = 0;
    int32_t  quantity  = 0;
    int32_t  order_id  = 0;
    char     symbol[4] = {};
    bool     is_buy    = false;
    bool     is_active = false;
};

static_assert(sizeof(Order) == 32, "Order must be 32 bytes");

// Utility: compute and print latency stats from sorted vector
void print_stats(const char* name, std::vector<uint64_t>& v) {
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    std::cout << name << ":\n";
    std::cout << "  min: " << v[0]               << " ns\n";
    std::cout << "  p50: " << v[n * 50  / 100]   << " ns\n";
    std::cout << "  p99: " << v[n * 99  / 100]   << " ns\n";
    std::cout << "  max: " << v[n - 1]            << " ns\n";
}

int main() {
    const int N = 1'000'000;

    std::cout << "Benchmark: 1M allocations each\n";
    std::cout << "==============================\n\n";

    // =========================================================
    // Benchmark 1: new/delete (no warmup)
    // =========================================================
    {
        std::vector<uint64_t> latencies;
        latencies.reserve(N);

        for (int i = 0; i < N; i++) {
            auto t1 = std::chrono::high_resolution_clock::now();
            Order* o = new Order();
            auto t2 = std::chrono::high_resolution_clock::now();
            delete o;
            latencies.push_back(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count()
            );
        }

        print_stats("new/delete (no warmup)", latencies);
    }

    // =========================================================
    // Benchmark 2: ObjectPool (warmed up in constructor)
    // =========================================================
    {
        ObjectPool<Order, 4096> pool;  // warmup happens in constructor

        std::vector<uint64_t> latencies;
        latencies.reserve(N);

        for (int i = 0; i < N; i++) {
            auto t1 = std::chrono::high_resolution_clock::now();
            Order* o = pool.acquire();
            auto t2 = std::chrono::high_resolution_clock::now();
            pool.release(o);
            latencies.push_back(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count()
            );
        }

        print_stats("ObjectPool (warmed up)", latencies);
    }

    // =========================================================
    // Benchmark 3: ArenaAllocator (warmed up in constructor)
    // =========================================================
    {
        ArenaAllocator arena(4096 * sizeof(Order));  // warmup in constructor

        std::vector<uint64_t> latencies;
        latencies.reserve(N);

        for (int i = 0; i < N; i++) {
            auto t1 = std::chrono::high_resolution_clock::now();
            Order* o = arena.allocate<Order>();
            auto t2 = std::chrono::high_resolution_clock::now();
            arena.reset();
            latencies.push_back(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count()
            );
        }

        print_stats("ArenaAllocator (warmed up)", latencies);
    }

    return 0;
}
