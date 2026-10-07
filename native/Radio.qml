import QtQuick
import QtQuick.Controls
RadioButton {
    id: control
    HoverHandler { cursorShape: Qt.PointingHandCursor }
    font.pixelSize: 13
    implicitHeight: 38
    indicator: Rectangle { x: 0; anchors.verticalCenter: parent.verticalCenter; width: 18; height: 18; radius: 9; color: "transparent"; border.width: 1; border.color: control.checked ? (Theme.accent) : (Theme.muted); Rectangle { anchors.centerIn: parent; width: 10; height: 10; radius: 5; visible: control.checked; color: Theme.accent } }
    Rectangle { anchors.fill: parent; radius: 8; color: "transparent"; border.width: 2; border.color: Theme.accent; visible: control.activeFocus&&Theme.keyboardFocus(control.focusReason) }
    contentItem: Text { text: control.text; font: control.font; leftPadding: 30; verticalAlignment: Text.AlignVCenter; color: Theme.text }
}
