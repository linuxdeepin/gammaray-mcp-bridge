# GammaRay MCP Bridge

A [MCP](https://modelcontextprotocol.io/) server that bridges [GammaRay](https://github.com/KDAB/GammaRay) probe introspection data into MCP tools, enabling LLMs to inspect and debug Qt Quick / QML scene graphs, items, geometry, materials, and Qt Widgets.

## Architecture

```
Target Qt/QML App  ──TCP──►  GammaRay MCP Bridge  ──stdio──►  LLM / AI Agent
  (GammaRay probe                (GammaRay client +               (opencode,
   injected)                      qtmcp MCP server)                ZCode, etc.)
```

The bridge is a GammaRay **client**: it connects to a probe injected into a target Qt app, reads the same models the GammaRay GUI uses, and translates them into MCP JSON-RPC tool calls over stdio. Supports both QML Quick apps (SceneGraph, QML items) and Qt Widget apps (widget hierarchy, properties, attributes).

## Prerequisites

- Qt 6.8+ (system)
- C++20 compiler (GCC 12+, Clang 16+)
- GammaRay 3.4.0+ (system package: `gammaray-dev` on Debian/Deepin)
- [qtmcp](https://github.com/signal-slot/qtmcp) (consumed via FetchContent — no manual install)
- Python 3.10+ with `pytest` (for running the test suite)

## Building

### 1. Install system dependencies

```bash
# Debian / Deepin
sudo apt install gammaray-dev gammaray-plugin-quickinspector
```

### 2. Build the bridge

```bash
cd bridge
cmake -S . -B build -G Ninja
cmake --build build
```

#### Offline build (using a local qtmcp clone)

If you have a local clone of qtmcp (e.g. at `/home/user/Sources/qtmcp`),
pass `FETCHCONTENT_SOURCE_DIR_QTMcp` to skip the network fetch:

```bash
cd bridge
cmake -S . -B build -G Ninja \
  -DFETCHCONTENT_SOURCE_DIR_QTMcp=/home/user/Sources/qtmcp \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build build
```

### 3. Run

```bash
# Start a probe (inject into a QML app)
gammaray --inject-only --listen tcp://127.0.0.1:11732 \
  --injector preload /usr/lib/qt6/bin/qml /path/to/app.qml -platform offscreen

# Or inject into a widget app
gammaray --inject-only --listen tcp://127.0.0.1:11732 \
  --injector preload /path/to/widget-app

# Start the bridge (stdio MCP server)
bridge/run.sh
```

The bridge starts in lazy-connect mode. Call `connectProbe("127.0.0.1", 11732)` from your MCP client once the probe is up.

### 4. Build a .deb package (optional)

```bash
cd bridge/build
cpack -G DEB
```

The package installs the bridge binary (`/usr/bin/gammaray-mcp-bridge`),
qtmcp shared libs and plugins, and the `run.sh` helper script.
System dependencies (`gammaray >= 3.4.0`, Qt6 libs) are auto-detected.

## MCP Tools

### Connection management
| Tool | Description |
|---|---|
| `connectProbe(host, port)` | Connect to a GammaRay probe (defaults: 127.0.0.1:11732) |
| `connectProbeDefault()` | Convenience: connect to 127.0.0.1:11732 |
| `disconnectProbe()` | Drop connection and forget URL |
| `probeStatus()` | Report connection state |

### QML Navigation
| Tool | Description |
|---|---|
| `listQuickWindows()` | List QQuickWindows in the target app |
| `selectQuickWindow(index)` | Select a window for scene graph introspection |
| `listQuickItems()` | Recursive QQuickItem tree with types and flags |
| `listScenegraphNodes()` | Recursive QSGNode tree (all node types) |

### QML Item inspection
| Tool | Description |
|---|---|
| `selectQuickItem(address)` | Select a QML item by address, populating its properties model |
| `getItemProperties(address)` | Get all Q_PROPERTY values (x, y, width, height, opacity, visible, z, anchors, text, font, etc.) |

### SG Node inspection
| Tool | Description |
|---|---|
| `selectScenegraphNode(address)` | Select a SG node, populating geometry/material sub-models |
| `getNodeVertices(address)` | Read vertex data of a GeometryNode |
| `getNodeAdjacency(address)` | Read adjacency/drawing mode of a GeometryNode |
| `getMaterialShaders(address)` | List shader stages for a node's material |
| `getShaderSource(row)` | Get shader source code (async via MaterialExtensionInterface) |
| `getMaterialProperties(address)` | Get material property name/value pairs |

### Rendering visualization
| Tool | Description |
|---|---|
| `setRenderMode(mode)` | Set render mode (NormalRendering, VisualizeOverdraw, etc.) |
| `setSlowMode(enabled)` | Toggle continuous rendering |

### Widget inspection
| Tool | Description |
|---|---|
| `listWidgets()` | Recursive QWidget hierarchy (types, names, visibility) |
| `selectWidget(address)` | Select a widget, populating its property and attribute models |
| `getWidgetProperties(address)` | Get all Q_PROPERTY values (geometry, font, palette, window flags, etc.) |
| `getWidgetAttributes(address)` | Get Qt::WidgetAttribute flags (acceptDrops, enabled, etc.) |

## Testing

```bash
# QML test suite (requires a QML app probe):
./tests/run_tests.sh

# Widget test suite (requires a widget app probe):
./tests/run_widget_tests.sh

# Unit tests only:
./tests/run_tests.sh --no-probe
```

See `tests/README.md` for details.

## License

GPL-2.0-or-later. The bridge links GammaRay libraries (GPL-2.0-or-later) and uses qtmcp (available under LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only — we comply under GPL-2.0-only).