import QtQuick

ConfirmDialog {
    id: control

    property string appId: ""
    property string gameName: ""

    signal confirmed(string appId)
    signal cleared

    dialogTitle: "删除存档快照"
    message: "确认删除 “" + control.gameName + "” 的本地存档快照？"
    detail: "这只会删除软件在本地快照目录中生成的 zip 快照和索引文件，不会删除游戏真实存档，也不会删除网盘里的云端快照。恢复前自动备份不在这里清理。"
    confirmText: "删除快照"
    cancelText: "取消"
    icon: "\uE74D"
    danger: true

    onConfirmRequested: {
        if (control.appId.length > 0) {
            control.confirmed(control.appId);
        }
        control.cleared();
    }

    onCancelRequested: control.cleared()
    onClosed: control.cleared()
}
