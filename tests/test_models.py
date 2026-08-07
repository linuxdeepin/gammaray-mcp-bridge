"""Tests for Qt model (QAbstractItemModel) inspection tools.

These exercise listQtModels, selectModel, getModelContent, getModelHeaders,
getModelCell and listSelectionModels against the model(s) present in the probe
target — a QQmlListModel in the QML test app, and a QStandardItemModel +
QListWidgetModel in the widget test app (--widget-app).
"""

import pytest


def _flatten(models, out=None):
    """Flatten the nested model list (proxies nested under their source)."""
    if out is None:
        out = []
    for m in models or []:
        out.append(m)
        _flatten(m.get("proxies", []), out)
    return out


class TestModelTools:
    """Verify the Qt model inspection toolset."""

    @pytest.mark.probe
    def test_tools_listed(self, bridge):
        tools = bridge.list_tools()
        names = [t["name"] for t in tools]
        for expected in ["listQtModels", "selectModel", "getModelContent",
                         "getModelHeaders", "getModelCell", "listSelectionModels"]:
            assert expected in names, f"Missing tool: {expected}"

    @pytest.mark.probe
    def test_list_models(self, connected_bridge):
        models = connected_bridge.call_tool("listQtModels")
        assert isinstance(models, dict), f"listQtModels failed: {models}"
        assert "error" not in models, f"listQtModels error: {models}"
        assert models.get("count", 0) >= 1, f"No models found: {models}"
        flat = _flatten(models.get("models", []))
        assert len(flat) >= 1, "No model entries after flattening"
        for m in flat:
            assert "address" in m, f"Model missing address: {m}"
            assert "type" in m, f"Model missing type: {m}"

    @pytest.mark.probe
    def test_select_model_and_content(self, connected_bridge):
        """Select a model and read its content, expecting a non-empty table."""
        models = connected_bridge.call_tool("listQtModels")
        flat = _flatten(models.get("models", []))

        selected = None
        content = None
        for m in flat:
            addr = m["address"]
            result = connected_bridge.call_tool("selectModel", {"address": addr})
            if not isinstance(result, dict) or "error" in result:
                continue
            if result.get("rows", 0) > 0 and result.get("columns", 0) > 0:
                selected = addr
                break

        if not selected:
            pytest.skip("No selectable model with rows/columns found")

        content = connected_bridge.call_tool(
            "getModelContent", {"address": selected, "maxRows": 10, "maxColumns": 5})
        assert isinstance(content, dict), f"getModelContent failed: {content}"
        assert "error" not in content, f"getModelContent error: {content}"
        assert content.get("totalRows", 0) >= 1
        data = content.get("data", [])
        assert isinstance(data, list) and len(data) >= 1, f"No content rows: {content}"
        assert isinstance(data[0], list) and len(data[0]) >= 1, f"Row not a list: {content}"

    @pytest.mark.probe
    def test_get_model_cell(self, connected_bridge):
        """Read every role of cell (0,0) for a populated model."""
        models = connected_bridge.call_tool("listQtModels")
        flat = _flatten(models.get("models", []))

        selected = None
        for m in flat:
            addr = m["address"]
            result = connected_bridge.call_tool("selectModel", {"address": addr})
            if (isinstance(result, dict) and "error" not in result
                    and result.get("rows", 0) > 0 and result.get("columns", 0) > 0):
                selected = addr
                break

        if not selected:
            pytest.skip("No selectable model found")

        cell = connected_bridge.call_tool(
            "getModelCell", {"address": selected, "row": 0, "column": 0})
        assert isinstance(cell, dict), f"getModelCell failed: {cell}"
        assert "error" not in cell, f"getModelCell error: {cell}"
        roles = cell.get("roles", [])
        assert isinstance(roles, list) and len(roles) >= 1, f"No roles returned: {cell}"
        role_names = [r.get("role") for r in roles]
        assert any("DisplayRole" in r for r in role_names), f"Missing DisplayRole: {role_names}"

    @pytest.mark.probe
    def test_get_model_headers(self, connected_bridge):
        """Read headers for a populated model."""
        models = connected_bridge.call_tool("listQtModels")
        flat = _flatten(models.get("models", []))

        selected = None
        for m in flat:
            addr = m["address"]
            result = connected_bridge.call_tool("selectModel", {"address": addr})
            if (isinstance(result, dict) and "error" not in result
                    and result.get("rows", 0) > 0):
                selected = addr
                break

        if not selected:
            pytest.skip("No selectable model found")

        headers = connected_bridge.call_tool(
            "getModelHeaders", {"address": selected, "maxSections": 20})
        assert isinstance(headers, dict), f"getModelHeaders failed: {headers}"
        assert "error" not in headers, f"getModelHeaders error: {headers}"
        assert isinstance(headers.get("horizontal"), list)
        assert isinstance(headers.get("vertical"), list)

    @pytest.mark.probe
    def test_get_model_cell_out_of_range(self, connected_bridge):
        """Cell access outside the model bounds must produce a clean error."""
        models = connected_bridge.call_tool("listQtModels")
        flat = _flatten(models.get("models", []))

        selected = None
        for m in flat:
            addr = m["address"]
            result = connected_bridge.call_tool("selectModel", {"address": addr})
            if (isinstance(result, dict) and "error" not in result
                    and result.get("rows", 0) > 0 and result.get("columns", 0) > 0):
                selected = addr
                break

        if not selected:
            pytest.skip("No selectable model found")

        result = connected_bridge.call_tool(
            "getModelCell", {"address": selected, "row": 9999, "column": 0})
        assert isinstance(result, dict)
        assert "error" in result, f"Expected an error for out-of-range cell: {result}"

    # ------------------------------------------------------------------
    # App-specific deterministic checks (skip when the model type is absent)
    # ------------------------------------------------------------------

    def _find_model_by_type(self, connected_bridge, type_name):
        models = connected_bridge.call_tool("listQtModels")
        if "error" in models:
            return None
        for m in _flatten(models.get("models", [])):
            if m.get("type") == type_name:
                return m["address"]
        return None

    @pytest.mark.probe
    def test_widget_model_content(self, connected_bridge):
        """The widget app's QStandardItemModel is 3x2 with known values."""
        addr = self._find_model_by_type(connected_bridge, "QStandardItemModel")
        if not addr:
            pytest.skip("QStandardItemModel not found (not running with --widget-app)")

        result = connected_bridge.call_tool("selectModel", {"address": addr})
        assert isinstance(result, dict) and "error" not in result, f"selectModel: {result}"
        assert result.get("rows") == 3
        assert result.get("columns") == 2

        content = connected_bridge.call_tool(
            "getModelContent", {"address": addr, "maxRows": 10, "maxColumns": 5})
        assert "error" not in content, f"getModelContent: {content}"
        first_col = [row[0] for row in content["data"]]
        assert first_col == ["Alpha", "Beta", "Gamma"], f"Unexpected content: {first_col}"

        headers = connected_bridge.call_tool(
            "getModelHeaders", {"address": addr, "maxSections": 10})
        assert "error" not in headers, f"getModelHeaders: {headers}"
        assert headers["horizontal"] == ["Name", "Value"], f"Unexpected headers: {headers}"

    @pytest.mark.probe
    def test_qml_list_model_content(self, connected_bridge):
        """The QML app's ListModel holds 4 items (Alpha..Delta)."""
        addr = self._find_model_by_type(connected_bridge, "QQmlListModel")
        if not addr:
            pytest.skip("QQmlListModel not found (not running with the QML app)")

        result = connected_bridge.call_tool("selectModel", {"address": addr})
        assert isinstance(result, dict) and "error" not in result, f"selectModel: {result}"
        assert result.get("rows") == 4

        content = connected_bridge.call_tool(
            "getModelContent", {"address": addr, "maxRows": 10, "maxColumns": 5})
        assert "error" not in content, f"getModelContent: {content}"
        names = [row[0] for row in content["data"]]
        assert names == ["Alpha", "Beta", "Gamma", "Delta"], f"Unexpected content: {names}"

    @pytest.mark.probe
    def test_list_selection_models(self, connected_bridge):
        """listSelectionModels must return a well-formed result for a model."""
        models = connected_bridge.call_tool("listQtModels")
        flat = _flatten(models.get("models", []))

        selected = None
        for m in flat:
            addr = m["address"]
            result = connected_bridge.call_tool("selectModel", {"address": addr})
            if (isinstance(result, dict) and "error" not in result
                    and result.get("rows", 0) > 0):
                selected = addr
                break

        if not selected:
            pytest.skip("No selectable model found")

        result = connected_bridge.call_tool(
            "listSelectionModels", {"address": selected})
        assert isinstance(result, dict), f"listSelectionModels failed: {result}"
        assert "error" not in result, f"listSelectionModels error: {result}"
        assert "selectionModels" in result
        assert isinstance(result.get("count", 0), int) and result["count"] >= 0
