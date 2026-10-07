import QtQuick
import QtQuick.Controls
ComboBox {
    id: control
    onActiveFocusChanged: if(activeFocus)Qt.callLater(function(){Theme.revealControl(control)})
    property string accessibleLabel: "Output format"
    Accessible.name: accessibleLabel
    Accessible.description: transparencyIndices.indexOf(currentIndex)>=0 ? "Supports transparency" : ""
    property var transparencyIndices: []
    property var compactIndices: []
    implicitHeight: Theme.controlHeight
    font.pixelSize: 13
    opacity: 1
    focusPolicy: Qt.StrongFocus
    contentItem: Item {
        Checker { id: badge; objectName: "transparencyBadge"; visible: control.transparencyIndices.indexOf(control.currentIndex)>=0; x: parent.width-width-38; width: 15; height: 15; anchors.verticalCenter: parent.verticalCenter }
        Glyph { id: compactBadge; objectName: "compactBadge"; name: "compact"; visible: control.compactIndices.indexOf(control.currentIndex)>=0; color: Theme.accent; x: parent.width-width-38-(badge.visible?22:0); width: 17; height: 17; anchors.verticalCenter: parent.verticalCenter }
        Text { anchors.fill: parent; leftPadding: 14; rightPadding: 34+(badge.visible?22:0)+(compactBadge.visible?22:0); verticalAlignment: Text.AlignVCenter; text: control.displayText; color: control.enabled ? Theme.text : Theme.disabled; font: control.font; elide: Text.ElideRight }
    }
    background: Rectangle {
        radius: Theme.radius
        color: control.hovered ? (Theme.hover) : (Theme.field)
        border.width: control.activeFocus&&Theme.keyboardFocus(control.focusReason) ? 2 : 1
        border.color: !control.enabled ? Theme.softBorder : control.activeFocus&&Theme.keyboardFocus(control.focusReason) ? Theme.accent : Theme.border
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
    indicator: Glyph { name: "chevron"; color: control.enabled ? Theme.muted : Theme.disabled; width: 16; height: 16; x: control.width-width-12; y: (control.height-height)/2 }
    HoverHandler { id: fieldHover }
    Hint { visible: fieldHover.hovered&&(control.transparencyIndices.indexOf(control.currentIndex)>=0||control.compactIndices.indexOf(control.currentIndex)>=0); text: (control.transparencyIndices.indexOf(control.currentIndex)>=0?"Supports transparency":"")+(control.transparencyIndices.indexOf(control.currentIndex)>=0&&control.compactIndices.indexOf(control.currentIndex)>=0?" · ":"")+(control.compactIndices.indexOf(control.currentIndex)>=0?"Efficient compression":"") }
    delegate: ItemDelegate {
        id: option
        required property int index
        required property var modelData
        width: formatList.width
        height: Theme.controlHeight
        hoverEnabled: true
        highlighted: control.highlightedIndex===index
        contentItem: Item {
            Checker { id: entryBadge; visible: control.transparencyIndices.indexOf(option.index)>=0; x: parent.width-width-14; width: 15; height: 15; anchors.verticalCenter: parent.verticalCenter }
            Glyph { id: entryCompact; name: "compact"; color: Theme.accent; visible: control.compactIndices.indexOf(option.index)>=0; x: parent.width-width-14-(entryBadge.visible?22:0); width: 17; height: 17; anchors.verticalCenter: parent.verticalCenter }
            Text { anchors.fill: parent; leftPadding: 14; rightPadding: 14+(entryBadge.visible?22:0)+(entryCompact.visible?22:0); verticalAlignment: Text.AlignVCenter; text: typeof option.modelData === "object" ? option.modelData[control.textRole] : option.modelData; color: Theme.text; font: control.font; elide: Text.ElideRight }
        }
        leftPadding: 0; rightPadding: 0; topPadding: 0; bottomPadding: 0
        background: Rectangle {
            color: option.index===control.currentIndex ? Theme.selection : Theme.menuSurface
            Rectangle { anchors.fill: parent; color: Theme.stateLayer; opacity: option.hovered||option.highlighted?Theme.hoverOpacity:0; Behavior on opacity { NumberAnimation { duration: Theme.fast } } }
        }
    }
    popup: Popup {
        // Keep input routing in the application's overlay on every platform.
        popupType: Popup.Item
        modal: true
        dim: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        x: control.width+6
        y: (control.height-height)/2
        width: Math.max(control.width,190)
        readonly property real maximumMenuHeight: Math.max(Theme.controlHeight,control.Overlay.overlay?control.Overlay.overlay.height-26:480)
        implicitHeight: Math.min(control.count*Theme.controlHeight,maximumMenuHeight)+2
        padding: 1
        margins: 12
        background: Rectangle { radius: Theme.radius; color: Theme.menuSurface; border.width: 1; border.color: Theme.border }
        contentItem: Item {
            id: menuViewport
            implicitHeight: Math.min(formatList.contentHeight,control.popup.maximumMenuHeight)
            layer.enabled: true
            layer.effect: ShaderEffect {
                property var source
                property vector2d dimensions: Qt.vector2d(menuViewport.width,menuViewport.height)
                property vector2d cornerRadii: Qt.vector2d(Theme.radius-1,Theme.radius-1)
                property vector2d bottomRadii: cornerRadii
                fragmentShader: "qrc:/shaders/rounded-mask.frag.qsb"
            }
            ListView { id: formatList; objectName: "formatMenuList"; anchors.fill: parent; clip: true; spacing: 0; boundsBehavior: Flickable.StopAtBounds; model: control.popup.visible ? control.delegateModel : null; currentIndex: control.highlightedIndex; ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; visible: formatList.contentHeight>formatList.height+0.5; width: 6; contentItem: Rectangle { implicitWidth: 4; radius: 2; color: Theme.muted } } }
        }
    }
}
