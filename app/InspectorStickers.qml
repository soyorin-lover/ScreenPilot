import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
// Image stickers ("stickers" track). Images are copied into project-owned
// storage by the backend; the UI never edits id or imagePath.
ColumnLayout {
    id: root
    property real positionSeconds: 0
    readonly property string track: "stickers"
    readonly property var selected: { const it = effects.selectedItems[track]; return it && it.id ? it : null }
    function imageUrl(item) {
        if (!item || !item.imagePath) return ""
        return effects.localFileUrl(String(item.imagePath))
    }
    Layout.fillWidth: true
    spacing: 22
    FileDialog {
        id: stickerDialog
        title: "选择贴图图片"
        nameFilters: ["图片 (*.png *.jpg *.jpeg *.webp *.bmp)"]
        onAccepted: { const source = project.sourceTime(root.positionSeconds); const id = effects.addSticker(selectedFile, source, Math.min(3, project.sourceDuration - source)); if (id !== "") effects.select(root.track, id) }
    }
    ColorDialog {
        id: borderColorDialog
        title: "贴图描边颜色"
        onAccepted: effects.updateSelected(root.track, { borderColor: selectedColor.toString() })
    }

    StudioSection {
        title: "贴图"; icon: "image"
        helper: "贴图位于屏幕之上、摄像头之下，按素材时间出现。图片会复制进工程，原文件移动或删除不影响。"
        StudioButton { text: "在播放头添加贴图…"; iconName: "plus"; primary: true; Layout.fillWidth: true; enabled: project.hasProject; onClicked: stickerDialog.open() }
        Repeater {
            model: effects.stickers
            delegate: ItemDelegate {
                id: row
                required property var modelData
                required property int index
                readonly property bool isSelected: (effects.selectedIds[root.track] || "") === modelData.id
                Layout.fillWidth: true
                implicitHeight: 54
                leftPadding: 6; rightPadding: 4
                onClicked: effects.select(root.track, modelData.id)
                contentItem: RowLayout {
                    spacing: 10
                    Rectangle {
                        width: 42; height: 42; radius: 9; color: StudioTheme.well; clip: true
                        border.color: row.isSelected ? StudioTheme.accent : "transparent"; border.width: 2
                        Image { objectName: "stickerThumbnail-" + row.modelData.id; anchors.fill: parent; anchors.margins: 3; source: root.imageUrl(row.modelData); fillMode: Image.PreserveAspectFit; asynchronous: true; sourceSize.width: 96; sourceSize.height: 96 }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 1
                        Text { text: "贴图 " + (row.index + 1); color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body; font.weight: row.isSelected ? Font.DemiBold : Font.Medium }
                        Text { text: "素材 " + Number(row.modelData.sourceStart).toFixed(2) + "s · " + Number(row.modelData.sourceDuration).toFixed(1) + " 秒"; color: StudioTheme.faint; font.family: StudioTheme.monoFamily; font.pixelSize: StudioTheme.caption }
                    }
                    StudioButton { iconName: "trash"; quiet: true; compact: true; tip: "删除"; onClicked: effects.remove(root.track, row.modelData.id) }
                }
                background: Rectangle { radius: StudioTheme.radiusControl; color: row.isSelected ? StudioTheme.selection : row.hovered ? StudioTheme.well : "transparent" }
            }
        }
        StudioHelp { visible: effects.stickers.length === 0; text: "还没有贴图。" }
    }

    StudioSection {
        visible: root.selected !== null
        title: "位置与大小"; icon: "layout"
        helper: "在画布示意中拖动贴图调整位置。"
        Rectangle {
            id: pad
            Layout.fillWidth: true
            Layout.preferredHeight: width * 9 / 16
            radius: StudioTheme.radiusGroup
            color: StudioTheme.surfaceAlt
            border.color: StudioTheme.hairline
            clip: true
            StudioDotGrid { anchors.fill: parent; anchors.margins: 1 }
            Item {
                id: stickerGhost
                readonly property real nx: root.selected ? root.selected.x : .5
                readonly property real ny: root.selected ? root.selected.y : .5
                width: pad.width * (root.selected ? root.selected.width : .3)
                height: width * (ghostImage.implicitWidth > 0 ? ghostImage.implicitHeight / ghostImage.implicitWidth : .6)
                x: nx * pad.width - width / 2
                y: ny * pad.height - height / 2
                rotation: root.selected ? root.selected.rotation : 0
                opacity: root.selected ? Math.max(.25, root.selected.opacity) : 1
                Image { id: ghostImage; anchors.fill: parent; source: root.imageUrl(root.selected); fillMode: Image.PreserveAspectFit; asynchronous: true; sourceSize.width: 320 }
                Rectangle { anchors.fill: parent; color: "transparent"; border.color: StudioTheme.accent; border.width: 1.5; radius: 3 }
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                property real lastX: 0
                property real lastY: 0
                onPressed: mouse => { lastX = mouse.x; lastY = mouse.y }
                onReleased: mouse => effects.updateSelected(root.track, { x: Math.max(0, Math.min(1, mouse.x / pad.width)), y: Math.max(0, Math.min(1, mouse.y / pad.height)) })
            }
        }
        StudioSliderRow { label: "宽度"; valueText: Math.round((root.selected ? root.selected.width : 0) * 100) + "%"; from: .03; to: 1; stepSize: .01; value: root.selected ? root.selected.width : .3; onValueMoved: v => effects.updateSelected(root.track, { width: v }) }
        StudioSliderRow { label: "旋转"; valueText: Math.round(root.selected ? root.selected.rotation : 0) + "°"; from: -180; to: 180; stepSize: 1; value: root.selected ? root.selected.rotation : 0; onValueMoved: v => effects.updateSelected(root.track, { rotation: v }) }
        StudioSliderRow { label: "不透明度"; valueText: Math.round((root.selected ? root.selected.opacity : 1) * 100) + "%"; from: 0; to: 1; stepSize: .01; value: root.selected ? root.selected.opacity : 1; onValueMoved: v => effects.updateSelected(root.track, { opacity: v }) }
    }

    StudioSection {
        visible: root.selected !== null
        title: "样式与时间"; icon: "sliders"
        StudioSliderRow { label: "圆角"; valueText: Math.round((root.selected ? root.selected.radius : 0) * 100) + "%"; from: 0; to: .5; stepSize: .01; value: root.selected ? root.selected.radius : 0; onValueMoved: v => effects.updateSelected(root.track, { radius: v }) }
        StudioSliderRow { label: "描边"; valueText: Number(root.selected ? root.selected.borderWidth : 0).toFixed(1); from: 0; to: 12; stepSize: .5; value: root.selected ? root.selected.borderWidth : 0; onValueMoved: v => effects.updateSelected(root.track, { borderWidth: v }) }
        RowLayout {
            Layout.fillWidth: true; spacing: 10
            Text { text: "描边颜色"; color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body; font.weight: Font.Medium; Layout.fillWidth: true }
            AbstractButton {
                implicitWidth: 34; implicitHeight: 26
                onClicked: { borderColorDialog.selectedColor = root.selected ? root.selected.borderColor : "#ffffff"; borderColorDialog.open() }
                background: Rectangle { radius: 8; color: root.selected ? root.selected.borderColor : "#ffffff"; border.color: StudioTheme.border }
            }
        }
        StudioCheckBox { text: "投影"; checked: root.selected ? root.selected.shadow === true : false; onToggled: effects.updateSelected(root.track, { shadow: checked }) }
        StudioSliderRow { label: "进入动画"; valueText: Number(root.selected ? root.selected.enterDuration : 0).toFixed(2) + " 秒"; from: 0; to: 2; stepSize: .05; value: root.selected ? root.selected.enterDuration : 0; onValueMoved: v => effects.updateSelected(root.track, { enterDuration: v }) }
        StudioSliderRow { label: "退出动画"; valueText: Number(root.selected ? root.selected.exitDuration : 0).toFixed(2) + " 秒"; from: 0; to: 2; stepSize: .05; value: root.selected ? root.selected.exitDuration : 0; onValueMoved: v => effects.updateSelected(root.track, { exitDuration: v }) }
        StudioNumberField { label: "素材开始"; suffix: "秒"; value: root.selected ? root.selected.sourceStart : 0; to: project.sourceDuration; onCommitted: v => effects.updateSelected(root.track, { sourceStart: v }) }
        StudioNumberField { label: "持续时长"; suffix: "秒"; value: root.selected ? root.selected.sourceDuration : 0; from: .05; to: project.sourceDuration; onCommitted: v => effects.updateSelected(root.track, { sourceDuration: v }) }
        StudioButton { text: "删除此贴图"; iconName: "trash"; danger: true; Layout.fillWidth: true; onClicked: effects.removeSelected(root.track) }
    }
    Text { visible: effects.error !== ""; text: effects.error; color: StudioTheme.danger; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper; wrapMode: Text.Wrap; Layout.fillWidth: true }
}
