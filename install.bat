@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
cd /d "%ROOT%" || exit /b 1

set "BUILD_DIR=%ROOT%\_build"
set "INSTALL_DIR=%ROOT%\_install"
set "CONFIG=Release"
set "BUILD_FIRST=auto"
set "GENERATOR_VALUE="
set "ARCH_VALUE="
set "JOBS_VALUE="
set "EXTRA_BUILD_ARGS="
set "COMPONENT_ARG="

goto :parse_args

:usage
echo Usage:
echo   install.bat [options]
echo.
echo Options:
echo   --release             Install Release configuration. Default.
echo   --debug               Install Debug configuration.
echo   --config NAME         Configuration name. Example: Release, Debug.
echo   --build               Build before installing.
echo   --no-build            Do not build first. Fail if the build directory is missing.
echo   --build-dir DIR       CMake build directory. Default: _build.
echo   --prefix DIR          Install destination. Default: _install.
echo   --install-dir DIR     Same as --prefix.
echo   --generator NAME      Generator to pass to build.bat when a build is needed.
echo   --arch ARCH           Architecture to pass to build.bat when a build is needed.
echo   --jobs N              Parallel build job count when a build is needed.
echo   --component NAME      Install a specific CMake component.
echo   -h, --help            Show this help.
echo.
echo Examples:
echo   install.bat
echo   install.bat --build
echo   install.bat --prefix dist\TreeSheets
echo   install.bat --debug --build
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
if /I "%~1"=="--build" (
    set "BUILD_FIRST=yes"
    shift
    goto :parse_args
)
if /I "%~1"=="--no-build" (
    set "BUILD_FIRST=no"
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
if /I "%~1"=="--prefix" (
    if "%~2"=="" goto :missing_value
    set "INSTALL_DIR=%~2"
    shift
    shift
    goto :parse_args
)
if /I "%~1"=="--install-dir" (
    if "%~2"=="" goto :missing_value
    set "INSTALL_DIR=%~2"
    shift
    shift
    goto :parse_args
)
if /I "%~1"=="--generator" (
    if "%~2"=="" goto :missing_value
    set "GENERATOR_VALUE=%~2"
    shift
    shift
    goto :parse_args
)
if /I "%~1"=="--arch" (
    if "%~2"=="" goto :missing_value
    set "ARCH_VALUE=%~2"
    shift
    shift
    goto :parse_args
)
if /I "%~1"=="--jobs" (
    if "%~2"=="" goto :missing_value
    set "JOBS_VALUE=%~2"
    shift
    shift
    goto :parse_args
)
if /I "%~1"=="--component" (
    if "%~2"=="" goto :missing_value
    set "COMPONENT_ARG=--component %~2"
    shift
    shift
    goto :parse_args
)
echo Unknown option: %~1
echo Run "install.bat --help" for usage.
exit /b 2

:missing_value
echo Missing value for option.
exit /b 2

:after_parse
for %%I in ("%BUILD_DIR%") do set "BUILD_DIR=%%~fI"
for %%I in ("%INSTALL_DIR%") do set "INSTALL_DIR=%%~fI"

where cmake.exe >nul 2>nul
if errorlevel 1 (
    echo Error: cmake.exe was not found in PATH.
    exit /b 1
)

if /I "%BUILD_FIRST%"=="yes" goto :run_build
if /I "%BUILD_FIRST%"=="auto" (
    if not exist "%BUILD_DIR%\CMakeCache.txt" goto :run_build
)
goto :do_install

:run_build
if not exist "%ROOT%\build.bat" (
    echo Error: "%ROOT%\build.bat" was not found.
    exit /b 1
)

set BUILD_COMMAND="%ROOT%\build.bat" --build-dir "%BUILD_DIR%" --config "%CONFIG%"
if defined GENERATOR_VALUE set BUILD_COMMAND=%BUILD_COMMAND% --generator "%GENERATOR_VALUE%"
if defined ARCH_VALUE set BUILD_COMMAND=%BUILD_COMMAND% --arch "%ARCH_VALUE%"
if defined JOBS_VALUE set BUILD_COMMAND=%BUILD_COMMAND% --jobs "%JOBS_VALUE%"
if defined EXTRA_BUILD_ARGS set BUILD_COMMAND=%BUILD_COMMAND% %EXTRA_BUILD_ARGS%

call %BUILD_COMMAND%
if errorlevel 1 exit /b 1

:do_install
if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo Error: build directory "%BUILD_DIR%" is not configured.
    echo Run "build.bat" first, or run "install.bat --build".
    exit /b 1
)

echo.
echo Build dir:   %BUILD_DIR%
echo Install dir: %INSTALL_DIR%
echo Config:      %CONFIG%
echo.

cmake --install "%BUILD_DIR%" --prefix "%INSTALL_DIR%" --config "%CONFIG%" %COMPONENT_ARG%
if errorlevel 1 exit /b 1

echo.
echo Install completed successfully.
echo Executable: %INSTALL_DIR%\TreeSheets.exe
exit /b 0
