cmake_minimum_required(VERSION 3.14)
project(ra2_rewrite CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# GoogleTest - 强制使用动态运行时库以匹配MSVC默认设置
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
# third_party/gtest 由根 CMakeLists add_subdirectory 引入时跳过 FetchContent，
# 否则 gtest/gmock target 双份定义冲突（14257ae 引入的构建破坏）
if(NOT TARGET gtest)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.12.1
  )
  FetchContent_MakeAvailable(googletest)
endif()

enable_testing()

option(RA2_BUILD_TESTS "Build tests" ON)

# 算法重写模块
add_library(ra2_algorithms
  production_system.cpp
  threat_system.cpp)
target_include_directories(ra2_algorithms PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(RA2_BUILD_TESTS)
  add_executable(test_production test/test_production.cpp)
  target_link_libraries(test_production PRIVATE ra2_algorithms gtest_main)
  add_test(NAME production COMMAND test_production)

  add_executable(test_threat test/test_threat.cpp)
  target_link_libraries(test_threat PRIVATE ra2_algorithms gtest_main)
  add_test(NAME threat COMMAND test_threat)
endif()

add_subdirectory(mix)
add_subdirectory(rmg)
add_subdirectory(assets)
