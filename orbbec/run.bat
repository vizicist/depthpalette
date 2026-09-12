@echo off
setlocal
cd /d "%~dp0"
taskkill /F /IM depthpalette.exe >nul 2>&1
"%~dp0build\bin\depthpalette.exe" --window --color %*
