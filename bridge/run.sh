#!/bin/sh
# Run the GammaRay MCP bridge.
# Sets up LD_LIBRARY_PATH for qtmcp shared libs (in build tree) and QT_PLUGIN_PATH
# (qtmcp stdio backend plugin) so the bridge can find everything at runtime.
# GammaRay libraries come from the system package (gammaray-dev).
# SPDX-License-Identifier: GPL-2.0-or-later
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
BLD="$HERE/build"
export LD_LIBRARY_PATH="$BLD/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}"
export QT_PLUGIN_PATH="$BLD/lib/x86_64-linux-gnu/qt6/plugins:${QT_PLUGIN_PATH:-}"
export QT_QPA_PLATFORM=offscreen
exec "$BLD/gammaray-mcp-bridge" "$@"
