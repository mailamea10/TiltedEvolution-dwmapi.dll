@echo off
rem Builds LipSyncTogether.dll from a Visual Studio x64 developer prompt. No other dependencies.
setlocal
cd /d "%~dp0"
if not exist build mkdir build
rc /nologo /fo build\LipSyncTogether.res LipSyncTogether.rc || exit /b 1
cl /nologo /std:c++17 /O2 /MT /EHsc /W4 /LD LipSyncTogether.cpp build\LipSyncTogether.res /Fobuild\ /Fdbuild\ /Fe:build\LipSyncTogether.dll /link shell32.lib ole32.lib kernel32.lib user32.lib || exit /b 1
echo Built build\LipSyncTogether.dll
