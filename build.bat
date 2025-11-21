@echo off
REM Quick build script for Windows (Visual Studio)

echo ========================================
echo Opus All-in-One with QEXT Support
echo Building for Windows x64...
echo ========================================

REM Clean previous build
if exist build rmdir /s /q build

REM Configure
echo.
echo [1/2] Configuring CMake...
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if %errorlevel% neq 0 (
    echo ERROR: CMake configuration failed!
    pause
    exit /b 1
)

REM Build
echo.
echo [2/2] Building Release...
cmake --build build --config Release
if %errorlevel% neq 0 (
    echo ERROR: Build failed!
    pause
    exit /b 1
)

echo.
echo ========================================
echo Build completed successfully!
echo ========================================
echo.
echo Executables are in: build\Release\
echo   - opusenc.exe
echo   - opusdec.exe
echo.
echo Quick test:
echo   build\Release\opusenc.exe --help
echo.
pause
