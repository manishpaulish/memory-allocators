#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cassert>

/**
 * ArenaAllocator
 *
 * Bump-pointer arena allocator for HFT per-message temporaries.
 * Pre-allocates a fixed buffer at construction time.
 * O(1) allocation — just increment a pointer.
 * O(1) bulk reset — set pointer back to zero.
 * No individual deallocation supported.
 *
 * Perfect for: allocate N fields from a market data message,
 * process them, then reset() everything at once.
 *
 * Usage:
 *   ArenaAllocator arena(1024 * 1024);  // 1MB, at startup
 *   auto* price = arena.allocate<double>();
 *   auto* qty   = arena.allocate<int32_t>();
 *   // ... process ...
 *   arena.reset();  // O(1) — ready for next message
 */
class ArenaAllocator {
public:
    explicit ArenaAllocator(size_t bytes)
        : size_(bytes), offset_(0)
    {
        buffer_ = new char[bytes];
        // Warmup — trigger all OS page faults at startup
        std::memset(buffer_, 0, bytes);
    }

    ~ArenaAllocator() { delete[] buffer_; }

    // Non-copyable
    ArenaAllocator(const ArenaAllocator&)            = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    /**
     * Allocate bytes with given alignment.
     * Returns nullptr if arena is full.
     * O(1) — pointer bump + alignment rounding.
     */
    void* allocate(size_t bytes, size_t align = 8) {
        // Round offset up to alignment boundary
        // Formula: (offset + align - 1) & ~(align - 1)
        size_t aligned = (offset_ + align - 1) & ~(align - 1);
        if (aligned + bytes > size_) return nullptr;
        offset_ = aligned + bytes;
        return buffer_ + aligned;
    }

    /**
     * Type-safe allocation — allocate and return typed pointer.
     * O(1).
     */
    template<typename T>
    T* allocate() {
        return static_cast<T*>(allocate(sizeof(T), alignof(T)));
    }

    /**
     * Allocate array of N elements of type T.
     * O(1).
     */
    template<typename T>
    T* allocate_array(size_t count) {
        return static_cast<T*>(allocate(sizeof(T) * count, alignof(T)));
    }

    /**
     * Reset all allocations at once.
     * O(1) — just resets the offset pointer.
     * All previously returned pointers become invalid.
     */
    void reset() { offset_ = 0; }

    // Query
    size_t used()      const { return offset_; }
    size_t remaining() const { return size_ - offset_; }
    size_t capacity()  const { return size_; }

private:
    char*  buffer_;
    size_t size_;
    size_t offset_;
};
