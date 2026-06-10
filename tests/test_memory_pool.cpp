#include "obm/MemoryPool.hpp"
#include "obm/Order.hpp"
#include <gtest/gtest.h>
#include <unordered_set>

using namespace obm;

TEST(MemoryPool, AllocateDeallocate) {
    MemoryPool<Order> pool(1);
    EXPECT_EQ(pool.allocated(), 0u);
    EXPECT_GT(pool.capacity(), 0u);

    Order* a = pool.allocate();
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(pool.allocated(), 1u);

    Order* b = pool.allocate();
    ASSERT_NE(b, nullptr);
    EXPECT_NE(a, b);
    EXPECT_EQ(pool.allocated(), 2u);

    pool.deallocate(a);
    EXPECT_EQ(pool.allocated(), 1u);

    Order* c = pool.allocate(); // should reuse a's slot
    EXPECT_EQ(pool.allocated(), 2u);
    pool.deallocate(b);
    pool.deallocate(c);
    EXPECT_EQ(pool.allocated(), 0u);
}

TEST(MemoryPool, UniquePointers) {
    MemoryPool<Order> pool(1);
    constexpr int N = 500;
    std::unordered_set<Order*> ptrs;
    std::vector<Order*> held;

    for (int i = 0; i < N; ++i) {
        Order* p = pool.allocate();
        ASSERT_NE(p, nullptr);
        EXPECT_TRUE(ptrs.insert(p).second) << "Duplicate pointer returned";
        held.push_back(p);
    }
    EXPECT_EQ(pool.allocated(), static_cast<std::size_t>(N));
    for (Order* p : held) pool.deallocate(p);
    EXPECT_EQ(pool.allocated(), 0u);
}

TEST(MemoryPool, GrowsBeyondOneSlab) {
    MemoryPool<Order, 8> pool(1); // tiny slab
    std::vector<Order*> held;
    for (int i = 0; i < 20; ++i) {
        held.push_back(pool.allocate());
    }
    EXPECT_EQ(pool.allocated(), 20u);
    EXPECT_GE(pool.capacity(), 20u);
    for (Order* p : held) pool.deallocate(p);
}
