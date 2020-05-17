@echo off
set __SOURCE_PATH=%~dp0
set __SOURCE_PATH=%__SOURCE_PATH:~0,-1%

for /f %%i in ('where vcpkg.exe') do (
  set __SCRIPT_PATH=%%~dpiscripts
  goto :script_found
)

echo Could not find executable: vcpkg.exe
pause
goto :cleanup

:script_found
cmake -G "Visual Studio 16 2019" -A x64 -B "%__SOURCE_PATH%\build" ^
  -DCMAKE_TOOLCHAIN_FILE:PATH="%__SCRIPT_PATH%\buildsystems\vcpkg.cmake" ^
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE:PATH="%__SCRIPT_PATH%\toolchains\windows.cmake" ^
  -DVCPKG_TARGET_TRIPLET="%VCPKG_DEFAULT_TRIPLET%" -DCMAKE_CONFIGURATION_TYPES="Debug;Release" ^
  -DCMAKE_INSTALL_PREFIX:PATH="%__SOURCE_PATH%" "%__SOURCE_PATH%"
if %errorlevel% == 0 (
  rem powershell "Start-Process devenv '%__SOURCE_PATH%\build\deus.sln' -verb runas"
  cmake --open "%__SOURCE_PATH%\build"
) else (
  pause
)

:cleanup
set __SCRIPT_PATH=
set __SOURCE_PATH=
