import QtQuick
Item {
    implicitWidth: 15
    implicitHeight: 15
    Repeater {
        model: 9
        Rectangle {
            required property int index
            x: (index % 3)*parent.width/3
            y: Math.floor(index/3)*parent.height/3
            width: parent.width/3
            height: parent.height/3
            color: index%2 ? (backend.dark ? "#50515e" : "#b4b2c3") : (backend.dark ? "#c6c4d0" : "#f5f4fa")
        }
    }
}
