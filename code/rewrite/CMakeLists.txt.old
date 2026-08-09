# 算法重写模块: 生产系统 + 威胁评估系统 + 随机地图生成器
add_library(ra2_algorithms
  production_system.cpp
  threat_system.cpp)

target_include_directories(ra2_algorithms PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(RA2_BUILD_TESTS)
  # Google Test 用例
  add_executable(test_production test/test_production.cpp)
  target_link_libraries(test_production PRIVATE ra2_algorithms gtest_main)
  add_test(NAME production COMMAND test_production)

  add_executable(test_threat test/test_threat.cpp)
  target_link_libraries(test_threat PRIVATE ra2_algorithms gtest_main)
  add_test(NAME threat COMMAND test_threat)
endif()

# Westwood 资源容器的只读解析（.mix / .tmp）
add_subdirectory(mix)

# 随机地图生成器(独立子模块)
add_subdirectory(rmg)

# 游戏数据装载层（依赖上面两个）
add_subdirectory(assets)
