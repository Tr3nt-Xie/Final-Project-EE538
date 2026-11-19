cmake_minimum_required(VERSION 3.20)

project(AegisProxy
  VERSION 0.1.0
  DESCRIPTION "C++20 event-driven HTTP reverse proxy and load balancer"
  LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo)
endif()

option(AEGIS_BUILD_TESTS "Build unit and integration tests" ON)
option(AEGIS_BUILD_BENCHMARKS "Build micro-benchmarks" OFF)
option(AEGIS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(AEGIS_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(AEGIS_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(AEGIS_FETCH_GTEST "Download GoogleTest with FetchContent if not installed" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)

# epoll is Linux-only. Portable modules (common, datastructures, http, balancer,
# health, and the network core minus EpollPoller) still build elsewhere so that
# Members 2 and 3 can develop on macOS; the proxy executable needs Linux.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(AEGIS_HAVE_EPOLL ON)
else()
  set(AEGIS_HAVE_EPOLL OFF)
  message(STATUS "AegisProxy: non-Linux host — EpollPoller and the integration tests are skipped; "
                 "use scripts/docker-dev.sh for the full build")
endif()

# ---------------------------------------------------------------------------
# Interface target carrying shared compile options (warnings, sanitizers, includes)
# ---------------------------------------------------------------------------
add_library(aegis_options INTERFACE)
target_include_directories(aegis_options INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(aegis_options INTERFACE cxx_std_20)
aegis_apply_warnings(aegis_options)
aegis_apply_sanitizers(aegis_options)
if(AEGIS_HAVE_EPOLL)
  target_compile_definitions(aegis_options INTERFACE AEGIS_HAVE_EPOLL=1)
endif()

# ---------------------------------------------------------------------------
# Module libraries (one per workstream; see docs/ownership.md)
# ---------------------------------------------------------------------------
add_subdirectory(src/common)          # aegis_common         (shared)
add_subdirectory(src/datastructures)  # aegis_datastructures (Member 3, header-only)
add_subdirectory(src/network)         # aegis_network        (Member 1)
add_subdirectory(src/http)            # aegis_http           (Member 2)
add_subdirectory(src/balancer)        # aegis_balancer       (Member 3)
add_subdirectory(src/health)          # aegis_health         (Member 3)
add_subdirectory(src/proxy)           # aegis_proxy          (Member 2)

# ---------------------------------------------------------------------------
# Main executable
# ---------------------------------------------------------------------------
add_executable(aegisproxy src/main.cpp)
target_compile_definitions(aegisproxy PRIVATE AEGIS_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(aegisproxy PRIVATE
  aegis_options aegis_common aegis_network aegis_http aegis_proxy aegis_balancer aegis_health)

# ---------------------------------------------------------------------------
# Examples (Member 2's blocking baseline; the Python backend needs no build)
# ---------------------------------------------------------------------------
add_subdirectory(examples/blocking_proxy)

# ---------------------------------------------------------------------------
# Tests / benchmarks
# ---------------------------------------------------------------------------
if(AEGIS_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
endif()

if(AEGIS_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
