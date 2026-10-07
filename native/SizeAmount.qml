import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
RowLayout {
    property bool valid: amount.acceptableInput
    implicitHeight: 42
    spacing: 10
    Input {
        id: amount
        Accessible.name: "Maximum file size in MB"
        Layout.preferredWidth: 104
        Layout.preferredHeight: 42
        text: ""
        Component.onCompleted: text=(backend.options.target_bytes/1000000).toString()
        enabled: !backend.busy
        selectByMouse: true
        font.pixelSize: 13
        color: Theme.text
        validator: DoubleValidator { bottom: .000001; top: 1000000000; decimals: 6; notation: DoubleValidator.StandardNotation; locale: "en_US" }
        onTextEdited: if(acceptableInput) backend.setOption("target_bytes",Math.round(Number(text.replace(/,/g,""))*1000000))
    }
    Text { text: "MB per file"; color: Theme.muted; font.pixelSize: 12 }
    Item { Layout.fillWidth: true }
}
