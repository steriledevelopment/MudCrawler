@echo off
pushd "%~dp0"
echo Packing Mud Crawler for the Microsoft Store...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-ChildItem -Path . -Recurse | Unblock-File; & '.\pack.ps1'"
popd
cd /d "%USERPROFILE%"
echo.
echo You can close this window; it is no longer using the extracted folder.
pause
