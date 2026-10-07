import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Item {
    id: choice
    property bool targetValid: true
    property bool valid: !backend.options.size_mode || targetValid
    property int selectedIndex: backend.options.convert_only ? 2 : backend.options.size_mode ? 1 : 0
    implicitHeight: 44
    Rectangle { anchors.fill: parent; color: Theme.header; topLeftRadius: 16; topRightRadius: 16 }
    Row {
        anchors.fill: parent
        Repeater {
            id: tabs
            model: ["Compress by quality","Compress to target size","Convert"]
            delegate: Control {
                id: tab
                objectName: "modeTab"+index
                required property int index
                required property string modelData
                enabled: !backend.busy
                property bool hoverActive: tabHover.hovered&&!backend.busy
                function animateHover(active){hoverTransition.stop();hoverTransition.to=active?Math.sqrt(width*width+height*height)*2:0;hoverTransition.restart()}
                onHoverActiveChanged: animateHover(hoverActive)
                onWidthChanged: if(hoverActive)animateHover(true)
                activeFocusOnTab: true
                Accessible.role: Accessible.PageTab
                Accessible.name: modelData
                Accessible.checkable: true
                Accessible.checked: choice.selectedIndex===index
                Accessible.onPressAction: if(!backend.busy) backend.setOption("mode",["quality","target","convert"][index])
                Keys.onLeftPressed: if(!backend.busy){var i=(index+2)%3;backend.setOption("mode",["quality","target","convert"][i]);tabs.itemAt(i).forceActiveFocus(Qt.TabFocusReason)}
                Keys.onRightPressed: if(!backend.busy){var i=(index+1)%3;backend.setOption("mode",["quality","target","convert"][i]);tabs.itemAt(i).forceActiveFocus(Qt.TabFocusReason)}
                Keys.onReturnPressed: if(!backend.busy) backend.setOption("mode",["quality","target","convert"][index])
                Keys.onSpacePressed: if(!backend.busy) backend.setOption("mode",["quality","target","convert"][index])
                width: choice.width/3
                height: choice.height
                background: Rectangle { topLeftRadius: tab.index===0 ? 16 : 0; topRightRadius: tab.index===2 ? 16 : 0; color: choice.selectedIndex===tab.index ? Theme.surface : "transparent" }
                clip: true
                layer.enabled: index!==1
                layer.effect: ShaderEffect {
                    property var source
                    property vector2d dimensions: Qt.vector2d(tab.width,tab.height)
                    property vector2d cornerRadii: Qt.vector2d(tab.index===0 ? 16 : 0,tab.index===2 ? 16 : 0)
                    property vector2d bottomRadii: Qt.vector2d(0,0)
                    fragmentShader: "qrc:/shaders/rounded-mask.frag.qsb"
                }
                Rectangle {
                    id: hoverReveal
                    objectName: "tabReveal"+tab.index
                    anchors.centerIn: parent
                    width: 0
                    height: width
                    radius: width/2
                    color: Theme.stateLayer
                    opacity: Theme.hoverOpacity

                }
                NumberAnimation { id: hoverTransition; objectName: "tabHoverAnimation"+tab.index; target: hoverReveal; property: "width"; duration: Theme.reveal; easing.type: Easing.OutCubic }
                Rectangle { id: ripple; anchors.centerIn: parent; width: 0; height: width; radius: width/2; color: Theme.accent; opacity: 0 }
                SequentialAnimation {
                    id: pulse
                    PropertyAction { target: ripple; property: "width"; value: 0 }
                    PropertyAction { target: ripple; property: "opacity"; value: .45 }
                    NumberAnimation { target: ripple; property: "width"; to: Math.sqrt(tab.width*tab.width+tab.height*tab.height)*2; duration: Theme.reveal; easing.type: Easing.OutCubic }
                    NumberAnimation { target: ripple; property: "opacity"; to: 0; duration: Theme.fast }
                }
                Text { anchors.centerIn: parent; text: tab.modelData; font.pixelSize: 13; font.weight: choice.selectedIndex===tab.index ? Font.DemiBold : Font.Normal; color: choice.selectedIndex===tab.index ? (Theme.accent) : (Theme.muted); fontSizeMode: Text.Fit; width: parent.width-16; horizontalAlignment: Text.AlignHCenter }
                Rectangle { anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; width: choice.selectedIndex===tab.index ? parent.width-32 : 0; height: 2; radius: 1; color: Theme.accent; Behavior on width { NumberAnimation { duration: Theme.fast } } }
                Rectangle { anchors.fill: parent; anchors.margins: 3; color: "transparent"; border.width: 2; border.color: Theme.accent; visible: tab.activeFocus&&Theme.keyboardFocus(tab.focusReason) }
                HoverHandler { id: tabHover; objectName: "tabPointer"+tab.index; cursorShape: backend.busy ? Qt.ArrowCursor : Qt.PointingHandCursor }
                MouseArea { id: mouse; objectName: "tabMouse"+tab.index; anchors.fill: parent; hoverEnabled: true; enabled: !backend.busy; cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor; onClicked: { tab.forceActiveFocus(Qt.MouseFocusReason); pulse.restart(); backend.setOption("mode",["quality","target","convert"][tab.index]) } }
            }
        }
    }
    Rectangle { anchors.fill: parent; topLeftRadius: 16; topRightRadius: 16; color: "transparent"; border.width: 1; border.color: Theme.groupBorder; z: 2 }
}
