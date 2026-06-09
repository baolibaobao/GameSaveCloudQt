import QtQuick
import QtQuick.Controls

ScrollBar {
    id: control

    property bool darkMode: false

    policy: ScrollBar.AsNeeded
    interactive: true
    hoverEnabled: true
    minimumSize: 0.08
    padding: 0
    implicitWidth: 8

    background: Item {
        implicitWidth: control.implicitWidth
    }

    contentItem: Rectangle {
        implicitWidth: (control.hovered || control.active) ? 6 : 4
        radius: width / 2
        color: control.darkMode
               ? (control.hovered || control.active ? "#9AA8BB" : "#7B8798")
               : (control.hovered || control.active ? "#8FA2BA" : "#A9B8CA")
        opacity: control.size < 1.0 ? ((control.hovered || control.active) ? 0.62 : 0.0) : 0.0

        Behavior on opacity {
            NumberAnimation { duration: 180 }
        }

        Behavior on implicitWidth {
            NumberAnimation { duration: 120 }
        }
    }
}
