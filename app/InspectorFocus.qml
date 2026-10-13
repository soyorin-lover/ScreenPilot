import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ScreenPilot.Render 1.0
// Local blur / spotlight regions ("blurRegions" track, source-time ranges).
ColumnLayout {
    id: root
    property real positionSeconds: 0
    property var sourcePreview: null
    readonly property string track: "blurRegions"
    readonly property var selected: { const it = effects.selectedItems[track]; return it && it.id ? it : null }
    function add(style) {
        const id = effects.addAtOutput(track, positionSeconds, 2)
        if (id === "") return
        effects.update(track, id, { style: style, strength: style === "spotlight" ? .6 : .7 })
        effects.select(track, id)
    }
    Layout.fillWidth: true
    spacing: 22

    StudioSection {
        title: "模糊与聚光灯"; icon: "zoom"
        helper: "模糊遮挡敏感信息；聚光灯压暗区域外的画面来突出重点。区域按素材时间保存。"
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            StudioButton { text: "添加模糊"; iconName: "plus"; Layout.fillWidth: true; enabled: project.hasProject; onClicked: root.add("blur") }
            StudioButton { text: "添加聚光灯"; iconName: "sun"; Layout.fillWidth: true; enabled: project.hasProject; onClicked: root.add("spotlight") }
        }
        EffectItemList {
            track: root.track
            items: effects.blurRegions
            iconName: "zoom"
            describe: (item, index) => (item.style === "spotlight" ? "聚光灯 " : "模糊 ") + (index + 1) + " · " + Number(item.sourceDuration).toFixed(1) + " 秒"
        }
    }

    StudioSection {
        visible: root.selected !== null
        title: "区域"; icon: "crop"
        helper: "在原始画面上拖动框选区域，拖动角点调整大小。"
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 158
            radius: StudioTheme.radiusGroup; color: "#141518"; clip: true
            CropPreview { id: framePreview; anchors.fill: parent; anchors.margins: 4; sourcePreview: root.sourcePreview }
            RegionBox {
                anchors.fill: framePreview
                imageRect: framePreview.imageRect
                tint: root.selected && root.selected.style === "spotlight" ? StudioTheme.amber : "white"
                dimOutside: root.selected !== null && root.selected.style === "spotlight"
                rx: root.selected ? root.selected.x : 0; ry: root.selected ? root.selected.y : 0
                rw: root.selected ? root.selected.width : 1; rh: root.selected ? root.selected.height : 1
                onCommitted: (x, y, w, h) => effects.updateSelected(root.track, { x: x, y: y, width: w, height: h })
            }
        }
    }

    StudioSection {
        visible: root.selected !== null
        title: "效果"; icon: "sliders"
        StudioSegmented {
            Layout.fillWidth: true
            model: ["模糊", "聚光灯"]
            icons: ["zoom", "sun"]
            currentIndex: root.selected && root.selected.style === "spotlight" ? 1 : 0
            onActivated: index => effects.updateSelected(root.track, { style: index === 1 ? "spotlight" : "blur" })
        }
        StudioSliderRow {
            label: root.selected && root.selected.style === "spotlight" ? "外部压暗" : "模糊强度"
            valueText: Math.round((root.selected ? root.selected.strength : 0) * 100) + "%"
            from: 0; to: 1; stepSize: .01
            value: root.selected ? root.selected.strength : 0
            onValueMoved: v => effects.updateSelected(root.track, { strength: v })
        }
        StudioSliderRow { label: "淡入"; valueText: Number(root.selected ? root.selected.enterDuration : 0).toFixed(1) + " 秒"; from: 0; to: 2; stepSize: .05; value: root.selected ? root.selected.enterDuration : 0; onValueMoved: v => effects.updateSelected(root.track, { enterDuration: v }) }
        StudioSliderRow { label: "淡出"; valueText: Number(root.selected ? root.selected.exitDuration : 0).toFixed(1) + " 秒"; from: 0; to: 2; stepSize: .05; value: root.selected ? root.selected.exitDuration : 0; onValueMoved: v => effects.updateSelected(root.track, { exitDuration: v }) }
        StudioNumberField { label: "素材开始"; suffix: "秒"; value: root.selected ? root.selected.sourceStart : 0; to: project.sourceDuration; onCommitted: v => effects.updateSelected(root.track, { sourceStart: v }) }
        StudioNumberField { label: "持续时长"; suffix: "秒"; value: root.selected ? root.selected.sourceDuration : 0; from: .05; to: project.sourceDuration; onCommitted: v => effects.updateSelected(root.track, { sourceDuration: v }) }
        StudioButton { text: "删除此区域"; iconName: "trash"; danger: true; Layout.fillWidth: true; onClicked: effects.removeSelected(root.track) }
    }
    StudioHelp { text: "模糊为近似实现，画面细节可能与预期略有差异。" }
    Text { visible: effects.error !== ""; text: effects.error; color: StudioTheme.danger; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper; wrapMode: Text.Wrap; Layout.fillWidth: true }
}
