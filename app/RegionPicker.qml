import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
// Choose a capture region inside the selected source. Values are normalized to
// the full source; Apply calls recorder.setCaptureRegion, Full clears it.
StudioDialog {
    id: picker
    objectName: "regionPicker"
    title: "录制区域"
    subtitle: source ? source.label : "请先选择显示器或窗口"
    iconName: "region"
    width: Math.min(760, parent ? parent.width - 40 : 760)
    height: Math.min(620, parent ? parent.height - 40 : 620)
    readonly property var source: recorder.selectedSource >= 0 && recorder.selectedSource < recorder.sources.length ? recorder.sources[recorder.selectedSource] : null
    // Geometry belongs to the capture backend; source ids are not screen indices.
    readonly property real pixelWidth: source ? Number(source.pixelWidth || 0) : 0
    readonly property real pixelHeight: source ? Number(source.pixelHeight || 0) : 0
    readonly property real sourceAspect: pixelWidth > 0 && pixelHeight > 0 ? pixelWidth / pixelHeight : 16 / 9
    property real rx: 0
    property real ry: 0
    property real rw: 1
    property real rh: 1
    onOpened: { const r = recorder.captureRegion; rx = r.x; ry = r.y; rw = r.width; rh = r.height }
    function preset(aspect) {
        // Largest centred rectangle of `aspect` inside the source.
        let w = 1, h = sourceAspect / aspect
        if (h > 1) { w = aspect / sourceAspect; h = 1 }
        w *= .9; h *= .9
        rx = (1 - w) / 2; ry = (1 - h) / 2; rw = w; rh = h
    }
    contentItem: ColumnLayout {
        spacing: 14
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            radius: StudioTheme.radiusCard
            color: StudioTheme.surfaceAlt
            border.color: StudioTheme.hairline
            clip: true
            StudioDotGrid { anchors.fill: parent; anchors.margins: 1 }
            Item {
                id: screenFrame
                readonly property real fit: Math.min((parent.width - 40) / picker.sourceAspect, parent.height - 40)
                width: fit * picker.sourceAspect; height: fit
                anchors.centerIn: parent
                Rectangle {
                    anchors.fill: parent; radius: 6
                    gradient: Gradient { GradientStop { position: 0; color: "#3b4a6b" } GradientStop { position: 1; color: "#1d2436" } }
                    border.color: "#40ffffff"
                    StudioIcon { anchors.centerIn: parent; name: picker.source && picker.source.kind === "window" ? "window" : "monitor"; width: 42; height: 42; color: "#55ffffff" }
                }
                RegionBox {
                    anchors.fill: parent
                    tint: StudioTheme.recording
                    rx: picker.rx; ry: picker.ry; rw: picker.rw; rh: picker.rh
                    onCommitted: (x, y, w, h) => { picker.rx = x; picker.ry = y; picker.rw = w; picker.rh = h }
                }
            }
            Rectangle {
                anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 12
                height: 26; radius: 13; width: sizeLabel.implicitWidth + 20; color: "#cc141518"
                Text {
                    id: sizeLabel; anchors.centerIn: parent; color: "white"; font.family: StudioTheme.monoFamily; font.pixelSize: StudioTheme.caption
                    text: picker.pixelWidth > 0 ? (Math.ceil((picker.rx + picker.rw) * picker.pixelWidth) - Math.floor(picker.rx * picker.pixelWidth)) + " × " + (Math.ceil((picker.ry + picker.rh) * picker.pixelHeight) - Math.floor(picker.ry * picker.pixelHeight)) + " px" : Math.round(picker.rw * 100) + "% × " + Math.round(picker.rh * 100) + "%"
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 6
            Text { text: "快速选择"; color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper; Layout.rightMargin: 4 }
            StudioButton { text: "完整画面"; compact: true; onClicked: { picker.rx = 0; picker.ry = 0; picker.rw = 1; picker.rh = 1 } }
            StudioButton { text: "16:9"; compact: true; onClicked: picker.preset(16 / 9) }
            StudioButton { text: "4:3"; compact: true; onClicked: picker.preset(4 / 3) }
            StudioButton { text: "1:1"; compact: true; onClicked: picker.preset(1) }
            StudioButton { text: "9:16"; compact: true; onClicked: picker.preset(9 / 16) }
            Item { Layout.fillWidth: true }
        }
        StudioHelp { text: picker.source && picker.source.kind === "window" ? "窗口尺寸在录制开始前未知，示意按 16:9 显示；区域按窗口比例换算。" : "区域会裁切实际像素，鼠标轨迹和预览同步调整。切换来源会清除区域。" }
    }
    footer: StudioDialogFooter {
        StudioButton { text: "使用完整画面"; quiet: true; enabled: !recorder.busy; onClicked: { recorder.clearCaptureRegion(); picker.close() } }
        Item { Layout.fillWidth: true }
        StudioButton { text: "取消"; onClicked: picker.close() }
        StudioButton {
            text: "使用此区域"; primary: true
            enabled: !recorder.busy && picker.source !== null
            onClicked: {
                if (picker.rw > .999 && picker.rh > .999) recorder.clearCaptureRegion()
                else recorder.setCaptureRegion(Qt.rect(picker.rx, picker.ry, picker.rw, picker.rh))
                picker.close()
            }
        }
    }
}
