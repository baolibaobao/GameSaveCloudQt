import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: control

    property string appId: ""
    property string gameName: ""
    property color panelColor: "#FFFFFF"
    property color textColor: "#172033"
    property color mutedTextColor: "#6B7588"

    signal confirmed(string appId)
    signal cleared()

    modal: true
    title: "删除存档快照"
    standardButtons: Dialog.Cancel | Dialog.Ok

    onAccepted: {
        if (control.appId.length > 0) {
            control.confirmed(control.appId)
        }
        control.cleared()
    }

    onRejected: control.cleared()

    contentItem: Rectangle {
        color: control.panelColor
        implicitWidth: 480
        implicitHeight: 180

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 10

            Text {
                text: "确认删除 “" + control.gameName + "” 的本地存档快照？"
                color: control.textColor
                font.pixelSize: 15
                font.bold: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Text {
                text: "这只会删除软件在本地快照目录中生成的 zip 快照和索引文件，不会删除游戏真实存档，也不会删除夸克网盘中的云端快照。恢复前自动备份属于后续恢复阶段的保护备份，不在这里清理。"
                color: control.mutedTextColor
                font.pixelSize: 13
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }
}
