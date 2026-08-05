# ra2-reverse-AI 顶层构建配置
# 架构参考: PaddlePaddle/Paddle (third_party 子模块 + 模块化 CMake)
cmake_minimum_required(VERSION 3.18)
project(ra2-reverse LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 源码含中文注释: MSVC 需要 /utf-8 否则按 GBK 解析导致语法错乱
if(MSVC)
  add_compile_options(/utf-8)
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# 测试开关 (关闭后仅编译算法库)
option(RA2_BUILD_TESTS "Build tests" ON)

if(RA2_BUILD_TESTS)
  # third_party 依赖 (git submodule, Paddle 方式)
  add_subdirectory(third_party/gtest)
  enable_testing()
  add_subdirectory(code/rewrite)
endif()
