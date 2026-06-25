add_executable(obm_tests
    test_memory_pool.cpp
    test_matching.cpp
    test_order_types.cpp
    test_property.cpp
    test_ring_buffer.cpp
    test_integration.cpp
    test_flat_map.cpp
    test_auction_cross.cpp
    test_risk_engine.cpp
    test_modify_order.cpp
    test_partitioning.cpp
)
target_link_libraries(obm_tests PRIVATE obm_core GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(obm_tests)
