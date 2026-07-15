# Test Suite

Integration tests for the gammaray-mcp bridge, written in Python using `pytest`.
Tests communicate with the bridge over stdio JSON-RPC, exercising every MCP tool
end-to-end against a real GammaRay probe.

## Quick start

```bash
./run_tests.sh --no-probe   # unit tests only (no probe needed)
./run_tests.sh -v           # full suite, verbose
./run_tests.sh -k "item"    # item-related tests only
```

## Test files

| File | Tests | Probe required |
|---|---|---|
| `test_connection.py` | `connectProbe`, `probeStatus`, `disconnectProbe`, reconnect | Yes (except `test_tools_listed`, `test_probe_status_disconnected`) |
| `test_navigation.py` | `listQuickWindows`, `selectQuickWindow`, `listQuickItems`, `listScenegraphNodes` | Yes |
| `test_items.py` | `selectQuickItem`, `getItemProperties`, position, visual properties, groups | Yes |
| `test_scenegraph.py` | `selectScenegraphNode`, `getNodeVertices`, `getNodeAdjacency`, `getMaterialShaders`, `getMaterialProperties`, `setRenderMode`, `setSlowMode` | Yes |
| `test_widgets.py` | `listWidgets`, `selectWidget`, `getWidgetProperties`, `getWidgetAttributes` | Yes (--widget-app) |

## Test applications

`testapp/main.qml` is a simple Qt Quick Controls application that exercises all
the QML tools: buttons, switches, sliders, a list view with delegate items, and
visibility/opacity controls.

`testapp/widget_test_app.cpp` is a simple QWidget application (QLabel, QLineEdit,
QCheckBox, QGroupBox, QPushButton) for testing widget inspection tools. Use
`run_widget_tests.sh` to run tests against this app.

## Configuration

**`run_tests.sh` is the single configuration point.** All paths default to
project-local locations. When GammaRay is installed elsewhere (e.g. from a
system package), override these by editing the variables at the top of
`run_tests.sh`:

| Variable | Default | Description |
|---|---|---|
| `GAMMARAY_BIN` | `install-prefix/bin/gammaray` | Path to the probe binary |
| `GAMMARAY_LIB_DIR` | `install-prefix/lib` | Runtime library directory for GammaRay |
| `QML_RUNNER` | `/usr/lib/qt6/bin/qml` | QML runtime executable |
| `TEST_QML` | `testapp/main.qml` | Test QML application |
| `BRIDGE_EXE` | `bridge/build/gammaray-mcp-bridge` | Bridge executable |
| `BRIDGE_RUN_SCRIPT` | `bridge/run.sh` | Bridge wrapper script |

You can also override any of these via environment variables when calling
`run_tests.sh` or `pytest` directly:

```bash
GAMMARAY_BIN=/usr/bin/gammaray GAMMARAY_LIB_DIR=/usr/lib/x86_64-linux-gnu \
  ./run_tests.sh --no-probe
```

## pytest options

| Flag | Default | Description |
|---|---|---|
| `--no-probe` | `false` | Skip all probe-dependent tests |
| `--probe-timeout` | 15s | Seconds to wait for probe to become ready |

## Markers

- `@pytest.mark.probe` — test requires a running GammaRay probe.
  Automatically skipped with `--no-probe`.

## Fixtures

- `bridge` — fresh `McpClient` instance per test (initialized, not connected)
- `connected_bridge` — bridge connected to a probe (via `connectProbeDefault`)
- `probe_process` — session-scoped, starts a probe via `systemd-run` (fallback:
  direct spawn) and waits for `ready()`