"""Tests for screenshot functionality (screenshotQuickItem, screenshotWidget)."""

import base64

import pytest


def _find_first(items_list, type_hint=None):
    """Find the first item address in a tree, optionally filtering by type."""
    for item in items_list:
        addr = item.get("name", "")
        if addr and not addr.startswith("Loading"):
            if not type_hint or type_hint in (item.get("type", "")):
                return addr
        found = _find_first(item.get("children", []), type_hint)
        if found:
            return found
    return None


class TestScreenshotToolsListed:
    """Verify screenshot tools are registered."""

    @pytest.mark.probe
    def test_screenshot_tools_listed(self, bridge):
        tools = bridge.list_tools()
        names = [t["name"] for t in tools]
        for expected in ["screenshotQuickItem", "screenshotWidget"]:
            assert expected in names, f"Missing tool: {expected}"


class TestQuickItemScreenshot:
    """Verify screenshotQuickItem returns a valid image."""

    @pytest.mark.probe
    def test_screenshot_quick_item(self, connected_bridge):
        connected_bridge.call_tool("selectQuickWindow", {"index": 0})
        items = connected_bridge.call_tool("listQuickItems")
        assert isinstance(items, list) and len(items) > 0, "No items found"

        addr = _find_first(items, "Button")
        if not addr:
            addr = _find_first(items)
        if not addr:
            pytest.skip("No usable item address found")

        result = connected_bridge.call_tool(
            "screenshotQuickItem", {"address": addr}, timeout=30
        )
        assert isinstance(result, dict), f"Unexpected result type: {type(result)}"
        assert "_image_data" in result, \
            f"Expected image content, got keys: {list(result.keys())}"
        data = result["_image_data"]
        assert data, "Image data is empty"
        # Verify it's valid base64
        try:
            decoded = base64.b64decode(data)
        except Exception as e:
            pytest.fail(f"Image data is not valid base64: {e}")
        # PNG header check
        assert decoded.startswith(b"\x89PNG"), \
            f"Image data does not start with PNG header: {decoded[:8]}"
        assert len(decoded) > 100, f"Image too small: {len(decoded)} bytes"


class TestWidgetScreenshot:
    """Verify screenshotWidget returns a valid image."""

    @pytest.mark.probe
    def test_screenshot_widget(self, connected_bridge):
        widgets = connected_bridge.call_tool("listWidgets")
        assert isinstance(widgets, list) and len(widgets) > 0, "No widgets found"

        addr = _find_first(widgets)
        if not addr:
            pytest.skip("No widget address found")

        result = connected_bridge.call_tool(
            "screenshotWidget", {"address": addr}, timeout=30
        )
        assert isinstance(result, dict), f"Unexpected result type: {type(result)}"
        assert "_image_data" in result, \
            f"Expected image content, got keys: {list(result.keys())}"
        data = result["_image_data"]
        assert data, "Image data is empty"
        try:
            decoded = base64.b64decode(data)
        except Exception as e:
            pytest.fail(f"Image data is not valid base64: {e}")
        assert decoded.startswith(b"\x89PNG"), \
            f"Image data does not start with PNG header: {decoded[:8]}"
        assert len(decoded) > 100, f"Image too small: {len(decoded)} bytes"