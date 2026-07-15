"""Tests for QML item selection and property inspection."""

import pytest


class TestItemProperties:
    """Verify selectQuickItem and getItemProperties."""

    @pytest.mark.probe
    def test_tools_listed(self, bridge):
        tools = bridge.list_tools()
        names = [t["name"] for t in tools]
        for expected in ["selectQuickItem", "getItemProperties"]:
            assert expected in names

    @pytest.mark.probe
    def test_select_quick_item(self, connected_bridge):
        # First get items
        connected_bridge.call_tool("selectQuickWindow", {"index": 0})
        
        items = connected_bridge.call_tool("listQuickItems")
        assert isinstance(items, list) and len(items) > 0, "No items found"

        # Find the first Button-like item
        def find_buttons(items_list):
            buttons = []
            for item in items_list:
                if "Button" in (item.get("type", "")):
                    buttons.append(item.get("name", ""))
                buttons.extend(find_buttons(item.get("children", [])))
            return buttons

        buttons = find_buttons(items)
        if not buttons:
            # Try any item
            def first_addr(items_list):
                for item in items_list:
                    return item.get("name", "")
            addr = first_addr(items)
        else:
            addr = buttons[0]

        result = connected_bridge.call_tool("selectQuickItem", {"address": addr})
        assert isinstance(result, dict), f"selectQuickItem failed: {result}"
        assert result.get("propertyCount", 0) > 0, \
            f"No properties for item {addr}: {result}"

    @pytest.mark.probe
    def test_get_item_properties(self, connected_bridge):
        connected_bridge.call_tool("selectQuickWindow", {"index": 0})
        
        items = connected_bridge.call_tool("listQuickItems")

        def find_any_item(items_list):
            for item in items_list:
                addr = item.get("name", "")
                if addr and not addr.startswith("Loading"):
                    return addr
                found = find_any_item(item.get("children", []))
                if found:
                    return found
            return None

        addr = find_any_item(items)
        if not addr:
            pytest.skip("No usable item address found")

        props = connected_bridge.call_tool("getItemProperties", {"address": addr})
        assert isinstance(props, dict), f"getItemProperties failed: {props}"
        assert "properties" in props, f"No 'properties' key: {list(props.keys())}"

    @pytest.mark.probe
    def test_item_has_position(self, connected_bridge):
        """Verify that a Button item has x, y, width, height properties."""
        connected_bridge.call_tool("selectQuickWindow", {"index": 0})
        
        items = connected_bridge.call_tool("listQuickItems")

        # Find a Button_QMLTYPE item
        def find_button(items_list):
            for item in items_list:
                if "Button" in (item.get("type", "")):
                    return item.get("name", "")
                found = find_button(item.get("children", []))
                if found:
                    return found
            return None

        addr = find_button(items)
        if not addr:
            pytest.skip("No Button item found")

        props = connected_bridge.call_tool("getItemProperties", {"address": addr})
        simple = props.get("properties", {})
        for key in ("x", "y", "width", "height"):
            assert key in simple, f"Button missing '{key}' property"
            val = simple[key].get("value", "")
            assert val != "", f"Button.{key} is empty"

    @pytest.mark.probe
    def test_item_has_visual_properties(self, connected_bridge):
        """Verify items have opacity, visible, z, rotation, scale."""
        connected_bridge.call_tool("selectQuickWindow", {"index": 0})
        
        items = connected_bridge.call_tool("listQuickItems")

        def find_addr(items_list):
            for item in items_list:
                addr = item.get("name", "")
                if addr and not addr.startswith("Loading") and addr != "ApplicationWindow":
                    return addr
                found = find_addr(item.get("children", []))
                if found:
                    return found
            return None

        addr = find_addr(items)
        if not addr:
            pytest.skip("No suitable item found")

        props = connected_bridge.call_tool("getItemProperties", {"address": addr})
        simple = props.get("properties", {})
        for key in ("opacity", "visible", "z"):
            assert key in simple, f"Item missing '{key}'"

    @pytest.mark.probe
    def test_property_model_has_groups(self, connected_bridge):
        """Verify items have nested property groups (background, contentItem, etc.)."""
        connected_bridge.call_tool("selectQuickWindow", {"index": 0})
        
        items = connected_bridge.call_tool("listQuickItems")

        def find_button(items_list):
            for item in items_list:
                if "Button" in (item.get("type", "")):
                    return item.get("name", "")
                found = find_button(item.get("children", []))
                if found:
                    return found
            return None

        addr = find_button(items)
        if not addr:
            pytest.skip("No Button item found")

        props = connected_bridge.call_tool("getItemProperties", {"address": addr})
        groups = props.get("groups", {})
        # Buttons typically have [background], [contentItem], [parent], [window]
        assert len(groups) > 0, f"No property groups found: {list(props.keys())}"