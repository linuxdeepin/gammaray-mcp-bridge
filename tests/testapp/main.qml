import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    visible: true
    width: 600
    height: 500
    title: "GammaRay MCP Test App"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 5

        // Row of buttons at the top
        RowLayout {
            spacing: 6
            Button { text: "Add Item"; onClicked: listModel.append({ name: "Item " + (listModel.count + 1) }) }
            Button { text: "Remove Item"; onClicked: { if (listModel.count > 0) listModel.remove(listModel.count - 1) } }
            Button { text: "Force Layout"; onClicked: listView.forceLayout() }
        }

        // Switch to toggle visibility
        RowLayout {
            spacing: 6
            Label { text: "Show ListView:" }
            Switch {
                id: visibilitySwitch
                checked: true
                onCheckedChanged: listView.visible = checked
            }
            Label { text: "Opacity:" }
            Slider {
                id: opacitySlider
                from: 0.0; to: 1.0; value: 1.0
                onValueChanged: listView.opacity = value
            }
        }

        // ListView with delegate items
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#f0f0f0"
            radius: 4
            border.color: "#ccc"
            border.width: 1

            ListView {
                id: listView
                anchors.fill: parent
                anchors.margins: 4
                model: ListModel {
                    id: listModel
                    ListElement { name: "Alpha" }
                    ListElement { name: "Beta" }
                    ListElement { name: "Gamma" }
                    ListElement { name: "Delta" }
                }
                delegate: Rectangle {
                    width: parent.width
                    height: 32
                    color: index % 2 === 0 ? "#ffffff" : "#f5f5f5"
                    border.color: "#ddd"
                    border.width: 1
                    radius: 2

                    Text {
                        anchors.centerIn: parent
                        text: model.name
                        font.pixelSize: 14
                    }
                }
            }
        }

        // Bottom status bar
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            color: "#e0e0e0"
            radius: 2

            Text {
                anchors.centerIn: parent
                text: "Items: " + listModel.count + "  |  Visible: " + listView.visible
                font.pixelSize: 11
                color: "#666"
            }
        }
    }
}