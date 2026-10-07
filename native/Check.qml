import QtQuick
import QtQuick.Controls
CheckBox {
    id: control
    property bool caution: false
    font.pixelSize: 13
    implicitHeight: 32
    HoverHandler { cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
    indicator: Rectangle { x: 0; anchors.verticalCenter: parent.verticalCenter; width: 18; height: 18; radius: 5; color: control.checked ? Theme.accent : Theme.field; border.width: control.checked ? 0 : 1; border.color: Theme.border; Glyph { anchors.centerIn: parent; width: 14; height: 14; visible: control.checked; name: "check"; color: Theme.onAccent } }
    Rectangle { anchors.fill: parent; radius: 8; color: "transparent"; border.width: 2; border.color: Theme.accent; visible: control.activeFocus&&Theme.keyboardFocus(control.focusReason) }
    contentItem: Text { text: control.text; font: control.font; leftPadding: 26; verticalAlignment: Text.AlignVCenter; color: control.enabled ? (Theme.text) : (Theme.muted) }
}
