"""Tests for navigation tools (windows, items, scene graph tree)."""

import pytest


class TestNavigation:
    """Verify listQuickWindows, selectQuickWindow, listQuickItems, listScenegraphNodes."""

    def test_tools_listed(self, bridge):
        tools = bridge.list_tools()
        names = [t["name"] for t in tools]
        for expected in ["listQuickWindows", "selectQuickWindow",
                         "listQuickItems", "listScenegraphNodes"]:
            assert expected in names

    @pytest.mark.probe
    def test_list_windows(self, connected_bridge):
        windows = connected_bridge.call_tool("listQuickWindows")
        assert isinstance(windows, list)
        assert len(windows) >= 1
        for w in windows:
            assert "address" in w
            assert "type" in w

    @pytest.mark.probe
    def test_select_window(self, connected_bridge):
        result = connected_bridge.call_tool("selectQuickWindow", {"index": 0})
        assert isinstance(result, dict)
        assert result.get("status") == "selected"

    @pytest.mark.probe
    def test_list_items(self, connected_bridge):
        connected_bridge.call_tool("selectQuickWindow", {"index": 0})
        items = connected_bridge.call_tool("listQuickItems")
        assert isinstance(items, list)
        if items:
            assert isinstance(items[0], dict)
            assert "type" in items[0]

    @pytest.mark.probe
    def test_list_items_with_window_count(self, connected_bridge):
        connected_bridge.call_tool("selectQuickWindow", {"index": 0})
        items = connected_bridge.call_tool("listQuickItems")

        def count_all(items_list):
            count = 0
            for item in items_list:
                count += 1
                count += count_all(item.get("children", []))
            return count

        if items:
            total = count_all(items)
            assert total > 0

    @pytest.mark.probe
    def test_list_scenegraph_nodes(self, connected_bridge):
        connected_bridge.call_tool("selectQuickWindow", {"index": 0})
        nodes = connected_bridge.call_tool("listScenegraphNodes")
        assert isinstance(nodes, list)
        if nodes:
            assert isinstance(nodes[0], dict)
            assert nodes[0].get("type") == "Root Node"

    @pytest.mark.probe
    def test_scenegraph_has_geometry_nodes(self, connected_bridge):
        connected_bridge.call_tool("selectQuickWindow", {"index": 0})
        nodes = connected_bridge.call_tool("listScenegraphNodes")

        def find_geometry(items_list):
            for item in items_list:
                if item.get("type") == "Geometry Node":
                    return item
                found = find_geometry(item.get("children", []))
                if found:
                    return found
            return None

        geo = find_geometry(nodes)
        assert geo is not None, "No Geometry Node found in SG tree"
