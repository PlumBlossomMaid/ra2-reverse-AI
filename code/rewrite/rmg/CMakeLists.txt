# 随机地图生成器(RMG)算法重写模块
add_library(ra2_rmg
  randomizer.cpp
  scaled_random.cpp
  fast_math.cpp
  map_size.cpp
  map_seed_params.cpp
  blob_region.cpp
  fp_guard.cpp)

target_include_directories(ra2_rmg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 逐位复刻要求浮点行为完全由代码决定，不许编译器代劳。见 fp_guard.h 的坑位清单。
if(MSVC)
  # /fp:strict 保证 fesetround 生效且禁止跨语句重排；MSVC 默认不做 FMA 收缩
  target_compile_options(ra2_rmg PUBLIC /fp:strict)
else()
  target_compile_options(ra2_rmg PUBLIC
    -ffp-contract=off      # 禁止把 a*b+c 融成 fma（ARM/LoongArch 上默认是开的）
    -frounding-math        # 承认舍入模式可能不是就近，禁止据此优化
    -fno-fast-math)
  # 32 位 x86 上必须用 SSE2 而非 x87，否则中间结果是 80 位
  if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND CMAKE_SYSTEM_PROCESSOR MATCHES "i.86|x86")
    target_compile_options(ra2_rmg PUBLIC -msse2 -mfpmath=sse)
  endif()
endif()

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

  add_executable(test_map_size test/test_map_size.cpp)
  target_link_libraries(test_map_size PRIVATE ra2_rmg gtest_main)
  add_test(NAME map_size COMMAND test_map_size)

  add_executable(test_map_seed_params test/test_map_seed_params.cpp)
  target_link_libraries(test_map_seed_params PRIVATE ra2_rmg gtest_main)
  add_test(NAME map_seed_params COMMAND test_map_seed_params)

  add_executable(test_map_geometry test/test_map_geometry.cpp)
  target_link_libraries(test_map_geometry PRIVATE ra2_rmg gtest_main)
  add_test(NAME map_geometry COMMAND test_map_geometry)

  add_executable(test_blob_heap test/test_blob_heap.cpp)
  target_link_libraries(test_blob_heap PRIVATE ra2_rmg gtest_main)
  add_test(NAME blob_heap COMMAND test_blob_heap)

  add_executable(test_blob_region test/test_blob_region.cpp)
  target_link_libraries(test_blob_region PRIVATE ra2_rmg gtest_main)
  add_test(NAME blob_region COMMAND test_blob_region)
endif()
