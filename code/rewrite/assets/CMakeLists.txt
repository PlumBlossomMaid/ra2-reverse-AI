# 游戏数据装载层：把 .mix / .tmp / theater ini 变成算法要吃的数值
#
# 依赖 ra2_mix（容器解析）与 ra2_rmg（TheaterData 与 kTerrainToGround）。
# 算法核心 ra2_rmg 反过来**不**依赖本层——它必须能在没有游戏数据的机器上
# 编译与自测。
add_library(ra2_assets
  theater_assets.cpp)

target_include_directories(ra2_assets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ra2_assets PUBLIC ra2_mix ra2_rmg)

if(RA2_BUILD_TESTS)
  add_executable(test_theater_assets test/test_theater_assets.cpp)
  target_link_libraries(test_theater_assets PRIVATE ra2_assets gtest_main)
  target_compile_definitions(test_theater_assets PRIVATE
    RA2_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/memory/data/rmg")
  add_test(NAME theater_assets COMMAND test_theater_assets)
endif()
