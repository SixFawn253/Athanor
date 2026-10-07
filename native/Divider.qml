import QtQuick
Item {
    id: divider
    property color color: Theme.softBorder
    implicitWidth: 1
    implicitHeight: 24
    Accessible.ignored: true
    Rectangle { anchors.centerIn: parent; width: parent.width; height: parent.height; radius: width/2; gradient: Gradient { GradientStop { position: 0; color: "transparent" } GradientStop { position: 0.3; color: divider.color } GradientStop { position: 0.7; color: divider.color } GradientStop { position: 1; color: "transparent" } } }
}
