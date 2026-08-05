# 算法重写模块: 生产系统 + 威胁评估系统
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
