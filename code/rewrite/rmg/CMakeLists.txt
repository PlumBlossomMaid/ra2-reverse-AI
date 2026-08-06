# 随机地图生成器(RMG)算法重写模块
add_library(ra2_rmg
  randomizer.cpp
  scaled_random.cpp
  fast_math.cpp)

target_include_directories(ra2_rmg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(RA2_BUILD_TESTS)
  add_executable(test_randomizer test/test_randomizer.cpp)
  target_link_libraries(test_randomizer PRIVATE ra2_rmg gtest_main)
  add_test(NAME randomizer COMMAND test_randomizer)

  add_executable(test_scaled_random test/test_scaled_random.cpp)
  target_link_libraries(test_scaled_random PRIVATE ra2_rmg gtest_main)
  add_test(NAME scaled_random COMMAND test_scaled_random)

  add_executable(test_fast_math test/test_fast_math.cpp)
  target_link_libraries(test_fast_math PRIVATE ra2_rmg gtest_main)
  add_test(NAME fast_math COMMAND test_fast_math)
endif()
