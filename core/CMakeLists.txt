cmake_minimum_required(VERSION 3.20)
# swarm-core comes from its vcpkg port (swarm-routing/.github#489): with the
# vcpkg toolchain, vcpkg installs the version ../vcpkg.json pins from
# swarm-routing/vcpkg-registry at ../vcpkg-configuration.json's baseline
# (manifest mode). The manifest is at the repository root, not in core/: tell
# the toolchain (read by project() below) where it is, unless the caller did.
if(NOT DEFINED VCPKG_MANIFEST_DIR AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/../vcpkg.json"
   AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(VCPKG_MANIFEST_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
endif()
# Keep VERSION in sync with the top-level VERSION file (the release workflow
# checks the git tag against it).
project(anthocnet_core VERSION 2.7.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(_anthocnet_core_top ON)
else()
  set(_anthocnet_core_top OFF)
endif()
# ANTHOCNET_CORE_TESTS builds the tests and the speed benchmark (ON at the top
# level). ANTHOCNET_CORE_SWARM_CORE_FROM_PACKAGE takes swarm-core from
# find_package even when SWARM_CORE_DIR is set; ANTHOCNET_CORE_INSTALL (which
# needs the package) adds the install rules and the CMake package
# anthocnet-core: find_package(anthocnet-core CONFIG REQUIRED) gives the
# imported target anthocnet::core (#1037, swarm-routing/.github#436, the vcpkg
# port).
option(ANTHOCNET_CORE_TESTS "Build the tests and the speed benchmark" ${_anthocnet_core_top})
option(ANTHOCNET_CORE_SWARM_CORE_FROM_PACKAGE "Take swarm-core from find_package(swarm-core CONFIG) even when SWARM_CORE_DIR is set" OFF)
option(ANTHOCNET_CORE_INSTALL "Add the install rules and the CMake package anthocnet-core" OFF)
set(SWARM_CORE_DIR "" CACHE PATH "swarm-core's core/ directory; empty: find_package(swarm-core CONFIG), its vcpkg port")
if(SWARM_CORE_DIR AND NOT ANTHOCNET_CORE_SWARM_CORE_FROM_PACKAGE)
  set(_anthocnet_core_swarm_core_from_dir ON)
else()
  set(_anthocnet_core_swarm_core_from_dir OFF)
endif()
if(ANTHOCNET_CORE_INSTALL AND _anthocnet_core_swarm_core_from_dir)
  message(FATAL_ERROR "ANTHOCNET_CORE_INSTALL needs swarm-core from its package, not SWARM_CORE_DIR: the package links an installed swarm-core")
endif()

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
# primitives every protocol builds on (org golden rule 4): its installed CMake
# package (the vcpkg port, or any install on CMAKE_PREFIX_PATH;
# swarm-routing/ns3-swarm-core#137), which defines the imported target
# swarm::core, at the version ../vcpkg.json pins (the release ../swarm-core.lock
# names, the one the ns-3 build uses; swarm-routing/.github CONTRIBUTING
# "Dependencies", vcpkg mode):
#
#   cmake -S core -B core/build -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
#
# SWARM_CORE_DIR names a swarm-core core/ directory instead, added as a
# subproject that defines the same target and nothing else (no tests, no ctest
# entry, no -Werror on our sources; its README "Use core/ from another CMake
# project"): an unreleased swarm-core change, or the lock's release unpacked
# by tools/swarm_core.py path (what `make test` does without VCPKG_ROOT, and the
# coverage, fuzz and analysis jobs). After the coverage options, so its sources
# are built the same way.
if(_anthocnet_core_swarm_core_from_dir)
  if(NOT EXISTS "${SWARM_CORE_DIR}/swarm-ports.h")
    message(FATAL_ERROR "SWARM_CORE_DIR=${SWARM_CORE_DIR} is not swarm-core's core/ directory")
  endif()
  add_subdirectory(${SWARM_CORE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/swarm-core)
else()
  find_package(swarm-core CONFIG REQUIRED)
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
add_library(anthocnet::core ALIAS anthocnet_core)
# The installed package's imported target is anthocnet::core too.
set_target_properties(anthocnet_core PROPERTIES EXPORT_NAME core)
include(GNUInstallDirs)
target_include_directories(anthocnet_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(anthocnet_core PUBLIC swarm::core)

if(ANTHOCNET_CORE_TESTS)
  # The core has no simulator dependency, so it builds and tests standalone.
  enable_testing()
  add_subdirectory(tests)

  # Core speed benchmarks (#620): ns/op of the hot paths with no simulator.
  # Built with the library so it cannot rot; not a test. `make bench-core` runs it.
  add_executable(bench_core bench/bench_core.cpp)
  target_link_libraries(bench_core PRIVATE anthocnet_core)
endif()

if(ANTHOCNET_CORE_INSTALL)
  include(CMakePackageConfigHelpers)
  set(_anthocnet_core_cmake_dir ${CMAKE_INSTALL_LIBDIR}/cmake/anthocnet-core)
  install(TARGETS anthocnet_core EXPORT anthocnet-coreTargets
          ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
  # #include "anthocnet/core/<header>.h", as in the source tree
  install(DIRECTORY include/anthocnet DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/anthocnet-core)
  install(EXPORT anthocnet-coreTargets NAMESPACE anthocnet:: DESTINATION ${_anthocnet_core_cmake_dir})
  configure_package_config_file(anthocnet-coreConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/anthocnet-coreConfig.cmake
    INSTALL_DESTINATION ${_anthocnet_core_cmake_dir})
  # AntHocNet is past 1.0: a release satisfies a request for any earlier
  # version of its major (SemVer, docs/roadmap.md).
  write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/anthocnet-coreConfigVersion.cmake
    COMPATIBILITY SameMajorVersion)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/anthocnet-coreConfig.cmake
                ${CMAKE_CURRENT_BINARY_DIR}/anthocnet-coreConfigVersion.cmake
          DESTINATION ${_anthocnet_core_cmake_dir})
endif()
