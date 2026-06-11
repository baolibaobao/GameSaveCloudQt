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
    property string pendingRestoreSnapshot: ""
    property string pendingRestoreSnapshotName: ""
    property bool uploadInProgress: false
    property real uploadProgress: 0
    property string uploadStatus: ""
    property bool downloadInProgress: false
    property real downloadProgress: 0
    property string downloadStatus: ""

    signal refreshRequested
    signal uploadAllRequested
    signal downloadAllRequested
    signal openDirectoryRequested
    signal deleteRequested
    signal createSnapshotRequested
    signal uploadSelected(string snapshotPath)
    signal downloadSelected(string snapshotName)
    signal restoreSelected(string snapshotPath)

    Layout.fillWidth: true
    Layout.fillHeight: true

    function isCloudMode() {
        return control.mode === "cloud";
    }

    function isBackupMode() {
        return control.mode === "backup";
    }

    function isLocalMode() {
        return control.mode === "local";
    }

    function progressVisible() {
        return (control.isLocalMode() && (control.uploadInProgress || control.uploadStatus.length > 0))
                || (control.isCloudMode()
                    && (control.downloadInProgress
                        || control.downloadStatus.length > 0
                        || control.uploadInProgress
                        || control.uploadStatus.length > 0));
    }

    function progressText() {
        if (control.isCloudMode()) {
            if (control.downloadInProgress) {
                return control.downloadStatus;
            }
            if (control.uploadInProgress) {
                return control.uploadStatus;
            }
            return control.downloadStatus.length > 0 ? control.downloadStatus : control.uploadStatus;
        }
        return control.uploadStatus;
    }

    function progressValue() {
        if (control.isCloudMode()) {
            if (control.downloadInProgress) {
                return Math.max(0, Math.min(1, control.downloadProgress));
            }
            if (control.uploadInProgress) {
                return Math.max(0, Math.min(1, control.uploadProgress));
            }
            return Math.max(0, Math.min(1, control.downloadStatus.length > 0
                                        ? control.downloadProgress
                                        : control.uploadProgress));
        }
        return Math.max(0, Math.min(1, control.uploadProgress));
    }

    ConfirmDialog {
        id: restoreConfirmDialog

        dialogTitle: "确认恢复快照"
        message: control.isCloudMode() ? "将使用选中的云端 zip 快照覆盖游戏真实存档目录。" : (control.isBackupMode() ? "将使用恢复前自动备份回滚游戏真实存档目录。" : "将使用选中的本地 zip 快照覆盖游戏真实存档目录。")
        detail: (control.isCloudMode() ? "如果本地没有这个云端快照，软件会先下载；随后会自动备份当前存档，再执行覆盖。请确认游戏已经关闭。\n\n" : (control.isBackupMode() ? "执行前仍会再次备份当前存档。这个操作适合在误恢复后回到恢复前状态。请确认游戏已经关闭。\n\n" : "恢复会先自动备份当前存档，然后用选中的 zip 快照覆盖真实存档目录。请确认游戏已经关闭。\n\n")) + control.pendingRestoreSnapshotName
        confirmText: "确认恢复"
        cancelText: "取消"
        icon: "\uE777"
        danger: false
        darkMode: control.darkMode
        panelColor: control.cardColor
        textColor: control.textColor
        mutedTextColor: control.mutedTextColor
        accentColor: control.accentColor

        onConfirmRequested: control.restoreSelected(control.pendingRestoreSnapshot)
        onClosed: {
            control.pendingRestoreSnapshot = "";
            control.pendingRestoreSnapshotName = "";
        }
    }

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
                    label: control.isCloudMode() ? "刷新云端快照" : (control.isBackupMode() ? "刷新备份列表" : "刷新本地列表")
                    darkMode: control.darkMode
                    accentColor: control.accentColor
                    onClicked: control.refreshRequested()
                }

                ActionButton {
                    visible: control.isLocalMode()
                    width: 138
                    height: parent.height
                    kind: "primary"
                    icon: "\uE898"
                    label: control.uploadInProgress ? "上传中..." : "上传全部快照"
                    enabled: control.snapshots.length > 0 && !control.uploadInProgress
                    darkMode: control.darkMode
                    accentColor: control.accentColor
                    onClicked: control.uploadAllRequested()
                }

                ActionButton {
                    visible: control.isLocalMode() || control.isBackupMode()
                    width: 138
                    height: parent.height
                    kind: "ghost"
                    icon: "\uE8A5"
                    label: control.isBackupMode() ? "打开备份目录" : "打开快照目录"
                    darkMode: control.darkMode
                    accentColor: control.accentColor
                    onClicked: control.openDirectoryRequested()
                }

                ActionButton {
                    visible: control.isLocalMode() || control.isBackupMode()
                    width: 138
                    height: parent.height
                    kind: "danger"
                    icon: "\uE74D"
                    label: control.isBackupMode() ? "删除恢复备份" : "删除存档快照"
                    enabled: control.snapshots.length > 0
                    darkMode: control.darkMode
                    accentColor: control.accentColor
                    onClicked: control.deleteRequested()
                }

                ActionButton {
                    visible: control.isCloudMode()
                    width: 138
                    height: parent.height
                    kind: "primary"
                    icon: "\uE896"
                    label: control.downloadInProgress ? "下载中..." : "下载全部快照"
                    enabled: control.snapshots.length > 0 && !control.downloadInProgress
                    darkMode: control.darkMode
                    accentColor: control.accentColor
                    onClicked: control.downloadAllRequested()
                }
            }
        }

        Rectangle {
            visible: control.progressVisible()
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 58 : 0
            radius: 14
            color: control.darkMode ? "#172030" : "#F7F9FC"
            border.width: 1
            border.color: control.darkMode ? "#2A3448" : "#E8EDF5"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: control.progressText()
                    color: control.textColor
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 8
                    radius: 4
                    color: control.darkMode ? "#263244" : "#E2E8F0"
                    clip: true

                    Rectangle {
                        width: parent.width * control.progressValue()
                        height: parent.height
                        radius: 4
                        color: control.accentColor

                        Behavior on width {
                            NumberAnimation {
                                duration: 120
                                easing.type: Easing.OutCubic
                            }
                        }
                    }
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
                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }

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
                                text: control.mode === "cloud" ? (modelData.remotePath || "") : (control.isBackupMode() ? ((modelData.zipPath || "") + " · 恢复前备份") : ((modelData.zipPath || "") + " · " + (modelData.uploadState || "not_uploaded")))
                                color: control.mutedTextColor
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }

                        ActionButton {
                            visible: control.isLocalMode() || control.isCloudMode()
                            Layout.preferredWidth: 104
                            Layout.preferredHeight: 36
                            kind: "ghost"
                            icon: control.isCloudMode() ? "\uE896" : "\uE898"
                            label: control.isCloudMode() ? (control.downloadInProgress ? "下载中..." : "可选下载") : (control.uploadInProgress ? "上传中..." : "可选上传")
                            enabled: control.isCloudMode() ? !control.downloadInProgress : !control.uploadInProgress
                            darkMode: control.darkMode
                            accentColor: control.accentColor
                            labelPixelSize: 12
                            onClicked: {
                                if (control.isCloudMode()) {
                                    control.downloadSelected(modelData.fileName || modelData.remotePath || "");
                                } else {
                                    control.uploadSelected(modelData.zipPath || modelData.fileName || "");
                                }
                            }
                        }

                        ActionButton {
                            visible: control.isLocalMode() || control.isCloudMode() || control.isBackupMode()
                            Layout.preferredWidth: visible ? 82 : 0
                            Layout.preferredHeight: visible ? 36 : 0
                            kind: "ghost"
                            icon: "\uE777"
                            label: "恢复"
                            darkMode: control.darkMode
                            accentColor: control.accentColor
                            labelPixelSize: 12
                            onClicked: {
                                control.pendingRestoreSnapshot = modelData.zipPath || modelData.fileName || "";
                                control.pendingRestoreSnapshotName = modelData.fileName || control.pendingRestoreSnapshot;
                                restoreConfirmDialog.open();
                            }
                        }
                    }
                }
            }

            EmptyState {
                anchors.centerIn: parent
                visible: control.snapshots.length === 0
                icon: control.isCloudMode() ? "\uE753" : (control.isBackupMode() ? "\uE81C" : "\uE8B7")
                title: control.isCloudMode() ? "暂无云端存档快照" : (control.isBackupMode() ? "暂无恢复前备份" : "暂无本地存档快照")
                description: control.isCloudMode() ? (control.localSnapshotCount > 0 ? "云端还没有这个游戏的备份，可以先把本地快照上传到网盘。" : "当前没有可下载的云端快照，也还没有本地快照可上传。") : (control.isBackupMode() ? "恢复前备份会在每次恢复快照前自动创建，用于在误恢复时回到恢复前状态。" : (control.savePathCanSync ? "这里会保存每一次压缩后的存档版本，先创建一个快照就能开始回档。" : "当前游戏还没有可同步的存档目录，先在设置中指定存档位置。"))
                actionIcon: control.isCloudMode() ? "\uE898" : (control.isBackupMode() ? "\uE8A5" : "\uE710")
                actionLabel: control.isCloudMode() ? "上传全部快照" : (control.isBackupMode() ? "打开备份目录" : "创建快照")
                actionEnabled: control.isCloudMode() ? (control.localSnapshotCount > 0 && !control.uploadInProgress) : (control.isBackupMode() ? true : control.savePathCanSync)
                darkMode: control.darkMode
                textColor: control.textColor
                mutedTextColor: control.mutedTextColor
                accentColor: control.accentColor
                onActionClicked: {
                    if (control.isCloudMode()) {
                        control.uploadAllRequested();
                    } else if (control.isBackupMode()) {
                        control.openDirectoryRequested();
                    } else {
                        control.createSnapshotRequested();
                    }
                }
            }
        }
    }
}
