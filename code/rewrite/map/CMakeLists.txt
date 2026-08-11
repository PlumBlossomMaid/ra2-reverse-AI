# 地图编码模块：LZO/LCW 压缩 + base64 + 块打包（.map 输出端）
add_library(ra2_map
  lzo.cpp
  lcw.cpp
  base64.cpp
  isomap.cpp
  encode80.cpp)

target_include_directories(ra2_map PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(ra2_map PUBLIC cxx_std_17)

if(RA2_BUILD_TESTS)
  add_executable(test_mapenc test/test_mapenc.cpp)
  target_link_libraries(test_mapenc PRIVATE ra2_map gtest_main)
  target_compile_definitions(test_mapenc PRIVATE
    RA2_GOLDEN_DIR="${RA2_ROOT_DIR}/memory/data/rmg")
  add_test(NAME mapenc COMMAND test_mapenc)
endif()
