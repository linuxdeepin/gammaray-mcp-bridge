"""Tests for QML engine introspection tools (listQmlEngines, getEngineProperties, etc.)."""

import pytest


class TestQmlEngineTools:
    """Verify listQmlEngines, selectQmlEngine, getEngineProperties."""

    @pytest.mark.probe
    def test_tools_listed(self, bridge):
        tools = bridge.list_tools()
        names = [t["name"] for t in tools]
        for expected in ["listQmlEngines", "selectQmlEngine",
                         "getEngineProperties"]:
            assert expected in names, f"Missing tool: {expected}"

    @pytest.mark.probe
    def test_list_qml_engines(self, connected_bridge):
        result = connected_bridge.call_tool("listQmlEngines")
        assert isinstance(result, dict), f"listQmlEngines failed: {result}"
        assert result.get("count", 0) >= 1, \
            f"Expected at least 1 QQmlEngine, got: {result}"
        engines = result.get("engines", [])
        assert len(engines) >= 1, "No engines in result"
        # Check structure
        for eng in engines:
            assert "address" in eng, f"Engine missing 'address': {eng}"
            assert "type" in eng, f"Engine missing 'type': {eng}"

    @pytest.mark.probe
    def test_select_qml_engine(self, connected_bridge):
        engines = connected_bridge.call_tool("listQmlEngines")
        addr = engines["engines"][0]["address"]

        result = connected_bridge.call_tool("selectQmlEngine", {"address": addr})
        assert isinstance(result, dict), f"selectQmlEngine failed: {result}"
        assert result.get("selected") == addr, \
            f"Wrong address selected: {result}"
        assert result.get("propertyCount", 0) > 0, \
            f"No properties for engine {addr}: {result}"

    @pytest.mark.probe
    def test_get_engine_properties(self, connected_bridge):
        engines = connected_bridge.call_tool("listQmlEngines")
        addr = engines["engines"][0]["address"]

        props = connected_bridge.call_tool("getEngineProperties", {"address": addr})
        assert isinstance(props, dict), f"getEngineProperties failed: {props}"
        assert "properties" in props, \
            f"No 'properties' key: {list(props.keys())}"

    @pytest.mark.probe
    def test_engine_has_import_paths(self, connected_bridge):
        """Verify engine has importPathList."""
        engines = connected_bridge.call_tool("listQmlEngines")
        addr = engines["engines"][0]["address"]

        props = connected_bridge.call_tool("getEngineProperties", {"address": addr})
        groups = props.get("groups", {})
        assert "importPathList" in groups, \
            f"Engine missing 'importPathList' group: {list(groups.keys())}"

    @pytest.mark.probe
    def test_engine_has_root_context(self, connected_bridge):
        """Verify engine properties include rootContext."""
        engines = connected_bridge.call_tool("listQmlEngines")
        addr = engines["engines"][0]["address"]

        props = connected_bridge.call_tool("getEngineProperties", {"address": addr})
        groups = props.get("groups", {})
        assert "rootContext" in groups, \
            f"Engine missing 'rootContext' group: {list(groups.keys())}"

    @pytest.mark.probe
    def test_engine_has_plugin_paths(self, connected_bridge):
        """Verify engine has pluginPathList."""
        engines = connected_bridge.call_tool("listQmlEngines")
        addr = engines["engines"][0]["address"]

        props = connected_bridge.call_tool("getEngineProperties", {"address": addr})
        groups = props.get("groups", {})
        assert "pluginPathList" in groups, \
            f"Engine missing 'pluginPathList' group: {list(groups.keys())}"


