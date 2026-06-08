import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Effects
import QtQuick.Layouts
import "components"

ApplicationWindow {
    id: root
    width: 1180
    height: 720
    minimumWidth: 920
    minimumHeight: 560
    visible: true
    title: "GameSaveCloud-Qt"
    color: darkMode ? "#121722" : "#F5F7FB"
    font.family: "Segoe UI Variable"

    property bool darkMode: false
    readonly property string appFontFamily: "Segoe UI Variable"
    readonly property color backgroundColor: darkMode ? "#121722" : "#F5F7FB"
    readonly property color panelColor: darkMode ? "#1C2433" : "#FFFFFF"
    readonly property color cardColor: darkMode ? "#222B3C" : "#FFFFFF"
    readonly property color textColor: darkMode ? "#EDF2F7" : "#172033"
    readonly property color mutedTextColor: darkMode ? "#9CA8BA" : "#6B7588"
    readonly property color accentColor: "#0078D4"
    readonly property int sidebarWidth: 248
    property string pendingManualAppId: ""
    property var snapshotDialogItems: []
    property string snapshotDialogTitle: "本地快照"
    property string snapshotDialogMode: "local"
    property string snapshotDialogAppId: ""
    property string pendingDeleteSnapshotAppId: ""
    property string pendingDeleteSnapshotGameName: ""
    property int selectedGameIndex: -1
    property string selectedGameAppId: ""
    property var selectedGame: ({})
    property int gameDetailTab: 0
    property var detailLocalSnapshots: []
    property var detailCloudSnapshots: []
    property int gameStatsRevision: 0
    property int currentPage: 0
    function selectedGameValue(key, fallback) {
        if (!root.selectedGame || root.selectedGame[key] === undefined || root.selectedGame[key] === null) {
            return fallback
        }
        if (typeof root.selectedGame[key] === "string" && root.selectedGame[key].length === 0) {
            return fallback
        }
        return root.selectedGame[key]
    }
    function runningBrief(status) {
        return status && status.indexOf("正在运行") >= 0 ? "运行中" : "未运行"
    }
    function runningBadgeBackground(status) {
        return root.runningBrief(status) === "运行中"
                ? (root.darkMode ? "#243F33" : "#DCFCE7")
                : (root.darkMode ? "#263244" : "#E8EEF7")
    }
    function runningBadgeTextColor(status) {
        return root.runningBrief(status) === "运行中" ? "#16A34A" : "#64748B"
    }
    function saveBadgeText(canSync, status) {
        if (canSync) {
            return "存档可同步"
        }
        if (status === "需用户手动指定") {
            return "需手动指定"
        }
        return status && status.length > 0 ? status : "等待识别"
    }
    function saveBadgeBackground(canSync, status) {
        if (canSync) {
            return root.darkMode ? "#243F33" : "#DCFCE7"
        }
        if (status === "需用户手动指定") {
            return root.darkMode ? "#4A341D" : "#FEF3C7"
        }
        return root.darkMode ? "#263244" : "#E8EEF7"
    }
    function saveBadgeTextColor(canSync, status) {
        if (canSync) {
            return "#16A34A"
        }
        if (status === "需用户手动指定") {
            return "#D97706"
        }
        return "#64748B"
    }
    function countBadgeBackground(kind) {
        if (kind === "cloud") {
            return root.darkMode ? "#CC16384A" : "#E6E0F2FE"
        }
        return root.darkMode ? "#CC243449" : "#E6EEF2FF"
    }
    function countBadgeTextColor(kind) {
        return kind === "cloud" ? "#0284C7" : "#4F46E5"
    }
    function statBackground(key, value) {
        if (key === "running" && value <= 0) {
            return root.darkMode ? "#33334155" : "#66F8FAFC"
        }
        if (key === "running") {
            return root.darkMode ? "#4022C55E" : "#DDF0FDF4"
        }
        if (key === "syncable") {
            return root.darkMode ? "#4010B981" : "#DDECFEF6"
        }
        if (key === "manual") {
            return root.darkMode ? "#40D97706" : "#DDFEF7E7"
        }
        return root.darkMode ? "#33334155" : "#DDF8FAFC"
    }
    function statBorderColor(key, value) {
        if (key === "running" && value <= 0) {
            return root.darkMode ? "#334155" : "#E2E8F0"
        }
        if (key === "running") {
            return root.darkMode ? "#2F7D4A" : "#BBF7D0"
        }
        if (key === "syncable") {
            return root.darkMode ? "#23735F" : "#A7F3D0"
        }
        if (key === "manual") {
            return root.darkMode ? "#8B5B18" : "#FED7AA"
        }
        return root.darkMode ? "#334155" : "#E2E8F0"
    }
    function actionButtonBackground(kind, hovered) {
        if (kind === "primary") {
            return hovered ? "#006CBE" : root.accentColor
        }
        if (kind === "danger") {
            return hovered
                    ? (root.darkMode ? "#3A2029" : "#FEF2F2")
                    : (root.darkMode ? "#222B3C" : "#FFFFFF")
        }
        return hovered ? (root.darkMode ? "#2A3448" : "#E7F3FF") : (root.darkMode ? "#222B3C" : "#F1F7FD")
    }
    function actionButtonBorder(kind, hovered) {
        if (kind === "primary") {
            return hovered ? "#0062AD" : root.accentColor
        }
        if (kind === "danger") {
            return hovered ? "#EF4444" : (root.darkMode ? "#4B2630" : "#F3C6C6")
        }
        return hovered ? root.accentColor : (root.darkMode ? "#334155" : "#D8E4F2")
    }
    function actionButtonTextColor(kind, hovered) {
        if (kind === "primary") {
            return "#FFFFFF"
        }
        if (kind === "danger") {
            return hovered ? "#DC2626" : (root.darkMode ? "#FCA5A5" : "#B91C1C")
        }
        return root.accentColor
    }
    function runningGameCount() {
        var count = 0
        for (var i = 0; i < steamManager.gameModel.count; ++i) {
            var game = steamManager.gameModel.get(i)
            if (root.runningBrief(game.runningStatus) === "运行中") {
                ++count
            }
        }
        return count
    }
    function syncableGameCount() {
        var count = 0
        for (var i = 0; i < steamManager.gameModel.count; ++i) {
            var game = steamManager.gameModel.get(i)
            if (game.savePathCanSync) {
                ++count
            }
        }
        return count
    }
    function manualGameCount() {
        var count = 0
        for (var i = 0; i < steamManager.gameModel.count; ++i) {
            var game = steamManager.gameModel.get(i)
            if (game.savePathStatus === "需用户手动指定") {
                ++count
            }
        }
        return count
    }
    function refreshDetailSnapshots() {
        if (root.selectedGameAppId.length === 0) {
            root.detailLocalSnapshots = []
            root.detailCloudSnapshots = []
            return
        }
        root.detailLocalSnapshots = steamManager.snapshotsForGame(root.selectedGameAppId)
        root.detailCloudSnapshots = steamManager.cloudSnapshotsForGame(root.selectedGameAppId)
    }
    function refreshSelectedLocalSnapshots() {
        if (root.selectedGameAppId.length === 0) {
            return
        }
        steamManager.refreshLocalSnapshotsForGame(root.selectedGameAppId)
        root.detailLocalSnapshots = steamManager.snapshotsForGame(root.selectedGameAppId)
    }
    function refreshSelectedCloudSnapshots() {
        if (root.selectedGameAppId.length === 0) {
            return
        }
        steamManager.refreshCloudSnapshotsForGame(root.selectedGameAppId)
        root.detailCloudSnapshots = steamManager.cloudSnapshotsForGame(root.selectedGameAppId)
    }
    function refreshSelectedDetailData() {
        if (root.selectedGameAppId.length === 0) {
            root.refreshDetailSnapshots()
            return
        }
        root.refreshSelectedLocalSnapshots()
        root.refreshSelectedCloudSnapshots()
        root.refreshDetailSnapshots()
    }
    function switchDetailTab(index) {
        root.gameDetailTab = index
        if (index === 1) {
            root.refreshSelectedLocalSnapshots()
        } else if (index === 2) {
            root.refreshSelectedCloudSnapshots()
        }
        root.refreshDetailSnapshots()
    }
    function syncSelectedGame() {
        if (root.selectedGameAppId.length === 0) {
            return
        }
        for (var i = 0; i < steamManager.gameModel.count; ++i) {
            var game = steamManager.gameModel.get(i)
            if (game.appId === root.selectedGameAppId) {
                root.selectedGameIndex = i
                root.selectedGame = game
                root.refreshDetailSnapshots()
                return
            }
        }
        root.clearSelectedGame()
    }
    function selectGame(index) {
        var game = steamManager.gameModel.get(index)
        if (!game || !game.appId) {
            return
        }
        root.selectedGameIndex = index
        root.selectedGameAppId = game.appId
        root.selectedGame = game
        root.gameDetailTab = 0
        root.refreshSelectedDetailData()
    }
    function clearSelectedGame() {
        root.selectedGameIndex = -1
        root.selectedGameAppId = ""
        root.selectedGame = ({})
        root.gameDetailTab = 0
        root.detailLocalSnapshots = []
        root.detailCloudSnapshots = []
    }
    function savePathText(status, availability, path) {
        var displayStatus = status === "需用户手动指定"
                ? "数据库未获取到存档位置 需用户手动指定"
                : status

        if (path.length > 0) {
            return "存档 " + displayStatus + " · " + availability + " · " + path
        }

        return "存档 " + displayStatus
    }
    function navBackground(page, hovered) {
        if (root.currentPage === page) {
            return root.darkMode ? "#263A54" : "#E5F2FF"
        }
        return hovered ? (root.darkMode ? "#242F42" : "#F0F7FF") : "transparent"
    }
    function navTextColor(page, hovered) {
        if (root.currentPage === page) {
            return root.accentColor
        }
        return hovered ? root.accentColor : root.textColor
    }
    function navBorderColor(page, hovered) {
        if (root.currentPage === page) {
            return root.darkMode ? "#315A82" : "#B9DAF7"
        }
        return hovered ? (root.darkMode ? "#334155" : "#D8E4F2") : "transparent"
    }
    function logLevelColor(level) {
        if (level === "error") {
            return "#DC2626"
        }
        if (level === "warning") {
            return "#D97706"
        }
        if (level === "debug") {
            return "#64748B"
        }
        return root.accentColor
    }
    function connectionStatusColor() {
        if (steamManager.webDavTesting) {
            return root.accentColor
        }
        if (steamManager.webDavConnectionStatus.indexOf("失败") >= 0 || steamManager.webDavConnectionStatus.indexOf("不完整") >= 0) {
            return "#D97706"
        }
        if (steamManager.webDavConnectionStatus.indexOf("成功") >= 0 || steamManager.webDavConnectionStatus.indexOf("可用") >= 0) {
            return "#16A34A"
        }
        return root.mutedTextColor
    }

    Component.onCompleted: steamManager.refreshInstalledGames()

    FolderDialog {
        id: savePathDialog
        title: "选择游戏存档目录"

        onAccepted: {
            if (root.pendingManualAppId.length > 0) {
                steamManager.setManualSavePath(root.pendingManualAppId, selectedFolder)
                root.pendingManualAppId = ""
            }
        }

        onRejected: root.pendingManualAppId = ""
    }

    FolderDialog {
        id: snapshotRootDialog
        title: "选择游戏存档根目录"

        onAccepted: steamManager.setSnapshotRootPath(selectedFolder)
    }

    FileDialog {
        id: gameExecutableDialog
        title: "选择游戏启动程序"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Windows 可执行文件 (*.exe)"]

        onAccepted: steamManager.addManualGameFromExecutable(selectedFile)
    }

    Connections {
        target: steamManager

        function onCloudSnapshotsChanged(appId) {
            if ((root.snapshotDialogMode === "download" || root.snapshotDialogMode === "cloud")
                    && root.snapshotDialogAppId === appId) {
                root.snapshotDialogItems = steamManager.cloudSnapshotsForGame(appId)
            }
            if (root.selectedGameAppId === appId) {
                root.detailCloudSnapshots = steamManager.cloudSnapshotsForGame(appId)
                root.syncSelectedGame()
            }
        }

        function onInstalledGamesChanged() {
            root.syncSelectedGame()
        }

        function onQuarkGatewayStatusChanged() {
            if (steamManager.quarkGatewayStatus.indexOf("健康检查通过") >= 0
                    || steamManager.quarkGatewayStatus.indexOf("网关已就绪") >= 0) {
                steamManager.refreshCloudManifestFromRemote()
                root.refreshSelectedCloudSnapshots()
            }
        }
    }

    Connections {
        target: steamManager.gameModel

        function onDataChanged(topLeft, bottomRight, roles) {
            root.gameStatsRevision += 1
            root.syncSelectedGame()
        }

        function onModelReset() {
            root.gameStatsRevision += 1
            root.syncSelectedGame()
        }

        function onCountChanged() {
            root.gameStatsRevision += 1
            root.syncSelectedGame()
        }
    }

    Dialog {
        id: snapshotListDialog
        modal: true
        title: root.snapshotDialogTitle
        standardButtons: Dialog.Close
        width: Math.min(root.width - 80, 720)
        height: Math.min(root.height - 80, 520)
        anchors.centerIn: parent

        onOpened: {
            if (root.snapshotDialogMode === "download" || root.snapshotDialogMode === "cloud") {
                root.snapshotDialogItems = steamManager.cloudSnapshotsForGame(root.snapshotDialogAppId)
                steamManager.refreshCloudSnapshotsForGame(root.snapshotDialogAppId)
            } else {
                steamManager.refreshLocalSnapshotsForGame(root.snapshotDialogAppId)
                root.snapshotDialogItems = steamManager.snapshotsForGame(root.snapshotDialogAppId)
            }
        }

        contentItem: Rectangle {
            color: root.panelColor
            implicitWidth: 680
            implicitHeight: 430

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                RowLayout {
                    visible: root.snapshotDialogMode === "upload" || root.snapshotDialogMode === "download"
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 42 : 0
                    spacing: 10

                    Text {
                        text: root.snapshotDialogMode === "download"
                              ? "云端存档快照列表，可下载全部缺失快照，也可选择单个快照覆盖下载。"
                              : "本地存档快照列表，可上传全部本地快照，也可选择单个快照覆盖上传。"
                        color: root.mutedTextColor
                        font.pixelSize: 13
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Button {
                        Layout.preferredWidth: 126
                        Layout.preferredHeight: 32
                        text: root.snapshotDialogMode === "download" ? "下载全部快照" : "上传全部快照"
                        font.pixelSize: 12

                        onClicked: {
                            if (root.snapshotDialogMode === "download") {
                                steamManager.downloadAllSnapshotsForGame(root.snapshotDialogAppId)
                            } else {
                                steamManager.uploadAllSnapshotsForGame(root.snapshotDialogAppId)
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
                        model: root.snapshotDialogItems

                        delegate: Rectangle {
                            width: ListView.view.width
                            height: 92
                            radius: 12
                            color: root.darkMode ? "#222B3C" : "#F7F9FC"
                            border.width: 1
                            border.color: root.darkMode ? "#313C50" : "#E6EBF2"

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 10

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    Text {
                                        text: modelData.fileName || "未知快照"
                                        color: root.textColor
                                        font.pixelSize: 14
                                        font.bold: true
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: "创建时间 " + (modelData.createdAtUtc || "未知") + " · 文件数 " + (modelData.fileCount || "0") + " · 上传状态 " + (modelData.uploadState || "not_uploaded")
                                        color: root.mutedTextColor
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: (root.snapshotDialogMode === "download" || root.snapshotDialogMode === "cloud")
                                              ? (modelData.remotePath || "")
                                              : (modelData.zipPath || "")
                                        color: root.mutedTextColor
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }

                                Button {
                                    visible: root.snapshotDialogMode !== "view"
                                    Layout.preferredWidth: visible ? 96 : 0
                                    Layout.preferredHeight: visible ? 30 : 0
                                    text: (root.snapshotDialogMode === "download" || root.snapshotDialogMode === "cloud")
                                          ? "可选下载"
                                          : "可选上传"
                                    font.pixelSize: 12

                                    onClicked: {
                                        if (root.snapshotDialogMode === "download" || root.snapshotDialogMode === "cloud") {
                                            steamManager.downloadSelectedSnapshotForGame(root.snapshotDialogAppId, modelData.fileName || modelData.remotePath || "")
                                        } else {
                                            steamManager.uploadSelectedSnapshotForGame(root.snapshotDialogAppId, modelData.zipPath || modelData.fileName || "")
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: root.snapshotDialogItems.length === 0
                        text: (root.snapshotDialogMode === "download" || root.snapshotDialogMode === "cloud")
                              ? "暂无云端快照记录"
                              : "暂无本地快照"
                        color: root.mutedTextColor
                        font.pixelSize: 15
                    }
                }
            }
        }
    }

    Dialog {
        id: deleteLocalSnapshotsDialog
        modal: true
        title: "删除存档快照"
        standardButtons: Dialog.Cancel | Dialog.Ok
        width: Math.min(root.width - 80, 520)
        anchors.centerIn: parent

        onAccepted: {
            if (root.pendingDeleteSnapshotAppId.length > 0) {
                steamManager.deleteLocalSnapshotsForGame(root.pendingDeleteSnapshotAppId)
                if ((root.snapshotDialogMode === "upload"
                        || root.snapshotDialogMode === "local"
                        || root.snapshotDialogMode === "view")
                        && root.snapshotDialogAppId === root.pendingDeleteSnapshotAppId) {
                    root.snapshotDialogItems = steamManager.snapshotsForGame(root.pendingDeleteSnapshotAppId)
                }
            }
            root.pendingDeleteSnapshotAppId = ""
            root.pendingDeleteSnapshotGameName = ""
        }

        onRejected: {
            root.pendingDeleteSnapshotAppId = ""
            root.pendingDeleteSnapshotGameName = ""
        }

        contentItem: Rectangle {
            color: root.panelColor
            implicitWidth: 480
            implicitHeight: 180

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 10

                Text {
                    text: "确认删除 “" + root.pendingDeleteSnapshotGameName + "” 的本地存档快照？"
                    color: root.textColor
                    font.pixelSize: 15
                    font.bold: true
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Text {
                    text: "这只会删除软件在本地快照目录中生成的 zip 快照和索引文件，不会删除游戏真实存档，也不会删除夸克网盘中的云端快照。恢复前自动备份属于后续恢复阶段的保护备份，不在这里清理。"
                    color: root.mutedTextColor
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: root.backgroundColor

        RowLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 18

            Rectangle {
                id: sidebarPanel
                Layout.preferredWidth: root.sidebarWidth
                Layout.fillHeight: true
                radius: 24
                color: root.darkMode ? "#D91C2433" : "#EFFFFFFF"
                border.width: 1
                border.color: root.darkMode ? "#334155" : "#DFE8F3"
                layer.enabled: true
                layer.effect: MultiEffect {
                    autoPaddingEnabled: true
                    shadowEnabled: true
                    shadowOpacity: root.darkMode ? 0.28 : 0.12
                    shadowBlur: 0.75
                    shadowVerticalOffset: 10
                    shadowColor: root.darkMode ? "#66000000" : "#330B1B33"
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 14

                    Text {
                        text: "GameSaveCloud"
                        color: root.textColor
                        font.family: root.appFontFamily
                        font.pixelSize: 22
                        font.bold: true
                        Layout.topMargin: 6
                    }

                    Text {
                        text: steamManager.steamPath.length > 0 ? steamManager.steamPath : "未找到 Steam 目录"
                        color: root.mutedTextColor
                        font.family: root.appFontFamily
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                        maximumLineCount: 3
                        elide: Text.ElideRight
                    }

                    Text {
                        text: steamManager.snapshotRootPath.length > 0
                              ? "存档目录 " + steamManager.snapshotRootPath
                              : "未设置游戏存档目录"
                        color: root.mutedTextColor
                        font.family: root.appFontFamily
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                        maximumLineCount: 3
                        elide: Text.ElideRight
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 46
                        Layout.leftMargin: 6
                        Layout.rightMargin: 6
                        radius: 23
                        color: root.navBackground(0, gameLibraryArea.containsMouse)
                        border.width: 1
                        border.color: root.navBorderColor(0, gameLibraryArea.containsMouse)

                        Text {
                            anchors.centerIn: parent
                            text: "游戏库"
                            color: root.navTextColor(0, gameLibraryArea.containsMouse)
                            font.family: root.appFontFamily
                            font.pixelSize: 15
                            font.bold: true
                        }

                        MouseArea {
                            id: gameLibraryArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.currentPage = 0
                                root.clearSelectedGame()
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 46
                        Layout.leftMargin: 6
                        Layout.rightMargin: 6
                        radius: 23
                        color: root.navBackground(1, logPageArea.containsMouse)
                        border.width: 1
                        border.color: root.navBorderColor(1, logPageArea.containsMouse)

                        Text {
                            anchors.centerIn: parent
                            text: "运行日志"
                            color: root.navTextColor(1, logPageArea.containsMouse)
                            font.family: root.appFontFamily
                            font.pixelSize: 15
                            font.bold: true
                        }

                        MouseArea {
                            id: logPageArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.currentPage = 1
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 46
                        Layout.leftMargin: 6
                        Layout.rightMargin: 6
                        radius: 23
                        color: root.navBackground(2, cloudPageArea.containsMouse)
                        border.width: 1
                        border.color: root.navBorderColor(2, cloudPageArea.containsMouse)

                        Text {
                            anchors.centerIn: parent
                            text: "云同步设置"
                            color: root.navTextColor(2, cloudPageArea.containsMouse)
                            font.family: root.appFontFamily
                            font.pixelSize: 15
                            font.bold: true
                        }

                        MouseArea {
                            id: cloudPageArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.currentPage = 2
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 46
                        Layout.leftMargin: 6
                        Layout.rightMargin: 6
                        radius: 23
                        color: refreshArea.containsMouse ? (root.darkMode ? "#2A3448" : "#EEF5FC") : "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: "刷新扫描"
                            color: refreshArea.containsMouse ? root.accentColor : root.textColor
                            font.family: root.appFontFamily
                            font.pixelSize: 15
                        }

                        MouseArea {
                            id: refreshArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: steamManager.refreshInstalledGames()
                        }

                        Behavior on color {
                            ColorAnimation { duration: 140 }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 46
                        Layout.leftMargin: 6
                        Layout.rightMargin: 6
                        radius: 23
                        color: addGameArea.containsMouse ? (root.darkMode ? "#2A3448" : "#EEF5FC") : "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: "添加游戏"
                            color: addGameArea.containsMouse ? root.accentColor : root.textColor
                            font.family: root.appFontFamily
                            font.pixelSize: 15
                        }

                        MouseArea {
                            id: addGameArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: gameExecutableDialog.open()
                        }

                        Behavior on color {
                            ColorAnimation { duration: 140 }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 46
                        Layout.leftMargin: 6
                        Layout.rightMargin: 6
                        radius: 23
                        color: snapshotRootArea.containsMouse ? (root.darkMode ? "#2A3448" : "#EEF5FC") : "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: "设置存档目录"
                            color: snapshotRootArea.containsMouse ? root.accentColor : root.textColor
                            font.family: root.appFontFamily
                            font.pixelSize: 15
                        }

                        MouseArea {
                            id: snapshotRootArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: snapshotRootDialog.open()
                        }

                        Behavior on color {
                            ColorAnimation { duration: 140 }
                        }
                    }

                    Item {
                        Layout.fillHeight: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Text {
                            text: "深色模式"
                            color: root.mutedTextColor
                            font.pixelSize: 13
                            Layout.fillWidth: true
                        }

                        Switch {
                            checked: root.darkMode
                            onToggled: root.darkMode = checked
                        }
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                Rectangle {
                    anchors.fill: contentCard
                    anchors.topMargin: 8
                    anchors.leftMargin: 0
                    radius: contentCard.radius
                    color: root.darkMode ? "#0B0F17" : "#DDE5EF"
                    opacity: root.darkMode ? 0.28 : 0.45
                }

                Rectangle {
                    id: contentCard
                    anchors.fill: parent
                    radius: 26
                    color: root.panelColor
                    border.width: 1
                    border.color: root.darkMode ? "#2D3748" : "#E7ECF3"

                    ColumnLayout {
                        id: gameLibraryGridPage
                        anchors.fill: parent
                        anchors.margins: 24
                        spacing: 18
                        visible: root.currentPage === 0 && root.selectedGameAppId.length === 0

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6

                                    Text {
                                        text: "游戏库"
                                        color: root.textColor
                                        font.pixelSize: 28
                                        font.bold: true
                                    }

                                    Text {
                                        text: steamManager.gameModel.count + " 个游戏 · 点击卡片进入详情"
                                        color: root.mutedTextColor
                                        font.pixelSize: 14
                                    }
                                }

                                Rectangle {
                                    Layout.preferredWidth: 132
                                    Layout.preferredHeight: 42
                                    radius: 14
                                    color: gridScanArea.containsMouse ? "#006CBE" : root.accentColor

                                    Text {
                                        anchors.centerIn: parent
                                        text: "重新扫描"
                                        color: "white"
                                        font.pixelSize: 14
                                        font.bold: true
                                    }

                                    MouseArea {
                                        id: gridScanArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: steamManager.refreshInstalledGames()
                                    }
                                }

                                Rectangle {
                                    Layout.preferredWidth: 132
                                    Layout.preferredHeight: 42
                                    radius: 14
                                    color: gridAddGameArea.containsMouse ? (root.darkMode ? "#2A3448" : "#E5F2FF") : (root.darkMode ? "#222B3C" : "#EEF5FC")
                                    border.width: 1
                                    border.color: gridAddGameArea.containsMouse ? root.accentColor : (root.darkMode ? "#334155" : "#D8E4F2")

                                    Text {
                                        anchors.centerIn: parent
                                        text: "添加游戏"
                                        color: gridAddGameArea.containsMouse ? root.accentColor : root.textColor
                                        font.pixelSize: 14
                                        font.bold: true
                                    }

                                    MouseArea {
                                        id: gridAddGameArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: gameExecutableDialog.open()
                                    }
                                }

                                Rectangle {
                                    Layout.preferredWidth: 154
                                    Layout.preferredHeight: 42
                                    radius: 14
                                    color: gridSnapshotArea.containsMouse ? (root.darkMode ? "#2A3448" : "#E5F2FF") : (root.darkMode ? "#222B3C" : "#EEF5FC")
                                    border.width: 1
                                    border.color: gridSnapshotArea.containsMouse ? root.accentColor : (root.darkMode ? "#334155" : "#D8E4F2")

                                    Text {
                                        anchors.centerIn: parent
                                        text: "设置存档目录"
                                        color: gridSnapshotArea.containsMouse ? root.accentColor : root.textColor
                                        font.pixelSize: 14
                                        font.bold: true
                                    }

                                    MouseArea {
                                        id: gridSnapshotArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: snapshotRootDialog.open()
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Repeater {
                                    model: [
                                        { label: "全部", value: steamManager.gameModel.count, color: "#64748B", revision: root.gameStatsRevision },
                                        { label: "运行中", value: root.runningGameCount(), color: "#16A34A", revision: root.gameStatsRevision },
                                        { label: "可同步", value: root.syncableGameCount(), color: "#10B981", revision: root.gameStatsRevision },
                                        { label: "需手动", value: root.manualGameCount(), color: "#D97706", revision: root.gameStatsRevision }
                                    ]

                                    delegate: Rectangle {
                                        height: 30
                                        Layout.preferredWidth: statRow.implicitWidth + 18
                                        radius: 15
                                        color: root.darkMode ? "#222B3C" : "#F3F6FA"
                                        border.width: 1
                                        border.color: root.darkMode ? "#334155" : "#E1E8F2"

                                        RowLayout {
                                            id: statRow
                                            anchors.centerIn: parent
                                            spacing: 6

                                            Rectangle {
                                                width: 7
                                                height: 7
                                                radius: 4
                                                color: modelData.color
                                                opacity: modelData.label === "运行中" && modelData.value > 0 ? 1 : 0.82

                                                SequentialAnimation on opacity {
                                                    running: modelData.label === "运行中" && modelData.value > 0
                                                    loops: Animation.Infinite
                                                    NumberAnimation { to: 0.38; duration: 900; easing.type: Easing.InOutQuad }
                                                    NumberAnimation { to: 1.0; duration: 900; easing.type: Easing.InOutQuad }
                                                }
                                            }

                                            Text {
                                                text: modelData.value + " " + modelData.label
                                                color: root.textColor
                                                font.family: root.appFontFamily
                                                font.pixelSize: 12
                                                font.bold: true
                                            }
                                        }
                                    }
                                }

                                Item { Layout.fillWidth: true }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 20
                            color: root.darkMode ? "#172030" : "#F7F9FC"
                            border.width: 1
                            border.color: root.darkMode ? "#2A3448" : "#E8EDF5"
                            clip: true

                            GridView {
                                id: gameGrid
                                anchors.fill: parent
                                anchors.margins: 16
                                model: steamManager.gameModel
                                clip: true
                                property int gap: 18
                                property int hoverLift: 5
                                property int minCardWidth: 210
                                property int visibleColumns: width >= 1260 ? 5 : (width >= 960 ? 4 : (width >= 650 ? 3 : 2))
                                cellWidth: Math.floor(width / visibleColumns)
                                property real cardWidth: Math.max(0, Math.floor(cellWidth - gap))
                                property real cardHeight: Math.round(cardWidth * 1.28)
                                cellHeight: cardHeight + gap
                                topMargin: hoverLift + 4

                                delegate: Rectangle {
                                    width: gameGrid.cardWidth
                                    height: gameGrid.cardHeight
                                    z: gridHoverArea.containsMouse ? 10 : 0
                                    radius: 18
                                    color: root.darkMode ? "#202B3C" : "#FFFFFF"
                                    border.width: 1
                                    border.color: gridHoverArea.containsMouse ? root.accentColor : (root.darkMode ? "#344056" : "#E5EBF4")
                                    clip: false
                                    transform: Translate {
                                        id: gameCardLift
                                        y: gridHoverArea.containsMouse ? -gameGrid.hoverLift : 0

                                        Behavior on y {
                                            NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
                                        }
                                    }
                                    layer.enabled: true
                                    layer.smooth: true
                                    layer.effect: MultiEffect {
                                        autoPaddingEnabled: true
                                        shadowEnabled: true
                                        shadowOpacity: root.darkMode ? 0.32 : 0.18
                                        shadowBlur: 0.85
                                        shadowVerticalOffset: 12
                                        shadowColor: root.darkMode ? "#66000000" : "#330B1B33"
                                    }

                                    Rectangle {
                                        id: cardClipLayer
                                        anchors.fill: parent
                                        radius: parent.radius
                                        color: root.darkMode ? "#202B3C" : "#FFFFFF"
                                        clip: false
                                        layer.enabled: true
                                        layer.smooth: true
                                        layer.effect: MultiEffect {
                                            maskEnabled: true
                                            maskSource: cardRoundMask
                                        }

                                        Image {
                                            id: cardAtmosphereImage
                                            anchors.fill: parent
                                            source: headerImageUrl
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                            cache: true
                                            smooth: true
                                            visible: status === Image.Ready
                                            opacity: root.darkMode ? 0.20 : 0.24
                                            scale: 1.12
                                            layer.enabled: true
                                            layer.smooth: true
                                            layer.effect: MultiEffect {
                                                blurEnabled: true
                                                blur: 0.78
                                                blurMax: 30
                                                saturation: 0.85
                                                brightness: root.darkMode ? -0.22 : -0.02
                                            }
                                        }

                                        Rectangle {
                                            anchors.fill: parent
                                            color: root.darkMode ? "#33202B3C" : "#18FFFFFF"
                                        }

                                        Rectangle {
                                            id: imageFrame
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            anchors.leftMargin: 12
                                            anchors.rightMargin: 12
                                            anchors.topMargin: 12
                                            height: Math.round(Math.max(104, (parent.width - 24) * 0.47))
                                            radius: 8
                                            color: "transparent"
                                            clip: false
                                            antialiasing: true
                                            layer.enabled: true
                                            layer.smooth: true
                                            layer.effect: MultiEffect {
                                                maskEnabled: true
                                                maskSource: imageFrameRoundMask
                                            }

                                            Image {
                                                id: squareHeaderImage
                                                anchors.centerIn: parent
                                                width: parent.width
                                                height: Math.round(width * 0.47)
                                                source: headerImageUrl
                                                fillMode: Image.PreserveAspectFit
                                                asynchronous: true
                                                cache: true
                                                smooth: true
                                                mipmap: true
                                                visible: status === Image.Ready
                                                scale: gridHoverArea.containsMouse ? 1.04 : 1.0
                                                layer.enabled: true
                                                layer.smooth: true

                                                Behavior on scale {
                                                    NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
                                                }
                                            }

                                            Rectangle {
                                                anchors.fill: parent
                                                radius: parent.radius
                                                color: "transparent"
                                                border.width: 1
                                                border.color: root.darkMode ? "#33202B3C" : "#40FFFFFF"
                                                antialiasing: true
                                                opacity: 0.72
                                            }

                                            Text {
                                                anchors.centerIn: parent
                                                text: displayName.length > 0 ? displayName[0].toUpperCase() : "?"
                                                color: root.darkMode ? "white" : root.accentColor
                                                font.family: root.appFontFamily
                                                font.pixelSize: 34
                                                font.bold: true
                                                visible: squareHeaderImage.status !== Image.Ready
                                            }
                                        }

                                        Rectangle {
                                            id: imageFrameRoundMask
                                            anchors.fill: imageFrame
                                            radius: imageFrame.radius
                                            color: "white"
                                            visible: false
                                            antialiasing: true
                                            layer.enabled: true
                                            layer.smooth: true
                                        }

                                        Rectangle {
                                            anchors.fill: parent
                                            radius: parent.radius
                                            gradient: Gradient {
                                                GradientStop { position: 0.0; color: root.darkMode ? "#00111722" : "#00FFFFFF" }
                                                GradientStop { position: 0.46; color: root.darkMode ? "#00111722" : "#00FFFFFF" }
                                                GradientStop { position: 0.74; color: root.darkMode ? "#3A111722" : "#38172033" }
                                                GradientStop { position: 1.0; color: root.darkMode ? "#BC111722" : "#A8172033" }
                                            }
                                        }

                                        ColumnLayout {
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            anchors.margins: 12
                                            spacing: 7

                                            Text {
                                                text: displayName
                                                color: "white"
                                                font.family: root.appFontFamily
                                                font.pixelSize: 16
                                                font.bold: true
                                                wrapMode: Text.WordWrap
                                                maximumLineCount: 2
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 6

                                                Rectangle {
                                                    Layout.preferredHeight: 23
                                                    Layout.preferredWidth: localCountText.implicitWidth + 16
                                                    radius: 12
                                                    color: root.countBadgeBackground("local")

                                                    Text {
                                                        id: localCountText
                                                        anchors.centerIn: parent
                                                        text: "本地 " + snapshotCount
                                                        color: root.countBadgeTextColor("local")
                                                        font.family: root.appFontFamily
                                                        font.pixelSize: 11
                                                        font.bold: true
                                                    }
                                                }

                                                Rectangle {
                                                    Layout.preferredHeight: 23
                                                    Layout.preferredWidth: cloudCountText.implicitWidth + 16
                                                    radius: 12
                                                    color: root.countBadgeBackground("cloud")

                                                    Text {
                                                        id: cloudCountText
                                                        anchors.centerIn: parent
                                                        text: "云端 " + cloudSnapshotCount
                                                        color: root.countBadgeTextColor("cloud")
                                                        font.family: root.appFontFamily
                                                        font.pixelSize: 11
                                                        font.bold: true
                                                    }
                                                }

                                                Item { Layout.fillWidth: true }
                                            }

                                            Rectangle {
                                                Layout.preferredHeight: 24
                                                Layout.preferredWidth: saveBadgeLabel.implicitWidth + 18
                                                Layout.maximumWidth: parent.width
                                                radius: 12
                                                color: root.saveBadgeBackground(savePathCanSync, savePathStatus)

                                                Text {
                                                    id: saveBadgeLabel
                                                    anchors.centerIn: parent
                                                    text: root.saveBadgeText(savePathCanSync, savePathStatus)
                                                    color: root.saveBadgeTextColor(savePathCanSync, savePathStatus)
                                                    font.family: root.appFontFamily
                                                    font.pixelSize: 11
                                                    font.bold: true
                                                    elide: Text.ElideRight
                                                    width: parent.width - 14
                                                }
                                            }
                                        }

                                        Rectangle {
                                            anchors.top: parent.top
                                            anchors.right: parent.right
                                            anchors.topMargin: 10
                                            anchors.rightMargin: 10
                                            width: runningBadgeLabel.implicitWidth + 18
                                            height: 24
                                            radius: 13
                                            color: root.runningBadgeBackground(runningStatus)
                                            border.width: 1
                                            border.color: root.runningBrief(runningStatus) === "运行中" ? "#86EFAC" : "#CBD5E1"

                                            Text {
                                                id: runningBadgeLabel
                                                anchors.centerIn: parent
                                                text: root.runningBrief(runningStatus)
                                                color: root.runningBadgeTextColor(runningStatus)
                                                font.family: root.appFontFamily
                                                font.pixelSize: 12
                                                font.bold: true
                                            }
                                        }
                                    }

                                    Rectangle {
                                        id: cardRoundMask
                                        anchors.fill: cardClipLayer
                                        radius: cardClipLayer.radius
                                        color: "white"
                                        visible: false
                                        antialiasing: true
                                        layer.enabled: true
                                    }

                                    MouseArea {
                                        id: gridHoverArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.selectGame(index)
                                    }
                                }
                            }

                            Text {
                                anchors.centerIn: parent
                                visible: steamManager.gameModel.count === 0
                                text: "未扫描到已安装游戏"
                                color: root.mutedTextColor
                                font.pixelSize: 16
                            }
                        }
                    }

                    ColumnLayout {
                        id: gameDetailPage
                        anchors.fill: parent
                        anchors.margins: 24
                        spacing: 14
                        visible: root.currentPage === 0 && root.selectedGameAppId.length > 0

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 38
                            spacing: 12

                            Rectangle {
                                Layout.preferredWidth: 142
                                Layout.fillHeight: true
                                radius: 14
                                color: detailBackArea.containsMouse ? (root.darkMode ? "#263247" : "#E7F3FF") : "transparent"
                                border.width: detailBackArea.containsMouse ? 1 : 0
                                border.color: root.darkMode ? "#334155" : "#CFE5FA"

                                RowLayout {
                                    anchors.centerIn: parent
                                    spacing: 8

                                    Text {
                                        text: "\uE72B"
                                        color: root.accentColor
                                        font.family: "Segoe MDL2 Assets"
                                        font.pixelSize: 13
                                    }

                                    Text {
                                        text: "返回游戏库"
                                        color: detailBackArea.containsMouse ? root.accentColor : root.textColor
                                        font.pixelSize: 13
                                        font.bold: true
                                    }
                                }

                                MouseArea {
                                    id: detailBackArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.clearSelectedGame()
                                }
                            }

                            Text {
                                text: "游戏详情"
                                color: root.mutedTextColor
                                font.pixelSize: 13
                            }

                            Item { Layout.fillWidth: true }
                        }

                        Rectangle {
                            id: detailHero
                            Layout.fillWidth: true
                            Layout.preferredHeight: 188
                            radius: 22
                            color: root.darkMode ? "#202B3C" : "#EEF6FE"
                            clip: true
                            antialiasing: true
                            layer.enabled: true
                            layer.smooth: true
                            layer.effect: MultiEffect {
                                maskEnabled: true
                                maskSource: detailHeroMask
                            }

                            Rectangle {
                                id: detailHeroMask
                                anchors.fill: parent
                                radius: detailHero.radius
                                color: "white"
                                visible: false
                                antialiasing: true
                                layer.enabled: true
                                layer.smooth: true
                            }

                            Image {
                                anchors.fill: parent
                                source: root.selectedGameValue("headerImageUrl", "")
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                cache: true
                                smooth: true
                                opacity: root.darkMode ? 0.30 : 0.26
                                scale: 1.08
                                layer.enabled: true
                                layer.smooth: true
                                layer.effect: MultiEffect {
                                    blurEnabled: true
                                    blur: 0.74
                                    blurMax: 28
                                    saturation: 0.78
                                    brightness: root.darkMode ? -0.18 : 0.06
                                }
                            }

                            Rectangle {
                                anchors.fill: parent
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: root.darkMode ? "#33172030" : "#22FFFFFF" }
                                    GradientStop { position: 0.64; color: root.darkMode ? "#11172030" : "#00FFFFFF" }
                                    GradientStop { position: 1.0; color: root.darkMode ? "#202B3C" : "#EEF6FE" }
                                }
                            }

                            Rectangle {
                                anchors.fill: parent
                                radius: detailHero.radius
                                color: "transparent"
                                border.width: 1
                                border.color: root.darkMode ? "#334155" : "#DDEAF5"
                                antialiasing: true
                            }

                            Rectangle {
                                id: heroImagePanel
                                anchors.top: parent.top
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.margins: 18
                                width: Math.round(parent.width * 0.54)
                                radius: 16
                                color: root.darkMode ? "#1A111722" : "#18FFFFFF"
                                clip: true

                                Image {
                                    anchors.centerIn: parent
                                    width: parent.width - 20
                                    height: Math.round(width * 0.47)
                                    source: root.selectedGameValue("headerImageUrl", "")
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                    cache: true
                                    smooth: true
                                    mipmap: true
                                }
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                anchors.leftMargin: 18
                                anchors.topMargin: 18
                                anchors.bottomMargin: 18
                                anchors.right: heroImagePanel.left
                                anchors.rightMargin: 18
                                radius: 18
                                color: root.darkMode ? "#99172030" : "#DDF7FBFF"
                                border.width: 1
                                border.color: root.darkMode ? "#334155" : "#DDEAF5"

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 18
                                    spacing: 8

                                    Text {
                                        text: root.selectedGameValue("displayName", "未知游戏")
                                        color: root.textColor
                                        font.family: root.appFontFamily
                                        font.pixelSize: 26
                                        font.bold: true
                                        maximumLineCount: 2
                                        wrapMode: Text.WordWrap
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: "AppID " + root.selectedGameValue("appId", "-")
                                        color: root.mutedTextColor
                                        font.pixelSize: 13
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: root.selectedGameValue("processName", "未识别进程")
                                        color: root.mutedTextColor
                                        font.pixelSize: 13
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Item { Layout.fillHeight: true }

                                    Rectangle {
                                        Layout.preferredWidth: heroRunningText.implicitWidth + 22
                                        Layout.preferredHeight: 28
                                        radius: 14
                                        color: root.runningBrief(root.selectedGameValue("runningStatus", "")) === "运行中" ? "#DCFCE7" : "#E8EEF7"

                                        Text {
                                            id: heroRunningText
                                            anchors.centerIn: parent
                                            text: root.runningBrief(root.selectedGameValue("runningStatus", "等待进程监控"))
                                            color: root.runningBrief(root.selectedGameValue("runningStatus", "")) === "运行中" ? "#16A34A" : "#64748B"
                                            font.pixelSize: 12
                                            font.bold: true
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 54
                            radius: 18
                            color: root.darkMode ? "#172030" : "#EEF5FC"
                            border.width: 1
                            border.color: root.darkMode ? "#2A3448" : "#DDEAF5"

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 5
                                spacing: 5

                                Repeater {
                                    model: [
                                        { label: "概览", icon: "\uE80F" },
                                        { label: "本地快照", icon: "\uE8B7" },
                                        { label: "云端快照", icon: "\uE753" },
                                        { label: "设置", icon: "\uE713" }
                                    ]

                                    delegate: Rectangle {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        radius: 14
                                        color: root.gameDetailTab === index
                                               ? (root.darkMode ? "#263247" : "#FFFFFF")
                                               : (tabMouseArea.containsMouse ? (root.darkMode ? "#202B3C" : "#F7FBFF") : "transparent")
                                        border.width: root.gameDetailTab === index ? 1 : 0
                                        border.color: root.darkMode ? "#334155" : "#DDEAF5"

                                        RowLayout {
                                            anchors.centerIn: parent
                                            spacing: 8

                                            Text {
                                                text: modelData.icon
                                                color: root.gameDetailTab === index ? root.accentColor : root.mutedTextColor
                                                font.family: "Segoe MDL2 Assets"
                                                font.pixelSize: 13
                                            }

                                            Text {
                                                text: modelData.label
                                                color: root.gameDetailTab === index ? root.accentColor : root.mutedTextColor
                                                font.family: root.appFontFamily
                                                font.pixelSize: 14
                                                font.bold: root.gameDetailTab === index
                                            }
                                        }

                                        MouseArea {
                                            id: tabMouseArea
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: root.switchDetailTab(index)
                                        }
                                    }
                                }
                            }
                        }

                        StackLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            currentIndex: root.gameDetailTab

                            ScrollView {
                                clip: true
                                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                                ColumnLayout {
                                    width: parent.width
                                    spacing: 12

                                    GridLayout {
                                        Layout.fillWidth: true
                                        columns: 2
                                        rowSpacing: 14
                                        columnSpacing: 14

                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 132
                                            radius: 18
                                            color: root.cardColor
                                            border.width: 1
                                            border.color: root.darkMode ? "#313C50" : "#E6EBF2"
                                            layer.enabled: true
                                            layer.effect: MultiEffect {
                                                shadowEnabled: true
                                                shadowOpacity: root.darkMode ? 0.20 : 0.08
                                                shadowBlur: 0.50
                                                shadowVerticalOffset: 6
                                                shadowColor: root.darkMode ? "#66000000" : "#220B1B33"
                                            }

                                            RowLayout {
                                                anchors.fill: parent
                                                anchors.margins: 18
                                                spacing: 16

                                                Rectangle {
                                                    Layout.preferredWidth: 58
                                                    Layout.preferredHeight: 58
                                                    radius: 18
                                                    color: root.selectedGameValue("savePathCanSync", false) ? (root.darkMode ? "#243F33" : "#DCFCE7") : (root.darkMode ? "#4A341D" : "#FEF3C7")

                                                    Text {
                                                        anchors.centerIn: parent
                                                        text: "\uE8B7"
                                                        color: root.selectedGameValue("savePathCanSync", false) ? "#16A34A" : "#D97706"
                                                        font.family: "Segoe MDL2 Assets"
                                                        font.pixelSize: 25
                                                    }
                                                }

                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 5

                                                    Text {
                                                        text: "存档状态"
                                                        color: root.mutedTextColor
                                                        font.pixelSize: 12
                                                        font.bold: true
                                                    }

                                                    Text {
                                                        text: root.selectedGameValue("savePathCanSync", false) ? "存档可同步" : root.saveBadgeText(false, root.selectedGameValue("savePathStatus", "等待识别"))
                                                        color: root.selectedGameValue("savePathCanSync", false) ? "#16A34A" : "#D97706"
                                                        font.pixelSize: 22
                                                        font.bold: true
                                                        elide: Text.ElideRight
                                                        Layout.fillWidth: true
                                                    }

                                                    Text {
                                                        text: root.savePathText(root.selectedGameValue("savePathStatus", "未识别"), root.selectedGameValue("savePathAvailability", "未知"), root.selectedGameValue("savePath", ""))
                                                        color: root.mutedTextColor
                                                        font.pixelSize: 13
                                                        maximumLineCount: 2
                                                        wrapMode: Text.WordWrap
                                                        elide: Text.ElideRight
                                                        Layout.fillWidth: true
                                                    }
                                                }
                                            }
                                        }

                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 132
                                            radius: 18
                                            color: root.cardColor
                                            border.width: 1
                                            border.color: root.darkMode ? "#313C50" : "#E6EBF2"
                                            layer.enabled: true
                                            layer.effect: MultiEffect {
                                                shadowEnabled: true
                                                shadowOpacity: root.darkMode ? 0.20 : 0.08
                                                shadowBlur: 0.50
                                                shadowVerticalOffset: 6
                                                shadowColor: root.darkMode ? "#66000000" : "#220B1B33"
                                            }

                                            RowLayout {
                                                anchors.fill: parent
                                                anchors.margins: 18
                                                spacing: 16

                                                Rectangle {
                                                    Layout.preferredWidth: 58
                                                    Layout.preferredHeight: 58
                                                    radius: 18
                                                    color: root.selectedGameValue("snapshotCount", 0) > 0 ? (root.darkMode ? "#16384A" : "#E0F2FE") : (root.darkMode ? "#263244" : "#E8EEF7")

                                                    Text {
                                                        anchors.centerIn: parent
                                                        text: "\uE823"
                                                        color: root.selectedGameValue("snapshotCount", 0) > 0 ? "#0284C7" : "#64748B"
                                                        font.family: "Segoe MDL2 Assets"
                                                        font.pixelSize: 25
                                                    }
                                                }

                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 5

                                                    Text {
                                                        text: "快照状态"
                                                        color: root.mutedTextColor
                                                        font.pixelSize: 12
                                                        font.bold: true
                                                    }

                                                    Text {
                                                        text: root.selectedGameValue("snapshotCount", 0) > 0 ? "快照可用" : "尚未创建"
                                                        color: root.selectedGameValue("snapshotCount", 0) > 0 ? "#0284C7" : "#64748B"
                                                        font.pixelSize: 22
                                                        font.bold: true
                                                        elide: Text.ElideRight
                                                        Layout.fillWidth: true
                                                    }

                                                    Text {
                                                        text: "本地 " + root.selectedGameValue("snapshotCount", 0) + " 个 · 云端 " + root.selectedGameValue("cloudSnapshotCount", 0) + " 个 · " + root.selectedGameValue("snapshotStatus", "未检查本地快照")
                                                        color: root.mutedTextColor
                                                        font.pixelSize: 13
                                                        maximumLineCount: 2
                                                        wrapMode: Text.WordWrap
                                                        elide: Text.ElideRight
                                                        Layout.fillWidth: true
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 46
                                        spacing: 10

                                        Rectangle {
                                            Layout.preferredWidth: 132
                                            Layout.fillHeight: true
                                            radius: 14
                                            opacity: root.selectedGameValue("savePathCanSync", false) ? 1.0 : 0.45
                                            color: root.actionButtonBackground("primary", createSnapshotArea.containsMouse && root.selectedGameValue("savePathCanSync", false))
                                            border.width: 0

                                            RowLayout {
                                                anchors.centerIn: parent
                                                spacing: 7

                                                Text {
                                                    text: "\uE710"
                                                    color: root.actionButtonTextColor("primary", createSnapshotArea.containsMouse)
                                                    font.family: "Segoe MDL2 Assets"
                                                    font.pixelSize: 13
                                                }

                                                Text {
                                                    text: "创建快照"
                                                    color: root.actionButtonTextColor("primary", createSnapshotArea.containsMouse)
                                                    font.pixelSize: 14
                                                    font.bold: true
                                                }
                                            }

                                            MouseArea {
                                                id: createSnapshotArea
                                                anchors.fill: parent
                                                enabled: root.selectedGameValue("savePathCanSync", false)
                                                hoverEnabled: true
                                                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                                onClicked: steamManager.createSnapshotForGame(root.selectedGameAppId)
                                            }
                                        }

                                        Rectangle {
                                            Layout.preferredWidth: 148
                                            Layout.fillHeight: true
                                            radius: 14
                                            opacity: root.selectedGameValue("snapshotCount", 0) > 0 ? 1.0 : 0.45
                                            color: root.actionButtonBackground("primary", uploadAllArea.containsMouse && root.selectedGameValue("snapshotCount", 0) > 0)
                                            border.width: 0

                                            RowLayout {
                                                anchors.centerIn: parent
                                                spacing: 7

                                                Text {
                                                    text: "\uE898"
                                                    color: root.actionButtonTextColor("primary", uploadAllArea.containsMouse)
                                                    font.family: "Segoe MDL2 Assets"
                                                    font.pixelSize: 13
                                                }

                                                Text {
                                                    text: "上传全部快照"
                                                    color: root.actionButtonTextColor("primary", uploadAllArea.containsMouse)
                                                    font.pixelSize: 14
                                                    font.bold: true
                                                }
                                            }

                                            MouseArea {
                                                id: uploadAllArea
                                                anchors.fill: parent
                                                enabled: root.selectedGameValue("snapshotCount", 0) > 0
                                                hoverEnabled: true
                                                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                                onClicked: steamManager.uploadAllSnapshotsForGame(root.selectedGameAppId)
                                            }
                                        }

                                        Rectangle {
                                            Layout.preferredWidth: 122
                                            Layout.fillHeight: true
                                            radius: 14
                                            opacity: root.selectedGameValue("savePathCanSync", false) ? 1.0 : 0.45
                                            color: root.actionButtonBackground("ghost", checkSnapshotArea.containsMouse && root.selectedGameValue("savePathCanSync", false))
                                            border.width: 1
                                            border.color: root.actionButtonBorder("ghost", checkSnapshotArea.containsMouse && root.selectedGameValue("savePathCanSync", false))

                                            RowLayout {
                                                anchors.centerIn: parent
                                                spacing: 7

                                                Text {
                                                    text: "\uE721"
                                                    color: root.actionButtonTextColor("ghost", checkSnapshotArea.containsMouse)
                                                    font.family: "Segoe MDL2 Assets"
                                                    font.pixelSize: 13
                                                }

                                                Text {
                                                    text: "检查快照"
                                                    color: root.actionButtonTextColor("ghost", checkSnapshotArea.containsMouse)
                                                    font.pixelSize: 14
                                                    font.bold: true
                                                }
                                            }

                                            MouseArea {
                                                id: checkSnapshotArea
                                                anchors.fill: parent
                                                enabled: root.selectedGameValue("savePathCanSync", false)
                                                hoverEnabled: true
                                                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                                onClicked: steamManager.analyzeSnapshotForGame(root.selectedGameAppId)
                                            }
                                        }

                                        Rectangle {
                                            Layout.preferredWidth: 148
                                            Layout.fillHeight: true
                                            radius: 14
                                            opacity: steamManager.hasDownloadableSnapshot(root.selectedGameAppId) ? 1.0 : 0.45
                                            color: root.actionButtonBackground("ghost", downloadAllArea.containsMouse && steamManager.hasDownloadableSnapshot(root.selectedGameAppId))
                                            border.width: 1
                                            border.color: root.actionButtonBorder("ghost", downloadAllArea.containsMouse && steamManager.hasDownloadableSnapshot(root.selectedGameAppId))

                                            RowLayout {
                                                anchors.centerIn: parent
                                                spacing: 7

                                                Text {
                                                    text: "\uE896"
                                                    color: root.actionButtonTextColor("ghost", downloadAllArea.containsMouse)
                                                    font.family: "Segoe MDL2 Assets"
                                                    font.pixelSize: 13
                                                }

                                                Text {
                                                    text: "下载全部快照"
                                                    color: root.actionButtonTextColor("ghost", downloadAllArea.containsMouse)
                                                    font.pixelSize: 14
                                                    font.bold: true
                                                }
                                            }

                                            MouseArea {
                                                id: downloadAllArea
                                                anchors.fill: parent
                                                enabled: steamManager.hasDownloadableSnapshot(root.selectedGameAppId)
                                                hoverEnabled: true
                                                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                                onClicked: steamManager.downloadAllSnapshotsForGame(root.selectedGameAppId)
                                            }
                                        }

                                        Item { Layout.fillWidth: true }
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 178
                                        radius: 18
                                        color: root.cardColor
                                        border.width: 1
                                        border.color: root.darkMode ? "#313C50" : "#E6EBF2"

                                        ColumnLayout {
                                            anchors.fill: parent
                                            anchors.margins: 18
                                            spacing: 12

                                            Text {
                                                text: "最新动态"
                                                color: root.textColor
                                                font.pixelSize: 16
                                                font.bold: true
                                            }

                                            Repeater {
                                                model: [
                                                    { title: "本地最新快照", detail: root.selectedGameValue("latestSnapshotFileName", "暂无本地快照"), color: root.selectedGameValue("snapshotCount", 0) > 0 ? "#4F46E5" : "#94A3B8" },
                                                    { title: "云端最新快照", detail: root.selectedGameValue("latestCloudSnapshotFileName", "暂无云端快照"), color: root.selectedGameValue("cloudSnapshotCount", 0) > 0 ? "#0284C7" : "#94A3B8" },
                                                    { title: "处理状态", detail: root.selectedGameValue("snapshotDetail", "等待用户设置快照目录并执行快照预处理检查"), color: root.selectedGameValue("savePathCanSync", false) ? "#16A34A" : "#D97706" }
                                                ]

                                                delegate: RowLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 10

                                                    Rectangle {
                                                        Layout.preferredWidth: 9
                                                        Layout.preferredHeight: 9
                                                        radius: 5
                                                        color: modelData.color
                                                    }

                                                    Text {
                                                        Layout.preferredWidth: 116
                                                        text: modelData.title
                                                        color: root.textColor
                                                        font.pixelSize: 13
                                                        font.bold: true
                                                        elide: Text.ElideRight
                                                    }

                                                    Text {
                                                        Layout.fillWidth: true
                                                        text: modelData.detail
                                                        color: root.mutedTextColor
                                                        font.pixelSize: 13
                                                        elide: Text.ElideRight
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            SnapshotList {
                                mode: "local"
                                snapshots: root.detailLocalSnapshots
                                localSnapshotCount: root.detailLocalSnapshots.length
                                savePathCanSync: root.selectedGameValue("savePathCanSync", false)
                                darkMode: root.darkMode
                                cardColor: root.cardColor
                                textColor: root.textColor
                                mutedTextColor: root.mutedTextColor
                                accentColor: root.accentColor

                                onRefreshRequested: {
                                    steamManager.refreshLocalSnapshotsForGame(root.selectedGameAppId)
                                    root.refreshDetailSnapshots()
                                }
                                onUploadAllRequested: steamManager.uploadAllSnapshotsForGame(root.selectedGameAppId)
                                onOpenDirectoryRequested: steamManager.openSnapshotDirectoryForGame(root.selectedGameAppId)
                                onDeleteRequested: {
                                    root.pendingDeleteSnapshotAppId = root.selectedGameAppId
                                    root.pendingDeleteSnapshotGameName = root.selectedGameValue("displayName", "该游戏")
                                    deleteLocalSnapshotsDialog.open()
                                }
                                onCreateSnapshotRequested: steamManager.createSnapshotForGame(root.selectedGameAppId)
                                onUploadSelected: function(snapshotPath) {
                                    steamManager.uploadSelectedSnapshotForGame(root.selectedGameAppId, snapshotPath)
                                }
                            }

                            SnapshotList {
                                mode: "cloud"
                                snapshots: root.detailCloudSnapshots
                                localSnapshotCount: root.detailLocalSnapshots.length
                                savePathCanSync: root.selectedGameValue("savePathCanSync", false)
                                darkMode: root.darkMode
                                cardColor: root.cardColor
                                textColor: root.textColor
                                mutedTextColor: root.mutedTextColor
                                accentColor: root.accentColor

                                onRefreshRequested: steamManager.refreshCloudSnapshotsForGame(root.selectedGameAppId)
                                onDownloadAllRequested: steamManager.downloadAllSnapshotsForGame(root.selectedGameAppId)
                                onUploadAllRequested: steamManager.uploadAllSnapshotsForGame(root.selectedGameAppId)
                                onDownloadSelected: function(snapshotName) {
                                    steamManager.downloadSelectedSnapshotForGame(root.selectedGameAppId, snapshotName)
                                }
                            }
                            ScrollView {
                                clip: true
                                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                                ColumnLayout {
                                    width: parent.width
                                    spacing: 12

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 150
                                        radius: 16
                                        color: root.darkMode ? "#172030" : "#F7F9FC"
                                        border.width: 1
                                        border.color: root.darkMode ? "#2A3448" : "#E8EDF5"

                                        ColumnLayout {
                                            anchors.fill: parent
                                            anchors.margins: 14
                                            spacing: 8

                                            Text { text: "存档目录"; color: root.textColor; font.pixelSize: 15; font.bold: true }
                                            Text {
                                                text: root.selectedGameValue("savePath", "尚未识别或指定")
                                                color: root.mutedTextColor
                                                font.pixelSize: 13
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }

                                            RowLayout {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 44
                                                spacing: 10

                                                Rectangle {
                                                    Layout.preferredWidth: 138
                                                    Layout.fillHeight: true
                                                    radius: 14
                                                    color: root.actionButtonBackground("primary", chooseSavePathArea.containsMouse)

                                                    RowLayout {
                                                        anchors.centerIn: parent
                                                        spacing: 7

                                                        Text {
                                                            text: "\uE8B7"
                                                            color: "white"
                                                            font.family: "Segoe MDL2 Assets"
                                                            font.pixelSize: 13
                                                        }

                                                        Text {
                                                            text: "选择存档目录"
                                                            color: "white"
                                                            font.pixelSize: 13
                                                            font.bold: true
                                                        }
                                                    }

                                                    MouseArea {
                                                        id: chooseSavePathArea
                                                        anchors.fill: parent
                                                        hoverEnabled: true
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: {
                                                            root.pendingManualAppId = root.selectedGameAppId
                                                            savePathDialog.open()
                                                        }
                                                    }
                                                }

                                                Rectangle {
                                                    Layout.preferredWidth: 138
                                                    Layout.fillHeight: true
                                                    radius: 14
                                                    opacity: root.selectedGameValue("savePath", "").length > 0 ? 1.0 : 0.45
                                                    color: root.actionButtonBackground("ghost", openSavePathArea.containsMouse && root.selectedGameValue("savePath", "").length > 0)
                                                    border.width: 1
                                                    border.color: root.actionButtonBorder("ghost", openSavePathArea.containsMouse && root.selectedGameValue("savePath", "").length > 0)

                                                    RowLayout {
                                                        anchors.centerIn: parent
                                                        spacing: 7

                                                        Text {
                                                            text: "\uE8A5"
                                                            color: root.actionButtonTextColor("ghost", openSavePathArea.containsMouse)
                                                            font.family: "Segoe MDL2 Assets"
                                                            font.pixelSize: 13
                                                        }

                                                        Text {
                                                            text: "打开存档目录"
                                                            color: root.actionButtonTextColor("ghost", openSavePathArea.containsMouse)
                                                            font.pixelSize: 13
                                                            font.bold: true
                                                        }
                                                    }

                                                    MouseArea {
                                                        id: openSavePathArea
                                                        anchors.fill: parent
                                                        enabled: root.selectedGameValue("savePath", "").length > 0
                                                        hoverEnabled: true
                                                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                                        onClicked: steamManager.openSavePathForGame(root.selectedGameAppId)
                                                    }
                                                }

                                                Rectangle {
                                                    Layout.preferredWidth: 138
                                                    Layout.fillHeight: true
                                                    radius: 14
                                                    color: root.actionButtonBackground("ghost", openSettingsSnapshotDirArea.containsMouse)
                                                    border.width: 1
                                                    border.color: root.actionButtonBorder("ghost", openSettingsSnapshotDirArea.containsMouse)

                                                    RowLayout {
                                                        anchors.centerIn: parent
                                                        spacing: 7

                                                        Text {
                                                            text: "\uE8B7"
                                                            color: root.actionButtonTextColor("ghost", openSettingsSnapshotDirArea.containsMouse)
                                                            font.family: "Segoe MDL2 Assets"
                                                            font.pixelSize: 13
                                                        }

                                                        Text {
                                                            text: "打开快照目录"
                                                            color: root.actionButtonTextColor("ghost", openSettingsSnapshotDirArea.containsMouse)
                                                            font.pixelSize: 13
                                                            font.bold: true
                                                        }
                                                    }

                                                    MouseArea {
                                                        id: openSettingsSnapshotDirArea
                                                        anchors.fill: parent
                                                        hoverEnabled: true
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: steamManager.openSnapshotDirectoryForGame(root.selectedGameAppId)
                                                    }
                                                }

                                                Item { Layout.fillWidth: true }
                                            }
                                        }
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 150
                                        radius: 16
                                        color: root.darkMode ? "#172030" : "#F7F9FC"
                                        border.width: 1
                                        border.color: root.darkMode ? "#2A3448" : "#E8EDF5"

                                        ColumnLayout {
                                            anchors.fill: parent
                                            anchors.margins: 14
                                            spacing: 8

                                            Text { text: "识别信息"; color: root.textColor; font.pixelSize: 15; font.bold: true }
                                            Text {
                                                text: "来源 " + root.selectedGameValue("savePathSource", "未知") + " · PCGamingWiki " + root.selectedGameValue("pcGamingWikiPageName", "未匹配")
                                                color: root.mutedTextColor
                                                font.pixelSize: 13
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }
                                            Text {
                                                text: root.selectedGameValue("metadataStatus", "资料状态未知") + " · " + root.selectedGameValue("syncStatus", "未同步")
                                                color: root.mutedTextColor
                                                font.pixelSize: 13
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }
                                            Text {
                                                text: root.selectedGameValue("savePathDetail", "暂无存档识别详情")
                                                color: root.mutedTextColor
                                                font.pixelSize: 13
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        id: gamePage
                        anchors.fill: parent
                        anchors.margins: 24
                        spacing: 18
                        visible: false

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 16

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Text {
                                    text: "游戏库"
                                    color: root.textColor
                                    font.pixelSize: 28
                                    font.bold: true
                                }

                                Text {
                                    text: steamManager.gameModel.count + " 个游戏"
                                    color: root.mutedTextColor
                                    font.pixelSize: 14
                                }
                            }

                            Rectangle {
                                Layout.preferredWidth: 132
                                Layout.preferredHeight: 42
                                radius: 14
                                color: scanArea.containsMouse ? "#006CBE" : root.accentColor

                                Text {
                                    anchors.centerIn: parent
                                    text: "重新扫描"
                                    color: "white"
                                    font.pixelSize: 14
                                    font.bold: true
                                }

                                MouseArea {
                                    id: scanArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: steamManager.refreshInstalledGames()
                                }

                                Behavior on color {
                                    ColorAnimation { duration: 140 }
                                }
                            }

                            Rectangle {
                                Layout.preferredWidth: 132
                                Layout.preferredHeight: 42
                                radius: 14
                                color: addHeaderGameArea.containsMouse ? (root.darkMode ? "#2A3448" : "#E5F2FF") : (root.darkMode ? "#222B3C" : "#EEF5FC")
                                border.width: 1
                                border.color: addHeaderGameArea.containsMouse ? root.accentColor : (root.darkMode ? "#334155" : "#D8E4F2")

                                Text {
                                    anchors.centerIn: parent
                                    text: "添加游戏"
                                    color: addHeaderGameArea.containsMouse ? root.accentColor : root.textColor
                                    font.pixelSize: 14
                                    font.bold: true
                                }

                                MouseArea {
                                    id: addHeaderGameArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: gameExecutableDialog.open()
                                }

                                Behavior on color {
                                    ColorAnimation { duration: 140 }
                                }
                            }

                            Rectangle {
                                Layout.preferredWidth: 154
                                Layout.preferredHeight: 42
                                radius: 14
                                color: snapshotHeaderArea.containsMouse ? (root.darkMode ? "#2A3448" : "#E5F2FF") : (root.darkMode ? "#222B3C" : "#EEF5FC")
                                border.width: 1
                                border.color: snapshotHeaderArea.containsMouse ? root.accentColor : (root.darkMode ? "#334155" : "#D8E4F2")

                                Text {
                                    anchors.centerIn: parent
                                    text: "设置存档目录"
                                    color: snapshotHeaderArea.containsMouse ? root.accentColor : root.textColor
                                    font.pixelSize: 14
                                    font.bold: true
                                }

                                MouseArea {
                                    id: snapshotHeaderArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: snapshotRootDialog.open()
                                }

                                Behavior on color {
                                    ColorAnimation { duration: 140 }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 20
                            color: root.darkMode ? "#172030" : "#F7F9FC"
                            border.width: 1
                            border.color: root.darkMode ? "#2A3448" : "#E8EDF5"
                            clip: true

                            ListView {
                                id: gameList
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 10
                                model: steamManager.gameModel
                                clip: true

                                delegate: Rectangle {
                                    width: ListView.view.width
                                    height: 236
                                    radius: 18
                                    color: hoverArea.containsMouse ? (root.darkMode ? "#2A3448" : "#FFFFFF") : root.cardColor
                                    border.width: 1
                                    border.color: hoverArea.containsMouse ? root.accentColor : (root.darkMode ? "#313C50" : "#E6EBF2")

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 18
                                        anchors.rightMargin: 18
                                        spacing: 14

                                        Rectangle {
                                            Layout.preferredWidth: 96
                                            Layout.preferredHeight: 45
                                            radius: 12
                                            color: root.darkMode ? "#263247" : "#E5F2FF"
                                            clip: true

                                            Image {
                                                id: headerImage
                                                anchors.fill: parent
                                                source: headerImageUrl
                                                fillMode: Image.PreserveAspectCrop
                                                asynchronous: true
                                                cache: true
                                                visible: status === Image.Ready
                                            }

                                            Text {
                                                anchors.centerIn: parent
                                                text: displayName.length > 0 ? displayName[0].toUpperCase() : "?"
                                                color: root.darkMode ? "white" : root.accentColor
                                                font.pixelSize: 18
                                                font.bold: true
                                                visible: headerImage.status !== Image.Ready
                                            }
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 4

                                            Text {
                                                text: displayName
                                                color: root.textColor
                                                font.pixelSize: 16
                                                font.bold: true
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }

                                            Text {
                                                text: (developers.length > 0 ? developers.join(", ") : "开发者未知") + "  ·  " + metadataStatus
                                                color: root.mutedTextColor
                                                font.pixelSize: 12
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }

                                            Text {
                                                text: isManualGame
                                                      ? "手动添加  ·  " + (appId.indexOf("manual:") === 0 ? "未匹配 Steam AppID" : "AppID " + appId) + "  ·  " + processName + "  ·  " + runningStatus + "  ·  " + syncStatus
                                                      : "AppID " + appId + "  ·  " + installDir + "  ·  " + runningStatus + "  ·  " + syncStatus
                                                color: root.mutedTextColor
                                                font.pixelSize: 12
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }

                                            Text {
                                                text: root.savePathText(savePathStatus, savePathAvailability, savePath)
                                                color: savePathStatus === "需用户手动指定"
                                                       || savePathAvailability === "未生成存档"
                                                       || savePathAvailability === "存档目录为空"
                                                       || savePathAvailability === "本地存档目录为空，Steam已云存档，无需同步"
                                                       || savePathAvailability === "需确认存档目录"
                                                       ? "#D97706" : root.mutedTextColor
                                                font.pixelSize: 12
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }

                                            Text {
                                                text: snapshotStatus.length > 0
                                                      ? "快照 " + snapshotStatus + (snapshotDetail.length > 0 ? " · " + snapshotDetail : "")
                                                      : "快照 未检查本地快照"
                                                color: snapshotNeedsCreate ? root.accentColor : root.mutedTextColor
                                                font.pixelSize: 12
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }

                                            Text {
                                                text: snapshotCount > 0
                                                      ? "本地快照 " + snapshotCount + " 个 · 最新 " + latestSnapshotFileName
                                                      : "本地快照 0 个"
                                                color: root.mutedTextColor
                                                font.pixelSize: 12
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }

                                            Text {
                                                text: cloudSnapshotCount > 0
                                                      ? "云端快照 " + cloudSnapshotCount + " 个 · 最新 " + latestCloudSnapshotFileName
                                                      : (cloudSnapshotStatus.length > 0 ? "云端 " + cloudSnapshotStatus : "云端 未读取索引")
                                                color: cloudSnapshotCount > 0 ? root.accentColor : root.mutedTextColor
                                                font.pixelSize: 12
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }
                                        }

                                        ColumnLayout {
                                            Layout.preferredWidth: 112
                                            Layout.alignment: Qt.AlignVCenter
                                            spacing: 5

                                            Button {
                                                visible: savePathStatus === "需用户手动指定"
                                                Layout.preferredWidth: visible ? 112 : 0
                                                Layout.preferredHeight: visible ? 34 : 0
                                                text: "选择目录"
                                                font.pixelSize: 12

                                                onClicked: {
                                                    root.pendingManualAppId = appId
                                                    savePathDialog.open()
                                                }
                                            }

                                            Button {
                                                visible: savePathCanSync
                                                Layout.preferredWidth: visible ? 112 : 0
                                                Layout.preferredHeight: visible ? 30 : 0
                                                text: "检查快照"
                                                font.pixelSize: 12

                                                onClicked: steamManager.analyzeSnapshotForGame(appId)
                                            }

                                            Button {
                                                visible: savePathCanSync
                                                Layout.preferredWidth: visible ? 112 : 0
                                                Layout.preferredHeight: visible ? 30 : 0
                                                text: "创建快照"
                                                font.pixelSize: 12

                                                onClicked: steamManager.createSnapshotForGame(appId)
                                            }

                                            Button {
                                                visible: snapshotCount > 0
                                                Layout.preferredWidth: visible ? 112 : 0
                                                Layout.preferredHeight: visible ? 30 : 0
                                                text: "上传快照"
                                                font.pixelSize: 12

                                                onClicked: {
                                                    steamManager.refreshLocalSnapshotsForGame(appId)
                                                    root.snapshotDialogMode = "upload"
                                                    root.snapshotDialogAppId = appId
                                                    root.snapshotDialogTitle = displayName + " 的本地存档快照 · 上传"
                                                    root.snapshotDialogItems = steamManager.snapshotsForGame(appId)
                                                    snapshotListDialog.open()
                                                }
                                            }

                                            Button {
                                                visible: cloudSnapshotCount >= 0 && steamManager.hasDownloadableSnapshot(appId)
                                                Layout.preferredWidth: visible ? 112 : 0
                                                Layout.preferredHeight: visible ? 30 : 0
                                                text: "下载快照"
                                                font.pixelSize: 12

                                                onClicked: {
                                                    root.snapshotDialogMode = "download"
                                                    root.snapshotDialogAppId = appId
                                                    root.snapshotDialogTitle = displayName + " 的云端存档快照 · 下载"
                                                    root.snapshotDialogItems = steamManager.cloudSnapshotsForGame(appId)
                                                    snapshotListDialog.open()
                                                    steamManager.refreshCloudSnapshotsForGame(appId)
                                                }
                                            }

                                            Button {
                                                visible: snapshotCount > 0
                                                Layout.preferredWidth: visible ? 112 : 0
                                                Layout.preferredHeight: visible ? 30 : 0
                                                text: "打开快照目录"
                                                font.pixelSize: 12

                                                onClicked: steamManager.openSnapshotDirectoryForGame(appId)
                                            }

                                            Button {
                                                visible: snapshotCount > 0
                                                Layout.preferredWidth: visible ? 112 : 0
                                                Layout.preferredHeight: visible ? 30 : 0
                                                text: "删除存档快照"
                                                font.pixelSize: 12

                                                onClicked: {
                                                    root.pendingDeleteSnapshotAppId = appId
                                                    root.pendingDeleteSnapshotGameName = displayName
                                                    deleteLocalSnapshotsDialog.open()
                                                }
                                            }
                                        }
                                    }

                                    MouseArea {
                                        id: hoverArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        acceptedButtons: Qt.NoButton
                                    }

                                    Behavior on color {
                                        ColorAnimation { duration: 120 }
                                    }
                                }
                            }

                            Text {
                                anchors.centerIn: parent
                                visible: steamManager.gameModel.count === 0
                                text: "未扫描到已安装游戏"
                                color: root.mutedTextColor
                                font.pixelSize: 16
                            }
                        }
                    }

                    ColumnLayout {
                        id: logPage
                        anchors.fill: parent
                        anchors.margins: 24
                        spacing: 18
                        visible: root.currentPage === 1

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 16

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Text {
                                    text: "运行日志"
                                    color: root.textColor
                                    font.pixelSize: 28
                                    font.bold: true
                                }

                                Text {
                                    text: "当前显示日志 " + steamManager.logModel.count + " 条 · 总日志 " + steamManager.logModel.totalCount + " 条 · 文件 " + steamManager.logFilePath
                                    color: root.mutedTextColor
                                    font.pixelSize: 14
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }

                            Button {
                                Layout.preferredWidth: 126
                                Layout.preferredHeight: 38
                                text: "打开目录"
                                onClicked: steamManager.openLogDirectory()
                            }

                            Button {
                                Layout.preferredWidth: 126
                                Layout.preferredHeight: 38
                                text: "清空界面"
                                onClicked: steamManager.clearVisibleLogs()
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Text {
                                text: "日志目录 " + steamManager.logDirectory
                                color: root.mutedTextColor
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Repeater {
                                model: [
                                    { label: "全部", value: "all" },
                                    { label: "信息", value: "info" },
                                    { label: "警告", value: "warning" },
                                    { label: "错误", value: "error" }
                                ]

                                delegate: Rectangle {
                                    Layout.preferredWidth: 64
                                    Layout.preferredHeight: 32
                                    radius: 10
                                    color: steamManager.logModel.filterLevel === modelData.value
                                           ? root.accentColor
                                           : (filterArea.containsMouse ? (root.darkMode ? "#2A3448" : "#E5F2FF") : (root.darkMode ? "#222B3C" : "#EEF5FC"))
                                    border.width: 1
                                    border.color: steamManager.logModel.filterLevel === modelData.value
                                                  ? root.accentColor
                                                  : (root.darkMode ? "#334155" : "#D8E4F2")

                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData.label
                                        color: steamManager.logModel.filterLevel === modelData.value ? "white" : root.textColor
                                        font.pixelSize: 13
                                        font.bold: steamManager.logModel.filterLevel === modelData.value
                                    }

                                    MouseArea {
                                        id: filterArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            steamManager.logModel.filterLevel = modelData.value
                                            logList.positionViewAtBeginning()
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 20
                            color: root.darkMode ? "#172030" : "#F7F9FC"
                            border.width: 1
                            border.color: root.darkMode ? "#2A3448" : "#E8EDF5"
                            clip: true

                            ListView {
                                id: logList
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 8
                                model: steamManager.logModel
                                clip: true

                                delegate: Rectangle {
                                    width: ListView.view.width
                                    height: 72
                                    radius: 14
                                    color: root.cardColor
                                    border.width: 1
                                    border.color: root.darkMode ? "#313C50" : "#E6EBF2"
                                    clip: true

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 14
                                        anchors.rightMargin: 14
                                        spacing: 12

                                        Rectangle {
                                            Layout.preferredWidth: 8
                                            Layout.fillHeight: true
                                            Layout.topMargin: 14
                                            Layout.bottomMargin: 14
                                            radius: 4
                                            color: root.logLevelColor(level)
                                        }

                                        ColumnLayout {
                                            Layout.preferredWidth: 156
                                            spacing: 4

                                            Text {
                                                text: levelName
                                                color: root.logLevelColor(level)
                                                font.pixelSize: 13
                                                font.bold: true
                                            }

                                            Text {
                                                text: timeText
                                                color: root.mutedTextColor
                                                font.pixelSize: 12
                                            }
                                        }

                                        Text {
                                            text: message
                                            color: root.textColor
                                            font.pixelSize: 13
                                            wrapMode: Text.Wrap
                                            maximumLineCount: 2
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                    }
                                }
                            }

                            Text {
                                anchors.centerIn: parent
                                visible: steamManager.logModel.count === 0
                                text: steamManager.logModel.totalCount === 0 ? "暂无运行日志" : "当前筛选条件下暂无日志"
                                color: root.mutedTextColor
                                font.pixelSize: 16
                            }
                        }
                    }

                    ColumnLayout {
                        id: cloudPage
                        anchors.fill: parent
                        anchors.margins: 24
                        spacing: 18
                        visible: root.currentPage === 2

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 16

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Text {
                                    text: "云同步设置"
                                    color: root.textColor
                                    font.pixelSize: 28
                                    font.bold: true
                                }

                                Text {
                                    text: steamManager.webDavConnectionStatus
                                    color: root.connectionStatusColor()
                                    font.pixelSize: 14
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 300
                            radius: 20
                            color: root.darkMode ? "#172030" : "#F7F9FC"
                            border.width: 1
                            border.color: root.darkMode ? "#2A3448" : "#E8EDF5"

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 22
                                spacing: 14

                                Text {
                                    text: "夸克网盘连接"
                                    color: root.textColor
                                    font.pixelSize: 18
                                    font.bold: true
                                    Layout.fillWidth: true
                                }

                                TextField {
                                    id: quarkCookieConnectField
                                    text: steamManager.quarkCookie
                                    placeholderText: "粘贴夸克 Cookie"
                                    echoMode: TextInput.Password
                                    color: root.textColor
                                    selectByMouse: true
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 42
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10

                                    Button {
                                        Layout.preferredWidth: 128
                                        Layout.preferredHeight: 40
                                        text: steamManager.webDavTesting ? "连接中" : "连接"
                                        enabled: !steamManager.webDavTesting
                                        onClicked: steamManager.saveQuarkCookieGateway(quarkCookieConnectField.text)
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: steamManager.quarkCookie.length > 0 ? "已保存 Cookie；如连接失败或 Cookie 过期，请粘贴新的 Cookie 后重新连接。" : "首次连接成功后会自动保存配置，之后启动软件会自动连接。"
                                        color: root.mutedTextColor
                                        font.pixelSize: 13
                                        wrapMode: Text.Wrap
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 90
                                    radius: 14
                                    color: root.darkMode ? "#222B3C" : "#FFFFFF"
                                    border.width: 1
                                    border.color: root.darkMode ? "#313C50" : "#E6EBF2"

                                    Text {
                                        anchors.fill: parent
                                        anchors.margins: 14
                                        text: steamManager.webDavConnectionStatus + "\n" + steamManager.quarkGatewayStatus
                                        color: root.connectionStatusColor()
                                        font.pixelSize: 13
                                        wrapMode: Text.Wrap
                                        maximumLineCount: 3
                                        elide: Text.ElideRight
                                    }
                                }
                            }

                            GridLayout {
                                visible: false
                                anchors.fill: parent
                                anchors.margins: 22
                                columns: 2
                                columnSpacing: 16
                                rowSpacing: 14

                                Text {
                                    text: "服务器地址"
                                    color: root.textColor
                                    font.pixelSize: 14
                                    Layout.preferredWidth: 96
                                }

                                TextField {
                                    id: webDavServerUrlField
                                    text: steamManager.webDavServerUrl
                                    placeholderText: "https://example.com/dav"
                                    color: root.textColor
                                    selectByMouse: true
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: "用户名"
                                    color: root.textColor
                                    font.pixelSize: 14
                                    Layout.preferredWidth: 96
                                }

                                TextField {
                                    id: webDavUsernameField
                                    text: steamManager.webDavUsername
                                    placeholderText: "WebDAV 用户名"
                                    color: root.textColor
                                    selectByMouse: true
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: "密码"
                                    color: root.textColor
                                    font.pixelSize: 14
                                    Layout.preferredWidth: 96
                                }

                                TextField {
                                    id: webDavPasswordField
                                    text: steamManager.webDavPassword
                                    placeholderText: "WebDAV 密码"
                                    echoMode: TextInput.Password
                                    color: root.textColor
                                    selectByMouse: true
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: "远程目录"
                                    color: root.textColor
                                    font.pixelSize: 14
                                    Layout.preferredWidth: 96
                                }

                                TextField {
                                    id: webDavRemoteRootField
                                    text: steamManager.webDavRemoteRootPath.length > 0 ? steamManager.webDavRemoteRootPath : "/GameSaveCloudQt"
                                    placeholderText: "/GameSaveCloudQt"
                                    color: root.textColor
                                    selectByMouse: true
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: "夸克 Cookie"
                                    color: root.textColor
                                    font.pixelSize: 14
                                    Layout.preferredWidth: 96
                                }

                                TextField {
                                    id: quarkCookieField
                                    text: steamManager.quarkCookie
                                    placeholderText: "粘贴夸克网盘 Cookie"
                                    echoMode: TextInput.Password
                                    color: root.textColor
                                    selectByMouse: true
                                    Layout.fillWidth: true
                                }

                                Item {
                                    Layout.preferredWidth: 96
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10

                                    Button {
                                        Layout.preferredWidth: 118
                                        Layout.preferredHeight: 38
                                        text: "保存配置"
                                        enabled: !steamManager.webDavTesting
                                        onClicked: steamManager.saveWebDavSettings(
                                            webDavServerUrlField.text,
                                            webDavUsernameField.text,
                                            webDavPasswordField.text,
                                            webDavRemoteRootField.text)
                                    }

                                    Button {
                                        Layout.preferredWidth: 126
                                        Layout.preferredHeight: 38
                                        text: steamManager.webDavTesting ? "测试中" : "测试连接"
                                        enabled: !steamManager.webDavTesting
                                        onClicked: steamManager.testWebDavConnection(
                                            webDavServerUrlField.text,
                                            webDavUsernameField.text,
                                            webDavPasswordField.text,
                                            webDavRemoteRootField.text)
                                    }

                                    Button {
                                        Layout.preferredWidth: 190
                                        Layout.preferredHeight: 38
                                        text: "保存夸克 Cookie"
                                        enabled: !steamManager.webDavTesting
                                        onClicked: steamManager.saveQuarkCookieGateway(quarkCookieField.text)
                                    }

                                    Item {
                                        Layout.fillWidth: true
                                    }
                                }

                                Item {
                                    Layout.preferredWidth: 96
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 86
                                    radius: 14
                                    color: root.darkMode ? "#222B3C" : "#FFFFFF"
                                    border.width: 1
                                    border.color: root.darkMode ? "#313C50" : "#E6EBF2"

                                    Text {
                                        anchors.fill: parent
                                        anchors.margins: 14
                                        text: steamManager.webDavConnectionStatus + "\n" + steamManager.quarkGatewayStatus
                                        color: root.connectionStatusColor()
                                        font.pixelSize: 13
                                        wrapMode: Text.Wrap
                                        maximumLineCount: 2
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 112
                            visible: false
                            radius: 20
                            color: root.darkMode ? "#172030" : "#F7F9FC"
                            border.width: 1
                            border.color: root.darkMode ? "#2A3448" : "#E8EDF5"

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 18
                                spacing: 8

                                Text {
                                    text: "当前保存位置"
                                    color: root.textColor
                                    font.pixelSize: 15
                                    font.bold: true
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: "服务器 " + (steamManager.webDavServerUrl.length > 0 ? steamManager.webDavServerUrl : "未配置") + " · 远程目录 " + (steamManager.webDavRemoteRootPath.length > 0 ? steamManager.webDavRemoteRootPath : "/GameSaveCloudQt")
                                    color: root.mutedTextColor
                                    font.pixelSize: 13
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: "本地快照目录 " + steamManager.snapshotRootPath
                                    color: root.mutedTextColor
                                    font.pixelSize: 13
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                        }

                        Item {
                            Layout.fillHeight: true
                        }
                    }
                }
            }
        }
    }
}
