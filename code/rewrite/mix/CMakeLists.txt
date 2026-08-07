# Westwood 资源容器的只读解析：.mix 归档 + .tmp 等距砖
#
# 独立成库，不与 rmg 算法核心混在一起：算法核心必须是纯计算、可在任何平台跑，
# 而这一层要碰文件与游戏数据。
add_library(ra2_mix
  blowfish.cpp
  bignum.cpp
  mix_archive.cpp
  tmp_file.cpp)

target_include_directories(ra2_mix PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(RA2_BUILD_TESTS)
  add_executable(test_mix test/test_mix.cpp)
  target_link_libraries(test_mix PRIVATE ra2_mix gtest_main)
  add_test(NAME mix COMMAND test_mix)
endif()
