# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
#
# A scratch consumer of the installed package anthocnet-core (ci-core.yml's
# package check): find_package finds it and its swarm-core dependency, and
# anthocnet::core links.
cmake_minimum_required(VERSION 3.20)
project(anthocnet_core_package_consumer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(anthocnet-core CONFIG REQUIRED)
message(STATUS "anthocnet-core ${anthocnet-core_VERSION} from ${anthocnet-core_DIR}")
add_executable(consumer consumer.cpp)
target_link_libraries(consumer PRIVATE anthocnet::core)
