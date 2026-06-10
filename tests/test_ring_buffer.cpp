#include "obm/RingBuffer.hpp"
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace obm;

TEST(RingBuffer, PushPopSingleThread) {
    SPSCRingBuffer<int, 8> rb;
    EXPECT_TRUE(rb.empty());

    EXPECT_TRUE(rb.try_push(42));
    EXPECT_FALSE(rb.empty());

    int val = 0;
    EXPECT_TRUE(rb.try_pop(val));
    EXPECT_EQ(val, 42);
    EXPECT_TRUE(rb.empty());
}

TEST(RingBuffer, FullBuffer) {
    SPSCRingBuffer<int, 4> rb; // capacity 4, but usable = 3 (one slot for sentinel)
    EXPECT_TRUE(rb.try_push(1));
    EXPECT_TRUE(rb.try_push(2));
    EXPECT_TRUE(rb.try_push(3));
    EXPECT_FALSE(rb.try_push(4)); // full

    int v = 0;
    EXPECT_TRUE(rb.try_pop(v)); EXPECT_EQ(v, 1);
    EXPECT_TRUE(rb.try_push(4)); // now space available
}

TEST(RingBuffer, OrderPreserved) {
    SPSCRingBuffer<int, 16> rb;
    for (int i = 0; i < 10; ++i) EXPECT_TRUE(rb.try_push(i));
    for (int i = 0; i < 10; ++i) {
        int v = -1;
        EXPECT_TRUE(rb.try_pop(v));
        EXPECT_EQ(v, i);
    }
}

TEST(RingBuffer, ProducerConsumerThreads) {
    static constexpr int N = 100'000;
    SPSCRingBuffer<int, 131072> rb; // 128K slots

    std::vector<int> received;
    received.reserve(N);

    std::thread producer([&] {
        for (int i = 0; i < N; ++i) {
            while (!rb.try_push(i)) {} // spin
        }
    });

    std::thread consumer([&] {
        int val;
        int count = 0;
        while (count < N) {
            if (rb.try_pop(val)) {
                received.push_back(val);
                ++count;
            }
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(static_cast<int>(received.size()), N);
    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(received[i], i) << "Mismatch at index " << i;
    }
}

TEST(RingBuffer, OrderEventFits) {
    // OrderEvent must be trivially copyable (required by SPSC template)
    static_assert(std::is_trivially_copyable_v<OrderEvent>);
    SPSCRingBuffer<OrderEvent, 1024> rb;
    OrderEvent ev{};
    ev.order_id = 12345;
    EXPECT_TRUE(rb.try_push(ev));
    OrderEvent out{};
    EXPECT_TRUE(rb.try_pop(out));
    EXPECT_EQ(out.order_id, 12345u);
}
