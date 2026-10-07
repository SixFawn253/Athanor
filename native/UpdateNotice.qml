import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Dialog {
    id: notice
    objectName: "updateNotice"
    anchors.centerIn: parent
    width: Math.min(parent.width-48,540)
    modal: true
    padding: 24
    title: updater.availableVersion
    header: Text { text: notice.title; color: Theme.text; font.pixelSize: 20; font.weight: Font.DemiBold; leftPadding: 24; rightPadding: 24; topPadding: 24; bottomPadding: 8; elide: Text.ElideRight }
    palette.windowText: Theme.text
    background: Rectangle { color: Theme.menuSurface; radius: 24; border.width: 1; border.color: Theme.border }
    contentItem: ColumnLayout { spacing: 20
        ColumnLayout { Layout.fillWidth: true; spacing: 8
            Text { text: "Keep it portable"; color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
            Text { Layout.fillWidth: true; text: nativePlatform==="Windows"?"Download a separate copy. Its runtime and settings stay beside the executable, so you can move the entire folder.":"Download a separate app package to your chosen location. Your current installation stays unchanged."; color: Theme.muted; font.pixelSize: 13; wrapMode: Text.Wrap }
            Text { objectName: "portableDownloadLink"; textFormat: Text.RichText; text: "<a href=\""+updater.portableUrl.replace(/&/g,"&amp;").replace(/\"/g,"&quot;")+"\">Download portable</a>"; color: Theme.accent; linkColor: Theme.accent; font.pixelSize: 14; onLinkActivated: updater.openPortableDownload(); HoverHandler { cursorShape: Qt.PointingHandCursor } }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.softBorder }
        ColumnLayout { Layout.fillWidth: true; spacing: 8
            Text { text: "Update this installation"; color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
            Text { Layout.fillWidth: true; text: "Replace this app with the latest version and restart. This switches to an installed setup using this computer’s application data, so it loses portability. Saved settings and originals are kept; the current queue is cleared."; color: Theme.muted; font.pixelSize: 13; wrapMode: Text.Wrap }
            ActionButton { objectName: "applyUpdateButton"; text: updater.downloading?"Updating "+updater.progress+"%":"Update in place"; primary: true; enabled: updater.canInstall&&!backend.busy&&!backend.previewBusy; onClicked: updater.install() }
        }
        ProgressBar { Layout.fillWidth: true; visible: updater.downloading; from: 0; to: 100; value: updater.progress; palette.highlight: Theme.accent }
        ActionButton { text: "Close"; quiet: true; Layout.alignment: Qt.AlignRight; onClicked: notice.close() }
    }
}
