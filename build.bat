@echo off
rem 本地构建：在 "x64 Native Tools Command Prompt for VS 2022" 中运行
cd /d %~dp0
if not exist build mkdir build
if not exist dist mkdir dist
rc /nologo /c 65001 /fo build\res.res src\res.rc || exit /b 1
cl /nologo /utf-8 /W3 /O1 /GL /Gy /Gw /MT /EHs-c- /GR- /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 ^
  src\main.cpp build\res.res ^
  /Fe:dist\Launcher-x64.exe ^
  /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS ^
  user32.lib gdi32.lib shell32.lib comdlg32.lib
echo 输出: dist\Launcher-x64.exe
