@echo off
rem Builds RaceMenuTogether.dll from a Visual Studio x64 developer prompt. No other dependencies.
setlocal
cd /d "%~dp0"
if not exist build mkdir build
rc /nologo /fo build\RaceMenuTogether.res RaceMenuTogether.rc || exit /b 1
cl /nologo /std:c++17 /O2 /MT /EHsc /W4 /LD RaceMenuTogether.cpp build\RaceMenuTogether.res /Fobuild\ /Fdbuild\ /Fe:build\RaceMenuTogether.dll /link shell32.lib ole32.lib kernel32.lib || exit /b 1
echo Built build\RaceMenuTogether.dll
