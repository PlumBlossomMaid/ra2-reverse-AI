@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if not exist E:\code\ra2-reverse-AI\code\modifier\build mkdir E:\code\ra2-reverse-AI\code\modifier\build
cd /d E:\code\ra2-reverse-AI\code\modifier
cl /nologo /utf-8 /LD /O2 /W3 /EHsc rmg_hook\dllmain.cpp /link user32.lib /OUT:build\rmg_hook.dll || exit /b 1
cl /nologo /utf-8 /O2 /W3 /EHsc rmgctl\rmgctl.cpp /link user32.lib /OUT:build\rmgctl.exe || exit /b 1
echo BUILD OK
