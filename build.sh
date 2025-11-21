#!/bin/bash
# Quick build script for Linux/macOS

set -e

echo "========================================"
echo "Opus All-in-One with QEXT Support"
echo "Building for $(uname -s)..."
echo "========================================"

# Clean previous build
if [ -d "build" ]; then
    rm -rf build
fi

# Configure
echo ""
echo "[1/2] Configuring CMake..."
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build
echo ""
echo "[2/2] Building..."
cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo ""
echo "========================================"
echo "Build completed successfully!"
echo "========================================"
echo ""
echo "Executables are in: build/"
echo "  - opusenc"
echo "  - opusdec"
echo ""
echo "Quick test:"
echo "  ./build/opusenc --help"
echo ""
