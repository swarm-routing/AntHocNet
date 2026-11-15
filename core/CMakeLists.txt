cmake_minimum_required(VERSION 3.20)
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
# level). ANTHOCNET_CORE_SWARM_CORE_FROM_PACKAGE takes swarm-core from an
# installed package (find_package(swarm-core CONFIG), e.g. its vcpkg port)
# instead of swarm-core.lock; ANTHOCNET_CORE_INSTALL (which needs it) adds the
# install rules and the CMake package anthocnet-core: find_package(anthocnet-core
# CONFIG REQUIRED) gives the imported target anthocnet::core (#1037,
# swarm-routing/.github#436, the vcpkg port).
option(ANTHOCNET_CORE_TESTS "Build the tests and the speed benchmark" ${_anthocnet_core_top})
option(ANTHOCNET_CORE_SWARM_CORE_FROM_PACKAGE "Take swarm-core from find_package(swarm-core CONFIG)" OFF)
option(ANTHOCNET_CORE_INSTALL "Add the install rules and the CMake package anthocnet-core" OFF)
if(ANTHOCNET_CORE_INSTALL AND NOT ANTHOCNET_CORE_SWARM_CORE_FROM_PACKAGE)
  message(FATAL_ERROR "ANTHOCNET_CORE_INSTALL needs ANTHOCNET_CORE_SWARM_CORE_FROM_PACKAGE=ON: the package links an installed swarm-core")
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
# primitives every protocol builds on (org golden rule 4), as a subproject: it
# defines the library target swarm::core and nothing else (no tests, no ctest
# entry, no -Werror on our sources; its README "Use core/ from another CMake
# project"). The release ../swarm-core.lock names (swarm-routing/ns3-swarm-core#134,
# swarm-routing/.github CONTRIBUTING "Dependencies"): its tarball by URL,
# checked against the lock's SHA256 (from the release's SHA256SUMS); CMake
# strips the tarball's top directory. SWARM_CORE_DIR names a local swarm-core
# core/ directory instead (offline, or an unreleased swarm-core change).
# After the coverage options, so swarm-core's sources are built the same way.
set(SWARM_CORE_DIR "" CACHE PATH "swarm-core's core/ directory; empty: fetch the release swarm-core.lock names")
if(ANTHOCNET_CORE_SWARM_CORE_FROM_PACKAGE)
  find_package(swarm-core CONFIG REQUIRED)
elseif(SWARM_CORE_DIR)
  if(NOT EXISTS "${SWARM_CORE_DIR}/swarm-ports.h")
    message(FATAL_ERROR "SWARM_CORE_DIR=${SWARM_CORE_DIR} is not swarm-core's core/ directory")
  endif()
  add_subdirectory(${SWARM_CORE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/swarm-core)
else()
  file(STRINGS ${CMAKE_CURRENT_LIST_DIR}/../swarm-core.lock _swarm_core_lock REGEX "^SWARM_CORE_[A-Z0-9_]+=")
  # a lock bump re-runs the configure step
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/../swarm-core.lock)
  foreach(_swarm_core_kv IN LISTS _swarm_core_lock)
    string(REGEX MATCH "^([A-Z0-9_]+)=(.*)$" _swarm_core_m "${_swarm_core_kv}")
    set(_LOCK_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
  endforeach()
  foreach(_swarm_core_key REPO TAG ASSET SHA256)
    if(NOT _LOCK_SWARM_CORE_${_swarm_core_key})
      message(FATAL_ERROR "swarm-core.lock: no SWARM_CORE_${_swarm_core_key}")
    endif()
  endforeach()
  # Extracted files get the extraction time (CMake 3.24+), so a lock bump
  # rebuilds every swarm-core object.
  set(_swarm_core_timestamp)
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    set(_swarm_core_timestamp DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
  endif()
  include(FetchContent)
  FetchContent_Declare(swarm_core_src
    URL https://github.com/${_LOCK_SWARM_CORE_REPO}/releases/download/${_LOCK_SWARM_CORE_TAG}/${_LOCK_SWARM_CORE_ASSET}
    URL_HASH SHA256=${_LOCK_SWARM_CORE_SHA256}
    ${_swarm_core_timestamp}
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
