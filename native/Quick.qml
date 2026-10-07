import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ApplicationWindow {
    id: window
    HoverHandler { onPointChanged: Theme.pointer=point.position; onHoveredChanged: if(!hovered)Theme.pointer=Qt.point(-1000,-1000) }
    visible: true
    width: 520
    height: 570
    minimumWidth: 500
    minimumHeight: 500
    title: "Athanor"
    color: Theme.canvas
    property color fg: Theme.text
    property color muted: Theme.muted
    property string category: backend.firstCategory
    readonly property var summary: backend.queueSummary
    palette.window: color
    palette.windowText: fg
    palette.text: fg
    palette.placeholderText: muted
    palette.buttonText: fg
    palette.button: Theme.field
    palette.base: Theme.field
    palette.highlight: Theme.accent
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 14
        Text { text: category==="pdf"?"Convert PDF":"Convert file"; color: window.fg; font.pixelSize: 26; font.bold: true }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: backend.queue
            clip: true
            delegate: Column {
                required property string fileName
                required property string fileStatus
                required property int fileProgress
                required property string warning
                width: ListView.view.width
                spacing: 10
                Text { width: parent.width; text: fileName; font.pixelSize: 14; color: window.fg; elide: Text.ElideMiddle }
                Row { spacing: 8; Glyph { width: 16; height: 16; name: Theme.statusIcon(fileStatus,warning); color: Theme.statusColor(fileStatus,warning) } Text { text: fileStatus; font.pixelSize: 12; color: Theme.statusColor(fileStatus,warning) } }
                ProgressBar { visible: backend.busy; width: parent.width; value: fileProgress/100; background: Rectangle { radius: 4; color: Theme.border } contentItem: Item { Rectangle { width: parent.width*parent.parent.visualPosition; height: parent.height; radius: 4; color: Theme.statusColor(fileStatus,warning) } } }
                Text { visible: warning.length>0; width: parent.width; text: warning; color: Theme.statusColor(fileStatus,warning); wrapMode: Text.Wrap; font.pixelSize: 12 }
            }
        }
        ProgressBar { objectName: "overallProgress"; Accessible.name: "Overall queue progress"; Layout.fillWidth: true; from: 0; to: 100; value: window.summary.percent; palette.highlight: Theme.accent }
        Text { Layout.fillWidth: true; text: window.summary.percent+"% · "+window.summary.saved+" saved"+(backend.busy ? " · "+(window.summary.remaining || "Estimating time…") : ""); color: window.muted; font.pixelSize: 12; elide: Text.ElideRight }
        Card {
            objectName: "outputCard"
            Layout.fillWidth: true
            implicitHeight: settings.implicitHeight+28
            ColumnLayout {
                id: settings
                anchors.fill: parent
                anchors.margins: 14
                spacing: 12
                SizeChoice { id: sizeChoice; Layout.fillWidth: true; Layout.leftMargin: -14; Layout.rightMargin: -14; Layout.topMargin: -14; targetValid: quickAmount.valid }
                FormatSelect { Layout.fillWidth: true; label: window.category+" output"; formats: window.category==="image"?Formats.images:window.category==="video"?Formats.videos:window.category==="audio"?Formats.audios:Formats.documents; value: window.category==="image"?backend.options.image:window.category==="video"?backend.options.video:window.category==="audio"?backend.options.audio:backend.options.pdf_output==="jpg"?"jpg-pages":"pdf"; optionKey: window.category==="pdf"?"pdf_output":window.category; onInfoRequested: function(key){formatGuide.openFormat(key)} }
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 42
                    RowLayout {
                        anchors.fill: parent
                        opacity: !backend.options.size_mode&&!backend.options.convert_only&&window.category!=="pdf" ? 1 : 0
                        enabled: opacity>0
                        Text { text: window.category==="audio"||Formats.audios.indexOf(backend.options.video)>=0&&window.category==="video"?"Bitrate · kb/s":window.category==="image"?"Quality 1–100":"CRF 0–63"; color: window.muted; font.pixelSize: 13 }
                        Spin { property bool audioMode: window.category==="audio"||window.category==="video"&&Formats.audios.indexOf(backend.options.video)>=0; from: audioMode?6:window.category==="image"?1:0; to: audioMode?320:window.category==="image"?100:63; value: audioMode?backend.options.audio_bitrate:window.category==="image"?backend.options.quality:backend.options.crf; enabled: !backend.busy; onValueModified: backend.setOption(audioMode?"audio_bitrate":window.category==="image"?"quality":"crf",value) }
                        Item { Layout.fillWidth: true }
                        ActionButton { visible: window.category==="image"||window.category==="video"&&Formats.audios.indexOf(backend.options.video)<0; text: "Preview"; icon: "eye"; enabled: !backend.busy&&(window.category==="image"?backend.hasImage:backend.hasVideo); onClicked: backend.preview(window.category,window.category==="image"?backend.options.quality:backend.options.crf) }
                    }
                    SizeAmount { id: quickAmount; anchors.fill: parent; visible: backend.options.size_mode }
                    Field { anchors.fill: parent; visible: window.category==="pdf"&&backend.options.pdf_output==="pdf"&&!backend.options.size_mode&&!backend.options.convert_only; model: ["Balanced — 150 dpi","Smallest — 100 dpi","Extreme — 72 dpi","Lossless"]; currentIndex: ["balanced","smallest","extreme","lossless"].indexOf(backend.options.pdf); enabled: !backend.busy; onActivated: function(index){backend.setOption("pdf",["balanced","smallest","extreme","lossless"][index]) } }
                    Text { anchors.fill: parent; visible: backend.options.convert_only; text: "Convert at the highest supported quality"; color: window.muted; font.pixelSize: 13; verticalAlignment: Text.AlignVCenter }
                }
                Spin { visible: window.category==="pdf"&&backend.options.pdf_output==="jpg"; from: 36; to: 600; stepSize: 12; value: backend.options.pdf_dpi; enabled: !backend.busy; Accessible.name: "PDF rendering DPI"; onValueModified: backend.setOption("pdf_dpi",value) }
                Check { text: "Delete original after successful conversion"; font.pixelSize: 12; checked: backend.options.delete; enabled: !backend.busy; onClicked: prompts.toggleDelete(this) }
            }
        }
        RowLayout { Layout.fillWidth: true; Text { Layout.fillWidth: true; text: backend.statusMessage; font.pixelSize: 12; color: window.muted; wrapMode: Text.Wrap } ActionButton { text: "Convert"; icon: "convert"; primary: true; enabled: backend.canConvert&&sizeChoice.valid&&!backend.outputError.length; onClicked: prompts.start() } ActionButton { text: backend.busy?"Cancel":"Close"; icon: "close"; onClicked: backend.busy?backend.cancel():window.close() } }
    }
    ConversionPrompts { id: prompts; objectName: "conversionPrompts" }
    FormatGuide { id: formatGuide; parent: Overlay.overlay }
    Preview { id: preview; parent: Overlay.overlay }
    Connections {
        target: backend
        function onBatchFinished(success,failed,cancelled){if(success>0&&failed===0&&cancelled===0)exitTimer.start()}
        function onPreviewOpened(kind,value){preview.showPreview(kind,value)}
    }
    Timer { id: exitTimer; interval: 700; onTriggered: window.close() }
}
