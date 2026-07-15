"""MCP client helper for testing the gammaray-mcp bridge."""

import json
import subprocess
import sys
import time
from pathlib import Path


class McpClient:
    """A minimal JSON-RPC MCP client communicating over stdio."""

    def __init__(self, bridge_path: Path):
        self._proc = subprocess.Popen(
            [str(bridge_path)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        self._next_id = 0
        self._initialized = False

    def close(self):
        if self._proc and self._proc.poll() is None:
            self._proc.terminate()
            try:
                self._proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self._proc.kill()

    @property
    def returncode(self):
        return self._proc.poll()

    @property
    def stderr(self):
        return self._proc.stderr

    def send_request(self, method: str, params: dict | None = None, timeout: float = 60.0) -> dict:
        self._next_id += 1
        req = {
            "jsonrpc": "2.0",
            "id": self._next_id,
            "method": method,
            "params": params or {},
        }
        line = json.dumps(req)
        self._proc.stdin.write(line + "\n")
        self._proc.stdin.flush()
        return self._read_response(self._next_id, timeout=timeout)

    def _read_response(self, expected_id: int, timeout: float = 60.0) -> dict:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = self._proc.stdout.readline()
            if not line:
                stderr = self._proc.stderr.read()
                rc = self._proc.poll()
                raise RuntimeError(
                    f"Bridge process terminated (rc={rc}, stderr: {stderr[-500:] if stderr else 'none'})")
            resp = json.loads(line)
            rid = resp.get("id")
            if rid == expected_id:
                return resp
            # Could be a notification; ignore and keep reading
        raise TimeoutError(f"No response for id={expected_id} within {timeout}s")

    def initialize(self, protocol_version: str = "2025-03-26") -> dict:
        resp = self.send_request("initialize", {
            "protocolVersion": protocol_version,
            "capabilities": {},
            "clientInfo": {"name": "test", "version": "1.0"},
        })
        # Send initialized notification
        self._proc.stdin.write(
            json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}}) + "\n"
        )
        self._proc.stdin.flush()
        self._initialized = True
        return resp

    def call_tool(self, name: str, arguments: dict | None = None, timeout: float = 60.0) -> dict:
        resp = self.send_request("tools/call", {"name": name, "arguments": arguments or {}}, timeout=timeout)
        content = resp.get("result", {}).get("content", [])
        if content and content[0].get("type") == "text":
            text = content[0]["text"]
            try:
                return json.loads(text)
            except (json.JSONDecodeError, TypeError):
                return {"_raw": text}
        return resp.get("result", {})

    def list_tools(self) -> list[dict]:
        resp = self.send_request("tools/list")
        return resp.get("result", {}).get("tools", [])