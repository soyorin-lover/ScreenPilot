import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
// Screen 3D: the static projection lives in canvasSettings; animated poses are
// effect items on the "screenMotionSegments" track (source-time ranges).
ColumnLayout {
    id: root
    property real positionSeconds: 0
    readonly property string track: "screenMotionSegments"
    readonly property var canvas: project.canvasSettings
    readonly property var selected: { const it = effects.selectedItems[track]; return it && it.id ? it : null }
    property int poseSide: 0  // 0 edits `from`, 1 edits `to`
    readonly property var poseKeys: [
        { key: "screenRotationX", label: "俯仰", from: -80, to: 80, unit: "°", decimals: 0 },
        { key: "screenRotationY", label: "左右转", from: -80, to: 80, unit: "°", decimals: 0 },
        { key: "screenRotationZ", label: "旋转", from: -180, to: 180, unit: "°", decimals: 0 },
        { key: "screenScale", label: "缩放", from: .1, to: 3, unit: "×", decimals: 2 },
        { key: "screenX", label: "水平位移", from: -1, to: 1, unit: "", decimals: 2 },
        { key: "screenY", label: "垂直位移", from: -1, to: 1, unit: "", decimals: 2 },
        { key: "screenPerspective", label: "透视距离", from: 1, to: 10, unit: "", decimals: 1 }
    ]
    readonly property var presets: [
        { name: "平视", pose: { screenRotationX: 0, screenRotationY: 0, screenRotationZ: 0, screenScale: 1, screenX: 0, screenY: 0, screenPerspective: 3 } },
        { name: "左侧透视", pose: { screenRotationX: 4, screenRotationY: 20, screenRotationZ: 0, screenScale: .9, screenX: .03, screenY: 0, screenPerspective: 3 } },
        { name: "右侧透视", pose: { screenRotationX: 4, screenRotationY: -20, screenRotationZ: 0, screenScale: .9, screenX: -.03, screenY: 0, screenPerspective: 3 } },
        { name: "俯视桌面", pose: { screenRotationX: 26, screenRotationY: 0, screenRotationZ: 0, screenScale: .9, screenX: 0, screenY: .04, screenPerspective: 3.5 } },
        { name: "悬浮倾斜", pose: { screenRotationX: 14, screenRotationY: -16, screenRotationZ: 4, screenScale: .86, screenX: 0, screenY: 0, screenPerspective: 3 } }
    ]
    function poseOf(source) { const pose = {}; for (const k of poseKeys) pose[k.key] = Number(source[k.key] !== undefined ? source[k.key] : 0); return pose }
    function applyStatic(pose) { for (const k in pose) project.setCanvasOption(k, pose[k]) }
    function patchPose(key, value) {
        if (!selected) return
        const patch = {}; patch[poseSide === 0 ? "from" : "to"] = {}; patch[poseSide === 0 ? "from" : "to"][key] = value
        effects.updateSelected(track, patch)
    }
    readonly property var editedPose: selected ? (poseSide === 0 ? selected.from : selected.to) || {} : canvas
    Layout.fillWidth: true
    spacing: 22

    // Pose preview: a tilted card mirrors the pose being edited.
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 150
        radius: StudioTheme.radiusGroup
        color: StudioTheme.surfaceAlt
        border.color: StudioTheme.hairline
        clip: true
        StudioDotGrid { anchors.fill: parent; anchors.margins: 1 }
        Item {
            id: stageCard
            readonly property var pose: root.poseOf(root.editedPose)
            width: parent.width * .52; height: width * 9 / 16
            x: (parent.width - width) / 2 + pose.screenX * parent.width * .25
            y: (parent.height - height) / 2 + pose.screenY * parent.height * .25
            scale: Math.max(.2, Math.min(1.6, pose.screenScale))
            Rectangle { anchors.fill: parent; anchors.topMargin: 6; anchors.bottomMargin: -6; radius: 8; color: "#22000000" }
            Rectangle {
                anchors.fill: parent; radius: 8
                gradient: Gradient { GradientStop { position: 0; color: "#7f9cd6" } GradientStop { position: 1; color: "#3c4f86" } }
                Rectangle { x: 8; y: 7; width: 5; height: 5; radius: 2.5; color: "#ff6058" }
                Rectangle { x: 16; y: 7; width: 5; height: 5; radius: 2.5; color: "#ffbd2e" }
                Rectangle { x: 24; y: 7; width: 5; height: 5; radius: 2.5; color: "#28c840" }
                Rectangle { x: 10; y: 20; width: parent.width * .55; height: 5; radius: 2.5; color: "#66ffffff" }
                Rectangle { x: 10; y: 30; width: parent.width * .35; height: 5; radius: 2.5; color: "#44ffffff" }
            }
            transform: [
                Rotation { origin.x: stageCard.width / 2; origin.y: stageCard.height / 2; axis { x: 1; y: 0; z: 0 } angle: -stageCard.pose.screenRotationX },
                Rotation { origin.x: stageCard.width / 2; origin.y: stageCard.height / 2; axis { x: 0; y: 1; z: 0 } angle: stageCard.pose.screenRotationY },
                Rotation { origin.x: stageCard.width / 2; origin.y: stageCard.height / 2; axis { x: 0; y: 0; z: 1 } angle: stageCard.pose.screenRotationZ }
            ]
        }
        Rectangle {
            anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 8
            height: 22; radius: 11; width: tag.implicitWidth + 16; color: StudioTheme.surface; border.color: StudioTheme.hairline
            Text { id: tag; anchors.centerIn: parent; text: root.selected ? (root.poseSide === 0 ? "运动片段 · 起点姿态" : "运动片段 · 终点姿态") : "静态姿态"; color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.caption; font.weight: Font.Medium }
        }
    }

    StudioSection {
        title: "静态 3D 姿态"; icon: "cube"
        helper: "整段成片使用的屏幕投影；屏幕、外框、阴影和光标一起投影，背景与摄像头保持平面。"
        Flow {
            Layout.fillWidth: true; spacing: 6
            Repeater {
                model: root.presets
                delegate: StudioButton {
                    required property var modelData
                    text: modelData.name; compact: true
                    onClicked: root.applyStatic(modelData.pose)
                }
            }
        }
        Repeater {
            model: root.poseKeys
            delegate: StudioSliderRow {
                required property var modelData
                label: modelData.label
                valueText: Number(root.canvas[modelData.key] || 0).toFixed(modelData.decimals) + modelData.unit
                from: modelData.from; to: modelData.to
                stepSize: modelData.decimals === 0 ? 1 : Math.pow(10, -modelData.decimals)
                value: Number(root.canvas[modelData.key] || 0)
                onValueMoved: v => project.setCanvasOption(modelData.key, v)
            }
        }
        StudioButton { text: "恢复平视"; iconName: "refresh"; Layout.fillWidth: true; onClicked: root.applyStatic(root.presets[0].pose) }
    }

    StudioDivider {}

    StudioSection {
        title: "3D 运动片段"; icon: "motion"
        helper: "在一段时间内从起点姿态平滑过渡到终点姿态。片段按素材时间保存，剪辑和变速后自动跟随。"
        StudioButton {
            text: "在播放头添加 3D 运动"; iconName: "plus"; primary: true; Layout.fillWidth: true
            enabled: project.hasProject
            onClicked: { const id = effects.addAtOutput(root.track, root.positionSeconds, 2); if (id !== "") effects.select(root.track, id) }
        }
        EffectItemList {
            track: root.track
            items: effects.screenMotionSegments
            iconName: "cube"
            describe: (item, index) => "3D 运动 " + (index + 1) + " · " + Number(item.sourceDuration).toFixed(1) + " 秒"
        }
    }

    StudioSection {
        visible: root.selected !== null
        title: "所选运动"; icon: "sliders"
        StudioSegmented {
            Layout.fillWidth: true
            model: ["起点姿态", "终点姿态"]
            currentIndex: root.poseSide
            onActivated: index => root.poseSide = index
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            StudioButton { text: "取用静态姿态"; compact: true; Layout.fillWidth: true; onClicked: { const p = {}; p[root.poseSide === 0 ? "from" : "to"] = root.poseOf(root.canvas); effects.updateSelected(root.track, p) } }
            StudioButton { text: "起终互换"; iconName: "arrowLeftRight"; compact: true; Layout.fillWidth: true; onClicked: effects.updateSelected(root.track, { from: root.selected.to, to: root.selected.from }) }
        }
        Repeater {
            model: root.poseKeys
            delegate: StudioSliderRow {
                required property var modelData
                readonly property real current: Number((root.editedPose || {})[modelData.key] || 0)
                label: modelData.label
                valueText: current.toFixed(modelData.decimals) + modelData.unit
                from: modelData.from; to: modelData.to
                stepSize: modelData.decimals === 0 ? 1 : Math.pow(10, -modelData.decimals)
                value: current
                onValueMoved: v => root.patchPose(modelData.key, v)
            }
        }
        StudioNumberField { label: "素材开始"; suffix: "秒"; value: root.selected ? root.selected.sourceStart : 0; to: project.sourceDuration; onCommitted: v => effects.updateSelected(root.track, { sourceStart: v }) }
        StudioNumberField { label: "持续时长"; suffix: "秒"; value: root.selected ? root.selected.sourceDuration : 0; from: .05; to: project.sourceDuration; onCommitted: v => effects.updateSelected(root.track, { sourceDuration: v }) }
        StudioButton { text: "删除此运动"; iconName: "trash"; danger: true; Layout.fillWidth: true; onClicked: effects.removeSelected(root.track) }
    }
    Text { visible: effects.error !== ""; text: effects.error; color: StudioTheme.danger; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper; wrapMode: Text.Wrap; Layout.fillWidth: true }
}
