import QtQuick
Item {
    id: glyph
    property string name: ""
    property color color: Theme.text
    Accessible.ignored: true
    implicitWidth: 20
    implicitHeight: 20
    Image {
        anchors.fill: parent
        source: glyph.name.length ? "image://icons/"+glyph.name+"/"+glyph.color.toString().substring(1) : ""
        sourceSize: Qt.size(Math.max(1,glyph.width*2),Math.max(1,glyph.height*2))
        smooth: true
    }
}
