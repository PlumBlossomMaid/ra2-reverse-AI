# 随机地图生成器(RMG)算法重写模块
add_library(ra2_rmg
  randomizer.cpp)

target_include_directories(ra2_rmg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(RA2_BUILD_TESTS)
  add_executable(test_randomizer test/test_randomizer.cpp)
  target_link_libraries(test_randomizer PRIVATE ra2_rmg gtest_main)
  add_test(NAME randomizer COMMAND test_randomizer)
endif()
