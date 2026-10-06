# RA2 RMG 修改器：rmg_hook.dll（进程内钩子）+ rmgctl（前端）
# 必须 32 位配置：cmake -B build_cmake -A Win32
cmake_minimum_required(VERSION 3.20)
project(rmg_modifier CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "rmg_hook 必须以 32 位构建（-A Win32），目标进程 gamemd.exe 是 x86")
endif()

add_library(rmg_hook SHARED rmg_hook/dllmain.cpp)
add_executable(rmgctl rmgctl/rmgctl.cpp)
add_executable(test_load test_load.cpp)

foreach(t rmg_hook rmgctl test_load)
  target_compile_options(${t} PRIVATE /utf-8 /W3 /O2)
  target_link_libraries(${t} PRIVATE user32)
  # 产物统一进 code/modifier/build\（rmgctl.cpp 里 DLL_PATH 写死这个目录）
  set_target_properties(${t} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build")
endforeach()
