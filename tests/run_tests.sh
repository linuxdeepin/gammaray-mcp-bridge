#!/usr/bin/env bash
# Convenience script to run the gammaray-mcp test suite.
#
# This is the SINGLE configuration point for paths. If you install GammaRay
# from a system package instead of the local install-prefix, change the
# variables below. Everything else (conftest.py, the QML test app, etc.)
# reads from these environment variables.
#
# Usage:
#   ./run_tests.sh               # probe-required tests (default)
#   ./run_tests.sh --no-probe    # only unit tests (no probe needed)
#   ./run_tests.sh -v            # verbose
#   ./run_tests.sh -k "item"     # run only item-related tests

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# ---------------------------------------------------------------------------
# Configurable paths — adjust these if GammaRay is installed elsewhere
# ---------------------------------------------------------------------------
: "${GAMMARAY_BIN:=$PROJECT_ROOT/install-prefix/bin/gammaray}"
: "${GAMMARAY_LIB_DIR:=$PROJECT_ROOT/install-prefix/lib}"
: "${QML_RUNNER:=/usr/lib/qt6/bin/qml}"
: "${TEST_QML:=$SCRIPT_DIR/testapp/main.qml}"
: "${BRIDGE_EXE:=$PROJECT_ROOT/bridge/build/gammaray-mcp-bridge}"
: "${BRIDGE_RUN_SCRIPT:=$PROJECT_ROOT/bridge/run.sh}"

# Export so conftest.py can read them
export GAMMARAY_BIN
export GAMMARAY_LIB_DIR
export QML_RUNNER
export TEST_QML
export BRIDGE_EXE
export BRIDGE_RUN_SCRIPT

# ---------------------------------------------------------------------------
# Ensure the bridge is built
# ---------------------------------------------------------------------------
if [ ! -f "$BRIDGE_EXE" ]; then
    echo "Building bridge first..."
    cmake -S "$PROJECT_ROOT/bridge" -B "$PROJECT_ROOT/bridge/build" -G Ninja \
        -DCMAKE_PREFIX_PATH="$PROJECT_ROOT/install-prefix"
    cmake --build "$PROJECT_ROOT/bridge/build"
fi

# ---------------------------------------------------------------------------
# Runtime environment
# ---------------------------------------------------------------------------
export LD_LIBRARY_PATH="$GAMMARAY_LIB_DIR"
export QT_QPA_PLATFORM=offscreen
export QT_PLUGIN_PATH="$PROJECT_ROOT/bridge/build/lib/x86_64-linux-gnu/qt6/plugins"

cd "$SCRIPT_DIR"

exec uv run --frozen --directory "$PROJECT_ROOT" python "$SCRIPT_DIR/run_with_uv.py" "$@"