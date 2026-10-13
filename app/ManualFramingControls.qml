import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ScreenPilot.Render 1.0
ColumnLayout {
    id: root
    property double positionSeconds: 0
    property var sourcePreview: null
    property var segment: {
        const segments = project.manualFramingSegments
        for (let i = 0; i < segments.length; ++i)
            if (segments[i].id === project.selectedManualFraming) return segments[i]
        return null
    }
    function setValue(key, value) {
        if (!segment) return
        let changes = {}; changes[key] = value
        project.updateManualFraming(segment.id, changes)
    }
    component FieldRow: RowLayout {
        id: field
        property string key: ""
        property string label: ""
        property real maxValue: 6
        property real minValue: 0
        Layout.fillWidth: true
        spacing: 8
        Text { text: field.label; color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper; Layout.preferredWidth: 92; elide: Text.ElideRight }
        StudioTextField {
            Layout.fillWidth: true
            implicitHeight: 30
            font.family: StudioTheme.monoFamily
            text: root.segment ? Number(root.segment[field.key]).toFixed(3) : ""
            validator: DoubleValidator { bottom: field.minValue; top: field.maxValue; locale: "C" }
            onEditingFinished: root.setValue(field.key, Number(text))
        }
    }
    spacing: 22
    StudioSection {
        title: "手动运镜"; icon: "zoomIn"
        helper: "手动片段优先于自动运镜。起止时间使用原始素材秒数，裁剪、排序及倍速后自动映射。"
        RowLayout {
            Layout.fillWidth: true
            Text { text: "播放头"; color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper; Layout.fillWidth: true }
            Text { text: root.positionSeconds.toFixed(2) + " 秒 · 素材 " + project.sourceTime(root.positionSeconds).toFixed(2) + " 秒"; color: StudioTheme.text; font.family: StudioTheme.monoFamily; font.pixelSize: StudioTheme.helper }
        }
        StudioButton { text: "在播放头添加运镜"; iconName: "plus"; primary: true; Layout.fillWidth: true; enabled: project.hasProject; onClicked: project.addManualFraming(root.positionSeconds) }
        ManualZoomTrack { Layout.fillWidth: true; positionSeconds: root.positionSeconds }
    }
    StudioSection {
        visible: root.segment !== null
        title: "焦点"; icon: "zoom"
        helper: "点击原始画面设置放大焦点。"
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            radius: StudioTheme.radiusGroup
            color: "#141518"
            clip: true
            CropPreview { id: focusPreview; anchors.fill: parent; anchors.margins: 4; sourcePreview: root.sourcePreview }
            Rectangle {
                visible: root.segment !== null
                width: 16; height: 16; radius: 8; color: StudioTheme.amber; border.color: "white"; border.width: 2
                x: focusPreview.x + focusPreview.imageRect.x + (root.segment ? root.segment.centerX : .5) * focusPreview.imageRect.width - width/2
                y: focusPreview.y + focusPreview.imageRect.y + (root.segment ? root.segment.centerY : .5) * focusPreview.imageRect.height - height/2
            }
            MouseArea {
                anchors.fill: focusPreview
                cursorShape: Qt.CrossCursor
                onClicked: mouse => {
                    const r=focusPreview.imageRect
                    if (!root.segment || r.width<=0 || r.height<=0 || mouse.x<r.x || mouse.x>r.x+r.width || mouse.y<r.y || mouse.y>r.y+r.height) return
                    project.updateManualFraming(root.segment.id, {centerX:(mouse.x-r.x)/r.width, centerY:(mouse.y-r.y)/r.height})
                }
            }
        }
    }
    StudioSection {
        visible: root.segment !== null
        title: "参数"; icon: "sliders"
        FieldRow { key: "start"; label: "素材开始 (秒)"; maxValue: project.sourceDuration }
        FieldRow { key: "end"; label: "素材结束 (秒)"; maxValue: project.sourceDuration }
        FieldRow { key: "scale"; label: "缩放 (1–6)"; minValue: 1 }
        FieldRow { key: "centerX"; label: "焦点 X (0–1)"; maxValue: 1 }
        FieldRow { key: "centerY"; label: "焦点 Y (0–1)"; maxValue: 1 }
        FieldRow { key: "enter"; label: "进入 (0–5 秒)"; maxValue: 5 }
        FieldRow { key: "exit"; label: "退出 (0–5 秒)"; maxValue: 5 }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Text { text: "过渡曲线"; color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper; Layout.preferredWidth: 92 }
            StudioComboBox {
                id: curveBox
                Layout.fillWidth: true
                implicitHeight: 30
                model: ["linear", "quadratic", "cubic", "quintic", "spring", "custom"]
                currentIndex: root.segment ? model.indexOf(root.segment.curve) : 4
                onActivated: root.setValue("curve", currentText)
            }
        }
        GridLayout {
            visible: root.segment !== null && root.segment.curve === "custom"
            Layout.fillWidth: true
            columns: 4; columnSpacing: 6; rowSpacing: 6
            Repeater {
                model: ["x1", "y1", "x2", "y2"]
                delegate: StudioTextField {
                    required property string modelData
                    Layout.fillWidth: true
                    implicitHeight: 30
                    placeholderText: modelData
                    font.family: StudioTheme.monoFamily
                    text: root.segment ? Number(root.segment[modelData]).toFixed(2) : "0"
                    validator: DoubleValidator { bottom: 0; top: 1; locale: "C" }
                    onEditingFinished: root.setValue(modelData, Number(text))
                }
            }
        }
    }
    StudioSection {
        title: "片段操作"; icon: "scissors"
        GridLayout {
            Layout.fillWidth: true; columns: 2; columnSpacing: 8; rowSpacing: 8
            StudioButton { text: "在播放头分割"; iconName: "split"; Layout.fillWidth: true; Layout.columnSpan: 2; enabled: root.segment !== null; onClicked: project.splitManualFraming(root.positionSeconds) }
            StudioButton { text: "复制"; iconName: "copy"; Layout.fillWidth: true; enabled: root.segment !== null; onClicked: project.copyManualFraming() }
            StudioButton { text: "粘贴"; iconName: "paste"; Layout.fillWidth: true; enabled: project.hasManualClipboard && project.hasProject; onClicked: project.pasteManualFraming(root.positionSeconds) }
            StudioButton { text: "粘贴运镜属性"; Layout.fillWidth: true; Layout.columnSpan: 2; enabled: root.segment !== null && project.hasManualClipboard; onClicked: project.pasteManualFramingProperties() }
            StudioButton { text: "删除所选运镜"; iconName: "trash"; danger: true; Layout.fillWidth: true; Layout.columnSpan: 2; enabled: root.segment !== null; onClicked: project.removeManualFraming(root.segment.id) }
        }
        StudioHelp { text: "拖动运镜片段移动，拖动两端调整；按住 Shift 暂停吸附，Esc 取消拖动。" }
    }
}
