import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Dialog {
    id: guide
    property string category: "Images"
    property string selectedKey: ""
    function openFormat(key) { selectedKey=key;category=Formats.info(key).group||"Images";open();Qt.callLater(function(){scroller.contentY=0;for(let i=0;i<formatRepeater.count;i++){let card=formatRepeater.itemAt(i);if(card&&card.modelData.key===key){scroller.contentY=Math.min(card.y,Math.max(0,scroller.contentHeight-scroller.height));break}}}) }
    objectName: "formatGuide"
    anchors.centerIn: parent
    width: Math.min(parent.width-48,820)
    height: Math.min(parent.height-48,650)
    modal: true; padding: 24
    background: Card {}
    contentItem: ColumnLayout { spacing: 14
        RowLayout { Layout.fillWidth: true; Text { Layout.fillWidth: true; text: "Format guide"; color: Theme.text; font.pixelSize: 26; font.weight: Font.DemiBold } ActionButton { quiet: true; icon: "close"; implicitWidth: 36; implicitHeight: 36; accessibleText: "Close format guide"; onClicked: guide.close() } }
        RowLayout { Glyph { name: "compact"; color: Theme.accent } Text { Layout.fillWidth: true; text: "Efficient compression · AVIF, AV1 and Opus are size-saving choices. Actual size depends on content and settings."; color: Theme.muted; font.pixelSize: 12; wrapMode: Text.Wrap } }
        RowLayout { Glyph { name: "info"; color: Theme.info } Text { Layout.fillWidth: true; text: "Convert uses lossless encoding where available. JPG, GIF, Opus and MP3 have format limits; their highest-quality output cannot restore lost information."; color: Theme.muted; font.pixelSize: 12; wrapMode: Text.Wrap } }
        RowLayout { Layout.fillWidth: true; spacing: 0
            Repeater { model: ["Images","Audio","Video","Documents"]; ActionButton { required property string modelData; required property int index; Layout.fillWidth: true; text: modelData; selected: guide.category===modelData; joined: true; leftRounded: index===0; rightRounded: index===3; onClicked: {guide.category=modelData;scroller.contentY=0} } }
        }
        Flickable { id: scroller; Layout.fillWidth: true; Layout.fillHeight: true; contentHeight: cards.implicitHeight; clip: true; boundsBehavior: Flickable.StopAtBounds
            ColumnLayout { id: cards; width: scroller.width-14; spacing: 12
                Repeater { id: formatRepeater; model: Formats.entries.filter(function(entry){return entry.group===guide.category}); Card { required property var modelData; Layout.fillWidth: true; implicitHeight: details.implicitHeight+28; color: modelData.key===guide.selectedKey?Theme.selection:Theme.surfaceAlt
                    ColumnLayout { id: details; anchors.fill: parent; anchors.margins: 14; spacing: 8
                        RowLayout { Layout.fillWidth: true; Text { text: modelData.name; color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold } Item { Layout.fillWidth: true } Checker { visible: !!modelData.alpha; width: 15; height: 15 } Glyph { visible: !!modelData.compact; name: "compact"; color: Theme.accent; width: 18; height: 18 } ActionButton { quiet: true; icon: "external"; implicitWidth: 28; implicitHeight: 28; tooltip: "Source and reference"; accessibleText: modelData.name+" source"; onClicked: Qt.openUrlExternally(modelData.source) } }
                        Text { Layout.fillWidth: true; text: modelData.use; color: Theme.text; font.pixelSize: 13; wrapMode: Text.Wrap }
                        Text { Layout.fillWidth: true; text: "+ "+modelData.pros; color: Theme.success; font.pixelSize: 12; wrapMode: Text.Wrap }
                        Text { Layout.fillWidth: true; text: "− "+modelData.cons; color: Theme.warning; font.pixelSize: 12; wrapMode: Text.Wrap }
                    }
                } }
            }
            ScrollBar.vertical: ScrollBar { width: 8; policy: ScrollBar.AsNeeded; contentItem: Rectangle { implicitWidth: 8; radius: 4; color: Theme.muted } }
        }
    }
}
