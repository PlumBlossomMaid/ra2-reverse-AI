@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d E:\code\ra2-reverse-AI\code\modifier
cl /nologo /utf-8 /EHsc test_load.cpp /link /OUT:build\test_load.exe || exit /b 1
build\test_load.exe
