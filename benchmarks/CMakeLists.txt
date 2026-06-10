add_executable(obm_bench
    bench_matching.cpp
    bench_insertion.cpp
    bench_end_to_end.cpp
)
target_link_libraries(obm_bench PRIVATE obm_core benchmark::benchmark benchmark::benchmark_main)
