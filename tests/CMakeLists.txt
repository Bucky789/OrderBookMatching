add_executable(obm_tests
    test_memory_pool.cpp
    test_matching.cpp
    test_order_types.cpp
    test_property.cpp
    test_ring_buffer.cpp
    test_integration.cpp
)
target_link_libraries(obm_tests PRIVATE obm_core GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(obm_tests)
