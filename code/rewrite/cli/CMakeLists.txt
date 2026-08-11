# CLI 模块：完整 world + 流水线 + .map 组装（ra2_cli 库）与命令行入口。
add_library(ra2_cli
  cliworld.cpp
  mapwriter.cpp
  pipeline.cpp
  rules_loader.cpp)

target_include_directories(ra2_cli PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR})
target_link_libraries(ra2_cli PUBLIC ra2_rmg ra2_map)

add_executable(ra2_rmg_cli cli_main.cpp)
target_link_libraries(ra2_rmg_cli PRIVATE ra2_cli ra2_assets ra2_mix)
# __try/__except 需要 /EHa（/EHsc 不支持 SEH）
target_compile_options(ra2_rmg_cli PRIVATE /EHa)
# 调试定位：关 ASLR + 出 map 文件（崩溃地址 - 0x140000000 = RVA 查函数）
set_target_properties(ra2_rmg_cli PROPERTIES
  LINK_FLAGS "/DYNAMICBASE:NO /MAP:${CMAKE_BINARY_DIR}/cli/ra2_rmg_cli.map")

if(RA2_BUILD_TESTS)
  # CLI 冒烟测试：合成 theater/砖表跑全流水线，不依赖游戏数据
  add_executable(test_cli test/test_cli.cpp)
  target_link_libraries(test_cli PRIVATE ra2_cli gtest_main)
  add_test(NAME cli COMMAND test_cli)
endif()