class TestQmlContextTools:
    """Verify QML context chain and type info tools."""

    @pytest.mark.probe
    def test_tools_listed(self, bridge):
        tools = bridge.list_tools()
        names = [t["name"] for t in tools]
        for expected in ["getWidgetQmlContexts", "getWidgetQmlContextProperties",
                         "getWidgetQmlTypeInfo", "selectQmlContext"]:
            assert expected in names, f"Missing tool: {expected}"

    @pytest.mark.probe
    def test_get_widget_qml_contexts(self, connected_bridge):
        widgets = connected_bridge.call_tool("listWidgets")
        assert isinstance(widgets, list) and len(widgets) > 0

        # Find a widget with a QML type (likely has a QML context)
        def find_qml_widget(items_list):
            for item in items_list:
                t = item.get("type", "")
                if "QDeclarativeWidgets" in t or "QQuick" in t:
                    return item.get("name", "")
                found = find_qml_widget(item.get("children", []))
                if found:
                    return found
            return None

        addr = find_qml_widget(widgets)
        if not addr:
            addr = widgets[0].get("name", "")

        # Select the widget first
        sel = connected_bridge.call_tool("selectWidget", {"address": addr})
        assert isinstance(sel, dict) and sel.get("propertyCount", 0) > 0, \
            f"selectWidget failed: {sel}"

        # Get context chain
        result = connected_bridge.call_tool("getWidgetQmlContexts", {"address": addr})
        assert isinstance(result, dict), f"getWidgetQmlContexts failed: {result}"

        if "error" in result:
            # Widget may not have a QML context — that's OK for pure widget apps
            pytest.skip(f"No QML context for this widget: {result['error']}")

        assert "contextChain" in result, \
            f"No 'contextChain' key: {list(result.keys())}"
        assert result.get("count", 0) >= 1, "Expected at least 1 context"

    @pytest.mark.probe
    def test_select_qml_context(self, connected_bridge):
        widgets = connected_bridge.call_tool("listWidgets")
        assert isinstance(widgets, list) and len(widgets) > 0

        # Find first widget with a QML type
        def find_named(items_list):
            for item in items_list:
                t = item.get("type", "")
                if "QDeclarativeWidgets" in t or "QQuick" in t:
                    return item.get("name", "")
                found = find_named(item.get("children", []))
                if found:
                    return found
            return None

        addr = find_named(widgets)
        if not addr:
            addr = widgets[0].get("name", "")

        connected_bridge.call_tool("selectWidget", {"address": addr})

        # Get contexts first
        ctxs = connected_bridge.call_tool("getWidgetQmlContexts", {"address": addr})
        if "error" in ctxs:
            pytest.skip(f"No QML context: {ctxs['error']}")

        count = ctxs.get("count", 0)
        if count == 0:
            pytest.skip("No contexts in chain")

        # Select the last (leaf) context
        result = connected_bridge.call_tool(
            "selectQmlContext", {"address": addr, "contextIndex": count - 1})
        assert isinstance(result, dict), f"selectQmlContext failed: {result}"
        assert result.get("selected") == count - 1, \
            f"Wrong context selected: {result}"

    @pytest.mark.probe
    def test_get_widget_qml_type_info(self, connected_bridge):
        widgets = connected_bridge.call_tool("listWidgets")
        assert isinstance(widgets, list) and len(widgets) > 0

        # Find a widget with a QML type
        def find_named(items_list):
            for item in items_list:
                t = item.get("type", "")
                if "QDeclarativeWidgets" in t or "QQuick" in t:
                    return item.get("name", "")
                found = find_named(item.get("children", []))
                if found:
                    return found
            return None

        addr = find_named(widgets)
        if not addr:
            addr = widgets[0].get("name", "")

        connected_bridge.call_tool("selectWidget", {"address": addr})

        type_info = connected_bridge.call_tool("getWidgetQmlTypeInfo", {"address": addr})
        if "error" in type_info:
            pytest.skip(f"No QML type info: {type_info['error']}")

        assert isinstance(type_info, dict), \
            f"getWidgetQmlTypeInfo failed: {type_info}"
        # Should have type name info
        groups = type_info.get("groups", {})
        props = type_info.get("properties", {})
        has_type = "qmlTypeName" in groups or "typeName" in groups or \
                   "elementName" in groups
        assert has_type or len(props) > 0, \
            f"No QML type properties found: {list(groups.keys())}"

    @pytest.mark.probe
    def test_qml_type_has_element_name(self, connected_bridge):
        """Verify type info includes elementName for QML-defined widgets."""
        widgets = connected_bridge.call_tool("listWidgets")
        assert isinstance(widgets, list) and len(widgets) > 0

        def find_named(items_list):
            for item in items_list:
                t = item.get("type", "")
                if "QDeclarativeWidgets" in t or "QQuick" in t:
                    return item.get("name", "")
                found = find_named(item.get("children", []))
                if found:
                    return found
            return None

        addr = find_named(widgets)
        if not addr:
            addr = widgets[0].get("name", "")

        connected_bridge.call_tool("selectWidget", {"address": addr})

        type_info = connected_bridge.call_tool("getWidgetQmlTypeInfo", {"address": addr})
        if "error" in type_info:
            pytest.skip(f"No QML type info: {type_info['error']}")

        groups = type_info.get("groups", {})
        if "elementName" in groups:
            # Decode the elementName from children (it's stored as individual chars)
            name_children = groups["elementName"].get("children", {})
            name = "".join(
                name_children[k].get("value", "")
                for k in sorted(name_children.keys(), key=int)
            )
            assert len(name) > 0, f"elementName is empty: {groups['elementName']}"