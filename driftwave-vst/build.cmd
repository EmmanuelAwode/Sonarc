@echo off
rem Configure, build and test Driftwave FX with the Visual Studio 2022 toolchain.
rem Usage:  build.cmd [extra cmake configure arguments]
rem   e.g.  build.cmd -DDRIFTWAVE_JUCE_DIR=C:\dev\JUCE
setlocal

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Visual Studio 2022 or its Build Tools were not found. See README.md.
    exit /b 1
)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo The "Desktop development with C++" workload is not installed. See README.md.
    exit /b 1
)
rem vcvars looks for vswhere on PATH and prints a harmless error without it.
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

if not defined DRIFTWAVE_BUILD_DIR set "DRIFTWAVE_BUILD_DIR=%~dp0build"

cmake -S "%~dp0." -B "%DRIFTWAVE_BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release %* || exit /b 1
cmake --build "%DRIFTWAVE_BUILD_DIR%" || exit /b 1
ctest --test-dir "%DRIFTWAVE_BUILD_DIR%" --output-on-failure || exit /b 1

echo.
if exist "%DRIFTWAVE_BUILD_DIR%\DriftwaveFX_artefacts\Release\VST3\Driftwave FX.vst3" echo Built: %DRIFTWAVE_BUILD_DIR%\DriftwaveFX_artefacts\Release\VST3\Driftwave FX.vst3
