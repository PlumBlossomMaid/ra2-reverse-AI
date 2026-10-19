cmake_minimum_required(VERSION 3.14)

add_library(ra2_runtime
  lowlevel.cpp
  handles.cpp
  crt_init.cpp
  crt_error.cpp
  crt_format.cpp
  crt_stricmp.cpp
  crt_wcscpy.cpp
  crt_wcslen.cpp
  crt_memmove.cpp
  crt_qsort.cpp
  crt_free.cpp
  crt_boolnorm.cpp
  crt_malloc.cpp
  crt_thunk.cpp
  crt_wcsncpy.cpp
  facing.cpp
  ra2_string.cpp
  map_legal.cpp)
target_include_directories(ra2_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(ra2_runtime PUBLIC cxx_std_17)

if(RA2_BUILD_TESTS)
  add_executable(test_runtime test/test_lowlevel.cpp test/test_handles.cpp test/test_crt_init.cpp test/test_facing.cpp test/test_ra2_string.cpp test/test_map_legal.cpp)
  target_link_libraries(test_runtime PRIVATE ra2_runtime gtest_main)
  add_test(NAME runtime COMMAND test_runtime)

  # CRT 无效参数异常链（0x7DC720→0x7DCD9C→0x7DCDC0），golden 由
  # emu_oracle 指令级模拟取得（deadcode_7dc720_golden.txt，12 用例）
  add_executable(test_crt_error test/test_crt_error.cpp)
  target_link_libraries(test_crt_error PRIVATE ra2_runtime gtest_main)
  target_compile_definitions(test_crt_error PRIVATE
    RA2_GOLDEN_DIR="${RA2_ROOT_DIR}/memory/data/rmg")
  add_test(NAME crt_error COMMAND test_crt_error)

  # vsprintfW 薄封装（0x7CA564），golden 由 emu_oracle 指令级模拟取得
  # （deadcode_7ca564_golden.txt，6 用例；0x7D21BE 引擎与 0x7CE18D 终止
  # 符为本批桩边界，外壳语义真身实证）
  add_executable(test_crt_format test/test_crt_format.cpp)
  target_link_libraries(test_crt_format PRIVATE ra2_runtime gtest_main)
  target_compile_definitions(test_crt_format PRIVATE
    RA2_GOLDEN_DIR="${RA2_ROOT_DIR}/memory/data/rmg")
  add_test(NAME crt_format COMMAND test_crt_format)

  # _stricmp（0x7C8D20），golden 由 emu_oracle 指令级模拟取得
  # （deadcode_7c8d20_golden.txt，15 用例；快路径真身 12 + 慢路径
  # locale tolower 桩 3）
  add_executable(test_crt_stricmp test/test_crt_stricmp.cpp)
  target_link_libraries(test_crt_stricmp PRIVATE ra2_runtime gtest_main)
  target_compile_definitions(test_crt_stricmp PRIVATE
    RA2_GOLDEN_DIR="${RA2_ROOT_DIR}/memory/data/rmg")
  add_test(NAME crt_stricmp COMMAND test_crt_stricmp)

  # vsprintf 薄封装（0x7C8EF4，ANSI 单 NUL 变体），golden 由
  # emu_oracle 指令级模拟取得（deadcode_7c8ef4_golden.txt，6 用例；
  # 0x7CE2A5 引擎与 0x7CE18D 终止符为本批桩边界，外壳语义真身实证）
  add_executable(test_crt_vsprintf test/test_crt_vsprintf.cpp)
  target_link_libraries(test_crt_vsprintf PRIVATE ra2_runtime gtest_main)
  target_compile_definitions(test_crt_vsprintf PRIVATE
    RA2_GOLDEN_DIR="${RA2_ROOT_DIR}/memory/data/rmg")
  add_test(NAME crt_vsprintf COMMAND test_crt_vsprintf)

  # wcscpy（0x7CA489，宽字符字符串拷贝），golden 由 emu_oracle 指令级
  # 模拟取得（deadcode_7ca489_golden.txt，6 用例；纯内存复制零桩）
  add_executable(test_crt_wcscpy test/test_crt_wcscpy.cpp)
  target_link_libraries(test_crt_wcscpy PRIVATE ra2_runtime gtest_main)
  target_compile_definitions(test_crt_wcscpy PRIVATE
    RA2_GOLDEN_DIR="${RA2_ROOT_DIR}/memory/data/rmg")
  add_test(NAME crt_wcscpy COMMAND test_crt_wcscpy)

  # wcslen（0x7CA405，宽字符串长度），golden 由 emu_oracle 指令级
  # 模拟取得（deadcode_7ca405_golden.txt，8 用例；纯内存扫描零桩）
  add_executable(test_crt_wcslen test/test_crt_wcslen.cpp)
  target_link_libraries(test_crt_wcslen PRIVATE ra2_runtime gtest_main)
  target_compile_definitions(test_crt_wcslen PRIVATE
    RA2_GOLDEN_DIR="${RA2_ROOT_DIR}/memory/data/rmg")
  add_test(NAME crt_wcslen COMMAND test_crt_wcslen)

  # memmove（0x7CA090，重叠感知字节拷贝），golden 由 emu_oracle 指令级
  # 模拟取得（deadcode_7ca090_golden.txt，13 用例；纯内存复制零桩）
  add_executable(test_crt_memmove test/test_crt_memmove.cpp)
  target_link_libraries(test_crt_memmove PRIVATE ra2_runtime gtest_main)
  target_compile_definitions(test_crt_memmove PRIVATE
    RA2_GOLDEN_DIR="${RA2_ROOT_DIR}/memory/data/rmg")
  add_test(NAME crt_memmove COMMAND test_crt_memmove)

  # qsort（0x7C8B48，迭代式快排），golden 由 emu_oracle 指令级模拟取得
  # （deadcode_7c8b48_golden.txt，14 用例；比较器桩 hook 记录调用序列）
  add_executable(test_crt_qsort test/test_crt_qsort.cpp)
  target_link_libraries(test_crt_qsort PRIVATE ra2_runtime gtest_main)
  target_compile_definitions(test_crt_qsort PRIVATE
    RA2_GOLDEN_DIR="${RA2_ROOT_DIR}/memory/data/rmg")
  add_test(NAME crt_qsort COMMAND test_crt_qsort)

  # free（0x7C93E8，CRT free 调度入口），golden 由 emu_oracle 指令级
  # 模拟取得（deadcode_7c93e8_golden.txt，5 用例；锁/查找/释放/HeapFree
  # 均为桩注入的外部语义，结构指令真身）
  add_executable(test_crt_free test/test_crt_free.cpp)
  target_link_libraries(test_crt_free PRIVATE ra2_runtime gtest_main)
  target_compile_definitions(test_crt_free PRIVATE
    RA2_GOLDEN_DIR="${RA2_ROOT_DIR}/memory/data/rmg")
  add_test(NAME crt_free COMMAND test_crt_free)

  # bool 规范化（0x7C978A，neg/sbb/neg/dec 包装），golden 由 emu_oracle
  # 指令级模拟取得（deadcode_7c978a_golden.txt，8 用例；0x7C970C 桩注入）
  add_executable(test_crt_boolnorm test/test_crt_boolnorm.cpp)
  target_link_libraries(test_crt_boolnorm PRIVATE ra2_runtime gtest_main)
  target_compile_definitions(test_crt_boolnorm PRIVATE
    RA2_GOLDEN_DIR="${RA2_ROOT_DIR}/memory/data/rmg")
  add_test(NAME crt_boolnorm COMMAND test_crt_boolnorm)

  # malloc 入口（0x7C9430，核心 0x7C9442 桩），golden 由 emu_oracle 指令级
  # 模拟取得（deadcode_7c9430_golden.txt，8 用例；0x7C9442 注入返回值）
  add_executable(test_crt_malloc test/test_crt_malloc.cpp)
  target_link_libraries(test_crt_malloc PRIVATE ra2_runtime gtest_main)
  target_compile_definitions(test_crt_malloc PRIVATE
    RA2_GOLDEN_DIR="${RA2_ROOT_DIR}/memory/data/rmg")
  add_test(NAME crt_malloc COMMAND test_crt_malloc)

  # 数字解析转发 thunk（0x7C9BFD，目标 0x7C9B72 已有 REIMPL），golden 由
  # emu_oracle 指令级模拟取得（deadcode_7c9bfd_golden.txt，8 用例；桩注入）
  add_executable(test_crt_thunk test/test_crt_thunk.cpp)
  target_link_libraries(test_crt_thunk PRIVATE ra2_runtime gtest_main)
  target_compile_definitions(test_crt_thunk PRIVATE
    RA2_GOLDEN_DIR="${RA2_ROOT_DIR}/memory/data/rmg")
  add_test(NAME crt_thunk COMMAND test_crt_thunk)

  # wcsncpy（0x7CA422，宽字符计数复制），golden 由 emu_oracle 指令级
  # 模拟取得（deadcode_7ca422_golden.txt，10 用例；纯内存操作零桩）
  add_executable(test_crt_wcsncpy test/test_crt_wcsncpy.cpp)
  target_link_libraries(test_crt_wcsncpy PRIVATE ra2_runtime gtest_main)
  target_compile_definitions(test_crt_wcsncpy PRIVATE
    RA2_GOLDEN_DIR="${RA2_ROOT_DIR}/memory/data/rmg")
  add_test(NAME crt_wcsncpy COMMAND test_crt_wcsncpy)
endif()
