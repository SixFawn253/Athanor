import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Item {
    id: prompts
    readonly property bool active: deleteWarning.visible||conversionWarning.visible
    function toggleDelete(control) {
        if(!control.checked){ backend.setOption("delete",false);return }
        control.checked=false
        deleteWarning.checkControl=control
        deleteWarning.open()
    }
    function start() { if(backend.conversionWarning.length)conversionWarning.open();else backend.start() }
    component WarningDialog: Dialog {
        parent: Overlay.overlay; anchors.centerIn: parent; width: Math.min(parent.width-48,520); modal: true; focus: true; padding: 24
        onOpened: cancelButton.forceActiveFocus(Qt.OtherFocusReason)
        property string heading: ""
        property string message: ""
        property string actionText: "Continue"
        signal acceptedAction()
        background: Card {}
        contentItem: ColumnLayout { spacing: 18
            RowLayout { Glyph { name: "warning"; color: Theme.warning; width: 24; height: 24 } Text { Layout.fillWidth: true; text: heading; color: Theme.text; font.pixelSize: 21; font.weight: Font.DemiBold; wrapMode: Text.Wrap } }
            Text { Layout.fillWidth: true; text: message; color: Theme.text; font.pixelSize: 13; wrapMode: Text.Wrap; lineHeight: 1.3 }
            RowLayout { Layout.alignment: Qt.AlignRight; ActionButton { id: cancelButton; text: "Cancel"; onClicked: close() } ActionButton { text: actionText; primary: true; onClicked: {close();acceptedAction()} } }
        }
    }
    WarningDialog { id: deleteWarning; objectName: "deleteWarning"; property var checkControl: null; heading: "Delete originals?"; message: "Original files will be permanently deleted after their converted output has been validated. Keep a backup if you may need the original quality or format."; actionText: "Enable deletion"; onClosed: if(checkControl)checkControl.checked=Qt.binding(function(){return backend.options.delete}); onAcceptedAction: backend.setOption("delete",true) }
    WarningDialog { id: conversionWarning; objectName: "conversionWarning"; heading: "Review conversion changes"; message: backend.conversionWarning; actionText: "Convert"; onAcceptedAction: backend.start() }
}
