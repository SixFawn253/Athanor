import QtQuick
import QtQuick.Controls
ToolTip {
    id: hint
    delay: 500
    padding: 10
    width: Math.min(implicitWidth,600)
    contentItem: Text { text: hint.text; color: Theme.text; font.pixelSize: 12; wrapMode: Text.Wrap }
    background: Rectangle { radius: 8; color: Theme.field; border.width: 1; border.color: Theme.border }
}
