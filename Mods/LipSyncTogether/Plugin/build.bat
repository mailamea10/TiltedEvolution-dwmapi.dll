@echo off
rem Builds LipSyncTogether.dll from a Visual Studio x64 developer prompt.
rem The voice chat uses libopus (BSD), which fetch_opus.ps1 downloads from Xiph, checks and builds on first use.
setlocal
cd /d "%~dp0"
if not exist build mkdir build
if not exist build\opus\lib\opus.lib (
    rem PowerShell 7 if installed, else Windows PowerShell.
    where pwsh >nul 2>nul && (pwsh -NoProfile -ExecutionPolicy Bypass -File fetch_opus.ps1 || exit /b 1)
    where pwsh >nul 2>nul || (powershell -NoProfile -ExecutionPolicy Bypass -File fetch_opus.ps1 || exit /b 1)
)
rc /nologo /fo build\LipSyncTogether.res LipSyncTogether.rc || exit /b 1
cl /nologo /std:c++17 /O2 /MT /EHsc /W4 /LD /Ibuild\opus\include\opus LipSyncTogether.cpp Common.cpp Settings.cpp Voice.cpp build\LipSyncTogether.res /Fobuild\ /Fdbuild\ /Fe:build\LipSyncTogether.dll /link build\opus\lib\opus.lib shell32.lib ole32.lib kernel32.lib user32.lib ws2_32.lib || exit /b 1
echo Built build\LipSyncTogether.dll
