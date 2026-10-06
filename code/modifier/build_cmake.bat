@echo off
rem configure + build rmg_hook.dll / rmgctl.exe (32-bit, Ninja)
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d E:\code\ra2-reverse-AI\code\modifier
if not exist build_cmake cmake -G Ninja -B build_cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl || exit /b 1
cmake --build build_cmake || exit /b 1
echo BUILD OK
