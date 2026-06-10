#pragma once

#include "Types.hpp"
#include <array>
#include <cassert>
#include <cstddef>
#include <memory>
#include <new>

namespace obm {

// Slab allocator for fixed-size objects of type T.
// Pre-allocates slabs of BlockSize objects. allocate()/deallocate() are O(1)
// and never call the OS allocator once the pool is warmed up — no heap hits
// on the matching hot path.
//
// Slabs are never individually freed; lifetime == pool lifetime.
template<typename T, std::size_t BlockSize = 4096>
class MemoryPool {
    static_assert(sizeof(T) >= sizeof(void*), "T must be large enough to hold a freelist pointer");

    struct Slab {
        alignas(CACHE_LINE) std::array<T, BlockSize> objects;
        std::unique_ptr<Slab> next;
    };

    T*                    free_head_  = nullptr;
    std::unique_ptr<Slab> slabs_;
    std::size_t           allocated_  = 0;
    std::size_t           capacity_   = 0;

    void add_slab() {
        auto slab = std::make_unique<Slab>();
        // Build freelist through the new slab's objects
        for (std::size_t i = 0; i < BlockSize; ++i) {
            T* obj = &slab->objects[i];
            // Store next-freelist pointer in the first sizeof(T*) bytes of obj
            *reinterpret_cast<T**>(obj) = free_head_;
            free_head_ = obj;
        }
        capacity_ += BlockSize;
        slab->next = std::move(slabs_);
        slabs_ = std::move(slab);
    }

public:
    explicit MemoryPool(std::size_t initial_slabs = 2) {
        for (std::size_t i = 0; i < initial_slabs; ++i) {
            add_slab();
        }
    }

    // Non-copyable, non-movable (held by reference in OrderBook)
    MemoryPool(const MemoryPool&)            = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    MemoryPool(MemoryPool&&)                 = delete;
    MemoryPool& operator=(MemoryPool&&)      = delete;

    [[nodiscard]] T* allocate() {
        if (!free_head_) [[unlikely]] {
            add_slab();
        }
        T* obj = free_head_;
        free_head_ = *reinterpret_cast<T**>(obj);
        ++allocated_;
        return obj;
    }

    void deallocate(T* obj) noexcept {
        assert(obj != nullptr);
        *reinterpret_cast<T**>(obj) = free_head_;
        free_head_ = obj;
        --allocated_;
    }

    [[nodiscard]] std::size_t allocated() const noexcept { return allocated_; }
    [[nodiscard]] std::size_t capacity()  const noexcept { return capacity_;  }
    [[nodiscard]] std::size_t free()      const noexcept { return capacity_ - allocated_; }
};

} // namespace obm
