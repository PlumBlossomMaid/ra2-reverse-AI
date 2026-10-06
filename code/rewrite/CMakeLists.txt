cmake_minimum_required(VERSION 3.14)
project(ra2_rewrite CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 仓库根（golden 基准、rulesmd.ini 等原始数据的所在地）：
# 顶层 CMakeLists 在 code/rewrite，仓库根是其上两级。
set(RA2_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/../..")

# 源文件是 UTF-8（含中文注释）。MSVC 无 /utf-8 时按本机代码页读，
# UTF-8 字节被误解成 GBK 会破坏注释/字符串边界（C4819 起，严重时 #endif 错位）。
if(MSVC)
  add_compile_options(/utf-8)
endif()

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

add_subdirectory(runtime)
add_subdirectory(mix)
add_subdirectory(rmg)
add_subdirectory(assets)
add_subdirectory(map)
add_subdirectory(cli)
