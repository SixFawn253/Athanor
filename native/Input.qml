import QtQuick
import QtQuick.Controls
TextField {
    id: control
    onActiveFocusChanged: if(activeFocus)Qt.callLater(function(){Theme.revealControl(control)})
    property bool invalid: false
    property bool leftRounded: true
    property bool rightRounded: true
    implicitHeight: Theme.controlHeight
    font.pixelSize: 13
    color: Theme.text
    placeholderTextColor: Theme.muted
    selectionColor: Theme.accent
    selectedTextColor: Theme.onAccent
    selectByMouse: true
    leftPadding: 14
    rightPadding: 14
    background: Rectangle { topLeftRadius: control.leftRounded?Theme.radius:0; bottomLeftRadius: topLeftRadius; topRightRadius: control.rightRounded?Theme.radius:0; bottomRightRadius: topRightRadius; color: Theme.field; border.width: control.activeFocus ? 2 : 1; border.color: control.acceptableInput&&!control.invalid ? (control.activeFocus ? Theme.accent : Theme.border) : Theme.error }
}
