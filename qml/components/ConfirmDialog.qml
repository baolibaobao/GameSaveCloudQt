import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts

Dialog {
    id: control

    property string dialogTitle: "确认操作"
    property string message: ""
    property string detail: ""
    property string confirmText: "确定"
    property string cancelText: "取消"
    property string icon: "\uE9CE"
    property bool danger: false
    property bool showCancelButton: true
    property bool darkMode: false
    property color panelColor: darkMode ? "#1E293B" : "#FFFFFF"
    property color textColor: darkMode ? "#DDE7F3" : "#172033"
    property color mutedTextColor: darkMode ? "#8EA0B8" : "#6B7588"
    property color accentColor: "#0078D4"

    signal confirmRequested
    signal cancelRequested

    parent: Overlay.overlay
    modal: true
    focus: true
    dim: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    width: Math.min(500, parent ? parent.width - 64 : 500)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0

    Overlay.modal: Rectangle {
        color: control.darkMode ? "#B0060B14" : "#990F172A"
    }

    background: Rectangle {
        radius: 22
        color: control.panelColor
        border.width: 1
        border.color: control.darkMode ? "#334155" : "#E2E8F0"
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowBlur: 0.7
            shadowOpacity: control.darkMode ? 0.36 : 0.18
            shadowVerticalOffset: 18
            shadowColor: control.darkMode ? "#AA000000" : "#330B1B33"
        }
    }

    contentItem: ColumnLayout {
        id: dialogContent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 22
            Layout.rightMargin: 22
            Layout.topMargin: 20
            Layout.bottomMargin: 14
            spacing: 12

            Rectangle {
                Layout.preferredWidth: 42
                Layout.preferredHeight: 42
                radius: 14
                color: control.danger ? (control.darkMode ? "#3A2029" : "#FEF2F2") : (control.darkMode ? "#243B57" : "#E7F3FF")
                border.width: 1
                border.color: control.danger ? (control.darkMode ? "#63313D" : "#FECACA") : (control.darkMode ? "#315A82" : "#B9DAF7")

                Text {
                    anchors.centerIn: parent
                    text: control.icon
                    color: control.danger ? "#DC2626" : control.accentColor
                    font.family: "Segoe MDL2 Assets"
                    font.pixelSize: 20
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    Layout.fillWidth: true
                    text: control.dialogTitle
                    color: control.textColor
                    font.pixelSize: 17
                    font.bold: true
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    text: control.danger ? "请确认这个操作符合你的预期" : "继续前请确认下面的信息"
                    color: control.mutedTextColor
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 22
            Layout.rightMargin: 22
            Layout.preferredHeight: 1
            color: control.darkMode ? "#2A3448" : "#EEF2F7"
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 22
            Layout.rightMargin: 22
            Layout.topMargin: 18
            Layout.bottomMargin: 18
            spacing: 14

            Text {
                Layout.fillWidth: true
                text: control.message
                color: control.textColor
                font.pixelSize: 14
                font.bold: true
                wrapMode: Text.WordWrap
            }

            Rectangle {
                visible: control.detail.length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(46, detailText.implicitHeight + 24)
                radius: 14
                color: control.darkMode ? "#162033" : "#F8FAFC"
                border.width: 1
                border.color: control.darkMode ? "#2A3448" : "#E8EDF5"

                Text {
                    id: detailText
                    anchors.fill: parent
                    anchors.margins: 12
                    text: control.detail
                    color: control.mutedTextColor
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 72
            radius: 22
            color: control.darkMode ? "#182132" : "#F8FAFC"
            border.width: 1
            border.color: control.darkMode ? "#2A3448" : "#E8EDF5"

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: parent.radius
                color: parent.color
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 22
                anchors.rightMargin: 22
                spacing: 10

                Item {
                    Layout.fillWidth: true
                }

                ActionButton {
                    visible: control.showCancelButton
                    Layout.preferredWidth: visible ? 112 : 0
                    Layout.preferredHeight: visible ? 42 : 0
                    kind: "ghost"
                    icon: "\uE711"
                    label: control.cancelText
                    darkMode: control.darkMode
                    accentColor: control.accentColor
                    onClicked: {
                        control.cancelRequested();
                        control.close();
                    }
                }

                ActionButton {
                    Layout.preferredWidth: 128
                    Layout.preferredHeight: 42
                    kind: control.danger ? "danger" : "primary"
                    icon: control.danger ? "\uE74D" : "\uE8FB"
                    label: control.confirmText
                    darkMode: control.darkMode
                    accentColor: control.accentColor
                    onClicked: {
                        control.confirmRequested();
                        control.close();
                    }
                }
            }
        }
    }
}
