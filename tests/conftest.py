"""pytest fixtures for gammaray-mcp bridge tests.

All paths are configurable via environment variables. run_tests.sh is the
canonical configuration point — it sets sensible defaults and exports them
before calling pytest. If you prefer to run pytest directly, set these:

  GAMMARAY_BIN    — path to the gammaray probe binary
  GAMMARAY_LIB_DIR — directory containing GammaRay shared libraries
  QML_RUNNER      — path to the QML runtime executable
  TEST_QML        — path to the test QML application
  WIDGET_TEST_APP — path to the widget test application (C++ QWidget binary)
  BRIDGE_EXE      — path to the bridge executable
  BRIDGE_RUN_SCRIPT — path to bridge/run.sh
"""

import os
import socket
import subprocess
import time
from pathlib import Path

import pytest

from mcp_client import McpClient

PROJECT_ROOT = Path(__file__).resolve().parent.parent

# All paths read from env vars, with sensible project-local defaults.
GAMMARAY_BIN = Path(os.environ.get(
    "GAMMARAY_BIN",
    str(PROJECT_ROOT / "install-prefix" / "bin" / "gammaray"),
))
GAMMARAY_LIB_DIR = os.environ.get(
    "GAMMARAY_LIB_DIR",
    str(PROJECT_ROOT / "install-prefix" / "lib"),
)
QML_RUNNER = Path(os.environ.get(
    "QML_RUNNER",
    "/usr/lib/qt6/bin/qml",
))
TEST_QML = Path(os.environ.get(
    "TEST_QML",
    str(PROJECT_ROOT / "tests" / "testapp" / "main.qml"),
))
WIDGET_TEST_APP_BIN = Path(os.environ.get(
    "WIDGET_TEST_APP_BIN",
    str(PROJECT_ROOT / "tests" / "testapp" / "widget_test_app"),
))
WIDGET_TEST_APP_SRC = Path(os.environ.get(
    "WIDGET_TEST_APP_SRC",
    str(PROJECT_ROOT / "tests" / "testapp" / "widget_test_app.cpp"),
))
BRIDGE_EXE = Path(os.environ.get(
    "BRIDGE_EXE",
    str(PROJECT_ROOT / "bridge" / "build" / "gammaray-mcp-bridge"),
))
BRIDGE_RUN_SCRIPT = Path(os.environ.get(
    "BRIDGE_RUN_SCRIPT",
    str(PROJECT_ROOT / "bridge" / "run.sh"),
))


def _compile_widget_app() -> Path:
    """Compile the widget test app if needed. Returns the binary path."""
    if WIDGET_TEST_APP_BIN.exists():
        return WIDGET_TEST_APP_BIN
    if not WIDGET_TEST_APP_SRC.exists():
        pytest.skip(f"Widget test app source not found at {WIDGET_TEST_APP_SRC}")
    # Find Qt6 via pkg-config
    try:
        import subprocess as sp
        cflags = sp.check_output(
            ["pkg-config", "--cflags", "Qt6Core", "Qt6Gui", "Qt6Widgets"],
            text=True
        ).strip()
        libs = sp.check_output(
            ["pkg-config", "--libs", "Qt6Core", "Qt6Gui", "Qt6Widgets"],
            text=True
        ).strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        # Fallback: try qmake6
        try:
            qmake = sp.check_output(["which", "qmake6"], text=True).strip()
            prefix = sp.check_output([qmake, "-query", "QT_INSTALL_PREFIX"], text=True).strip()
            cflags = f"-I{prefix}/include -I{prefix}/include/QtCore -I{prefix}/include/QtGui -I{prefix}/include/QtWidgets"
            libs = f"-L{prefix}/lib -lQt6Core -lQt6Gui -lQt6Widgets"
        except (subprocess.CalledProcessError, FileNotFoundError):
            pytest.skip("Cannot find Qt6 build flags (need pkg-config or qmake6)")

    subprocess.run(
        ["g++", "-std=c++17", "-fPIC",
         str(WIDGET_TEST_APP_SRC),
         "-o", str(WIDGET_TEST_APP_BIN),
         *cflags.split(), *libs.split()],
        check=True, timeout=30,
    )
    return WIDGET_TEST_APP_BIN


