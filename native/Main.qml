import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
ApplicationWindow {
    id: window
    HoverHandler { onPointChanged: Theme.pointer=point.position; onHoveredChanged: if(!hovered)Theme.pointer=Qt.point(-1000,-1000) }
    visible: true
    width: Math.min(1180,Screen.desktopAvailableWidth-48)
    height: Math.min(820,Screen.desktopAvailableHeight-48)
    minimumWidth: Math.min(900,Screen.desktopAvailableWidth-48)
    minimumHeight: Math.min(600,Screen.desktopAvailableHeight-48)
    title: "Athanor"
    color: Theme.canvas
    property point pointerPosition: Theme.pointer
    property color fg: Theme.text
    property color muted: Theme.muted
    property color accent: Theme.accent
    property int page: 0
    property alias selectedRow: queuePanel.selectedRow
    property bool includeSubfolders: false
    palette.window: color
    palette.windowText: fg
    palette.text: fg
    palette.placeholderText: muted
    palette.buttonText: fg
    palette.base: Theme.field
    palette.button: Theme.field
    palette.highlight: accent
    palette.highlightedText: Theme.onAccent
    function revealFocus(item){Theme.revealControl(item)}
    onActiveFocusItemChanged: Qt.callLater(function(){if(window.activeFocusItem)Theme.revealControl(window.activeFocusItem)})
    function showPage(index,reason) {
        if(page===index) return
        reveal.stop()
        transitionCover.progress=0
        transitionCover.visible=true
        page=index
        backend.transitionDeadline()
        Qt.callLater(function(){ reveal.restart(); if(index===1)backButton.forceActiveFocus(reason===undefined?Qt.OtherFocusReason:reason);else queuePanel.focusQueue(reason===undefined?Qt.OtherFocusReason:reason) })
    }
    Shortcut { sequence: "Ctrl+,"; enabled: !help.visible&&!preview.visible&&!formatGuide.visible&&!prompts.active; onActivated: window.showPage(window.page===1?0:1,Qt.ShortcutFocusReason) }
    Shortcut { sequence: "F1"; enabled: !formatGuide.visible&&!prompts.active&&!preview.visible; onActivated: help.open() }
    Shortcut { sequence: "Escape"; enabled: window.page===1&&!help.visible&&!preview.visible&&!formatGuide.visible&&!prompts.active; onActivated: window.showPage(0,Qt.ShortcutFocusReason) }
    Shortcut { sequence: "Alt+Left"; enabled: window.page===1&&!help.visible&&!preview.visible&&!formatGuide.visible&&!prompts.active; onActivated: window.showPage(0,Qt.ShortcutFocusReason) }
    Shortcut { sequence: "Ctrl+O"; enabled: !help.visible&&!preview.visible&&!formatGuide.visible&&!prompts.active; onActivated: backend.addFiles() }
    Shortcut { sequence: "Ctrl+Return"; enabled: !updater.downloading&&backend.canConvert&&sizeChoice.valid&&!backend.outputError.length&&!help.visible&&!preview.visible&&!formatGuide.visible&&!prompts.active; onActivated: prompts.start() }
    DropArea { anchors.fill: parent; onDropped: function(drop) { if(drop.hasUrls) { backend.addUrls(drop.urls,window.includeSubfolders); drop.acceptProposedAction() } } }
    RowLayout {
        id: topBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 28
        anchors.rightMargin: 28
        anchors.topMargin: 16
        height: 42
        spacing: 10
        Image { Layout.preferredWidth: 25; Layout.preferredHeight: 25; sourceSize: Qt.size(50,50); source: "image://icons/brand-mark/"+window.fg.toString().substring(1) }
        Text { text: "ATHANOR"; color: window.fg; font.pixelSize: 20; font.bold: true }
        Item { Layout.fillWidth: true }
        RowLayout { visible: updater.availableVersion.length>0; spacing: 10
            Text { text: updater.availableVersion; color: Theme.muted; font.pixelSize: 12 }
            ActionButton { objectName: "downloadUpdateButton"; text: updater.downloading?"Downloading "+updater.progress+"%":"Download"; primary: true; enabled: updater.canInstall&&!backend.busy&&!backend.previewBusy; tooltip: "Download, verify and install the update, then restart Athanor. The current queue will be cleared."; onClicked: updater.install() }
        }
        ActionButton { objectName: "settingsButton"; Layout.preferredWidth: 42; quiet: true; selected: window.page===1; icon: "settings"; accessibleText: "Settings"; tooltip: "Settings · Ctrl+,"; onClicked: window.showPage(window.page===1 ? 0 : 1) }
        ActionButton { objectName: "helpButton"; Layout.preferredWidth: 42; quiet: true; icon: "help"; accessibleText: "Help"; tooltip: "Help"; onClicked: help.open() }
    }
    Item {
        id: body
        anchors.left: parent.left
        anchors.top: topBar.bottom
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        StackLayout {
            objectName: "pages"
            anchors.fill: parent
            anchors.margins: 28
            currentIndex: window.page
            ColumnLayout {
                objectName: "convertPage"
                spacing: 14
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    border.width: 1
                    border.color: Theme.softBorder
                    implicitWidth: toolbar.implicitWidth
                    implicitHeight: 52
                    radius: 16
                    color: Theme.field
                    Row {
                        id: toolbar
                        spacing: 0
                        ActionButton { joined: true; large: true; objectName: "addFilesButton"; text: "Add files"; icon: "file-plus"; radius: 16; rightRounded: false; onClicked: backend.addFiles() }
                        ActionButton { joined: true; large: true; text: "Add folder"; icon: "folder-plus"; leftRounded: false; rightRounded: false; onClicked: backend.addFolder(window.includeSubfolders) }
                        ActionButton { joined: true; large: true; text: "Remove"; icon: "remove"; leftRounded: false; rightRounded: false; enabled: !backend.busy&&window.selectedRow>=0; onClicked: { backend.remove(window.selectedRow); window.selectedRow=-1 } }
                        ActionButton { joined: true; large: true; text: "Clear"; icon: "trash"; radius: 16; leftRounded: false; enabled: backend.count>0&&!backend.busy; onClicked: { backend.clear(); window.selectedRow=-1 } }
                    }
                }
                Check { text: "Subfolders"; checked: window.includeSubfolders; onToggled: window.includeSubfolders=checked; enabled: true; Layout.alignment: Qt.AlignRight; font.pixelSize: 12 }
                Flickable {
                    id: panelsScroll
                    objectName: "panelsScroll"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    contentWidth: width
                    contentHeight: Math.max(height,panels.implicitHeight)
                    ColumnLayout {
                        id: panels
                        width: panelsScroll.width-(panelsBar.visible?14:0)
                        height: Math.max(panelsScroll.height,implicitHeight)
                        spacing: 14
                QueuePanel { id: queuePanel; Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 210 }
                Card {
                    objectName: "outputCard"
                    Layout.fillWidth: true
                    implicitHeight: 220+(backend.oversizedWebp?42:0)
                    SizeChoice { id: sizeChoice; anchors.top: parent.top; width: parent.width; targetValid: targetAmount.valid }
                    ColumnLayout {
                        id: outputSettings
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: sizeChoice.bottom
                        anchors.margins: 15
                        spacing: 10
                        GridLayout {
                            Layout.fillWidth: true; columns: 4; columnSpacing: 14
                            ColumnLayout { Layout.fillWidth: true; Layout.preferredWidth: 250; spacing: 7
                                FormatSelect { Layout.fillWidth: true; label: "Image output"; formats: Formats.images; value: backend.options.image; optionKey: "image"; field.objectName: "imageFormat"; onInfoRequested: function(key){formatGuide.openFormat(key)} }
                                Text { opacity: backend.options.size_mode?0:1; text: backend.options.size_mode?"Target size":backend.options.convert_only?"Convert":"Quality 1–100"; color: Theme.muted; font.pixelSize: 12 }
                                Item { opacity: backend.options.size_mode?0:1; enabled: opacity>0; Layout.fillWidth: true; Layout.preferredHeight: 42
                                    RowLayout { anchors.fill: parent; visible: !backend.options.size_mode&&!backend.options.convert_only
                                        Spin { objectName: "qualitySpin"; implicitWidth: 90; Accessible.name: "Image quality"; from: 1; to: 100; value: backend.options.quality; enabled: !backend.busy&&backend.options.image!=="png"&&backend.options.image!=="ico"; onValueModified: backend.setOption("quality",value) }
                                        ActionButton { text: "Preview"; icon: "eye"; enabled: !updater.downloading&&backend.hasImage&&!backend.busy&&backend.options.image!=="pdf"; tooltip: "Compare image quality"; onClicked: backend.preview("image",backend.options.quality,window.selectedRow) }
                                        Item { Layout.fillWidth: true }
                                    }
                                    Text { anchors.fill: parent; visible: backend.options.size_mode||backend.options.convert_only; text: backend.options.size_mode?"Use the maximum size below.":backend.options.image==="jpg"?"Highest quality JPG; alpha becomes white.":backend.options.image==="ico"?"Fit to a 256 × 256 icon canvas.":"Preserve image detail and transparency."; color: Theme.muted; font.pixelSize: 12; wrapMode: Text.Wrap; verticalAlignment: Text.AlignVCenter }
                                }
                            }
                            ColumnLayout { Layout.fillWidth: true; Layout.preferredWidth: 250; spacing: 7
                                FormatSelect { Layout.fillWidth: true; label: "Video output"; formats: Formats.videos; value: backend.options.video; optionKey: "video"; onInfoRequested: function(key){formatGuide.openFormat(key)} }
                                Text { opacity: backend.options.size_mode?0:1; text: backend.options.size_mode?"Target size":backend.options.convert_only?"Convert":Formats.audios.indexOf(backend.options.video)>=0?"Use audio settings":"CRF 0–63 · lower = better"; color: Theme.muted; font.pixelSize: 12 }
                                Item { opacity: backend.options.size_mode?0:1; enabled: opacity>0; Layout.fillWidth: true; Layout.preferredHeight: 42
                                    RowLayout { anchors.fill: parent; visible: !backend.options.size_mode&&!backend.options.convert_only&&Formats.audios.indexOf(backend.options.video)<0
                                        Spin { implicitWidth: 90; Accessible.name: "Video CRF"; from: 0; to: 63; value: backend.options.crf; enabled: !backend.busy; onValueModified: backend.setOption("crf",value) }
                                        ActionButton { text: "Preview"; icon: "eye"; enabled: !updater.downloading&&backend.hasVideo&&!backend.busy; tooltip: "Compare video quality"; onClicked: backend.preview("video",backend.options.crf,window.selectedRow) }
                                        Item { Layout.fillWidth: true }
                                    }
                                    Text { anchors.fill: parent; visible: backend.options.size_mode||backend.options.convert_only||Formats.audios.indexOf(backend.options.video)>=0; text: backend.options.size_mode?"Use the maximum size below.":Formats.audios.indexOf(backend.options.video)>=0?"Extract first audio track; use audio bitrate.":backend.options.video==="gif"?"Palette animation; binary transparency.":backend.options.video==="mkv"?"Lossless video; retain audio tracks.":backend.options.video==="mp4"?"Lossless RGB video; AAC audio.":"Preserve video detail."; color: Theme.muted; font.pixelSize: 12; wrapMode: Text.Wrap; verticalAlignment: Text.AlignVCenter }
                                }
                            }
                            ColumnLayout { Layout.fillWidth: true; Layout.preferredWidth: 250; spacing: 7
                                FormatSelect { Layout.fillWidth: true; label: "Audio output"; formats: Formats.audios; value: backend.options.audio; optionKey: "audio"; onInfoRequested: function(key){formatGuide.openFormat(key)} }
                                Text { opacity: backend.options.size_mode?0:1; text: backend.options.size_mode?"Target size":backend.options.convert_only?"Convert":"Bitrate · kb/s"; color: Theme.muted; font.pixelSize: 12 }
                                Item { opacity: backend.options.size_mode?0:1; enabled: opacity>0; Layout.fillWidth: true; Layout.preferredHeight: 42
                                    Spin { visible: !backend.options.size_mode&&!backend.options.convert_only; from: 6; to: 320; value: backend.options.audio_bitrate; enabled: !backend.busy&&(backend.options.audio!=="wav"||["opus","mp3"].indexOf(backend.options.video)>=0); Accessible.name: "Audio bitrate"; onValueModified: backend.setOption("audio_bitrate",value) }
                                    Text { anchors.fill: parent; visible: backend.options.size_mode||backend.options.convert_only; text: backend.options.size_mode?"Use the maximum size below.":backend.options.audio==="wav"?"Uncompressed 24-bit PCM.":"Highest-quality lossy encoding."; color: Theme.muted; font.pixelSize: 12; wrapMode: Text.Wrap; verticalAlignment: Text.AlignVCenter }
                                }
                            }
                            ColumnLayout { Layout.fillWidth: true; Layout.preferredWidth: 250; spacing: 7
                                FormatSelect { Layout.fillWidth: true; label: "PDF output"; formats: Formats.documents; value: backend.options.pdf_output==="jpg"?"jpg-pages":"pdf"; optionKey: "pdf_output"; onInfoRequested: function(key){formatGuide.openFormat(key)} }
                                Text { opacity: backend.options.size_mode?0:1; text: backend.options.pdf_output==="jpg"?"Page resolution · DPI":"Compression profile"; color: Theme.muted; font.pixelSize: 12 }
                                Item { opacity: backend.options.size_mode?0:1; enabled: opacity>0; Layout.fillWidth: true; Layout.preferredHeight: 42
                                    Field { anchors.fill: parent; visible: backend.options.pdf_output!=="jpg"; accessibleLabel: "PDF compression"; model: backend.options.convert_only?["Lossless"]:backend.options.size_mode?["Automatic"]:["Balanced — 150 dpi","Smallest — 100 dpi","Extreme — 72 dpi","Lossless"]; currentIndex: backend.options.size_mode||backend.options.convert_only?0:["balanced","smallest","extreme","lossless"].indexOf(backend.options.pdf); enabled: !backend.busy&&!backend.options.size_mode&&!backend.options.convert_only; onActivated: function(index){backend.setOption("pdf",["balanced","smallest","extreme","lossless"][index]) } }
                                    Spin { visible: backend.options.pdf_output==="jpg"; from: 36; to: 600; stepSize: 12; value: backend.options.pdf_dpi; enabled: !backend.busy; Accessible.name: "PDF rendering DPI"; onValueModified: backend.setOption("pdf_dpi",value) }
                                }
                            }
                        }
                        Check { visible: backend.oversizedWebp; checked: true; enabled: false; text: "WebP supports 16,383 pixels per side. Larger images will be resized to fit."; font.pixelSize: 12 }
                    }
                    RowLayout { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 15; anchors.bottomMargin: backend.oversizedWebp?57:15; height: 62; visible: backend.options.size_mode; spacing: 16
                        Glyph { name: "info"; color: Theme.info }
                        Text { text: "Maximum size per file"; color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
                        SizeAmount { id: targetAmount; Layout.preferredWidth: 220 }
                        Text { Layout.fillWidth: true; text: "PDF collections share one target"; color: Theme.muted; font.pixelSize: 12; wrapMode: Text.Wrap }
                    }

                }
                    }
                    ScrollBar.vertical: ScrollBar { id: panelsBar; width: 8; policy: ScrollBar.AsNeeded; visible: panelsScroll.contentHeight>panelsScroll.height+.5; contentItem: Rectangle { radius: 4; implicitWidth: 8; color: Theme.muted } background: Rectangle { radius: 4; color: Theme.surfaceAlt } }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "Save to"; color: window.muted; font.pixelSize: 12 }
                    RowLayout { Layout.fillWidth: true; spacing: 0
                    Input { rightRounded: false; id: outputLocation; invalid: backend.outputError.length>0; objectName: "savePath"; Layout.fillWidth: true; Layout.preferredHeight: 42; text: backend.options.output; placeholderText: "Beside originals"; enabled: !backend.busy; font.pixelSize: 13; onTextEdited: backend.setOption("output",text) }
                    ActionButton { leftRounded: false; objectName: "browseButton"; text: "Browse"; icon: "folder"; enabled: !backend.busy; onClicked: backend.chooseOutput() }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Check { objectName: "deleteOriginals"; text: "Delete originals after successful conversion"; font.pixelSize: 12; enabled: !backend.busy; checked: backend.options.delete; onClicked: prompts.toggleDelete(this) }
                    Item { Layout.fillWidth: true }
                    ActionButton { objectName: "convertButton"; text: "Convert"; icon: "convert"; primary: true; enabled: !updater.downloading&&backend.canConvert&&sizeChoice.valid&&!backend.outputError.length; tooltip: backend.busy?"A conversion is already running":backend.count===0?"Add files to convert":backend.outputError.length?backend.outputError:!sizeChoice.valid?"Enter a positive target size":!backend.canConvert?"All files are processed. Change a format or compression setting to convert again.":"Convert queued files · Ctrl+Enter"; onClicked: prompts.start() }
                    ActionButton { text: "Cancel"; icon: "close"; enabled: backend.busy; onClicked: backend.cancel() }
                }
                Text { visible: backend.outputError.length>0||backend.statusMessage.length>0; text: backend.outputError||backend.statusMessage; color: window.muted; font.pixelSize: 12 }
            }
            ColumnLayout {
                spacing: 18
                RowLayout { Layout.fillWidth: true
                    Text { text: "Settings"; color: Theme.text; font.pixelSize: 28; font.weight: Font.DemiBold }
                    Item { Layout.fillWidth: true }
                    ActionButton { id: backButton; objectName: "backButton"; icon: "arrow-left"; text: "Back to queue"; tooltip: "Return to queue · Escape"; onClicked: window.showPage(0) }
                }
                Flickable {
                    id: settingsScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: width
                    contentHeight: settingsContent.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded
                                visible: size < 0.99999; contentItem: Rectangle { implicitWidth: 10; radius: 5; color: Theme.muted } }
                    ColumnLayout {
                        id: settingsContent
                        width: parent.width-14
                        spacing: 20
                        Text { text: "Appearance"; color: window.fg; font.pixelSize: 17; font.bold: true }
                        Card {
                            Layout.fillWidth: true
                            implicitHeight: 154
                            Column {
                                anchors.fill: parent
                                anchors.margins: 16
                                Repeater { model: ["System","Light","Dark"]; delegate: Radio { required property string modelData; text: modelData+(modelData==="System"?"    Follow your system":""); checked: backend.appearance===modelData; onClicked: backend.appearance=modelData; font.pixelSize: 13 } }
                            }
                        }
                        Text { text: nativePlatform==="Windows"?"Windows Explorer":nativePlatform==="macOS"?"Finder":"File manager"; color: window.fg; font.pixelSize: 17; font.bold: true }
                        Card {
                            Layout.fillWidth: true
                            implicitHeight: 90
                            Column {
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 8
                                Check { text: "Enable “Convert using Athanor” in the context menu"; checked: backend.contextMenus; onToggled: backend.contextMenus=checked; font.pixelSize: 13 }
                                Text { text: nativePlatform==="Windows"?"On Windows 11, look under Show more options.":nativePlatform==="macOS"?"Available in Finder Quick Actions and Services.":"Available in KDE service menus and Nautilus/Nemo Scripts."; color: window.muted; font.pixelSize: 12 }
                            }
                        }
                        RowLayout { Layout.fillWidth: true
                            Text { text: "Motion"; color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
                            Item { Layout.fillWidth: true }
                            Field { Layout.preferredWidth: 280; accessibleLabel: "Animation preference"; model: ["Follow system","Full animations","Reduced motion"]; currentIndex: ["System","Full","Reduced"].indexOf(backend.motion); onActivated: function(index){backend.motion=["System","Full","Reduced"][index]} }
                        }
                        Text { text: "Updates"; color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
                        Card {
                            Layout.fillWidth: true
                            implicitHeight: updatesContent.implicitHeight+32
                            ColumnLayout { id: updatesContent; anchors.fill: parent; anchors.margins: 16; spacing: 12
                                Text { Layout.fillWidth: true; text: "Download installs the update automatically and restarts Athanor. Your current queue will be cleared; saved settings and originals are kept."; color: Theme.muted; font.pixelSize: 12; wrapMode: Text.Wrap }
                                Check { text: "Automatically check GitHub for updates"; checked: updater.automatic; onToggled: updater.automatic=checked }
                                RowLayout { Layout.fillWidth: true
                                    Text { Layout.fillWidth: true; text: updater.status; color: Theme.muted; wrapMode: Text.Wrap; font.pixelSize: 13 }
                                    ActionButton { text: "Check now"; enabled: !updater.working; onClicked: updater.check() }
                                    ActionButton { objectName: "settingsDownloadButton"; text: updater.downloading?"Downloading "+updater.progress+"%":"Download"; primary: true; visible: updater.availableVersion.length>0; enabled: updater.canInstall&&!backend.busy&&!backend.previewBusy; tooltip: "Download and automatically apply the update, then restart."; onClicked: updater.install() }
                                }
                                ProgressBar { Layout.fillWidth: true; visible: updater.working; from: 0; to: 100; value: updater.progress; indeterminate: updater.progress===0; palette.highlight: Theme.accent }
                            }
                        }
                        Text { text: "Performance"; color: window.fg; font.pixelSize: 17; font.bold: true }
                        Card {
                            Layout.fillWidth: true
                            implicitHeight: performanceSettings.implicitHeight+32
                            GridLayout {
                                id: performanceSettings
                                anchors.fill: parent
                                anchors.margins: 16
                                columns: 4
                                columnSpacing: 12
                                Text { text: "Encoding speed"; color: window.muted; font.pixelSize: 12 }
                                Text { text: "Video acceleration"; color: window.muted; font.pixelSize: 12 }
                                Text { text: "Files at once"; color: window.muted; font.pixelSize: 12 }
                                Text { text: "Threads (0 = auto)"; color: window.muted; font.pixelSize: 12 }
                                Field { Layout.fillWidth: true; accessibleLabel: "Encoding speed"; model: ["Fast","Balanced","Smallest"]; currentIndex: model.indexOf(backend.options.speed); enabled: !backend.busy; onActivated: function(index){backend.setOption("speed",model[index]) } }
                                Field { Layout.fillWidth: true; accessibleLabel: "Video acceleration"; model: ["Auto","CPU"]; currentIndex: backend.options.acceleration==="CPU"?1:0; enabled: !backend.busy; onActivated: function(index){backend.setOption("acceleration",model[index]) } }
                                Spin { Accessible.name: "Parallel files"; Layout.fillWidth: true; from: 1; to: 8; value: backend.options.batch_workers; enabled: !backend.busy&&!backend.options.auto_workers; onValueModified: backend.setOption("batch_workers",value) }
                                Spin { Accessible.name: "CPU threads"; Layout.fillWidth: true; from: 0; to: 256; editable: true; value: backend.options.threads; enabled: !backend.busy; onValueModified: backend.setOption("threads",value) }
                                Check { Layout.columnSpan: 4; text: "Automatically adjust parallel files to hardware load"; checked: backend.options.auto_workers; enabled: !backend.busy; onClicked: backend.setOption("auto_workers",checked) }
                                Text { Layout.columnSpan: 4; Layout.fillWidth: true; text: "Faster encoding trades some size efficiency for speed. Auto tries GPU AV1 video encoding, then CPU. Parallel files adapt to CPU and memory load; GPU load is used only during GPU encoding. Shortest estimated jobs start first."; color: window.muted; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.topMargin: 8 }
                            }
                        }
                    }
                }
            }
        }
        ShaderEffect {
            id: transitionCover
            objectName: "transitionCover"
            anchors.fill: parent
            z: 100
            visible: false
            property color coverColor: window.color
            property vector2d dimensions: Qt.vector2d(width,height)
            property real progress: 0
            fragmentShader: "qrc:/shaders/reveal.frag.qsb"
        }
        NumberAnimation { id: reveal; target: transitionCover; property: "progress"; from: 0; to: 1.01; duration: Theme.pageDuration; easing.type: Easing.InOutCubic; onFinished: transitionCover.visible=false }
    }
    ConversionPrompts { id: prompts; objectName: "conversionPrompts" }
    FormatGuide { id: formatGuide; parent: Overlay.overlay }
    Preview { id: preview; parent: Overlay.overlay }
    Dialog {
        id: help
        objectName: "helpDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(window.width-80,600)
        modal: true
        padding: 24
        background: Card {}
        contentItem: ColumnLayout {
            spacing: 20
            Text { text: "Athanor"; font.pixelSize: 26; font.bold: true; color: window.fg }
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: window.fg; font.pixelSize: 14; lineHeight: 1.4; text: "Compress images, audio, videos and PDFs on your computer.\n\nConvert between image, audio and video formats. Export video audio, turn PDF pages into JPGs, or combine queued images into a PDF. Compress existing PDFs while preserving selectable text and document structure.\n\nAdd files or folders, or drag them into the queue. Choose compression quality, a maximum file size, or conversion, and process the batch.\n\nAthanor saves each result as a new file. Originals are kept by default, with an option to delete them after successful conversion." }
            ActionButton { text: "Compare formats"; icon: "info"; onClicked: {help.close();formatGuide.openFormat("avif")} }
            ActionButton { text: "Close"; icon: "close"; Layout.alignment: Qt.AlignRight; onClicked: help.close() }
        }
    }
    Connections { target: backend; function onPreviewOpened(kind,value){preview.showPreview(kind,value)} function onFilesAdded(){window.showPage(0)} function onTransitionExpired(){reveal.stop();transitionCover.progress=1.01;transitionCover.visible=false} }
}
