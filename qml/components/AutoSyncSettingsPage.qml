import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: control

    property var backend
    property bool darkMode: false
    property color textColor: "#172033"
    property color mutedTextColor: "#6B7588"
    property color accentColor: "#0078D4"
    property int revision: 0

    anchors.margins: 24
    spacing: 18

    function gameAutoSyncEnabled(appId) {
        control.revision
        return control.backend && appId && appId.length > 0
                ? control.backend.autoSyncEnabledForGame(appId)
                : false
    }

    function recentResultText(status) {
        return status && status.length > 0 ? status : "暂无自动同步记录"
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 16

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            Text {
                text: "自动同步"
                color: control.textColor
                font.pixelSize: 28
                font.bold: true
            }

            Text {
                text: control.backend && control.backend.autoSyncEnabled
                      ? "已开启总开关。下面列表中启用的游戏会在关闭时自动检查存档变化，并在需要时创建本地 zip 快照。"
                      : "未开启总开关。开启后才会显示游戏参与列表，单个游戏默认不参与自动同步。"
                color: control.mutedTextColor
                font.pixelSize: 14
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 112
        radius: 20
        color: control.darkMode ? "#172030" : "#F7F9FC"
        border.width: 1
        border.color: control.darkMode ? "#2A3448" : "#E8EDF5"

        RowLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 16

            Rectangle {
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                radius: 16
                color: control.backend && control.backend.autoSyncEnabled
                       ? (control.darkMode ? "#1E4732" : "#DCFCE7")
                       : (control.darkMode ? "#263244" : "#E8EEF7")
                border.width: 1
                border.color: control.backend && control.backend.autoSyncEnabled
                              ? (control.darkMode ? "#23735F" : "#A7F3D0")
                              : (control.darkMode ? "#334155" : "#CBD5E1")

                Text {
                    anchors.centerIn: parent
                    text: "\uE895"
                    color: control.backend && control.backend.autoSyncEnabled ? "#16A34A" : "#64748B"
                    font.family: "Segoe MDL2 Assets"
                    font.pixelSize: 21
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5

                Text {
                    text: "全局自动同步"
                    color: control.textColor
                    font.pixelSize: 17
                    font.bold: true
                }

                Text {
                    text: "这是总开关。开启后，才可以为每个游戏单独设置是否参与自动同步。"
                    color: control.mutedTextColor
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            ToggleSwitch {
                checked: control.backend ? control.backend.autoSyncEnabled : false
                enabled: !!control.backend
                darkMode: control.darkMode
                accentColor: control.accentColor
                onToggled: function(nextChecked) {
                    control.backend.setAutoSyncEnabled(nextChecked)
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

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 12

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: !control.backend || !control.backend.autoSyncEnabled

                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 40, 520)
                    spacing: 12

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "\uE895"
                        color: control.mutedTextColor
                        font.family: "Segoe MDL2 Assets"
                        font.pixelSize: 42
                    }

                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: "开启全局自动同步后，可为每个游戏单独设置是否参与自动同步。"
                        color: control.textColor
                        font.pixelSize: 16
                        font.bold: true
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: "单个游戏默认关闭。你可以进入列表后手动开启指定游戏，或使用全开/全关批量管理。"
                        color: control.mutedTextColor
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: control.backend && control.backend.autoSyncEnabled
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Text {
                        Layout.fillWidth: true
                        text: "游戏参与列表"
                        color: control.textColor
                        font.pixelSize: 18
                        font.bold: true
                    }

                    ActionButton {
                        Layout.preferredWidth: 92
                        Layout.preferredHeight: 36
                        kind: "ghost"
                        icon: "\uE73E"
                        label: "全开"
                        darkMode: control.darkMode
                        accentColor: control.accentColor
                        labelPixelSize: 12
                        onClicked: control.backend.setAutoSyncEnabledForAllGames(true)
                    }

                    ActionButton {
                        Layout.preferredWidth: 92
                        Layout.preferredHeight: 36
                        kind: "ghost"
                        icon: "\uE711"
                        label: "全关"
                        darkMode: control.darkMode
                        accentColor: control.accentColor
                        labelPixelSize: 12
                        onClicked: control.backend.setAutoSyncEnabledForAllGames(false)
                    }

                    Text {
                        text: control.backend ? control.backend.gameModel.count + " 个游戏" : "0 个游戏"
                        color: control.mutedTextColor
                        font.pixelSize: 13
                    }
                }

                ListView {
                    id: gameList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 8
                    clip: true
                    model: control.backend ? control.backend.gameModel : null
                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }

                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 82
                        radius: 16
                        color: control.darkMode ? "#1E293B" : "#FFFFFF"
                        border.width: 1
                        border.color: control.darkMode ? "#313C50" : "#E6EBF2"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            spacing: 12

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4

                                RowLayout {
                                    Layout.fillWidth: true

                                    Text {
                                        Layout.fillWidth: true
                                        text: displayName
                                        color: control.textColor
                                        font.pixelSize: 14
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }

                                    Rectangle {
                                        Layout.preferredWidth: autoSyncStateText.implicitWidth + 16
                                        Layout.preferredHeight: 22
                                        radius: 11
                                        color: control.gameAutoSyncEnabled(appId)
                                               ? (control.darkMode ? "#1E4732" : "#DCFCE7")
                                               : (control.darkMode ? "#263244" : "#E8EEF7")
                                        border.width: 1
                                        border.color: control.gameAutoSyncEnabled(appId) ? "#86EFAC" : "#CBD5E1"

                                        Text {
                                            id: autoSyncStateText
                                            anchors.centerIn: parent
                                            text: control.gameAutoSyncEnabled(appId) ? "已开启" : "已关闭"
                                            color: control.gameAutoSyncEnabled(appId) ? "#16A34A" : "#64748B"
                                            font.pixelSize: 11
                                            font.bold: true
                                        }
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: "AppID " + appId
                                    color: control.mutedTextColor
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: "最近结果：" + control.recentResultText(syncStatus)
                                    color: control.mutedTextColor
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                            }

                            ToggleSwitch {
                                checked: control.gameAutoSyncEnabled(appId)
                                darkMode: control.darkMode
                                accentColor: control.accentColor
                                onToggled: function(nextChecked) {
                                    control.backend.setAutoSyncEnabledForGame(appId, nextChecked)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    component ToggleSwitch: Rectangle {
        id: switchControl

        property bool checked: false
        property bool darkMode: false
        property color accentColor: "#0078D4"

        signal toggled(bool checked)

        Layout.preferredWidth: 58
        Layout.preferredHeight: 30
        radius: 15
        opacity: enabled ? 1.0 : 0.45
        color: checked ? accentColor : (darkMode ? "#263244" : "#E4EBF4")
        border.width: 1
        border.color: checked ? accentColor : (darkMode ? "#334155" : "#CBD5E1")

        Rectangle {
            width: 24
            height: 24
            radius: 12
            x: switchControl.checked ? parent.width - width - 3 : 3
            y: 3
            color: "#FFFFFF"

            Behavior on x {
                NumberAnimation {
                    duration: 160
                    easing.type: Easing.OutCubic
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            enabled: switchControl.enabled
            hoverEnabled: true
            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: switchControl.toggled(!switchControl.checked)
        }
    }
}