def pytest_addoption(parser):
    parser.addoption(
        "--no-probe",
        action="store_true",
        default=False,
        help="Skip tests that need a running probe",
    )
    parser.addoption(
        "--probe-timeout",
        type=int,
        default=15,
        help="Seconds to wait for probe to be ready",
    )
    parser.addoption(
        "--widget-app",
        action="store_true",
        default=False,
        help="Use widget test app instead of QML app for probe target",
    )


def pytest_collection_modifyitems(config, items):
    if config.getoption("--no-probe"):
        skip_probe = pytest.mark.skip(reason="Skipping probe-dependent tests (--no-probe)")
        for item in items:
            if "probe" in item.keywords:
                item.add_marker(skip_probe)


@pytest.fixture(scope="session")
def bridge_path() -> Path:
    if not BRIDGE_EXE.exists():
        pytest.skip(f"Bridge not built at {BRIDGE_EXE}")
    return BRIDGE_EXE


@pytest.fixture(scope="session")
def run_script_path() -> Path:
    if not BRIDGE_RUN_SCRIPT.exists():
        pytest.skip(f"run.sh not found at {BRIDGE_RUN_SCRIPT}")
    return BRIDGE_RUN_SCRIPT


def _probe_accepting_connections(timeout: int = 20) -> bool:
    """Check if the probe is accepting connections via raw TCP."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            s = socket.create_connection(("127.0.0.1", 11732), timeout=2)
            s.close()
            return True
        except (OSError, socket.timeout):
            time.sleep(1)
    return False


@pytest.fixture(scope="session")
def probe_process(request) -> subprocess.Popen | None:
    """Start a GammaRay probe process (session-scoped, one per entire test run).

    Uses either the QML test app or the widget test app depending on --widget-app.
    """
    if request.config.getoption("--no-probe"):
        return None

    if not GAMMARAY_BIN.exists():
        pytest.skip(f"GammaRay probe binary not found at {GAMMARAY_BIN}")

    if request.config.getoption("--widget-app"):
        return _start_widget_probe(request)
    else:
        return _start_qml_probe(request)


def _start_qml_probe(request) -> subprocess.Popen:
    """Start probe with QML test app."""
    if not TEST_QML.exists():
        pytest.skip(f"Test QML file not found at {TEST_QML}")
    if not QML_RUNNER.exists():
        pytest.skip(f"QML runner not found at {QML_RUNNER}")

    env = os.environ.copy()
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["LD_LIBRARY_PATH"] = GAMMARAY_LIB_DIR

    subprocess.run(
        ["systemctl", "--user", "stop", "gammaray-probe-test.service"],
        capture_output=True, timeout=5,
    )
    subprocess.run(
        ["systemctl", "--user", "reset-failed", "gammaray-probe-test.service"],
        capture_output=True, timeout=5,
    )

    result = subprocess.run(
        [
            "systemd-run", "--user", "--no-block",
            "--unit=gammaray-probe-test",
            "--same-dir",
            "--working-directory", str(TEST_QML.parent),
            "-E", f"QT_QPA_PLATFORM={env['QT_QPA_PLATFORM']}",
            "-E", f"LD_LIBRARY_PATH={env['LD_LIBRARY_PATH']}",
            str(GAMMARAY_BIN),
            "--inject-only", "--listen", "tcp://127.0.0.1:11732",
            "--injector", "preload",
            str(QML_RUNNER), str(TEST_QML),
        ],
        capture_output=True, text=True, timeout=10,
    )

    timeout = request.config.getoption("--probe-timeout")
    if not _probe_accepting_connections(timeout):
        proc = subprocess.Popen(
            [str(GAMMARAY_BIN), "--inject-only", "--listen", "tcp://127.0.0.1:11732",
             "--injector", "preload", str(QML_RUNNER), str(TEST_QML)],
            env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        if not _probe_accepting_connections(timeout):
            pytest.fail(f"QML probe did not become ready within {timeout}s")
    else:
        proc = subprocess.Popen(["true"])

    def cleanup():
        subprocess.run(
            ["systemctl", "--user", "stop", "gammaray-probe-test.service"],
            capture_output=True, timeout=5,
        )
        subprocess.run(
            ["systemctl", "--user", "reset-failed", "gammaray-probe-test.service"],
            capture_output=True, timeout=5,
        )

    request.addfinalizer(cleanup)
    return proc


def _start_widget_probe(request) -> subprocess.Popen:
    """Start probe with widget test app."""
    widget_bin = _compile_widget_app()

    env = os.environ.copy()
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["LD_LIBRARY_PATH"] = GAMMARAY_LIB_DIR

    subprocess.run(
        ["systemctl", "--user", "stop", "gammaray-probe-test.service"],
        capture_output=True, timeout=5,
    )
    subprocess.run(
        ["systemctl", "--user", "reset-failed", "gammaray-probe-test.service"],
        capture_output=True, timeout=5,
    )

    result = subprocess.run(
        [
            "systemd-run", "--user", "--no-block",
            "--unit=gammaray-probe-test",
            "--same-dir",
            "--working-directory", str(widget_bin.parent),
            "-E", f"QT_QPA_PLATFORM={env['QT_QPA_PLATFORM']}",
            "-E", f"LD_LIBRARY_PATH={env['LD_LIBRARY_PATH']}",
            str(GAMMARAY_BIN),
            "--inject-only", "--listen", "tcp://127.0.0.1:11732",
            "--injector", "preload",
            str(widget_bin),
        ],
        capture_output=True, text=True, timeout=10,
    )

    timeout = request.config.getoption("--probe-timeout")
    if not _probe_accepting_connections(timeout):
        proc = subprocess.Popen(
            [str(GAMMARAY_BIN), "--inject-only", "--listen", "tcp://127.0.0.1:11732",
             "--injector", "preload", str(widget_bin)],
            env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        if not _probe_accepting_connections(timeout):
            pytest.fail(f"Widget probe did not become ready within {timeout}s")
    else:
        proc = subprocess.Popen(["true"])

    def cleanup():
        subprocess.run(
            ["systemctl", "--user", "stop", "gammaray-probe-test.service"],
            capture_output=True, timeout=5,
        )
        subprocess.run(
            ["systemctl", "--user", "reset-failed", "gammaray-probe-test.service"],
            capture_output=True, timeout=5,
        )

    request.addfinalizer(cleanup)
    return proc


@pytest.fixture(scope="module")
def bridge(run_script_path) -> McpClient:
    """Start a bridge instance per module, reused across tests in the module."""
    c = McpClient(bridge_path=run_script_path)
    c.initialize()
    yield c
    try:
        c.call_tool("disconnectProbe", timeout=5)
    except Exception:
        pass
    c.close()
    time.sleep(0.2)


@pytest.fixture(scope="module")
def connected_bridge(bridge, probe_process) -> McpClient:
    """Bridge connected to a running probe (module-scoped, reused across tests)."""
    if probe_process is None:
        pytest.skip("No probe available (try --no-probe to skip probe-dependent tests)")
    deadline = time.monotonic() + 30
    last_error = None
    while time.monotonic() < deadline:
        result = bridge.call_tool("connectProbeDefault")
        if isinstance(result, dict) and result.get("connected"):
            return bridge
        last_error = result
        time.sleep(1)
    raise AssertionError(f"Failed to connect to probe after 30s: {last_error}")