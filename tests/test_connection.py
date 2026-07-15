"""Tests for connection management tools.

These tests need function-scoped bridges because they explicitly test
connect/disconnect/reconnect lifecycle. The conftest.py module-scoped
fixtures would not work here since they share a single connection.
"""

import time

import pytest

from mcp_client import McpClient


@pytest.fixture(scope="function")
def local_bridge(run_script_path) -> McpClient:
    c = McpClient(bridge_path=run_script_path)
    c.initialize()
    yield c
    try:
        c.call_tool("disconnectProbe", timeout=5)
    except Exception:
        pass
    c.close()
    time.sleep(0.2)


@pytest.fixture(scope="function")
def local_connected_bridge(local_bridge, probe_process) -> McpClient:
    if probe_process is None:
        pytest.skip("No probe available")
    deadline = time.monotonic() + 30
    last_error = None
    while time.monotonic() < deadline:
        result = local_bridge.call_tool("connectProbeDefault")
        if isinstance(result, dict) and result.get("connected"):
            return local_bridge
        last_error = result
        time.sleep(1)
    raise AssertionError(f"Failed to connect to probe after 30s: {last_error}")


class TestConnection:
    """Verify connectProbe, probeStatus, disconnectProbe work."""

    def test_tools_listed(self, local_bridge):
        tools = local_bridge.list_tools()
        names = [t["name"] for t in tools]
        for expected in ["connectProbe", "connectProbeDefault",
                         "disconnectProbe", "probeStatus"]:
            assert expected in names, f"Missing tool: {expected}"

    def test_probe_status_disconnected(self, local_bridge):
        status = local_bridge.call_tool("probeStatus")
        assert isinstance(status, dict)
        assert status.get("state") in ("disconnected", "connecting")
        assert status.get("ready") is False

    def test_connect_and_disconnect(self, local_bridge, probe_process):
        if probe_process is None:
            pytest.skip("No probe available")
        result = local_bridge.call_tool("connectProbeDefault")
        assert isinstance(result, dict), f"connectProbeDefault failed: {result}"
        assert result.get("connected"), f"Not connected: {result}"
        status = local_bridge.call_tool("probeStatus")
        assert isinstance(status, dict)
        assert status.get("state") == "ready"
        assert status.get("ready") is True
        assert "tcp://" in status.get("url", "")
        result = local_bridge.call_tool("disconnectProbe")
        assert isinstance(result, dict)
        assert result.get("state") == "disconnected"

    def test_reconnect(self, local_bridge, probe_process):
        if probe_process is None:
            pytest.skip("No probe available")
        local_bridge.call_tool("connectProbeDefault")
        local_bridge.call_tool("disconnectProbe")
        result = local_bridge.call_tool("connectProbe",
                                        {"host": "127.0.0.1", "port": 11732})
        assert isinstance(result, dict)
        assert result.get("connected") is True
        assert result.get("state") == "ready"

    def test_reconnect_default_after_disconnect(self, local_connected_bridge):
        local_connected_bridge.call_tool("disconnectProbe")
        result = local_connected_bridge.call_tool("connectProbeDefault")
        assert isinstance(result, dict)
        assert result.get("connected") is True