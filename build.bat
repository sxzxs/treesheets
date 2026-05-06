@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
cd /d "%ROOT%" || exit /b 1

set "BUILD_DIR=%ROOT%\_build"
set "CONFIG=Release"
set "GENERATOR="
set "ARCH=x64"
set "TARGET="
set "JOBS="
set "VERBOSE="
set "LOBSTER_ARG="
set "EXTRA_CMAKE_ARGS="

goto :parse_args

:usage
echo Usage:
echo   build.bat [options] [-- extra-cmake-configure-args]
echo.
echo Options:
echo   --release             Build Release configuration. Default.
echo   --debug               Build Debug configuration.
echo   --config NAME         Build configuration name. Example: Release, Debug.
echo   --build-dir DIR       CMake build directory. Default: _build.
echo   --generator NAME      CMake generator. Default: Ninja if found, otherwise Visual Studio 17 2022.
echo   --arch ARCH           MSVC target architecture. Default: x64.
echo   --target NAME         Build a specific CMake target.
echo   --package             Build the package target.
echo   --jobs N              Parallel build job count. Default: CMake decides.
echo   --lobster             Enable Lobster scripting.
echo   --no-lobster          Disable Lobster scripting.
echo   --verbose             Verbose build output.
echo   -h, --help            Show this help.
echo.
echo Examples:
echo   build.bat
echo   build.bat --debug
echo   build.bat --package
echo   build.bat --generator "Visual Studio 17 2022" --arch x64
echo   build.bat -- --DTREESHEETS_VERSION=1.2.3
exit /b 0

:parse_args
if "%~1"=="" goto :after_parse
if /I "%~1"=="-h" goto :usage
if /I "%~1"=="--help" goto :usage
if /I "%~1"=="--release" (
    set "CONFIG=Release"
    shift
    goto :parse_args
)
if /I "%~1"=="--debug" (
    set "CONFIG=Debug"
    shift
    goto :parse_args
)
if /I "%~1"=="--config" (
    if "%~2"=="" goto :missing_value
    set "CONFIG=%~2"
    shift
    shift
    goto :parse_args
)
if /I "%~1"=="--build-dir" (
    if "%~2"=="" goto :missing_value
    set "BUILD_DIR=%~2"
    shift
    shift
    goto :parse_args
)
if /I "%~1"=="--generator" (
    if "%~2"=="" goto :missing_value
    set "GENERATOR=%~2"
    shift
    shift
    goto :parse_args
)
if /I "%~1"=="--arch" (
    if "%~2"=="" goto :missing_value
    set "ARCH=%~2"
    shift
    shift
    goto :parse_args
)
if /I "%~1"=="--target" (
    if "%~2"=="" goto :missing_value
    set "TARGET=%~2"
    shift
    shift
    goto :parse_args
)
if /I "%~1"=="--package" (
    set "TARGET=package"
    shift
    goto :parse_args
)
if /I "%~1"=="--jobs" (
    if "%~2"=="" goto :missing_value
    set "JOBS=%~2"
    shift
    shift
    goto :parse_args
)
if /I "%~1"=="--lobster" (
    set "LOBSTER_ARG=-DENABLE_LOBSTER=ON"
    shift
    goto :parse_args
)
if /I "%~1"=="--no-lobster" (
    set "LOBSTER_ARG=-DENABLE_LOBSTER=OFF"
    shift
    goto :parse_args
)
if /I "%~1"=="--verbose" (
    set "VERBOSE=--verbose"
    shift
    goto :parse_args
)
if "%~1"=="--" (
    shift
    goto :extra_args
)
echo Unknown option: %~1
echo Run "build.bat --help" for usage.
exit /b 2

:extra_args
if "%~1"=="" goto :after_parse
set "EXTRA_CMAKE_ARGS=!EXTRA_CMAKE_ARGS! %1"
shift
goto :extra_args

:missing_value
echo Missing value for option.
exit /b 2

:after_parse
for %%I in ("%BUILD_DIR%") do set "BUILD_DIR=%%~fI"

where cmake.exe >nul 2>nul
if errorlevel 1 (
    echo Error: cmake.exe was not found in PATH.
    exit /b 1
)

if not defined GENERATOR (
    where ninja.exe >nul 2>nul
    if errorlevel 1 (
        set "GENERATOR=Visual Studio 17 2022"
    ) else (
        set "GENERATOR=Ninja"
    )
)

call :ensure_msvc
if errorlevel 1 exit /b 1

if /I "%GENERATOR%"=="Ninja" (
    where ninja.exe >nul 2>nul
    if errorlevel 1 (
        echo Error: Ninja generator was selected, but ninja.exe was not found in PATH.
        exit /b 1
    )
)

set "PLATFORM_ARG="
echo %GENERATOR% | findstr /I /C:"Visual Studio" >nul
if not errorlevel 1 set "PLATFORM_ARG=-A %ARCH%"

set "CONFIG_ARG=-DCMAKE_BUILD_TYPE=%CONFIG%"
set "BUILD_CONFIG_ARG="
if defined PLATFORM_ARG set "BUILD_CONFIG_ARG=--config %CONFIG%"

set "TARGET_ARG="
if defined TARGET set "TARGET_ARG=--target %TARGET%"

set "PARALLEL_ARG=--parallel"
if defined JOBS set "PARALLEL_ARG=--parallel %JOBS%"

echo.
echo Root:      %ROOT%
echo Build dir: %BUILD_DIR%
echo Config:    %CONFIG%
echo Generator: %GENERATOR%
echo Arch:      %ARCH%
if defined TARGET echo Target:    %TARGET%
echo.

cmake -S "%ROOT%" -B "%BUILD_DIR%" -G "%GENERATOR%" %PLATFORM_ARG% %CONFIG_ARG% %LOBSTER_ARG% %EXTRA_CMAKE_ARGS%
if errorlevel 1 exit /b 1

cmake --build "%BUILD_DIR%" %BUILD_CONFIG_ARG% %TARGET_ARG% %PARALLEL_ARG% %VERBOSE%
if errorlevel 1 exit /b 1

echo.
if defined TARGET (
    echo Build target "%TARGET%" completed successfully.
) else (
    echo Build completed successfully.
)
echo Executable: %BUILD_DIR%\TreeSheets.exe
exit /b 0

:ensure_msvc
where cl.exe >nul 2>nul
if not errorlevel 1 exit /b 0

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Error: cl.exe was not found, and vswhere.exe was not found.
    echo Install Visual Studio 2022 with "Desktop development with C++", or run this from a Developer Command Prompt.
    exit /b 1
)

set "VSINSTALL="
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VSINSTALL=%%I"
)

if not defined VSINSTALL (
    echo Error: Visual Studio C++ tools were not found.
    echo Install Visual Studio 2022 with "Desktop development with C++".
    exit /b 1
)

if not exist "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" (
    echo Error: VsDevCmd.bat was not found under "%VSINSTALL%".
    exit /b 1
)

call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=%ARCH% -host_arch=x64
exit /b %errorlevel%
