#!/usr/bin/env bash
# Run widget-specific tests with a QWidget test app as the probe target.
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Usage:
#   ./run_widget_tests.sh              # full widget test suite
#   ./run_widget_tests.sh -v           # verbose
#   ./run_widget_tests.sh -k "attr"    # run only attribute-related tests

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# ---------------------------------------------------------------------------
# Configurable paths
# ---------------------------------------------------------------------------
: "${GAMMARAY_BIN:=$PROJECT_ROOT/install-prefix/bin/gammaray}"
: "${GAMMARAY_LIB_DIR:=$PROJECT_ROOT/install-prefix/lib}"
: "${WIDGET_TEST_APP_BIN:=$SCRIPT_DIR/testapp/widget_test_app}"
: "${WIDGET_TEST_APP_SRC:=$SCRIPT_DIR/testapp/widget_test_app.cpp}"
: "${BRIDGE_EXE:=$PROJECT_ROOT/bridge/build/gammaray-mcp-bridge}"
: "${BRIDGE_RUN_SCRIPT:=$PROJECT_ROOT/bridge/run.sh}"

# ---------------------------------------------------------------------------
# Ensure the widget test app is compiled
# ---------------------------------------------------------------------------
_compile_widget_app() {
    echo "Compiling widget test app..."
    local cflags
    cflags=$(pkg-config --cflags Qt6Core Qt6Gui Qt6Widgets 2>/dev/null) || {
        echo "Error: pkg-config for Qt6 not found. Install qt6-base-dev or set WIDGET_TEST_APP_BIN to a pre-built binary."
        exit 1
    }
    local libs
    libs=$(pkg-config --libs Qt6Core Qt6Gui Qt6Widgets 2>/dev/null) || {
        echo "Error: pkg-config for Qt6 libs not found."
        exit 1
    }
    g++ -std=c++17 -fPIC "$WIDGET_TEST_APP_SRC" -o "$WIDGET_TEST_APP_BIN" \
        $cflags $libs
    echo "Widget test app compiled: $WIDGET_TEST_APP_BIN"
}

if [ ! -f "$WIDGET_TEST_APP_BIN" ]; then
    _compile_widget_app
fi
export GAMMARAY_BIN
export GAMMARAY_LIB_DIR
export WIDGET_TEST_APP_BIN
export WIDGET_TEST_APP_SRC
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

echo "=== Widget Test Run ==="
echo "Widget app: $WIDGET_TEST_APP_BIN"
echo "Bridge: $BRIDGE_EXE"

exec uv run --frozen --directory "$PROJECT_ROOT" python "$SCRIPT_DIR/run_with_uv.py" --widget-app "$@"