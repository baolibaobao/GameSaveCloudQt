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
    property bool cookieVisible: false

    anchors.margins: 24
    spacing: 18

    function savedCookie() {
        return control.backend && control.backend.quarkCookie.length > 0
    }

    function connectButtonLabel() {
        if (control.backend && control.backend.webDavTesting) {
            return "连接中"
        }
        return control.savedCookie() ? "重新连接" : "连接"
    }

    function statusColor(message) {
        if (!message || message.length === 0) {
            return control.mutedTextColor
        }
        if (message.indexOf("失败") >= 0
                || message.indexOf("过期") >= 0
                || message.indexOf("异常") >= 0
                || message.indexOf("Forbidden") >= 0
                || message.indexOf("Precondition") >= 0) {
            return "#D97706"
        }
        if (message.indexOf("成功") >= 0
                || message.indexOf("通过") >= 0
                || message.indexOf("可用") >= 0
                || message.indexOf("可上传") >= 0) {
            return "#16A34A"
        }
        return control.backend && control.backend.webDavTesting ? control.accentColor : control.mutedTextColor
    }

    function webDavStatusText() {
        return control.backend ? control.backend.webDavConnectionStatus : ""
    }

    function quarkStatusText() {
        return control.backend ? control.backend.quarkGatewayStatus : ""
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 16

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            Text {
                text: "云同步设置"
                color: control.textColor
                font.pixelSize: 28
                font.bold: true
            }

            Text {
                text: control.webDavStatusText()
                color: control.statusColor(control.webDavStatusText())
                font.pixelSize: 14
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 218
        radius: 20
        color: control.darkMode ? "#172030" : "#F7F9FC"
        border.width: 1
        border.color: control.darkMode ? "#2A3448" : "#E8EDF5"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 22
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Text {
                    text: "夸克网盘连接"
                    color: control.textColor
                    font.pixelSize: 18
                    font.bold: true
                    Layout.fillWidth: true
                }

                Rectangle {
                    Layout.preferredWidth: connectionBadgeText.implicitWidth + 22
                    Layout.preferredHeight: 28
                    radius: 14
                    color: control.savedCookie()
                           ? (control.darkMode ? "#243F33" : "#DCFCE7")
                           : (control.darkMode ? "#263244" : "#E8EEF7")

                    Text {
                        id: connectionBadgeText
                        anchors.centerIn: parent
                        text: control.savedCookie() ? "已保存 Cookie" : "未连接"
                        color: control.savedCookie() ? "#16A34A" : "#64748B"
                        font.pixelSize: 12
                        font.bold: true
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 48
                spacing: 12

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 14
                    color: control.darkMode ? "#222B3C" : "#FFFFFF"
                    border.width: 1
                    border.color: cookieField.activeFocus
                                  ? control.accentColor
                                  : (control.darkMode ? "#334155" : "#D8E4F2")

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 8
                        spacing: 10

                        Text {
                            text: "\uE72E"
                            color: control.accentColor
                            font.family: "Segoe MDL2 Assets"
                            font.pixelSize: 15
                        }

                        TextField {
                            id: cookieField
                            text: control.backend ? control.backend.quarkCookie : ""
                            placeholderText: "粘贴夸克 Cookie"
                            echoMode: control.cookieVisible ? TextInput.Normal : TextInput.Password
                            color: control.textColor
                            placeholderTextColor: control.mutedTextColor
                            selectByMouse: true
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            verticalAlignment: TextInput.AlignVCenter
                            background: Item {}
                        }

                        Rectangle {
                            Layout.preferredWidth: 34
                            Layout.preferredHeight: 34
                            radius: 10
                            color: toggleCookieArea.containsMouse
                                   ? (control.darkMode ? "#2A3448" : "#E7F3FF")
                                   : "transparent"

                            Text {
                                anchors.centerIn: parent
                                text: control.cookieVisible ? "\uE890" : "\uE7B3"
                                color: control.mutedTextColor
                                font.family: "Segoe MDL2 Assets"
                                font.pixelSize: 14
                            }

                            MouseArea {
                                id: toggleCookieArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: control.cookieVisible = !control.cookieVisible
                            }
                        }
                    }
                }

                ActionButton {
                    Layout.preferredWidth: 128
                    Layout.fillHeight: true
                    kind: "primary"
                    icon: "\uE774"
                    label: control.connectButtonLabel()
                    enabled: control.backend && !control.backend.webDavTesting
                    darkMode: control.darkMode
                    accentColor: control.accentColor
                    labelPixelSize: 13
                    onClicked: control.backend.saveQuarkCookieGateway(cookieField.text)
                }
            }

            Text {
                Layout.fillWidth: true
                text: control.savedCookie()
                      ? "已保存 Cookie；如明确提示鉴权异常或 Cookie 过期，请粘贴新的 Cookie 后重新连接。"
                      : "首次连接成功后会自动保存配置，之后启动软件会自动连接。"
                color: control.mutedTextColor
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 170
        radius: 20
        color: control.darkMode ? "#172030" : "#F7F9FC"
        border.width: 1
        border.color: control.darkMode ? "#2A3448" : "#E8EDF5"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 22
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Text {
                    text: "连接状态"
                    color: control.textColor
                    font.pixelSize: 18
                    font.bold: true
                    Layout.fillWidth: true
                }

                ActionButton {
                    Layout.preferredWidth: 116
                    Layout.preferredHeight: 34
                    kind: "ghost"
                    icon: "\uE72C"
                    label: "重新检测"
                    enabled: control.backend && control.savedCookie() && !control.backend.webDavTesting
                    darkMode: control.darkMode
                    accentColor: control.accentColor
                    labelPixelSize: 12
                    onClicked: control.backend.recheckQuarkGatewayHealth()
                }
            }

            StatusRow {
                Layout.fillWidth: true
                title: "网盘连接"
                message: control.webDavStatusText()
                dotColor: control.statusColor(control.webDavStatusText())
                darkMode: control.darkMode
                textColor: control.textColor
                mutedTextColor: control.mutedTextColor
            }

            StatusRow {
                Layout.fillWidth: true
                title: "Cookie 健康检查"
                message: control.quarkStatusText()
                dotColor: control.statusColor(control.quarkStatusText())
                darkMode: control.darkMode
                textColor: control.textColor
                mutedTextColor: control.mutedTextColor
            }

            Text {
                Layout.fillWidth: true
                text: control.backend
                      ? "本地快照目录 " + control.backend.snapshotRootPath
                      : ""
                color: control.mutedTextColor
                font.pixelSize: 12
                elide: Text.ElideRight
            }
        }
    }

    Item {
        Layout.fillHeight: true
    }

    component StatusRow: Rectangle {
        id: rowControl

        property string title: ""
        property string message: ""
        property color dotColor: "#64748B"
        property bool darkMode: false
        property color textColor: "#172033"
        property color mutedTextColor: "#6B7588"

        implicitHeight: 42
        radius: 14
        color: rowControl.darkMode ? "#222B3C" : "#FFFFFF"
        border.width: 1
        border.color: rowControl.darkMode ? "#313C50" : "#E6EBF2"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 10

            Rectangle {
                Layout.preferredWidth: 9
                Layout.preferredHeight: 9
                radius: 5
                color: rowControl.dotColor
            }

            Text {
                Layout.preferredWidth: 112
                text: rowControl.title
                color: rowControl.textColor
                font.pixelSize: 13
                font.bold: true
                elide: Text.ElideRight
            }

            Text {
                Layout.fillWidth: true
                text: rowControl.message
                color: rowControl.mutedTextColor
                font.pixelSize: 13
                elide: Text.ElideRight
            }
        }
    }
}
