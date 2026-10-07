pragma Singleton
import QtQuick
QtObject {
    property point pointer: Qt.point(-1000,-1000)
    readonly property bool dark: backend.dark
    readonly property color canvas: dark ? "#191a1f" : "#f4f3f8"
    readonly property color surface: dark ? "#24262f" : "#fffbff"
    readonly property color surfaceAlt: dark ? "#20222a" : "#f0eef5"
    readonly property color header: dark ? "#15171e" : "#e2dfeb"
    readonly property color field: dark ? "#2c2f3a" : "#f3eff7"
    readonly property color border: dark ? "#77798c" : "#797184"
    readonly property color softBorder: dark ? "#414553" : "#d4cddd"
    readonly property color groupBorder: dark ? "#20222a" : "#d4cddd"
    readonly property color menuSurface: dark ? "#343744" : "#fffbff"
    readonly property color stateLayer: dark ? "#f1eff8" : "#292333"
    readonly property real hoverOpacity: 0.08
    readonly property color text: dark ? "#f1eff8" : "#292333"
    readonly property color muted: dark ? "#b4b4c7" : "#6a6178"
    readonly property color disabled: dark ? "#747888" : "#aaa2b5"
    readonly property color accent: dark ? "#b29aff" : "#7045c2"
    readonly property color onAccent: dark ? "#231738" : "#ffffff"
    readonly property color selection: dark ? "#393149" : "#eae1fb"
    readonly property color hover: dark ? "#414553" : "#e2ddec"
    readonly property color success: dark ? "#85d6ad" : "#216847"
    readonly property color info: dark ? "#91bcff" : "#285a9a"
    readonly property color warning: dark ? "#efc481" : "#85530b"
    readonly property color checkerA: dark ? "#242731" : "#d5cfdf"
    readonly property color checkerB: dark ? "#414655" : "#f7f5fb"
    readonly property color error: dark ? "#ffa3b1" : "#ab3049"
    readonly property string numericFont: nativePlatform==="Windows" ? "Consolas" : nativePlatform==="macOS" ? "Menlo" : "monospace"
    readonly property int controlHeight: 42
    readonly property int radius: 14
    readonly property int cardRadius: 18
    readonly property int fast: backend.reducedMotion ? 0 : 140
    readonly property int reveal: backend.reducedMotion ? 0 : 200
    readonly property int pageDuration: backend.reducedMotion ? 0 : 280
    function revealControl(item) {
        var ancestor=item.parent
        for(var i=0;ancestor&&i<64;i++) {
            if(typeof ancestor.contentY==="number"&&ancestor.contentItem) {
                var pos=item.mapToItem(ancestor.contentItem,0,0);var y=ancestor.contentY
                if(pos.y<y+12)y=pos.y-12
                else if(pos.y+item.height>y+ancestor.height-12)y=pos.y+item.height-ancestor.height+12
                ancestor.contentY=Math.max(0,Math.min(Math.max(0,ancestor.contentHeight-ancestor.height),y))
            }
            ancestor=ancestor.parent
        }
    }
    function keyboardFocus(reason) { return reason===Qt.TabFocusReason || reason===Qt.BacktabFocusReason || reason===Qt.ShortcutFocusReason }
    function statusColor(status, warning) {
        if(status==="Error") return error
        if(warning && warning.length) return Theme.warning
        if(status==="Done") return success
        if(status==="Queued"||status==="Cancelled") return muted
        return info
    }
    function statusIcon(status, warning) { return status==="Error" ? "close-circle" : warning&&warning.length ? "warning" : status==="Done" ? "check-circle" : status==="Queued"||status==="Cancelled" ? "clock" : "convert" }
}
