#!/bin/bash
# Build Diagon following the SOP
# This is a helper script that mirrors the /build skill

set -e  # Exit on error

# Parse arguments
TARGET="${1:-core}"
CLEAN="${2:-true}"
JOBS="${3:-8}"

PROJECT_ROOT="/home/ubuntu/diagon"
BUILD_DIR="$PROJECT_ROOT/build"

echo "=================================="
echo "Diagon Build Script (SOP-compliant)"
echo "=================================="
echo "Target: $TARGET"
echo "Clean: $CLEAN"
echo "Jobs: $JOBS"
echo "=================================="
echo ""

# Step 1: Clean if requested
if [ "$CLEAN" = "true" ]; then
    echo "[1/5] Cleaning build directory..."
    cd "$PROJECT_ROOT"
    rm -rf build
    mkdir build
    cd build
else
    echo "[1/5] Using existing build directory..."
    cd "$BUILD_DIR" || { mkdir -p "$BUILD_DIR" && cd "$BUILD_DIR"; }
fi

# Step 2: Configure CMake
echo "[2/5] Configuring CMake (Release, no LTO)..."
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS="-O3 -march=native" \
      -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
      -DDIAGON_BUILD_BENCHMARKS=ON \
      -DDIAGON_BUILD_TESTS=ON \
      ..

# Step 3: Build core library
echo "[3/5] Building diagon_core..."
make diagon_core -j"$JOBS"

# Step 4: Verify ICU linking
echo "[4/5] Verifying ICU linking..."
if ldd src/core/libdiagon_core.so | grep -q icu; then
    echo "✅ ICU linked successfully:"
    ldd src/core/libdiagon_core.so | grep icu
else
    echo "❌ ERROR: ICU not linked!"
    echo "See BUILD_SOP.md for troubleshooting."
    exit 1
fi

# Step 5: Build target
echo "[5/5] Building target: $TARGET..."
case $TARGET in
    core)
        echo "✅ Core library built (no additional targets)"
        ;;
    benchmarks)
        make benchmarks -j"$JOBS"
        echo "✅ Benchmarks built"
        ;;
    tests)
        make -j"$JOBS"
        echo "✅ Tests built"
        ;;
    all)
        make -j"$JOBS"
        echo "✅ All targets built"
        ;;
    *)
        echo "❌ Unknown target: $TARGET"
        echo "Valid targets: core, benchmarks, tests, all"
        exit 1
        ;;
esac

echo ""
echo "=================================="
echo "✅ Build completed successfully!"
echo "=================================="
echo ""
echo "Next steps:"
case $TARGET in
    core)
        echo "  • Build benchmarks: ./scripts/build.sh benchmarks"
        echo "  • Or use: /build target=benchmarks"
        ;;
    benchmarks)
        echo "  • Run benchmarks:"
        echo "    cd $BUILD_DIR"
        echo "    ./benchmarks/SearchBenchmark"
        ;;
    tests)
        echo "  • Run tests:"
        echo "    cd $BUILD_DIR"
        echo "    ctest"
        ;;
    all)
        echo "  • Run benchmarks: cd $BUILD_DIR && ./benchmarks/SearchBenchmark"
        echo "  • Run tests: cd $BUILD_DIR && ctest"
        ;;
esac
echo ""
