# Provides the GTest::gtest and GTest::gtest_main targets.
#
# Resolution order:
#   1. An installed GoogleTest (find_package). On Debian/Ubuntu: apt install libgtest-dev
#      (>= 1.11 ships CMake config files). On macOS: brew install googletest.
#   2. FetchContent download (needs network) when AEGIS_FETCH_GTEST=ON.
find_package(GTest QUIET)

if(NOT GTest_FOUND)
  if(NOT AEGIS_FETCH_GTEST)
    message(FATAL_ERROR
      "GoogleTest not found. Install it (apt install libgtest-dev / brew install googletest) "
      "or configure with -DAEGIS_FETCH_GTEST=ON (default) to download it.")
  endif()
  message(STATUS "AegisProxy: GoogleTest not installed — fetching v1.15.2")
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
  # Do not apply our strict warnings to third-party code.
  if(TARGET gtest)
    target_compile_options(gtest PRIVATE -w)
  endif()
  if(TARGET gtest_main)
    target_compile_options(gtest_main PRIVATE -w)
  endif()
endif()

# When the installed GoogleTest is a shared library (e.g. conda or Homebrew on macOS),
# make sure test binaries in the build tree can locate it without a global rpath.
if(GTest_FOUND AND TARGET GTest::gtest)
  get_target_property(_aegis_gtest_type GTest::gtest TYPE)
  if(_aegis_gtest_type STREQUAL "SHARED_LIBRARY")
    get_target_property(_aegis_gtest_loc GTest::gtest LOCATION)
    if(_aegis_gtest_loc)
      get_filename_component(_aegis_gtest_dir "${_aegis_gtest_loc}" DIRECTORY)
      list(APPEND CMAKE_BUILD_RPATH "${_aegis_gtest_dir}")
      message(STATUS "AegisProxy: shared GoogleTest at ${_aegis_gtest_dir} added to build rpath")
    endif()
  endif()
endif()
