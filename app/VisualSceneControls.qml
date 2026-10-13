import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ColumnLayout {
    id: root
    signal previewIntroRequested()
    // "frame" shows the screen mockup group, "intro" the opening animation, "all" both.
    property string section: "all"
    Layout.fillWidth: true
    spacing: 22
    enabled: project.hasProject && !exporter.busy
    function setValue(key,value) { project.setCanvasOption(key,value) }
    StudioSection {
        visible: root.section !== "intro"
        title: "屏幕样机"; icon: "mockup"
        StudioComboBox {
            Layout.fillWidth: true
            model: ["无边框", "浅色窗口", "深色窗口", "浅色浏览器", "深色浏览器", "自适应设备"]
            property var values: ["none","windowLight","windowDark","browserLight","browserDark","device"]
            currentIndex: Math.max(0,values.indexOf(project.canvasSettings.screenFrame))
            onActivated: root.setValue("screenFrame",values[currentIndex])
        }
        StudioTextField {
            Layout.fillWidth: true
            visible: String(project.canvasSettings.screenFrame).startsWith("window")
            placeholderText: "窗口标题（可留空）"; maximumLength: 120
            text: project.canvasSettings.screenFrameTitle
            onEditingFinished: root.setValue("screenFrameTitle",text)
        }
        StudioTextField {
            Layout.fillWidth: true
            visible: String(project.canvasSettings.screenFrame).startsWith("browser")
            placeholderText: "网址或文字"; maximumLength: 240
            text: project.canvasSettings.screenFrameBrowserAddress
            onEditingFinished: root.setValue("screenFrameBrowserAddress",text)
        }
        StudioButton { text: "还原圆角与阴影"; iconName: "refresh"; Layout.fillWidth: true; onClicked: { root.setValue("radius",24); root.setValue("shadow",.28) } }
    }
    StudioSection {
        visible: root.section !== "frame"
        title: "开场动画"; icon: "intro"
        StudioCheckBox { text: "启用开场"; checked: project.canvasSettings.introEnabled; onToggled: root.setValue("introEnabled",checked) }
        StudioSegmented {
            Layout.fillWidth: true
            model: ["淡入","缩放淡入","上移淡入"]
            property var values: ["fade","scale","slide"]
            currentIndex: Math.max(0,values.indexOf(project.canvasSettings.introStyle))
            onActivated: index => root.setValue("introStyle",values[index])
        }
        StudioSliderRow { label: "总时长"; valueText: Number(project.canvasSettings.introDuration).toFixed(1) + " 秒"; from: .4; to: 8; stepSize: .1; value: project.canvasSettings.introDuration; onValueMoved: v => root.setValue("introDuration",v) }
        StudioSliderRow { label: "节奏"; valueText: Number(project.canvasSettings.introTempo).toFixed(1) + "×"; from: .5; to: 2; stepSize: .1; value: project.canvasSettings.introTempo; onValueMoved: v => root.setValue("introTempo",v) }
        Text { text: "入场顺序"; color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body; font.weight: Font.Medium }
        StudioComboBox {
            Layout.fillWidth: true; model: ["同时入场","屏幕先入场","相机先入场"]
            property var values: ["together","screenFirst","cameraFirst"]
            currentIndex: Math.max(0,values.indexOf(project.canvasSettings.introOrder))
            onActivated: root.setValue("introOrder",values[currentIndex])
        }
        StudioSliderRow { label: "元素间隔"; valueText: Number(project.canvasSettings.introElementGap).toFixed(2) + " 秒"; from: 0; to: 1.2; stepSize: .02; value: project.canvasSettings.introElementGap; onValueMoved: v => root.setValue("introElementGap",v) }
        StudioHelp { text: "每个元素至少保留 0.18 秒入场动作。" }
        StudioButton { text: "从片头预览"; iconName: "play"; Layout.fillWidth: true; onClicked: root.previewIntroRequested() }
    }
}
