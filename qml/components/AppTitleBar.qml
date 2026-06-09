import QtQuick
import QtQuick.Layouts
import QtQuick.Window

Rectangle {
    id: control

    property Window targetWindow
    property bool darkMode: false
    property color backgroundColor: "#F5F7FB"
    property color textColor: "#172033"
    property color mutedTextColor: "#6B7588"
    property color accentColor: "#0078D4"
    property string fontFamily: "Segoe UI Variable"
    property string appTitle: "GameSaveCloud-Qt"
    property int cornerRadius: 0

    implicitHeight: 38
    radius: cornerRadius
    color: backgroundColor

    function toggleMaximized() {
        if (!control.targetWindow) {
            return
        }
        if (control.targetWindow.visibility === Window.Maximized) {
            control.targetWindow.showNormal()
        } else {
            control.targetWindow.showMaximized()
        }
    }

    MouseArea {
        anchors.fill: parent
        anchors.leftMargin: 140
        anchors.rightMargin: 138
        acceptedButtons: Qt.LeftButton
        onPressed: {
            if (control.targetWindow) {
                control.targetWindow.startSystemMove()
            }
        }
        onDoubleClicked: control.toggleMaximized()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        spacing: 8

        Rectangle {
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
            radius: 5
            color: control.darkMode ? "#1E293B" : "#EAF4FF"
            border.width: 1
            border.color: control.darkMode ? "#334155" : "#CFE5FA"

            Text {
                anchors.centerIn: parent
                text: "\uE8B7"
                color: control.accentColor
                font.family: "Segoe MDL2 Assets"
                font.pixelSize: 10
            }
        }

        Text {
            text: control.appTitle
            color: control.textColor
            font.family: control.fontFamily
            font.pixelSize: 12
            font.bold: true
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        WindowButton {
            iconText: "\uE921"
            tooltipText: "最小化"
            darkMode: control.darkMode
            textColor: control.textColor
            hoveredColor: control.darkMode ? "#263247" : "#E8F2FC"
            onClicked: {
                if (control.targetWindow) {
                    control.targetWindow.showMinimized()
                }
            }
        }

        WindowButton {
            iconText: control.targetWindow && control.targetWindow.visibility === Window.Maximized ? "\uE923" : "\uE922"
            tooltipText: control.targetWindow && control.targetWindow.visibility === Window.Maximized ? "还原" : "最大化"
            darkMode: control.darkMode
            textColor: control.textColor
            hoveredColor: control.darkMode ? "#263247" : "#E8F2FC"
            onClicked: control.toggleMaximized()
        }

        WindowButton {
            iconText: "\uE8BB"
            tooltipText: "关闭"
            darkMode: control.darkMode
            textColor: control.textColor
            hoveredColor: "#E81123"
            hoveredTextColor: "#FFFFFF"
            onClicked: {
                if (control.targetWindow) {
                    control.targetWindow.close()
                }
            }
        }
    }

    component WindowButton: Rectangle {
        id: button

        property string iconText: ""
        property string tooltipText: ""
        property bool darkMode: false
        property color textColor: "#172033"
        property color hoveredColor: "#E8F2FC"
        property color hoveredTextColor: textColor

        signal clicked()

        Layout.preferredWidth: 46
        Layout.fillHeight: true
        color: buttonArea.containsMouse ? hoveredColor : "transparent"

        Text {
            anchors.centerIn: parent
            text: button.iconText
            color: buttonArea.containsMouse ? button.hoveredTextColor : button.textColor
            font.family: "Segoe MDL2 Assets"
            font.pixelSize: 10
        }

        MouseArea {
            id: buttonArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: button.clicked()
        }

        Behavior on color {
            ColorAnimation { duration: 100 }
        }
    }
}
