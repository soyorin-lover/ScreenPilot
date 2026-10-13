import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
// Camera layout motion ("cameraMotionSegments", start/end in source seconds)
// and piecewise sync points ("syncPoints", sourceTime -> cameraTime).
ColumnLayout {
    id: root
    property real positionSeconds: 0
    property real canvasAspect: 16 / 9
    property real cameraAspect: 1
    readonly property var motion: { const it = effects.selectedItems["cameraMotionSegments"]; return it && it.id ? it : null }
    readonly property var sync: { const it = effects.selectedItems["syncPoints"]; return it && it.id ? it : null }
    readonly property var shapes: ["rounded", "circle", "rectangle", "landscape", "portrait", "source"]
    readonly property var shapeLabels: ["圆角方形", "圆形", "方形", "横向", "纵向", "原始比例"]
    // Dynamic query: re-evaluated when the playhead or project changes.
    readonly property real cameraTime: { const c = project.cameraSettings; return project.cameraTimeAt(positionSeconds) }
    readonly property real cameraRate: { const c = project.cameraSettings; return project.cameraRateAt(positionSeconds) }
    function patchMotion(p) { effects.updateSelected("cameraMotionSegments", p) }
    Layout.fillWidth: true
    spacing: 22

    StudioSection {
        title: "摄像头运动"; icon: "motion"
        helper: "在指定时间段把摄像头移到新位置、改变大小或切换全屏。片段不能重叠，转场总长不超过片段。"
        StudioButton {
            text: "在播放头添加运动"; iconName: "plus"; primary: true; Layout.fillWidth: true
            onClicked: { const id = effects.addAtOutput("cameraMotionSegments", root.positionSeconds, 2); if (id !== "") effects.select("cameraMotionSegments", id) }
        }
        EffectItemList {
            track: "cameraMotionSegments"
            items: effects.cameraMotionSegments
            iconName: "camera"
            describe: (item, index) => "运动 " + (index + 1) + " · " + (item.fullscreen ? "全屏" : root.shapeLabels[Math.max(0, root.shapes.indexOf(item.shape))]) + " · " + Number(item.end - item.start).toFixed(1) + " 秒"
        }
    }

    StudioSection {
        visible: root.motion !== null
        title: "所选运动"; icon: "sliders"
        Rectangle {
            id: pad
            Layout.fillWidth: true
            Layout.preferredHeight: width / Math.max(.01, root.canvasAspect)
            radius: StudioTheme.radiusGroup
            color: StudioTheme.surfaceAlt
            border.color: StudioTheme.hairline
            clip: true
            StudioDotGrid { anchors.fill: parent; anchors.margins: 1 }
            Rectangle { anchors.fill: parent; anchors.margins: 14; radius: 6; color: StudioTheme.well }
            Rectangle {
                visible: root.motion !== null
                readonly property real size: root.motion ? (root.motion.fullscreen ? 1 : root.motion.size) : .3
                readonly property real aspect: root.motion && root.motion.shape === "landscape" ? 16 / 9 : root.motion && root.motion.shape === "portrait" ? 9 / 16 : root.motion && root.motion.shape === "source" ? Math.max(.01, root.cameraAspect) : 1
                width: root.motion && root.motion.fullscreen ? pad.width : Math.min(pad.width * size, pad.height * aspect)
                height: root.motion && root.motion.fullscreen ? pad.height : width / aspect
                x: root.motion && root.motion.fullscreen ? 0 : Math.max(0, Math.min(pad.width - width, (root.motion ? root.motion.x : .5) * pad.width))
                y: root.motion && root.motion.fullscreen ? (pad.height - height) / 2 : Math.max(0, Math.min(pad.height - height, (root.motion ? root.motion.y : .5) * pad.height))
                radius: root.motion && root.motion.shape === "circle" ? width / 2 : root.motion && root.motion.shape === "rectangle" ? 2 : 10
                color: "#5b6f9c"; border.color: "white"; border.width: 2
                StudioIcon { anchors.centerIn: parent; name: "camera"; width: 18; height: 18; color: "white" }
            }
            MouseArea {
                anchors.fill: parent
                enabled: root.motion !== null && !root.motion.fullscreen
                cursorShape: Qt.PointingHandCursor
                onReleased: mouse => root.patchMotion({ x: Math.max(0, Math.min(1, mouse.x / pad.width)), y: Math.max(0, Math.min(1, mouse.y / pad.height)) })
            }
        }
        StudioCheckBox { text: "全屏"; checked: root.motion ? root.motion.fullscreen === true : false; onToggled: root.patchMotion({ fullscreen: checked }) }
        StudioSliderRow { enabled: root.motion && !root.motion.fullscreen; label: "大小"; valueText: Math.round((root.motion ? root.motion.size : 0) * 100) + "%"; from: .08; to: 1; stepSize: .01; value: root.motion ? root.motion.size : .3; onValueMoved: v => root.patchMotion({ size: v }) }
        StudioComboBox {
            Layout.fillWidth: true
            model: root.shapeLabels
            currentIndex: root.motion ? Math.max(0, root.shapes.indexOf(root.motion.shape)) : 0
            onActivated: root.patchMotion({ shape: root.shapes[currentIndex] })
        }
        StudioSliderRow { label: "进入转场"; valueText: Number(root.motion ? root.motion.enter : 0).toFixed(2) + " 秒"; from: 0; to: 3; stepSize: .05; value: root.motion ? root.motion.enter : 0; onValueMoved: v => root.patchMotion({ enter: v }) }
        StudioSliderRow { label: "退出转场"; valueText: Number(root.motion ? root.motion.exit : 0).toFixed(2) + " 秒"; from: 0; to: 3; stepSize: .05; value: root.motion ? root.motion.exit : 0; onValueMoved: v => root.patchMotion({ exit: v }) }
        StudioNumberField { label: "素材开始"; suffix: "秒"; value: root.motion ? root.motion.start : 0; to: project.sourceDuration; onCommitted: v => root.patchMotion({ start: v }) }
        StudioNumberField { label: "素材结束"; suffix: "秒"; value: root.motion ? root.motion.end : 0; to: project.sourceDuration; onCommitted: v => root.patchMotion({ end: v }) }
        StudioButton { text: "删除此运动"; iconName: "trash"; danger: true; Layout.fillWidth: true; onClicked: effects.removeSelected("cameraMotionSegments") }
    }

    StudioDivider {}

    StudioSection {
        title: "分段同步"; icon: "arrowLeftRight"
        helper: "摄像头与屏幕逐渐错位时，在多处添加同步点，把摄像头时间对齐到屏幕时间；两组时间都必须递增。"
        Rectangle {
            Layout.fillWidth: true; implicitHeight: syncReadout.implicitHeight + 20; radius: StudioTheme.radiusControl; color: StudioTheme.well
            GridLayout {
                id: syncReadout
                anchors.fill: parent; anchors.margins: 10
                columns: 2; rowSpacing: 4
                Text { text: "播放头素材时间"; color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper; Layout.fillWidth: true }
                Text { text: project.sourceTime(root.positionSeconds).toFixed(3) + " s"; color: StudioTheme.text; font.family: StudioTheme.monoFamily; font.pixelSize: StudioTheme.helper }
                Text { text: "对应摄像头时间"; color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper; Layout.fillWidth: true }
                Text { text: root.cameraTime.toFixed(3) + " s"; color: StudioTheme.text; font.family: StudioTheme.monoFamily; font.pixelSize: StudioTheme.helper }
                Text { text: "摄像头速率"; color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper; Layout.fillWidth: true }
                Text { text: root.cameraRate.toFixed(4) + "×"; color: StudioTheme.text; font.family: StudioTheme.monoFamily; font.pixelSize: StudioTheme.helper }
            }
        }
        StudioButton {
            text: "在播放头添加同步点"; iconName: "plus"; Layout.fillWidth: true
            onClicked: {
                const id = effects.addSyncAtOutput(root.positionSeconds)
                if (id === "") return
                effects.select("syncPoints", id)
            }
        }
        EffectItemList {
            track: "syncPoints"
            items: effects.syncPoints
            iconName: "flag"
            describe: (item, index) => "同步点 " + (index + 1) + " · 屏幕 " + Number(item.sourceTime).toFixed(2) + "s → 摄像头 " + Number(item.cameraTime).toFixed(2) + "s"
            emptyText: "未设置同步点时使用全局同步偏移。"
        }
    }

    StudioSection {
        visible: root.sync !== null
        title: "所选同步点"; icon: "flag"
        StudioNumberField { label: "屏幕时间"; suffix: "秒"; decimals: 3; value: root.sync ? root.sync.sourceTime : 0; to: project.sourceDuration; onCommitted: v => effects.updateSelected("syncPoints", { sourceTime: v }) }
        StudioNumberField { label: "摄像头时间"; suffix: "秒"; decimals: 3; value: root.sync ? root.sync.cameraTime : 0; to: 1e6; onCommitted: v => effects.updateSelected("syncPoints", { cameraTime: v }) }
        RowLayout {
            Layout.fillWidth: true; spacing: 6
            Repeater {
                model: [[-0.1, "−0.1s"], [-1 / 30, "−1帧"], [1 / 30, "+1帧"], [0.1, "+0.1s"]]
                delegate: StudioButton {
                    required property var modelData
                    text: modelData[1]; compact: true; Layout.fillWidth: true
                    onClicked: effects.updateSelected("syncPoints", { cameraTime: Math.max(0, root.sync.cameraTime + modelData[0]) })
                }
            }
        }
        StudioButton { text: "删除此同步点"; iconName: "trash"; danger: true; Layout.fillWidth: true; onClicked: effects.removeSelected("syncPoints") }
    }
    Text { visible: effects.error !== ""; text: effects.error; color: StudioTheme.danger; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper; wrapMode: Text.Wrap; Layout.fillWidth: true }
}
