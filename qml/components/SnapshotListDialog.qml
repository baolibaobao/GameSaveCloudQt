import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: control

    property var backend
    property string mode: "local"
    property string appId: ""
    property var snapshots: []
    property color panelColor: "#FFFFFF"
    property color textColor: "#172033"
    property color mutedTextColor: "#6B7588"
    property bool darkMode: false

    signal itemsRefreshed(var items)
    signal uploadAllRequested(string appId)
    signal downloadAllRequested(string appId)
    signal uploadSelected(string appId, string snapshotPathOrFileName)
    signal downloadSelected(string appId, string snapshotFileName)

    modal: true
    standardButtons: Dialog.Close

    function isCloudMode() {
        return control.mode === "download" || control.mode === "cloud"
    }

    onOpened: {
        if (!backend || control.appId.length === 0) {
            return
        }

        if (control.isCloudMode()) {
            control.itemsRefreshed(backend.cloudSnapshotsForGame(control.appId))
            backend.refreshCloudSnapshotsForGame(control.appId)
        } else {
            backend.refreshLocalSnapshotsForGame(control.appId)
            control.itemsRefreshed(backend.snapshotsForGame(control.appId))
        }
    }

    contentItem: Rectangle {
        color: control.panelColor
        implicitWidth: 680
        implicitHeight: 430

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8

            RowLayout {
                visible: control.mode === "upload" || control.mode === "download"
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? 42 : 0
                spacing: 10

                Text {
                    text: control.mode === "download"
                          ? "云端存档快照列表，可下载全部缺失快照，也可选择单个快照覆盖下载。"
                          : "本地存档快照列表，可上传全部本地快照，也可选择单个快照覆盖上传。"
                    color: control.mutedTextColor
                    font.pixelSize: 13
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Button {
                    Layout.preferredWidth: 126
                    Layout.preferredHeight: 32
                    text: control.mode === "download" ? "下载全部快照" : "上传全部快照"
                    font.pixelSize: 12

                    onClicked: {
                        if (control.mode === "download") {
                            control.downloadAllRequested(control.appId)
                        } else {
                            control.uploadAllRequested(control.appId)
                        }
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ListView {
                    anchors.fill: parent
                    spacing: 10
                    clip: true
                    model: control.snapshots

                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 92
                        radius: 12
                        color: control.darkMode ? "#222B3C" : "#F7F9FC"
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
                                    text: "创建时间 " + (modelData.createdAtUtc || "未知") + " · 文件数 " + (modelData.fileCount || "0") + " · 上传状态 " + (modelData.uploadState || "not_uploaded")
                                    color: control.mutedTextColor
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: control.isCloudMode()
                                          ? (modelData.remotePath || "")
                                          : (modelData.zipPath || "")
                                    color: control.mutedTextColor
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }

                            Button {
                                visible: control.mode !== "view"
                                Layout.preferredWidth: visible ? 96 : 0
                                Layout.preferredHeight: visible ? 30 : 0
                                text: control.isCloudMode() ? "可选下载" : "可选上传"
                                font.pixelSize: 12

                                onClicked: {
                                    if (control.isCloudMode()) {
                                        control.downloadSelected(control.appId, modelData.fileName || modelData.remotePath || "")
                                    } else {
                                        control.uploadSelected(control.appId, modelData.zipPath || modelData.fileName || "")
                                    }
                                }
                            }
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: control.snapshots.length === 0
                    text: control.isCloudMode() ? "暂无云端快照记录" : "暂无本地快照"
                    color: control.mutedTextColor
                    font.pixelSize: 15
                }
            }
        }
    }
}
