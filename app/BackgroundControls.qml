import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
ColumnLayout {
    objectName: "backgroundControls"
    Layout.fillWidth: true
    spacing: 8
    RowLayout {
        Layout.fillWidth: true; spacing: 8
        StudioButton { objectName: "chooseBackgroundImage"; text: qsTr("选择图片"); iconName: "image"; Layout.fillWidth: true; enabled: project.hasProject; onClicked: imageDialog.open() }
        StudioButton { objectName: "clearBackgroundImage"; iconName: "close"; tip: qsTr("清除图片"); enabled: project.canvasSettings.backgroundImage !== ""; onClicked: project.clearBackgroundImage() }
    }
    RowLayout {
        Layout.fillWidth: true; spacing: 8
        StudioButton { objectName: "chooseBackgroundVideo"; text: qsTr("选择动态背景"); iconName: "film"; Layout.fillWidth: true; enabled: project.hasProject; onClicked: videoDialog.open() }
        StudioButton { objectName: "clearBackgroundVideo"; iconName: "close"; tip: qsTr("清除视频"); enabled: project.canvasSettings.backgroundVideo !== ""; onClicked: project.clearBackgroundVideo() }
    }
    StudioHelp { text: qsTr("动态背景循环播放且静音；图片和视频只能选择一种。") }
    FileDialog { id: imageDialog; title: qsTr("选择背景图片"); nameFilters: [qsTr("图片 (*.png *.jpg *.jpeg *.webp *.bmp)")]; onAccepted: project.setBackgroundImage(selectedFile) }
    FileDialog { id: videoDialog; title: qsTr("选择动态背景视频"); nameFilters: [qsTr("视频 (*.mp4 *.mov)")]; onAccepted: project.setBackgroundVideo(selectedFile) }
}
