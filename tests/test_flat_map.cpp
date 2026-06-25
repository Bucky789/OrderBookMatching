#include "obm/FlatMap.hpp"
#include <gtest/gtest.h>
#include <functional>

using namespace obm;

// ── Ascending (std::less) ─────────────────────────────────────────────────────

TEST(FlatMap, InsertMaintainsSortedAscending) {
    FlatMap<int, int> m;
    m[3] = 30;
    m[1] = 10;
    m[5] = 50;
    m[2] = 20;

    ASSERT_EQ(m.size(), 4u);
    auto it = m.begin();
    EXPECT_EQ(it->first, 1); ++it;
    EXPECT_EQ(it->first, 2); ++it;
    EXPECT_EQ(it->first, 3); ++it;
    EXPECT_EQ(it->first, 5);
}

TEST(FlatMap, FindExisting) {
    FlatMap<int, int> m;
    m[10] = 100;
    m[20] = 200;
    auto it = m.find(10);
    ASSERT_NE(it, m.end());
    EXPECT_EQ(it->second, 100);
}

TEST(FlatMap, FindMissing) {
    FlatMap<int, int> m;
    m[10] = 100;
    EXPECT_EQ(m.find(99), m.end());
}

TEST(FlatMap, OperatorBracketInsertDefault) {
    FlatMap<int, int> m;
    m[5];               // inserts default-constructed (0)
    EXPECT_EQ(m.size(), 1u);
    EXPECT_EQ(m.find(5)->second, 0);
}

TEST(FlatMap, OperatorBracketUpdate) {
    FlatMap<int, int> m;
    m[7] = 1;
    m[7] = 99;          // update existing
    EXPECT_EQ(m.size(), 1u);
    EXPECT_EQ(m.find(7)->second, 99);
}

TEST(FlatMap, EraseByIterator) {
    FlatMap<int, int> m;
    m[1] = 1; m[2] = 2; m[3] = 3;
    auto it = m.find(2);
    ASSERT_NE(it, m.end());
    m.erase(it);
    EXPECT_EQ(m.size(), 2u);
    EXPECT_EQ(m.find(2), m.end());
    // Remaining sorted
    EXPECT_EQ(m.begin()->first, 1);
    EXPECT_EQ(std::next(m.begin())->first, 3);
}

TEST(FlatMap, EmptyAndClear) {
    FlatMap<int, int> m;
    EXPECT_TRUE(m.empty());
    m[1] = 1;
    EXPECT_FALSE(m.empty());
    m.clear();
    EXPECT_TRUE(m.empty());
    EXPECT_EQ(m.size(), 0u);
}

TEST(FlatMap, ReverseIterationAscending) {
    FlatMap<int, int> m;
    m[1] = 1; m[2] = 2; m[3] = 3;
    auto it = m.rbegin();
    EXPECT_EQ(it->first, 3); ++it;
    EXPECT_EQ(it->first, 2); ++it;
    EXPECT_EQ(it->first, 1);
}

// ── Descending (std::greater) — mirrors BidMap ─────────────────────────────

TEST(FlatMap, DescendingOrder) {
    FlatMap<int, int, std::greater<int>> m;
    m[3] = 30; m[1] = 10; m[5] = 50; m[2] = 20;

    // begin() should give highest key first
    auto it = m.begin();
    EXPECT_EQ(it->first, 5); ++it;
    EXPECT_EQ(it->first, 3); ++it;
    EXPECT_EQ(it->first, 2); ++it;
    EXPECT_EQ(it->first, 1);
}

TEST(FlatMap, DescendingFind) {
    FlatMap<int, int, std::greater<int>> m;
    m[10] = 100; m[20] = 200; m[30] = 300;
    auto it = m.find(20);
    ASSERT_NE(it, m.end());
    EXPECT_EQ(it->second, 200);
    EXPECT_EQ(m.find(99), m.end());
}

TEST(FlatMap, DescendingErase) {
    FlatMap<int, int, std::greater<int>> m;
    m[10] = 1; m[20] = 2; m[30] = 3;
    m.erase(m.find(20));
    ASSERT_EQ(m.size(), 2u);
    EXPECT_EQ(m.begin()->first, 30);
    EXPECT_EQ(std::next(m.begin())->first, 10);
}

// ── Move semantics ────────────────────────────────────────────────────────────

TEST(FlatMap, MoveConstruct) {
    FlatMap<int, int> a;
    a[1] = 10; a[2] = 20;
    FlatMap<int, int> b(std::move(a));
    EXPECT_EQ(b.size(), 2u);
    EXPECT_TRUE(a.empty());
}

TEST(FlatMap, MoveAssign) {
    FlatMap<int, int> a;
    a[1] = 10;
    FlatMap<int, int> b;
    b[99] = 99;
    b = std::move(a);
    EXPECT_EQ(b.size(), 1u);
    EXPECT_EQ(b.find(1)->second, 10);
}
