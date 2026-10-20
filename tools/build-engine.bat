@echo off
rem ---------------------------------------------------------------------------
rem  Builds the engine ALONE (static library `rapture` + the test executable `rapture_tests`), without any game.
rem  Used to check that the engine repository builds standalone.
rem
rem    engine\tools\build-engine.bat          configure + build into <root>\build-engine\
rem    engine\tools\build-engine.bat test     ... and run the tests that need no game data
rem    engine\tools\build-engine.bat clean    delete <root>\build-engine
rem
rem  <root> = the folder that contains engine\ (the game repo) or, when this file is in the root of the engine repo
rem  itself, the engine folder.  Compiler / CMake / SDL2 are found (or downloaded, portable) by setup-build-env.ps1.
rem ---------------------------------------------------------------------------
setlocal
for %%I in ("%~dp0..") do set "ENGDIR=%%~fI"
for %%I in ("%ENGDIR%") do set "ENGNAME=%%~nxI"
if /i "%ENGNAME%"=="engine" ( for %%I in ("%ENGDIR%\..") do set "ROOT=%%~fI" ) else ( set "ROOT=%ENGDIR%" )
set "RAPTURE_ROOT=%ROOT%"
set "OUT=%ROOT%\build-engine"
if /i "%~1"=="clean" ( rmdir /s /q "%OUT%" 2>nul & echo Deleted %OUT% & goto :eof )

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup-build-env.ps1"
if errorlevel 1 ( echo The build tools are not ready. & exit /b 1 )
call "%ROOT%\build.env.bat"
set "PATH=%MS_GCC_BIN%;%MS_CMAKE_BIN%;%PATH%"

cmake -S "%ENGDIR%" -B "%OUT%" -G "MinGW Makefiles" -DSDL2_DIR="%MS_SDL2_DIR%" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 ( echo Configuration failed. & exit /b 1 )
cmake --build "%OUT%" -j %NUMBER_OF_PROCESSORS%
if errorlevel 1 ( echo BUILD FAILED. & exit /b 1 )
echo.
echo Build OK: %OUT%\rapture_tests.exe

if /i "%~1"=="test" (
    rem the exe is a GUI-subsystem program: its output goes to the console only when started from one; redirect to see it
    for %%T in ("--engine-test 1" "--engine-test 3" "--engine-test 4" "--engine-test 9" "--game-test arcade" "--game-test host") do (
        "%OUT%\rapture_tests.exe" %%~T > "%OUT%\test.log" 2>&1
        echo %%~T: exit code %errorlevel%
        findstr /C:"checks" /C:"failure" /C:"OK" "%OUT%\test.log"
    )
)
