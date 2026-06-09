import QtQuick
import QtQuick.Layouts
import QtQuick.Effects

Rectangle {
    id: control

    property string kind: "ghost"
    property string icon: ""
    property string label: ""
    property bool darkMode: false
    property color accentColor: "#0078D4"
    property int buttonRadius: 14
    property int labelPixelSize: 13
    property bool boldLabel: true

    signal clicked()

    implicitWidth: Math.max(96, buttonContent.implicitWidth + 28)
    implicitHeight: 44
    radius: buttonRadius
    opacity: enabled ? 1.0 : 0.45
    color: buttonBackground(kind, buttonMouseArea.containsMouse && enabled)
    border.width: kind === "primary" ? 0 : 1
    border.color: buttonBorder(kind, buttonMouseArea.containsMouse && enabled)
    layer.enabled: darkMode && enabled && kind === "primary"
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowBlur: 0.38
        shadowOpacity: buttonMouseArea.containsMouse ? 0.28 : 0.15
        shadowVerticalOffset: 4
        shadowColor: "#553B82F6"
    }

    function buttonBackground(buttonKind, hovered) {
        if (buttonKind === "primary") {
            return hovered ? (darkMode ? "#1D4ED8" : "#006CBE") : (darkMode ? "#2563EB" : accentColor)
        }
        if (buttonKind === "danger") {
            return hovered
                    ? (darkMode ? "#3A2029" : "#FEF2F2")
                    : (darkMode ? "#222B3C" : "#FFFFFF")
        }
        return hovered
                ? (darkMode ? "#29384F" : "#E7F3FF")
                : (darkMode ? "#1E293B" : "#F1F7FD")
    }

    function buttonBorder(buttonKind, hovered) {
        if (buttonKind === "primary") {
            return hovered ? "#0062AD" : accentColor
        }
        if (buttonKind === "danger") {
            return hovered ? "#EF4444" : (darkMode ? "#4B2630" : "#F3C6C6")
        }
        return hovered ? accentColor : (darkMode ? "#334155" : "#D8E4F2")
    }

    function buttonTextColor(buttonKind, hovered) {
        if (buttonKind === "primary") {
            return "#FFFFFF"
        }
        if (buttonKind === "danger") {
            return hovered ? "#DC2626" : (darkMode ? "#FCA5A5" : "#B91C1C")
        }
        return accentColor
    }

    RowLayout {
        id: buttonContent
        anchors.centerIn: parent
        spacing: 7

        Text {
            visible: control.icon.length > 0
            text: control.icon
            color: control.buttonTextColor(control.kind, buttonMouseArea.containsMouse && control.enabled)
            font.family: "Segoe MDL2 Assets"
            font.pixelSize: Math.max(12, control.labelPixelSize)
        }

        Text {
            text: control.label
            color: control.buttonTextColor(control.kind, buttonMouseArea.containsMouse && control.enabled)
            font.pixelSize: control.labelPixelSize
            font.bold: control.boldLabel
        }
    }

    MouseArea {
        id: buttonMouseArea
        anchors.fill: parent
        enabled: control.enabled
        hoverEnabled: true
        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: control.clicked()
    }
}
