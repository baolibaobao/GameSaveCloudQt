import QtQuick
import QtQuick.Layouts
import QtQuick.Effects

Rectangle {
    id: control

    property bool darkMode: false
    property color textColor: "#172033"
    property color mutedTextColor: "#6B7588"
    property color accentColor: "#0078D4"
    property string fontFamily: "Segoe UI Variable"

    signal toggled(bool darkMode)

    implicitHeight: 46
    radius: 16
    color: toggleArea.containsMouse
           ? (darkMode ? "#243148" : "#F1F7FD")
           : "transparent"

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 8
        spacing: 10

        Rectangle {
            Layout.preferredWidth: 30
            Layout.preferredHeight: 30
            radius: 15
            color: control.darkMode ? "#2B3B55" : "#E8F2FF"
            border.width: 1
            border.color: control.darkMode ? "#3A4D6D" : "#CFE5FA"

            Text {
                anchors.centerIn: parent
                text: control.darkMode ? "☾" : "☀"
                color: control.darkMode ? "#BFDBFE" : "#F59E0B"
            font.pixelSize: control.darkMode ? 18 : 15
            font.bold: true
            font.family: control.fontFamily
        }
    }

        Text {
            Layout.fillWidth: true
            text: control.darkMode ? "深色模式" : "浅色模式"
            color: control.textColor
            font.family: control.fontFamily
            font.pixelSize: 15
            font.bold: true
            elide: Text.ElideRight
        }

        Rectangle {
            id: track
            Layout.preferredWidth: 52
            Layout.preferredHeight: 28
            radius: 14
            color: control.darkMode ? "#1D4ED8" : "#E4EBF4"
            border.width: 1
            border.color: control.darkMode ? "#3B82F6" : "#CBD5E1"
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowBlur: 0.32
                shadowOpacity: control.darkMode ? 0.25 : 0.10
                shadowVerticalOffset: 2
                shadowColor: control.darkMode ? "#553B82F6" : "#220B1B33"
            }

            Rectangle {
                width: 22
                height: 22
                radius: 11
                x: control.darkMode ? track.width - width - 3 : 3
                y: 3
                color: "#FFFFFF"

                Behavior on x {
                    NumberAnimation {
                        duration: 180
                        easing.type: Easing.OutCubic
                    }
                }
            }
        }
    }

    MouseArea {
        id: toggleArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: control.toggled(!control.darkMode)
    }
}
