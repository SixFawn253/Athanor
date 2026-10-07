import QtQuick
import QtQuick.Controls
Control {
    id: control
    onActiveFocusChanged: if(activeFocus)Qt.callLater(function(){Theme.revealControl(control)})
    property string text: ""
    property string icon: ""
    property string accessibleText: text
    property string tooltip: ""
    property bool joined: false
    property bool quiet: false
    focusPolicy: Qt.TabFocus
    property bool large: false
    property bool primary: false
    property bool sidebar: false
    property bool selected: false
    property bool leftRounded: true
    property bool rightRounded: true
    property int radius: sidebar ? 0 : 12
    property color normalColor: primary ? Theme.accent : selected ? Theme.selection : quiet ? "transparent" : Theme.field
    property color foreground: !enabled ? Theme.disabled : primary ? Theme.onAccent : selected ? Theme.accent : Theme.text
    function activate(){if(enabled){pulse.restart();clicked()}}
    signal clicked()
    implicitWidth: label.implicitWidth + (icon.length ? (large ? 76 : 56) : (large ? 48 : 34))
    implicitHeight: sidebar ? 44 : large ? 52 : 42
    opacity: 1
    Accessible.role: Accessible.Button
    Accessible.name: accessibleText
    Accessible.onPressAction: activate()
    Rectangle {
        id: background
        anchors.fill: parent
        color: control.enabled ? control.normalColor : (control.sidebar ? "transparent" : Theme.field)
        topLeftRadius: control.leftRounded ? control.radius : 0
        bottomLeftRadius: topLeftRadius
        topRightRadius: control.rightRounded ? control.radius : 0
        bottomRightRadius: topRightRadius
        border.width: control.joined||control.quiet||control.primary ? 0 : 1
        border.color: Theme.softBorder
        clip: true
        layer.enabled: control.radius>0
        layer.effect: ShaderEffect {
            property var source
            property vector2d dimensions: Qt.vector2d(background.width,background.height)
            property vector2d cornerRadii: Qt.vector2d(control.leftRounded ? control.radius : 0,control.rightRounded ? control.radius : 0)
            property vector2d bottomRadii: cornerRadii
            fragmentShader: "qrc:/shaders/rounded-mask.frag.qsb"
        }
        Rectangle {
            anchors.centerIn: parent
            width: hover.hovered && control.enabled ? Math.sqrt(parent.width*parent.width + parent.height*parent.height)*2 : 0
            height: width
            radius: width/2
            color: Theme.hover
            opacity: control.primary ? 0.22 : 1
            Behavior on width { NumberAnimation { duration: Theme.fast; easing.type: Easing.OutCubic } }
        }
        Rectangle {
            id: ripple
            anchors.centerIn: parent
            width: 0
            height: width
            radius: width/2
            color: Theme.accent
            opacity: 0
        }
    }
    Row {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: control.sidebar ? parent.left : undefined
        anchors.leftMargin: control.sidebar ? 22 : 0
        anchors.horizontalCenter: control.sidebar ? undefined : parent.horizontalCenter
        spacing: 9
        Glyph {
            visible: control.icon.length > 0
            width: visible ? (control.large ? 20 : 18) : 0
            height: control.large ? 20 : 18
            anchors.verticalCenter: parent.verticalCenter
            name: control.icon
            color: control.foreground
        }
        Text {
            id: label
            visible: control.text.length>0
            text: control.text
            color: control.foreground
            font.pixelSize: control.large ? 14 : 13
            font.weight: control.primary ? Font.DemiBold : Font.Normal
        }
    }
    HoverHandler { id: hover; cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
    readonly property bool pointerInside: { var p=mapToItem(null,0,0);return Theme.pointer.x>=p.x&&Theme.pointer.x<p.x+width&&Theme.pointer.y>=p.y&&Theme.pointer.y<p.y+height }
    Hint { visible: control.visible&&control.pointerInside&&control.tooltip.length>0; text: control.tooltip }
    TapHandler {
        enabled: control.enabled
        onTapped: { control.forceActiveFocus(Qt.MouseFocusReason); control.activate() }
    }
    SequentialAnimation {
        id: pulse
        PropertyAction { target: ripple; property: "width"; value: 0 }
        PropertyAction { target: ripple; property: "opacity"; value: 0.75 }
        NumberAnimation { target: ripple; property: "width"; to: Math.sqrt(control.width*control.width+control.height*control.height)*2; duration: Theme.reveal; easing.type: Easing.OutCubic }
        NumberAnimation { target: ripple; property: "opacity"; to: 0; duration: Theme.fast }
    }
    Rectangle { anchors.fill: parent; anchors.margins: 2; radius: control.radius; color: "transparent"; border.width: 2; border.color: Theme.accent; visible: control.activeFocus&&Theme.keyboardFocus(control.focusReason) }
    Keys.onReturnPressed: activate()
    Keys.onSpacePressed: activate()
}
