import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ColumnLayout {
    id: control
    property string label: "Output"
    property var formats: []
    property string value: ""
    property string optionKey: ""
    property alias field: field
    signal infoRequested(string key)
    spacing: 5
    RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 24
        Text { Layout.fillWidth: true; text: control.label.toUpperCase(); font.pixelSize: 11; color: Theme.muted }
        ActionButton { quiet: true; icon: "info"; implicitWidth: 24; implicitHeight: 24; accessibleText: control.label+" format information"; tooltip: "About "+Formats.info(control.value).name; onClicked: control.infoRequested(control.value) }
    }
    Field { id: field; Layout.fillWidth: true; accessibleLabel: control.label; model: Formats.names(control.formats); transparencyIndices: Formats.alphaIndices(control.formats); compactIndices: Formats.compactIndices(control.formats); currentIndex: control.formats.indexOf(control.value); enabled: !backend.busy; onActivated: backend.setOption(control.optionKey,control.formats[index]==="jpg-pages"?"jpg":control.formats[index]) }
}
