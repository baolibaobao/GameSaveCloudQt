import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: control

    property string mode: "local"
    property var snapshots: []
    property int localSnapshotCount: 0
    property bool savePathCanSync: false
    property bool darkMode: false
    property color cardColor: "#FFFFFF"
    property color textColor: "#172033"
    property color mutedTextColor: "#6B7588"
    property color accentColor: "#0078D4"

    signal refreshRequested()
    signal uploadAllRequested()
    signal downloadAllRequested()
    signal openDirectoryRequested()
    signal deleteRequested()
    signal createSnapshotRequested()
    signal uploadSelected(string snapshotPath)
    signal downloadSelected(string snapshotName)

    Layout.fillWidth: true
    Layout.fillHeight: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 44

            Row {
                anchors.fill: parent
                spacing: 10

                ActionButton {
                    width: 138
                    height: parent.height
                    kind: "ghost"
                    icon: "\uE72C"
                    label: control.mode === "cloud" ? "刷新云端快照" : "刷新本地列表"
                    darkMode: control.darkMode
                    accentColor: control.accentColor
                    onClicked: control.refreshRequested()
                }

                ActionButton {
                    visible: control.mode === "local"
                    width: 138
                    height: parent.height
                    kind: "primary"
                    icon: "\uE898"
                    label: "上传全部快照"
                    enabled: control.snapshots.length > 0
                    darkMode: control.darkMode
                    accentColor: control.accentColor
                    onClicked: control.uploadAllRequested()
                }

                ActionButton {
                    visible: control.mode === "local"
                    width: 138
                    height: parent.height
                    kind: "ghost"
                    icon: "\uE8A5"
                    label: "打开快照目录"
                    darkMode: control.darkMode
                    accentColor: control.accentColor
                    onClicked: control.openDirectoryRequested()
                }

                ActionButton {
                    visible: control.mode === "local"
                    width: 138
                    height: parent.height
                    kind: "danger"
                    icon: "\uE74D"
                    label: "删除存档快照"
                    enabled: control.snapshots.length > 0
                    darkMode: control.darkMode
                    accentColor: control.accentColor
                    onClicked: control.deleteRequested()
                }

                ActionButton {
                    visible: control.mode === "cloud"
                    width: 138
                    height: parent.height
                    kind: "primary"
                    icon: "\uE896"
                    label: "下载全部快照"
                    enabled: control.snapshots.length > 0
                    darkMode: control.darkMode
                    accentColor: control.accentColor
                    onClicked: control.downloadAllRequested()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 16
            color: control.darkMode ? "#172030" : "#F7F9FC"
            border.width: 1
            border.color: control.darkMode ? "#2A3448" : "#E8EDF5"
            clip: true

            ListView {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8
                clip: true
                visible: control.snapshots.length > 0
                model: control.snapshots
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    width: ListView.view.width
                    height: 82
                    radius: 14
                    color: control.cardColor
                    border.width: 1
                    border.color: control.darkMode ? "#313C50" : "#E6EBF2"

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Text {
                                text: modelData.fileName || "未知快照"
                                color: control.textColor
                                font.pixelSize: 14
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Text {
                                text: control.mode === "cloud"
                                      ? (modelData.remotePath || "")
                                      : ((modelData.zipPath || "") + " · " + (modelData.uploadState || "not_uploaded"))
                                color: control.mutedTextColor
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }

                        ActionButton {
                            Layout.preferredWidth: 104
                            Layout.preferredHeight: 36
                            kind: "ghost"
                            icon: control.mode === "cloud" ? "\uE896" : "\uE898"
                            label: control.mode === "cloud" ? "可选下载" : "可选上传"
                            darkMode: control.darkMode
                            accentColor: control.accentColor
                            labelPixelSize: 12
                            onClicked: {
                                if (control.mode === "cloud") {
                                    control.downloadSelected(modelData.fileName || modelData.remotePath || "")
                                } else {
                                    control.uploadSelected(modelData.zipPath || modelData.fileName || "")
                                }
                            }
                        }
                    }
                }
            }

            EmptyState {
                anchors.centerIn: parent
                visible: control.snapshots.length === 0
                icon: control.mode === "cloud" ? "\uE753" : "\uE8B7"
                title: control.mode === "cloud" ? "暂无云端存档快照" : "暂无本地存档快照"
                description: control.mode === "cloud"
                             ? (control.localSnapshotCount > 0
                                ? "云端还没有这个游戏的备份，可以先把本地快照上传到网盘。"
                                : "当前没有可下载的云端快照，也还没有本地快照可上传。")
                             : (control.savePathCanSync
                                ? "这里会保存每一次压缩后的存档版本，先创建一个快照就能开始回档。"
                                : "当前游戏还没有可同步的存档目录，先在设置中指定存档位置。")
                actionIcon: control.mode === "cloud" ? "\uE898" : "\uE710"
                actionLabel: control.mode === "cloud" ? "上传全部快照" : "创建快照"
                actionEnabled: control.mode === "cloud" ? control.localSnapshotCount > 0 : control.savePathCanSync
                darkMode: control.darkMode
                textColor: control.textColor
                mutedTextColor: control.mutedTextColor
                accentColor: control.accentColor
                onActionClicked: {
                    if (control.mode === "cloud") {
                        control.uploadAllRequested()
                    } else {
                        control.createSnapshotRequested()
                    }
                }
            }
        }
    }
}
