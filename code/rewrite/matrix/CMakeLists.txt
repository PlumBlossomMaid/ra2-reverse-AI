cmake_minimum_required(VERSION 3.14)

add_library(ra2_matrix
  matrix3d.cpp)
target_include_directories(ra2_matrix PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(ra2_matrix PUBLIC cxx_std_17)

# Matrix3D 的旋转依赖原版查表 sin/cos（ra2::rmg::Sin/Cos），
# golden 逐位对拍要求 FP 行为完全由代码决定。
if(MSVC)
  target_compile_options(ra2_matrix PUBLIC /fp:strict)
else()
  target_compile_options(ra2_matrix PUBLIC
    -ffp-contract=off
    -frounding-math
    -fno-fast-math)
endif()

# 函数实现用 ra2::rmg 的 Sin/Cos/ChopFloat/FpuChopScope
target_link_libraries(ra2_matrix PUBLIC ra2_rmg)

if(RA2_BUILD_TESTS)
  add_executable(test_matrix3d test/test_matrix3d.cpp)
  target_link_libraries(test_matrix3d PRIVATE ra2_matrix gtest_main)
  target_include_directories(test_matrix3d PRIVATE "${RA2_ROOT_DIR}/memory/data/rmg")
  add_test(NAME matrix3d COMMAND test_matrix3d)
endif()