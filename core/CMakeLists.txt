cmake_minimum_required(VERSION 3.20)
# Keep VERSION in sync with the top-level VERSION file (the release workflow
# checks the git tag against it).
project(anthocnet_core VERSION 2.7.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

# Optional gcov instrumentation for coverage reporting (#162). Off by default so
# the normal build/test is bit-for-bit unchanged; CI's `core-coverage` job turns
# it on and post-processes the .gcda/.gcno with gcovr. Applies to the library and
# to everything under tests/ (both need instrumenting to link):
#   cmake -S core -B core/build-coverage -DANTHOCNET_COVERAGE=ON
option(ANTHOCNET_COVERAGE "Build with gcov instrumentation (gcc/clang)" OFF)
if(ANTHOCNET_COVERAGE)
  # -O0 keeps line/branch attribution honest (inlining smears it); the options
  # are appended after CMAKE_CXX_FLAGS_<CONFIG>, so this wins over Release's -O3.
  add_compile_options(--coverage -O0 -g)
  # add_link_options() needs CMake 3.13 and this project declares 3.20;
  # link_libraries() is directory-scoped, propagates into tests/, and CMake
  # passes a leading-dash entry through to the link line verbatim.
  link_libraries(--coverage)
endif()

# swarm-core's dual-licensed core/ (swarm-routing/ns3-swarm-core), the shared
# primitives every protocol builds on (org golden rule 4), as a subproject: it
# defines the library target swarm::core and nothing else (no tests, no ctest
# entry, no -Werror on our sources; its README "Use core/ from another CMake
# project"). swarm-routing/ns3-swarm-core#14 slice S1: linked, nothing used
# yet. SWARM_CORE_DIR names a local swarm-core core/ directory (an offline
# build); empty fetches SWARM_CORE_REF, a full commit SHA bumped by PR together
# with the Makefile's and every workflow's core-deps (`swarm-core / pin`).
# After the coverage options, so swarm-core's sources are built the same way.
set(SWARM_CORE_DIR "" CACHE PATH "swarm-core's core/ directory; empty: fetch SWARM_CORE_REF")
set(SWARM_CORE_REF "2987cfbc202596c657e58f7be6a6bd09112fe2a8" CACHE STRING
    "swarm-routing/ns3-swarm-core commit to fetch when SWARM_CORE_DIR is empty")
if(SWARM_CORE_DIR)
  if(NOT EXISTS "${SWARM_CORE_DIR}/swarm-ports.h")
    message(FATAL_ERROR "SWARM_CORE_DIR=${SWARM_CORE_DIR} is not swarm-core's core/ directory")
  endif()
  add_subdirectory(${SWARM_CORE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/swarm-core)
else()
  include(FetchContent)
  FetchContent_Declare(swarm_core_src
    GIT_REPOSITORY https://github.com/swarm-routing/ns3-swarm-core.git
    GIT_TAG ${SWARM_CORE_REF}
    SOURCE_SUBDIR core)
  FetchContent_MakeAvailable(swarm_core_src)
endif()

add_library(anthocnet_core STATIC
  src/pheromone_table.cpp
  src/pheromone_engine.cpp
  src/ant_history.cpp
  src/ant_message_codec.cpp
  src/ant_router_logic.cpp
  src/link_metric_registry.cpp
  # #296 item 1: the oracle control arm's Dijkstra layer. Not protocol code —
  # see the header for why it lives in core/ (simulator-agnostic + AGENTS.md
  # rule 7 wants a core test on it).
  src/shortest_path.cpp
)
target_include_directories(anthocnet_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(anthocnet_core PUBLIC swarm::core)

# The core has no simulator dependency, so it builds and tests standalone.
enable_testing()
add_subdirectory(tests)

# Core speed benchmarks (#620): ns/op of the hot paths with no simulator.
# Built with the library so it cannot rot; not a test. `make bench-core` runs it.
add_executable(bench_core bench/bench_core.cpp)
target_link_libraries(bench_core PRIVATE anthocnet_core)
