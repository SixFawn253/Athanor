import QtQuick
import QtQuick.Controls
SpinBox {
    id: control
    onActiveFocusChanged: if(activeFocus)Qt.callLater(function(){Theme.revealControl(control)})
    implicitWidth: 104
    implicitHeight: 42
    leftPadding: 28
    rightPadding: 28
    editable: true
    font.pixelSize: 13
    property bool upAvailable: enabled && (to>=from ? value<to : value>to)
    property bool downAvailable: enabled && (to>=from ? value>from : value<from)
    property int previousValue: value
    property bool initialized: false
    Component.onCompleted: { previousValue=value; initialized=true }
    onValueChanged: {
        if(initialized&&value!==previousValue) {
            slide.stop(); slide.from=value>previousValue ? -12 : 12; slide.restart(); fade.restart()
        }
        previousValue=value
    }
    background: Rectangle { radius: Theme.radius; color: Theme.field; border.width: control.activeFocus&&Theme.keyboardFocus(control.focusReason) ? 2 : 1; border.color: control.activeFocus&&Theme.keyboardFocus(control.focusReason) ? Theme.accent : Theme.border }
    contentItem: TextInput {
        id: number
        property real shift: 0
        transform: Translate { x: number.shift }
        text: control.textFromValue(control.value,control.locale)
        font: control.font
        color: Theme.text
        selectionColor: Theme.accent
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        clip: true
    }
    NumberAnimation { id: slide; target: number; property: "shift"; to: 0; duration: Theme.fast; easing.type: Easing.OutCubic }
    NumberAnimation { id: fade; target: number; property: "opacity"; from: .35; to: 1; duration: Theme.fast }
    up.indicator: Rectangle {
        x: control.width-width; width: 28; height: control.height
        topRightRadius: 12; bottomRightRadius: 12
        color: control.up.pressed ? (Theme.selection) : control.up.hovered ? (Theme.hover) : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.fast } }
        Text { anchors.centerIn: parent; text: "+"; font.pixelSize: 19; color: control.upAvailable ? (Theme.text) : (Theme.disabled) }
        HoverHandler { enabled: control.upAvailable; cursorShape: Qt.PointingHandCursor }
    }
    down.indicator: Rectangle {
        x: 0; width: 28; height: control.height
        topLeftRadius: 12; bottomLeftRadius: 12
        color: control.down.pressed ? (Theme.selection) : control.down.hovered ? (Theme.hover) : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.fast } }
        Text { anchors.centerIn: parent; text: "−"; font.pixelSize: 19; color: control.downAvailable ? (Theme.text) : (Theme.disabled) }
        HoverHandler { enabled: control.downAvailable; cursorShape: Qt.PointingHandCursor }
    }
}
