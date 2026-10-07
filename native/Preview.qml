import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Dialog {
    id: dialog
    objectName: "previewDialog"
    property var sources: [backend.previewOriginal,backend.previewResult]
    property real zoom: 1
    property real panX: 0
    property real panY: 0
    property string category: "image"
    property int quality: 75
    property bool initializing: false
    modal: true
    width: Math.min(parent.width-40,960)
    height: Math.min(parent.height-40,680)
    anchors.centerIn: parent
    padding: 22
    background: Card {}
    onClosed: { debounce.stop(); backend.closePreview() }
    function showPreview(kind,value) { zoom=1;panX=0;panY=0;initializing=true; category=kind; quality=value; slider.value=value; initializing=false; open() }
    contentItem: ColumnLayout {
        spacing: 16
        Text { text: dialog.category === "image" ? "Image quality preview" : "Video CRF preview"; font.pixelSize: 24; font.bold: true; color: Theme.text }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Repeater {
                model: dialog.sources
                delegate: ColumnLayout {
                    required property string modelData
                    required property int index
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Text { text: index===0 ? "Original" : "Converted"; color: Theme.muted }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 12
                        color: backend.dark ? "#17181d" : "#ececf3"
                        clip: true
                        ShaderEffect {
                            anchors.fill: parent
                            property vector2d dimensions: Qt.vector2d(width,height)
                            property color colorA: Theme.checkerA
                            property color colorB: Theme.checkerB
                            fragmentShader: "qrc:/shaders/checker.frag.qsb"
                        }
                        Flickable {
                            id: viewport
                            anchors.fill: parent
                            anchors.margins: 10
                            clip: true
                            contentWidth: width*dialog.zoom
                            contentHeight: height*dialog.zoom
                            contentX: dialog.panX*Math.max(0,contentWidth-width)
                            contentY: dialog.panY*Math.max(0,contentHeight-height)
                            interactive: dialog.zoom>1
                            boundsBehavior: Flickable.StopAtBounds
                            onContentXChanged: if(moving&&contentWidth>width) dialog.panX=contentX/(contentWidth-width)
                            onContentYChanged: if(moving&&contentHeight>height) dialog.panY=contentY/(contentHeight-height)
                            Image { width: viewport.contentWidth; height: viewport.contentHeight; source: modelData; asynchronous: true; fillMode: Image.PreserveAspectFit; sourceSize: Qt.size(1024,1024); cache: false }
                            WheelHandler { target: null; onWheel: function(event){dialog.zoom=Math.max(1,Math.min(4,dialog.zoom+(event.angleDelta.y>0?.25:-.25)));event.accepted=true} }
                        }
                        BusyIndicator { anchors.centerIn: parent; running: index===1 && backend.previewBusy; visible: running }
                    }
                }
            }
        }
        Text { visible: backend.previewError.length>0; text: backend.previewError; wrapMode: Text.Wrap; Layout.fillWidth: true; color: Theme.error }
        RowLayout {
            Layout.fillWidth: true
            Text { text: "Zoom"; color: Theme.muted; font.pixelSize: 12 }
            Spin { from: 100; to: 400; stepSize: 25; value: Math.round(dialog.zoom*100); onValueModified: dialog.zoom=value/100 }
            Text { text: "%"; color: Theme.muted; font.pixelSize: 12 }
            ActionButton { text: "Fit"; icon: "fit"; onClicked: {dialog.zoom=1;dialog.panX=0;dialog.panY=0} }
            Item { Layout.fillWidth: true }
        }
        RowLayout {
            Text { text: (dialog.category==="image" ? "Quality " : "CRF ")+Math.round(slider.value); color: Theme.text }
            Slider { id: slider; implicitHeight: Theme.controlHeight;
                background: Rectangle { x: slider.leftPadding; y: slider.topPadding+(slider.availableHeight-height)/2; width: slider.availableWidth; height: 6; radius: 3; color: Theme.softBorder; Rectangle { width: slider.visualPosition*parent.width; height: parent.height; radius: 3; color: Theme.accent } }
                handle: Rectangle { x: slider.leftPadding+slider.visualPosition*(slider.availableWidth-width); y: slider.topPadding+(slider.availableHeight-height)/2; width: 22; height: 22; radius: 11; color: Theme.accent; border.width: slider.activeFocus&&Theme.keyboardFocus(slider.focusReason) ? 2 : 0; border.color: Theme.text }
 Layout.fillWidth: true; from: dialog.category==="image" ? 1 : 0; to: dialog.category==="image" ? 100 : 63; stepSize: 1; onValueChanged: if(dialog.visible&&!dialog.initializing) { dialog.quality=Math.round(value); debounce.restart() } }
            ActionButton { text: "Use this quality"; icon: "check"; primary: true; onClicked: { backend.usePreview(dialog.category,Math.round(slider.value)); dialog.close() } }
            ActionButton { text: "Close"; icon: "close"; onClicked: dialog.close() }
        }
    }
    Timer { id: debounce; interval: 300; onTriggered: backend.updatePreview(Math.round(slider.value)) }
}
