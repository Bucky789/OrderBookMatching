cmake_minimum_required(VERSION 3.20)
project(OrderBookMatching VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Compiler flags ────────────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(
        /W4 /WX
        /wd4324             # C4324: structure padded — intentional for cache-line alignment
        /O2 /GL
        /arch:AVX2
        /Zc:preprocessor
        /EHsc
        /MP                 # parallel compilation
    )
    add_link_options(/LTCG)
else()
    add_compile_options(
        -Wall -Wextra -Wpedantic -Werror
        -O3 -march=native
    )
endif()

# ── Dependencies ──────────────────────────────────────────────────────────────
include(cmake/FetchDependencies.cmake)

# ── Core library ──────────────────────────────────────────────────────────────
add_library(obm_core STATIC
    src/Order.cpp
    src/PriceLevel.cpp
    src/OrderBook.cpp
    src/MatchingEngine.cpp
    src/RiskEngine.cpp
    src/AuctionCross.cpp
    src/FIXParser.cpp
    src/FIXEncoder.cpp
    src/FIXSession.cpp
    src/MarketDataSimulator.cpp
    src/TickDataReplayer.cpp
)
target_include_directories(obm_core PUBLIC include)
target_link_libraries(obm_core PUBLIC spdlog::spdlog)
if(WIN32)
    target_link_libraries(obm_core PUBLIC Ws2_32)
endif()

# ── Simulation executable ─────────────────────────────────────────────────────
add_executable(obm_sim src/main.cpp)
target_link_libraries(obm_sim PRIVATE obm_core)

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)

# ── Benchmarks ────────────────────────────────────────────────────────────────
add_subdirectory(benchmarks)

# ── Python bindings ───────────────────────────────────────────────────────────
find_package(Python COMPONENTS Interpreter Development QUIET)
if(Python_FOUND)
    add_subdirectory(python)
else()
    message(STATUS "Python not found — skipping pybind11 bindings")
endif()
