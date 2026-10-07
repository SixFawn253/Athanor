import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Card {
    id: panel
    objectName: "queueCard"
    property int selectedRow: -1
    property var widths: [.23,.09,.10,.08,.09,.10,.12,.19]
    readonly property var summary: backend.queueSummary
    readonly property var selectedInfo: { var revision=backend.queueSummary; return backend.rowInfo(selectedRow) }
    readonly property var minimums: [140,76,90,62,90,84,98,130]
    readonly property var sizes: calculateSizes(table.width-(bar.visible?18:0),widths)
    function focusQueue(reason){ table.forceActiveFocus(reason) }
    function calculateSizes(area,weights) {
        var sum=0;for(var i=0;i<8;i++)sum+=minimums[i];
        var extra=Math.max(0,area-sum);var values=[];
        for(var j=0;j<8;j++)values.push(minimums[j]+extra*weights[j]);return values
    }
    function resizeColumn(index,delta,original) {
        var next=original.slice();var sum=0;for(var i=0;i<8;i++)sum+=minimums[i];
        var extra=Math.max(1,table.width-(bar.visible?18:0)-sum);
        var change=Math.max(-next[index],Math.min(next[index+1],delta/extra));
        next[index]+=change;next[index+1]-=change;widths=next
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            Glyph { name: "folder"; color: Theme.muted; Layout.preferredWidth: 18; Layout.preferredHeight: 18 }
            Text { text: "Queue"; color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
            Text { visible: backend.busy; text: summary.done+" / "+backend.count+" complete"; color: Theme.info; font.pixelSize: 12 }
            Text { visible: !backend.busy&&summary.done>0; text: summary.done+" complete"; color: Theme.success; font.pixelSize: 12 }
            Text { visible: summary.warnings>0; text: summary.warnings+" warning"+(summary.warnings===1?"":"s"); color: Theme.warning; font.pixelSize: 12 }
            Text { visible: summary.failed>0; text: summary.failed+" failed"; color: Theme.error; font.pixelSize: 12 }
            Text { visible: !backend.busy&&summary.done>0; text: summary.saved.startsWith("−")?summary.saved.substring(1)+" larger":summary.saved+" saved"; color: summary.saved.startsWith("−")?Theme.warning:Theme.muted; font.pixelSize: 12 }

            Item { Layout.fillWidth: true }
            ActionButton { implicitHeight: 28; text: "Open result"; icon: "external"; visible: panel.selectedRow>=0; enabled: !!panel.selectedInfo.hasOutput; tooltip: enabled ? "Open the converted file" : "Available after successful conversion"; onClicked: backend.openOutput(panel.selectedRow) }
            ActionButton { implicitWidth: 28; implicitHeight: 28; icon: "folder"; visible: panel.selectedRow>=0; accessibleText: "Open containing folder"; tooltip: accessibleText; onClicked: backend.openFolder(panel.selectedRow) }
            Text { text: backend.count+" file(s)"; color: Theme.muted; font.pixelSize: 12 }
        }
        Rectangle {
            id: header
            objectName: "queueHeader"
            Layout.fillWidth: true
            Layout.leftMargin: -16
            Layout.rightMargin: -16
            implicitHeight: 38
            color: Theme.header
            Row {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16+(bar.visible?18:0)
                Repeater {
                    model: ["File","File size","Compressed size","% smaller","Gained space","Status","Progress","Output / details"]
                    delegate: Item {
                        id: heading
                        required property int index
                        required property string modelData
                        width: panel.sizes[index]
                        height: 38
                        Text { anchors.fill: parent; rightPadding: 10; leftPadding: index===0 ? 4 : 0; text: heading.modelData; color: Theme.muted; font.pixelSize: 11; wrapMode: Text.Wrap; verticalAlignment: Text.AlignVCenter; horizontalAlignment: index>=1&&index<=4 ? Text.AlignRight : Text.AlignLeft }
                        MouseArea { visible: heading.index<7; x: parent.width-4; width: 8; height: parent.height; cursorShape: Qt.SplitHCursor; hoverEnabled: true; property real initialX: 0; property var original: []
                            onPressed: function(mouse){ initialX=mapToItem(header,mouse.x,mouse.y).x;original=panel.widths.slice() }
                            onPositionChanged: function(mouse){if(pressed)panel.resizeColumn(heading.index,mapToItem(header,mouse.x,mouse.y).x-initialX,original)}
                            Rectangle { anchors.centerIn: parent; height: 18; width: 2; radius: 1; color: Theme.accent; visible: parent.containsMouse||parent.pressed }
                        }
                    }
                }
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
        ListView {
            id: table
            objectName: "queueList"
            width: parent.width
            height: Math.floor(parent.height/42)*42
            clip: true
            model: backend.queue
            currentIndex: panel.selectedRow
            activeFocusOnTab: true
            boundsBehavior: Flickable.StopAtBounds
            onCurrentIndexChanged: if(activeFocus) panel.selectedRow=currentIndex
            Keys.onDeletePressed: if(!backend.busy&&panel.selectedRow>=0){backend.remove(panel.selectedRow);panel.selectedRow=-1}
            Keys.onReturnPressed: if(panel.selectedInfo.hasOutput)backend.openOutput(panel.selectedRow)
            delegate: Rectangle {
                required property int index
                required property string sourcePath
                required property string fileName
                required property string fileStatus
                required property int fileProgress
                required property string outputPath
                required property string warning
                required property string fileSize
                required property string compressedSize
                required property string percentSmaller
                property real displayedProgress: fileProgress
                Behavior on displayedProgress { NumberAnimation { duration: Theme.fast } }
                required property string gainedSpace
                width: table.width-(bar.visible?18:0)
                height: 42
                radius: 8
                color: panel.selectedRow===index ? Theme.selection : index%2 ? Theme.surfaceAlt : Theme.surface
                border.width: panel.selectedRow===index&&table.activeFocus ? 1 : 0
                border.color: Theme.accent
                Row {
                    anchors.fill: parent
                    Text { width: panel.sizes[0]; height: parent.height; leftPadding: 4; rightPadding: 10; text: fileName; color: Theme.text; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; elide: Text.ElideMiddle }
                    Text { width: panel.sizes[1]; height: parent.height; rightPadding: 10; text: fileSize; color: Theme.text; font.family: Theme.numericFont; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignRight; elide: Text.ElideRight }
                    Text { width: panel.sizes[2]; height: parent.height; rightPadding: 10; text: compressedSize; color: Theme.text; font.family: Theme.numericFont; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignRight; elide: Text.ElideRight }
                    Text { width: panel.sizes[3]; height: parent.height; rightPadding: 10; text: percentSmaller; color: fileStatus==="Done" ? (percentSmaller.startsWith("-") ? Theme.warning : Theme.success) : Theme.muted; font.family: Theme.numericFont; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignRight; elide: Text.ElideRight }
                    Text { width: panel.sizes[4]; height: parent.height; rightPadding: 10; text: gainedSpace; color: Theme.text; font.family: Theme.numericFont; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignRight; elide: Text.ElideRight }
                    Item { width: panel.sizes[5]; height: parent.height
                        Glyph { objectName: "statusGlyph"+index; anchors.verticalCenter: parent.verticalCenter; width: 14; height: 14; name: Theme.statusIcon(fileStatus,warning); color: Theme.statusColor(fileStatus,warning) }
                        Text { anchors.fill: parent; leftPadding: 20; rightPadding: 6; text: ["Queued","Done","Error","Cancelled"].indexOf(fileStatus)>=0 ? fileStatus : "Working"; color: Theme.statusColor(fileStatus,warning); font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                    }
                    Item { objectName: "progressCell"+index; width: panel.sizes[6]; height: parent.height
                        Rectangle { objectName: "progressTrack"+index; anchors.verticalCenter: parent.verticalCenter; width: Math.max(10,parent.width-42); height: 6; radius: 3; color: Theme.softBorder
                            Rectangle { width: parent.width*displayedProgress/100; height: parent.height; radius: 3; color: fileStatus==="Done" ? Theme.success : fileStatus==="Error" ? Theme.error : Theme.info }
                        }
                        Text { objectName: "progressLabel"+index; anchors.right: parent.right; anchors.rightMargin: 5; anchors.verticalCenter: parent.verticalCenter; text: fileProgress+"%"; color: Theme.muted; font.family: Theme.numericFont; font.pixelSize: 11 }
                    }
                    Text { width: panel.sizes[7]; height: parent.height; leftPadding: 6; rightPadding: 4; text: warning.length ? warning : outputPath.length ? outputPath : ["Queued","Cancelled"].indexOf(fileStatus)<0 ? fileStatus : ""; color: warning.length ? Theme.statusColor(fileStatus,warning) : Theme.muted; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; elide: Text.ElideMiddle }
                }
                HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
                Hint { visible: rowHover.hovered; delay: 600; text: sourcePath+"\n"+fileStatus+(warning.length?"\n"+warning:outputPath.length?"\n"+outputPath:"") }
                TapHandler { onTapped: {panel.selectedRow=index;table.forceActiveFocus(Qt.MouseFocusReason)} onDoubleTapped: if(outputPath.length)backend.openOutput(index) }
            }
            ScrollBar.vertical: ScrollBar { id: bar; width: 10; minimumSize: Math.min(1,28/table.height); policy: ScrollBar.AsNeeded; visible: size<.99999; contentItem: Rectangle { radius: 5; color: Theme.muted; implicitWidth: 10 } background: Rectangle { radius: 5; color: Theme.surfaceAlt } }
            Column { anchors.centerIn: parent; spacing: 12; visible: backend.count===0
                Glyph { name: "folder-plus"; color: Theme.accent; width: 32; height: 32; anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "Drop files or folders here"; color: Theme.text; font.pixelSize: 22; font.weight: Font.DemiBold; anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "Ctrl+O to add files · Ctrl+Enter to convert"; color: Theme.muted; font.pixelSize: 12; anchors.horizontalCenter: parent.horizontalCenter }
            }
        }
        }
    }
}
