import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ScreenPilot.Render 1.0
StudioDialog {
    id: dialog
    property var sourcePreview
    property var project
    property real cropX: 0
    property real cropY: 0
    property real cropWidth: 1
    property real cropHeight: 1
    title: "裁切屏幕"
    subtitle: "拖动选择区域，拖动角点调整大小；取消不会保存。"
    iconName: "crop"
    modal: true
    width: Math.min(820, parent ? parent.width - 32 : 820)
    height: Math.min(620, parent ? parent.height - 32 : 620)
    anchors.centerIn: parent
    onOpened: {
        const s = project.canvasSettings
        cropX = s.cropX === undefined ? 0 : s.cropX
        cropY = s.cropY === undefined ? 0 : s.cropY
        cropWidth = s.cropWidth === undefined ? 1 : s.cropWidth
        cropHeight = s.cropHeight === undefined ? 1 : s.cropHeight
    }
    onAccepted: project.setCanvasCrop(cropX, cropY, cropWidth, cropHeight)
    contentItem: ColumnLayout {
        spacing: 12
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true
            Rectangle { anchors.fill: parent; radius: StudioTheme.radiusGroup; color: "#141518" }
            CropPreview { id: preview; anchors.fill: parent; sourcePreview: dialog.sourcePreview }
            Rectangle {
                id: selection
                x: preview.imageRect.x + dialog.cropX * preview.imageRect.width
                y: preview.imageRect.y + dialog.cropY * preview.imageRect.height
                width: dialog.cropWidth * preview.imageRect.width
                height: dialog.cropHeight * preview.imageRect.height
                color: "#18ffffff"; border.color: "white"; border.width: 2
            }
            MouseArea {
                anchors.fill: parent
                property real startX
                property real startY
                property real oldX
                property real oldY
                property real oldW
                property real oldH
                property int mode: 0
                function nx(v) { return Math.max(0, Math.min(1, (v - preview.imageRect.x) / preview.imageRect.width)) }
                function ny(v) { return Math.max(0, Math.min(1, (v - preview.imageRect.y) / preview.imageRect.height)) }
                onPressed: mouse => {
                    if (preview.imageRect.width <= 0) return
                    startX = nx(mouse.x); startY = ny(mouse.y)
                    oldX = dialog.cropX; oldY = dialog.cropY; oldW = dialog.cropWidth; oldH = dialog.cropHeight
                    const left = Math.abs(mouse.x - selection.x) < 12
                    const right = Math.abs(mouse.x - selection.x - selection.width) < 12
                    const top = Math.abs(mouse.y - selection.y) < 12
                    const bottom = Math.abs(mouse.y - selection.y - selection.height) < 12
                    mode = (left || right) && (top || bottom) ? (left ? 1 : 2) + (top ? 0 : 2) :
                           (oldW < .999 || oldH < .999) && startX > oldX && startX < oldX + oldW && startY > oldY && startY < oldY + oldH ? 5 : 0
                }
                onPositionChanged: mouse => {
                    if (!pressed || preview.imageRect.width <= 0) return
                    const x = nx(mouse.x), y = ny(mouse.y)
                    if (mode === 5) {
                        dialog.cropX = Math.max(0, Math.min(1 - oldW, oldX + x - startX))
                        dialog.cropY = Math.max(0, Math.min(1 - oldH, oldY + y - startY))
                    } else {
                        const anchorX = mode === 1 || mode === 3 ? oldX + oldW : mode === 2 || mode === 4 ? oldX : startX
                        const anchorY = mode === 1 || mode === 2 ? oldY + oldH : mode === 3 || mode === 4 ? oldY : startY
                        dialog.cropX = Math.min(anchorX, Math.min(x, .99))
                        dialog.cropY = Math.min(anchorY, Math.min(y, .99))
                        dialog.cropWidth = Math.min(1 - dialog.cropX, Math.max(.01, Math.abs(x - anchorX)))
                        dialog.cropHeight = Math.min(1 - dialog.cropY, Math.max(.01, Math.abs(y - anchorY)))
                    }
                }
            }
            Repeater {
                model: 4
                Rectangle {
                    x: selection.x + (index % 2 ? selection.width : 0) - 6
                    y: selection.y + (index >= 2 ? selection.height : 0) - 6
                    width: 12; height: 12; radius: 6; color: "white"; border.color: StudioTheme.accent; border.width: 2
                }
            }
        }
    }
    footer: StudioDialogFooter {
        StudioButton { text: "重置为完整画面"; iconName: "refresh"; quiet: true; onClicked: { dialog.cropX = 0; dialog.cropY = 0; dialog.cropWidth = 1; dialog.cropHeight = 1 } }
        Item { Layout.fillWidth: true }
        StudioButton { text: "取消"; onClicked: dialog.reject() }
        StudioButton { text: "应用裁切"; primary: true; onClicked: dialog.accept() }
    }
}
