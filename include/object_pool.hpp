#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <new>

/**
 * ObjectPool<T, N>
 *
 * Fixed-size object pool for HFT hot path.
 * Pre-allocates N objects at construction time.
 * O(1) acquire and release — no malloc/free ever called in hot path.
 * Warmup in constructor triggers all OS page faults at startup.
 *
 * Usage:
 *   ObjectPool<Order, 4096> pool;  // at startup
 *   Order* o = pool.acquire();     // O(1), no malloc
 *   pool.release(o);               // O(1), no free
 */
template<typename T, size_t N>
class ObjectPool {
public:
    ObjectPool() {
        // Initialize freelist — all slots available
        for (int i = 0; i < static_cast<int>(N); i++)
            freelist_[i] = i;

        // Warmup — touch every cache line to trigger OS page faults NOW
        // so the hot path never sees a page fault during trading
        for (size_t i = 0; i < sizeof(storage_); i += 64)
            storage_[i] = 0;
    }

    // Non-copyable, non-movable — pool owns its storage
    ObjectPool(const ObjectPool&)            = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&)                 = delete;
    ObjectPool& operator=(ObjectPool&&)      = delete;

    /**
     * Acquire an object from the pool.
     * Calls T's default constructor via placement new.
     * Returns nullptr if pool is exhausted.
     * O(1) — one array index operation.
     */
    T* acquire() {
        if (free_count_ == 0) return nullptr;
        int slot = freelist_[--free_count_];
        void* mem = storage_ + slot * sizeof(T);
        return new(mem) T();
    }

    /**
     * Acquire an object and construct with arguments.
     * O(1) — one array index operation.
     */
    template<typename... Args>
    T* acquire(Args&&... args) {
        if (free_count_ == 0) return nullptr;
        int slot = freelist_[--free_count_];
        void* mem = storage_ + slot * sizeof(T);
        return new(mem) T(static_cast<Args&&>(args)...);
    }

    /**
     * Release an object back to the pool.
     * Calls T's destructor explicitly.
     * O(1) — one array index operation.
     * MUST be called with a pointer returned by acquire().
     */
    void release(T* obj) {
        assert(obj != nullptr);
        assert(owns(obj) && "pointer not from this pool");

        int slot = static_cast<int>(
            (reinterpret_cast<char*>(obj) - storage_) / sizeof(T)
        );

        obj->~T();                         // explicit destructor
        freelist_[free_count_++] = slot;   // return slot to freelist
    }

    // Query
    size_t capacity()  const { return N; }
    int    available() const { return free_count_; }
    int    in_use()    const { return static_cast<int>(N) - free_count_; }
    bool   empty()     const { return free_count_ == 0; }
    bool   full()      const { return free_count_ == static_cast<int>(N); }

    /**
     * Check if pointer belongs to this pool.
     */
    bool owns(const T* obj) const {
        const char* p = reinterpret_cast<const char*>(obj);
        return p >= storage_ && p < storage_ + N * sizeof(T);
    }

private:
    // Raw storage — no constructors called until acquire()
    alignas(T) char storage_[N * sizeof(T)];

    // Freelist — stack of available slot indices
    int freelist_[N];
    int free_count_ = static_cast<int>(N);
};
