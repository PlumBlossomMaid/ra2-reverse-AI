# 随机地图生成器(RMG)算法重写模块
add_library(ra2_rmg
  randomizer.cpp
  scaled_random.cpp
  fast_math.cpp
  map_size.cpp
  map_seed_params.cpp
  blob_region.cpp
  blob_paint.cpp
  cell_ref.cpp
  shore_mask.cpp
  shore_smooth.cpp
  stage_water.cpp
  water_refine.cpp
  water_blob.cpp
  waterfall.cpp
  rivers.cpp
  make_grid.cpp
  place_blobs.cpp
  place_tile.cpp
  regions.cpp
  making_regions.cpp
  ramps.cpp
  cliff_work.cpp
  recalc.cpp
  zones.cpp
  start_points.cpp
  height_field.cpp
  corner_lattice.cpp
  ore_field.cpp
  tech_buildings.cpp
  lat_spread.cpp
  lats.cpp
  theater.cpp
  fp_guard.cpp)

target_include_directories(ra2_rmg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(ra2_rmg PUBLIC cxx_std_17)

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
  # sincos_golden.inc 是生成的对拍数据，直接 #include，不进版本库以外的构建目录
  target_include_directories(test_fast_math PRIVATE "${CMAKE_SOURCE_DIR}/memory/data/rmg")
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

  add_executable(test_blob_paint test/test_blob_paint.cpp)
  target_link_libraries(test_blob_paint PRIVATE ra2_rmg gtest_main)
  add_test(NAME blob_paint COMMAND test_blob_paint)

  add_executable(test_shore_mask test/test_shore_mask.cpp)
  target_link_libraries(test_shore_mask PRIVATE ra2_rmg gtest_main)
  add_test(NAME shore_mask COMMAND test_shore_mask)

  add_executable(test_make_grid test/test_make_grid.cpp)
  target_link_libraries(test_make_grid PRIVATE ra2_rmg gtest_main)
  add_test(NAME make_grid COMMAND test_make_grid)

  add_executable(test_place_blobs test/test_place_blobs.cpp)
  target_link_libraries(test_place_blobs PRIVATE ra2_rmg gtest_main)
  add_test(NAME place_blobs COMMAND test_place_blobs)

  add_executable(test_theater test/test_theater.cpp)
  target_link_libraries(test_theater PRIVATE ra2_rmg gtest_main)
  add_test(NAME theater COMMAND test_theater)

  add_executable(test_place_tile test/test_place_tile.cpp)
  target_link_libraries(test_place_tile PRIVATE ra2_rmg gtest_main)
  add_test(NAME place_tile COMMAND test_place_tile)

  # 岸线平滑的基准有 17 万字节，编成 .inc 太笨重，直接在运行期读原档
  add_executable(test_shore_smooth test/test_shore_smooth.cpp)
  target_link_libraries(test_shore_smooth PRIVATE ra2_rmg gtest_main)
  target_compile_definitions(test_shore_smooth PRIVATE
    RA2_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/memory/data/rmg")
  add_test(NAME shore_smooth COMMAND test_shore_smooth)

  add_executable(test_stage_water test/test_stage_water.cpp)
  target_link_libraries(test_stage_water PRIVATE ra2_rmg gtest_main)
  target_compile_definitions(test_stage_water PRIVATE
    RA2_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/memory/data/rmg")
  add_test(NAME stage_water COMMAND test_stage_water)

  add_executable(test_regions test/test_regions.cpp)
  target_link_libraries(test_regions PRIVATE ra2_rmg gtest_main)
  target_compile_definitions(test_regions PRIVATE
    RA2_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/memory/data/rmg")
  add_test(NAME regions COMMAND test_regions)

  # 两个通用区域算子（阶段 2 河湖支路与阶段 4 共用的积木）
  add_executable(test_region_ops test/test_region_ops.cpp)
  target_link_libraries(test_region_ops PRIVATE ra2_rmg gtest_main)
  target_compile_definitions(test_region_ops PRIVATE
    RA2_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/memory/data/rmg")
  add_test(NAME region_ops COMMAND test_region_ops)

  # 按方位角优先的堆扩散（河道/湖泊走向的来源）
  add_executable(test_angular_grow test/test_angular_grow.cpp)
  target_link_libraries(test_angular_grow PRIVATE ra2_rmg gtest_main)
  target_compile_definitions(test_angular_grow PRIVATE
    RA2_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/memory/data/rmg")
  add_test(NAME angular_grow COMMAND test_angular_grow)

  # 湖泊涂块（MapType 3/4 支路的第一块积木）
  add_executable(test_water_blob test/test_water_blob.cpp)
  target_link_libraries(test_water_blob PRIVATE ra2_rmg gtest_main)
  target_compile_definitions(test_water_blob PRIVATE
    RA2_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/memory/data/rmg")
  add_test(NAME water_blob COMMAND test_water_blob)

  # 瀑布（河道落差）——基准 3 万多行，运行期读原档；砖几何读 tile_geometry.txt
  add_executable(test_waterfall test/test_waterfall.cpp)
  target_link_libraries(test_waterfall PRIVATE ra2_rmg gtest_main)
  target_compile_definitions(test_waterfall PRIVATE
    RA2_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/memory/data/rmg")
  add_test(NAME waterfall COMMAND test_waterfall)

  add_executable(test_rivers test/test_rivers.cpp)
  target_link_libraries(test_rivers PRIVATE ra2_rmg gtest_main)
  target_compile_definitions(test_rivers PRIVATE
    RA2_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/memory/data/rmg")
  add_test(NAME rivers COMMAND test_rivers)

  add_executable(test_height_field test/test_height_field.cpp)
  target_link_libraries(test_height_field PRIVATE ra2_rmg gtest_main)
  target_compile_definitions(test_height_field PRIVATE
    RA2_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/memory/data/rmg")
  add_test(NAME height_field COMMAND test_height_field)

  add_executable(test_height_anchors test/test_height_anchors.cpp)
  target_link_libraries(test_height_anchors PRIVATE ra2_rmg gtest_main)
  target_compile_definitions(test_height_anchors PRIVATE
    RA2_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/memory/data/rmg")
  add_test(NAME height_anchors COMMAND test_height_anchors)

  # 基准 5000 多行，运行期直接读原档
  add_executable(test_corner_lattice test/test_corner_lattice.cpp)
  target_link_libraries(test_corner_lattice PRIVATE ra2_rmg gtest_main)
  target_compile_definitions(test_corner_lattice PRIVATE
    RA2_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/memory/data/rmg")
  add_test(NAME corner_lattice COMMAND test_corner_lattice)

  add_executable(test_start_points test/test_start_points.cpp)
  target_link_libraries(test_start_points PRIVATE ra2_rmg gtest_main)
  target_compile_definitions(test_start_points PRIVATE
    RA2_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/memory/data/rmg")
  add_test(NAME start_points COMMAND test_start_points)

  # 阶段 6 整段（起区域 + 摆点 + 调度）的端到端对拍，基准 4 万行，运行期读原档
  add_executable(test_place_start_points test/test_place_start_points.cpp)
  target_link_libraries(test_place_start_points PRIVATE ra2_rmg gtest_main)
  target_compile_definitions(test_place_start_points PRIVATE
    RA2_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/memory/data/rmg")
  add_test(NAME place_start_points COMMAND test_place_start_points)

  add_executable(test_zones test/test_zones.cpp)
  target_link_libraries(test_zones PRIVATE ra2_rmg gtest_main)
  target_compile_definitions(test_zones PRIVATE
    RA2_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/memory/data/rmg")
  add_test(NAME zones COMMAND test_zones)

  add_executable(test_green_spread test/test_green_spread.cpp)
  target_link_libraries(test_green_spread PRIVATE ra2_rmg gtest_main)
  target_compile_definitions(test_green_spread PRIVATE
    RA2_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/memory/data/rmg")
  add_test(NAME green_spread COMMAND test_green_spread)

  add_executable(test_ore_field test/test_ore_field.cpp)
  target_link_libraries(test_ore_field PRIVATE ra2_rmg gtest_main)
  target_compile_definitions(test_ore_field PRIVATE
    RA2_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/memory/data/rmg")
  add_test(NAME ore_field COMMAND test_ore_field)

  # 阶段 8 外层整段（逐区域布矿 + 出生点补偿）的端到端对拍
  add_executable(test_place_tiberium test/test_place_tiberium.cpp)
  target_link_libraries(test_place_tiberium PRIVATE ra2_rmg gtest_main)
  target_compile_definitions(test_place_tiberium PRIVATE
    RA2_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/memory/data/rmg")
  add_test(NAME place_tiberium COMMAND test_place_tiberium)

  add_executable(test_tech_buildings test/test_tech_buildings.cpp)
  target_link_libraries(test_tech_buildings PRIVATE ra2_rmg gtest_main)
  target_compile_definitions(test_tech_buildings PRIVATE
    RA2_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/memory/data/rmg")
  add_test(NAME tech_buildings COMMAND test_tech_buildings)

  add_executable(test_lats test/test_lats.cpp)
  target_link_libraries(test_lats PRIVATE ra2_rmg gtest_main)
  target_compile_definitions(test_lats PRIVATE
    RA2_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/memory/data/rmg")
  add_test(NAME lats COMMAND test_lats)

  add_executable(test_making_regions test/test_making_regions.cpp)
  target_link_libraries(test_making_regions PRIVATE ra2_rmg gtest_main)
  target_compile_definitions(test_making_regions PRIVATE
    RA2_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/memory/data/rmg")
  add_test(NAME making_regions COMMAND test_making_regions)

  add_executable(test_ramps test/test_ramps.cpp)
  target_link_libraries(test_ramps PRIVATE ra2_rmg gtest_main)
  target_compile_definitions(test_ramps PRIVATE
    RA2_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/memory/data/rmg")
  add_test(NAME ramps COMMAND test_ramps)
endif()
