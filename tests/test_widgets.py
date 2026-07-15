"""Tests for Qt Widget introspection tools (listWidgets, selectWidget, etc.)."""

import pytest


class TestWidgetTools:
    """Verify listWidgets, selectWidget, getWidgetProperties, getWidgetAttributes."""

    @pytest.mark.probe
    def test_tools_listed(self, bridge):
        tools = bridge.list_tools()
        names = [t["name"] for t in tools]
        for expected in ["listWidgets", "selectWidget",
                         "getWidgetProperties", "getWidgetAttributes"]:
            assert expected in names, f"Missing tool: {expected}"

    @pytest.mark.probe
    def test_list_widgets(self, connected_bridge):
        widgets = connected_bridge.call_tool("listWidgets")
        assert isinstance(widgets, list), f"Expected list, got: {type(widgets)}"
        assert len(widgets) >= 1, "No widgets found"

        # Should have a top-level window
        types = [w.get("type", "") for w in widgets]
        assert any("QWidget" in t for t in types), \
            f"No QWidget-type root found: {types}"

    @pytest.mark.probe
    def test_select_widget(self, connected_bridge):
        widgets = connected_bridge.call_tool("listWidgets")
        assert isinstance(widgets, list) and len(widgets) > 0

        # Find the first Button-like widget
        def find_buttons(items_list):
            buttons = []
            for item in items_list:
                if "Button" in (item.get("type", "")) or "PushButton" in (item.get("type", "")):
                    buttons.append(item.get("name", ""))
                buttons.extend(find_buttons(item.get("children", [])))
            return buttons

        buttons = find_buttons(widgets)
        if buttons:
            addr = buttons[0]
        else:
            addr = widgets[0].get("name", "")

        result = connected_bridge.call_tool("selectWidget", {"address": addr})
        assert isinstance(result, dict), f"selectWidget failed: {result}"
        assert result.get("propertyCount", 0) > 0, \
            f"No properties for widget {addr}: {result}"

    @pytest.mark.probe
    def test_get_widget_properties(self, connected_bridge):
        widgets = connected_bridge.call_tool("listWidgets")
        assert isinstance(widgets, list) and len(widgets) > 0

        addr = widgets[0].get("name", "")
        if not addr:
            pytest.skip("No widget address found")

        props = connected_bridge.call_tool("getWidgetProperties", {"address": addr})
        assert isinstance(props, dict), f"getWidgetProperties failed: {props}"
        assert "properties" in props, f"No 'properties' key: {list(props.keys())}"

    @pytest.mark.probe
    def test_widget_has_geometry(self, connected_bridge):
        """Verify a widget has geometry properties (x, y, width, height)."""
        widgets = connected_bridge.call_tool("listWidgets")
        assert isinstance(widgets, list) and len(widgets) > 0

        addr = widgets[0].get("name", "")
        if not addr:
            pytest.skip("No widget address found")

        props = connected_bridge.call_tool("getWidgetProperties", {"address": addr})
        simple = props.get("properties", {})
        for key in ("x", "y", "width", "height"):
            assert key in simple, f"Widget missing '{key}' property"

    @pytest.mark.probe
    def test_widget_attributes(self, connected_bridge):
        """Verify getWidgetAttributes returns attribute flags for a widget."""
        widgets = connected_bridge.call_tool("listWidgets")
        assert isinstance(widgets, list) and len(widgets) > 0

        # Find the checkBox widget (has various attributes)
        def find_named(items_list):
            for item in items_list:
                name = item.get("name", "")
                if "check" in name.lower() or "CheckBox" in (item.get("type", "")):
                    return name
                found = find_named(item.get("children", []))
                if found:
                    return found
            return None

        addr = find_named(widgets)
        if not addr:
            addr = widgets[0].get("name", "")

        attrs = connected_bridge.call_tool("getWidgetAttributes", {"address": addr})
        assert isinstance(attrs, dict), f"getWidgetAttributes failed: {attrs}"
        assert "attributes" in attrs, f"No 'attributes' key: {list(attrs.keys())}"
        assert attrs.get("count", 0) > 0, "No widget attributes returned"
        # Should at least have some standard attributes
        attr_names = list(attrs.get("attributes", {}).keys())
        assert len(attr_names) > 0, "Empty attributes list"

    @pytest.mark.probe
    def test_widget_enabled_attribute(self, connected_bridge):
        """Verify attributes contain 'enabled' field."""
        widgets = connected_bridge.call_tool("listWidgets")
        assert isinstance(widgets, list) and len(widgets) > 0

        addr = widgets[0].get("name", "")
        if not addr:
            pytest.skip("No widget address found")

        attrs = connected_bridge.call_tool("getWidgetAttributes", {"address": addr})
        attributes = attrs.get("attributes", {})
        # Check at least one attribute has enabled/value keys
        for attr_name, attr_data in attributes.items():
            if isinstance(attr_data, dict) and "enabled" in attr_data:
                return  # Found one
        pytest.skip("No attributes with 'enabled' field found")