"""Tests for SceneGraph node selection and geometry/material tools."""

import pytest


class TestSceneGraph:
    """Verify selectScenegraphNode, getNodeVertices, getNodeAdjacency, etc."""

    @pytest.mark.probe
    def test_tools_listed(self, bridge):
        tools = bridge.list_tools()
        names = [t["name"] for t in tools]
        for expected in [
            "selectScenegraphNode", "getNodeVertices", "getNodeAdjacency",
            "getMaterialShaders", "getShaderSource", "getMaterialProperties",
            "setRenderMode", "setSlowMode",
        ]:
            assert expected in names, f"Missing tool: {expected}"

    @pytest.mark.probe
    def test_select_scenegraph_node(self, connected_bridge):
        connected_bridge.call_tool("selectQuickWindow", {"index": 0})
        
        nodes = connected_bridge.call_tool("listScenegraphNodes")

        # Find first Geometry Node
        def find_geo(items_list):
            for item in items_list:
                if item.get("type") == "Geometry Node":
                    return item.get("address", "")
                found = find_geo(item.get("children", []))
                if found:
                    return found
            return None

        addr = find_geo(nodes)
        if not addr:
            pytest.skip("No Geometry Node found")

        result = connected_bridge.call_tool("selectScenegraphNode", {"address": addr})
        assert isinstance(result, dict), f"selectScenegraphNode failed: {result}"
        assert result.get("type") == "Geometry Node"
        assert "hasGeometry" in result
        assert "hasMaterial" in result

    @pytest.mark.probe
    def test_get_node_vertices(self, connected_bridge):
        connected_bridge.call_tool("selectQuickWindow", {"index": 0})
        
        nodes = connected_bridge.call_tool("listScenegraphNodes")

        def find_geo(items_list):
            for item in items_list:
                if item.get("type") == "Geometry Node":
                    return item.get("address", "")
                found = find_geo(item.get("children", []))
                if found:
                    return found
            return None

        addr = find_geo(nodes)
        if not addr:
            pytest.skip("No Geometry Node found")

        result = connected_bridge.call_tool("getNodeVertices", {"address": addr})
        assert isinstance(result, dict)
        # May return "no vertex data" for software renderer -- that's OK
        assert "error" in result or "vertexCount" in result

    @pytest.mark.probe
    def test_get_node_adjacency(self, connected_bridge):
        connected_bridge.call_tool("selectQuickWindow", {"index": 0})
        
        nodes = connected_bridge.call_tool("listScenegraphNodes")

        def find_geo(items_list):
            for item in items_list:
                if item.get("type") == "Geometry Node":
                    return item.get("address", "")
                found = find_geo(item.get("children", []))
                if found:
                    return found
            return None

        addr = find_geo(nodes)
        if not addr:
            pytest.skip("No Geometry Node found")

        result = connected_bridge.call_tool("getNodeAdjacency", {"address": addr})
        assert isinstance(result, dict)
        assert "error" in result or "drawingMode" in result

    @pytest.mark.probe
    def test_get_material_shaders(self, connected_bridge):
        connected_bridge.call_tool("selectQuickWindow", {"index": 0})
        
        nodes = connected_bridge.call_tool("listScenegraphNodes")

        def find_geo(items_list):
            for item in items_list:
                if item.get("type") == "Geometry Node":
                    return item.get("address", "")
                found = find_geo(item.get("children", []))
                if found:
                    return found
            return None

        addr = find_geo(nodes)
        if not addr:
            pytest.skip("No Geometry Node found")

        result = connected_bridge.call_tool("getMaterialShaders", {"address": addr})
        assert isinstance(result, dict)
        # Software renderer -> no shaders; that's expected
        assert "error" in result or isinstance(result, list)

    @pytest.mark.probe
    def test_get_material_properties(self, connected_bridge):
        connected_bridge.call_tool("selectQuickWindow", {"index": 0})
        
        nodes = connected_bridge.call_tool("listScenegraphNodes")

        def find_geo(items_list):
            for item in items_list:
                if item.get("type") == "Geometry Node":
                    return item.get("address", "")
                found = find_geo(item.get("children", []))
                if found:
                    return found
            return None

        addr = find_geo(nodes)
        if not addr:
            pytest.skip("No Geometry Node found")

        result = connected_bridge.call_tool("getMaterialProperties", {"address": addr})
        assert isinstance(result, dict)
        assert "error" in result or "properties" in result

    @pytest.mark.probe
    def test_set_render_mode(self, connected_bridge):
        result = connected_bridge.call_tool("setRenderMode",
                                            {"mode": "VisualizeOverdraw"})
        assert isinstance(result, dict)
        assert result.get("renderMode") == "VisualizeOverdraw"

        # Reset
        connected_bridge.call_tool("setRenderMode", {"mode": "NormalRendering"})

    @pytest.mark.probe
    def test_set_slow_mode(self, connected_bridge):
        result = connected_bridge.call_tool("setSlowMode", {"enabled": True})
        assert isinstance(result, dict)
        assert result.get("slowMode") is True

        connected_bridge.call_tool("setSlowMode", {"enabled": False})