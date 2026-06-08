import QtQuick
import QtQuick.Layouts

Item {
    id: control

    property string icon: "\uE8B7"
    property string title: "暂无内容"
    property string description: ""
    property string actionIcon: ""
    property string actionLabel: ""
    property bool actionEnabled: true
    property bool darkMode: false
    property color textColor: "#172033"
    property color mutedTextColor: "#6B7588"
    property color accentColor: "#0078D4"
    property int maxContentWidth: 400

    signal actionClicked()

    implicitWidth: maxContentWidth
    implicitHeight: 220

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, control.maxContentWidth)
        spacing: 10

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: control.icon
            color: control.darkMode ? "#45546A" : "#CBD5E1"
            font.family: "Segoe MDL2 Assets"
            font.pixelSize: 54
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: control.title
            color: control.textColor
            font.pixelSize: 18
            font.bold: true
        }

        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: control.description
            color: control.mutedTextColor
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }

        ActionButton {
            visible: control.actionLabel.length > 0
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Math.max(128, implicitWidth)
            Layout.preferredHeight: 40
            kind: "primary"
            icon: control.actionIcon
            label: control.actionLabel
            enabled: control.actionEnabled
            darkMode: control.darkMode
            accentColor: control.accentColor
            labelPixelSize: 13
            onClicked: control.actionClicked()
        }
    }
}
