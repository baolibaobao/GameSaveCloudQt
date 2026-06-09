import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: control

    property var backend
    property bool darkMode: false
    property color cardColor: "#FFFFFF"
    property color textColor: "#172033"
    property color mutedTextColor: "#6B7588"
    property color accentColor: "#0078D4"

    anchors.margins: 24
    spacing: 18

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
        return control.accentColor
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 16

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            Text {
                text: "运行日志"
                color: control.textColor
                font.pixelSize: 28
                font.bold: true
            }

            Text {
                text: !control.backend
                      ? ""
                      : "当前显示日志 " + control.backend.logModel.count + " 条 · 总日志 " + control.backend.logModel.totalCount + " 条 · 文件 " + control.backend.logFilePath
                color: control.mutedTextColor
                font.pixelSize: 14
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }

        ActionButton {
            Layout.preferredWidth: 132
            Layout.preferredHeight: 40
            kind: "ghost"
            icon: "\uE8B7"
            label: "打开目录"
            enabled: !!control.backend
            darkMode: control.darkMode
            accentColor: control.accentColor
            labelPixelSize: 13
            onClicked: control.backend.openLogDirectory()
        }

        ActionButton {
            Layout.preferredWidth: 132
            Layout.preferredHeight: 40
            kind: "danger"
            icon: "\uE74D"
            label: "清空显示"
            enabled: !!control.backend
            darkMode: control.darkMode
            accentColor: control.accentColor
            labelPixelSize: 13
            onClicked: control.backend.clearVisibleLogs()
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 10

        Text {
            text: !control.backend ? "" : "日志目录 " + control.backend.logDirectory
            color: control.mutedTextColor
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
                color: control.backend && control.backend.logModel.filterLevel === modelData.value
                       ? control.accentColor
                       : (filterArea.containsMouse ? (control.darkMode ? "#2A3448" : "#E5F2FF") : (control.darkMode ? "#222B3C" : "#EEF5FC"))
                border.width: 1
                border.color: control.backend && control.backend.logModel.filterLevel === modelData.value
                              ? control.accentColor
                              : (control.darkMode ? "#334155" : "#D8E4F2")

                Text {
                    anchors.centerIn: parent
                    text: modelData.label
                    color: control.backend && control.backend.logModel.filterLevel === modelData.value ? "white" : control.textColor
                    font.pixelSize: 13
                    font.bold: control.backend && control.backend.logModel.filterLevel === modelData.value
                }

                MouseArea {
                    id: filterArea
                    anchors.fill: parent
                    enabled: !!control.backend
                    hoverEnabled: true
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: {
                        control.backend.logModel.filterLevel = modelData.value
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
        color: control.darkMode ? "#172030" : "#F7F9FC"
        border.width: 1
        border.color: control.darkMode ? "#2A3448" : "#E8EDF5"
        clip: true

        ListView {
            id: logList
            anchors.fill: parent
            anchors.margins: 14
            spacing: 8
            model: control.backend ? control.backend.logModel : null
            clip: true

            delegate: Rectangle {
                width: ListView.view.width
                height: 72
                radius: 14
                color: control.cardColor
                border.width: 1
                border.color: control.darkMode ? "#313C50" : "#E6EBF2"
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
                        color: control.logLevelColor(level)
                    }

                    ColumnLayout {
                        Layout.preferredWidth: 156
                        spacing: 4

                        Text {
                            text: levelName
                            color: control.logLevelColor(level)
                            font.pixelSize: 13
                            font.bold: true
                        }

                        Text {
                            text: timeText
                            color: control.mutedTextColor
                            font.pixelSize: 12
                        }
                    }

                    Text {
                        text: message
                        color: control.textColor
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
            visible: control.backend && control.backend.logModel.count === 0
            text: control.backend && control.backend.logModel.totalCount === 0 ? "暂无运行日志" : "当前筛选条件下暂无日志"
            color: control.mutedTextColor
            font.pixelSize: 16
        }
    }
}
