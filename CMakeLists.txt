cmake_minimum_required(VERSION 3.24)
project(ScreenPilot VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "This development build currently supports Windows only.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 Multimedia Network)
qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(ScreenPilot WIN32
    app/main.cpp
    app/FrameConversion.cpp
    app/FrameConversion.h
    app/FrameConversionSmokeTest.cpp
    app/FrameConversionSmokeTest.h
    app/AppSettings.cpp
    app/AppSettings.h
    app/UpdateController.cpp
    app/UpdateController.h
    app/EffectController.cpp
    app/EffectController.h
    app/BackendBatchSmokeTest.cpp
    app/BackendBatchSmokeTest.h
    app/CameraMotion.cpp
    app/CameraMotion.h
    app/ScreenEffects.cpp
    app/ScreenEffects.h
    app/PcmWaveFile.cpp
    app/PcmWaveFile.h
    app/AudioMix.cpp
    app/AudioMix.h
    app/RecoveryController.cpp
    app/RecoveryController.h
    app/RecordingProfile.h
    app/RecordingProfileSmokeTest.cpp
    app/RecordingProfileSmokeTest.h
    app/NativeExportSmokeTest.cpp
    app/NativeExportSmokeTest.h
    app/AudioCaptureContinuitySmokeTest.cpp
    app/AudioCaptureContinuitySmokeTest.h
    app/RecentProjectsController.h
    app/RecentProjectsController.cpp
    app/RecentProjectsSmokeTest.h
    app/RecentProjectsSmokeTest.cpp
    app/RecentUiSmokeTest.h
    app/RecentUiSmokeTest.cpp
    app/TimelineInteractionSmokeTest.h
    app/TimelineInteractionSmokeTest.cpp
    app/EditorShortcutSmokeTest.h
    app/EditorShortcutSmokeTest.cpp
    app/SceneEffects.h
    app/SceneEffects.cpp
    app/BackgroundVideo.h
    app/BackgroundVideo.cpp
    app/DynamicBackgroundSmokeTest.h
    app/DynamicBackgroundSmokeTest.cpp
    app/VisualEffectsSmokeTest.h
    app/VisualEffectsSmokeTest.cpp
    app/VisualProjectSmokeTest.h
    app/VisualProjectSmokeTest.cpp
    app/VisualUiSmokeTest.h
    app/VisualUiSmokeTest.cpp
    app/ScenesUiSmokeTest.h
    app/ScenesUiSmokeTest.cpp
    app/CropPreview.h
    app/CropPreview.cpp
    app/CropSmokeTest.h
    app/CropSmokeTest.cpp
    app/ManualFramingSmokeTest.h
    app/ManualFramingSmokeTest.cpp
    app/ScenePresetController.h
    app/ScenePresetController.cpp
    app/PresetSmokeTest.h
    app/PresetSmokeTest.cpp
    app/MemoController.h
    app/MemoController.cpp
    app/MemoSmokeTest.h
    app/MemoSmokeTest.cpp
    app/MarkerSmokeTest.h
    app/MarkerSmokeTest.cpp
    app/ExportDeliverySmokeTest.h
    app/ExportDeliverySmokeTest.cpp
    app/CursorEffects.h
    app/CursorEffects.cpp
    app/CursorSmokeTest.h
    app/CursorSmokeTest.cpp
    app/RecordingControlSmokeTest.h
    app/RecordingControlSmokeTest.cpp
    app/PreviewAudioProcessor.h
    app/PreviewAudioProcessor.cpp
    app/PreviewAudioSmokeTest.h
    app/PreviewAudioSmokeTest.cpp
    app/AudioTimeStretch.h
    app/AudioTimeStretch.cpp
    app/AudioTimeStretchSmokeTest.h
    app/AudioTimeStretchSmokeTest.cpp
    app/CameraController.h
    app/CameraController.cpp
    app/CameraOverlay.h
    app/CameraOverlay.cpp
    app/CameraSmokeTest.h
    app/CameraSmokeTest.cpp
    app/ExportOptionsSmokeTest.h
    app/ExportOptionsSmokeTest.cpp
    app/ProjectFlowSmokeTest.h
    app/ProjectFlowSmokeTest.cpp
    app/TimelineMedia.h
    app/TimelineMedia.cpp
    app/TimelineSmokeTest.h
    app/TimelineSmokeTest.cpp
    app/AudioSmokeTest.h
    app/AudioSmokeTest.cpp
    app/AudioPacketTimeline.h
    app/WindowsAudioCapture.h
    app/WindowsAudioCapture.cpp
    app/RecordingController.h
    app/RecordingController.cpp
    app/ProjectController.h
    app/ProjectController.cpp
    app/ExportController.h
    app/ExportController.cpp
    app/AutoFraming.h
    app/AutoFraming.cpp
    app/CanvasRenderer.h
    app/CanvasRenderer.cpp
    app/CanvasPreview.h
    app/CanvasPreview.cpp
)
set_source_files_properties(app/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
set_source_files_properties(app/Editor.qml PROPERTIES QT_RESOURCE_ALIAS Editor.qml)
set_source_files_properties(app/TimelineStrip.qml PROPERTIES QT_RESOURCE_ALIAS TimelineStrip.qml)
set_source_files_properties(app/MemoWindow.qml PROPERTIES QT_RESOURCE_ALIAS MemoWindow.qml)
set_source_files_properties(app/CropEditor.qml PROPERTIES QT_RESOURCE_ALIAS CropEditor.qml)
set_source_files_properties(app/ManualFramingControls.qml PROPERTIES QT_RESOURCE_ALIAS ManualFramingControls.qml)
set_source_files_properties(app/ManualZoomTrack.qml PROPERTIES QT_RESOURCE_ALIAS ManualZoomTrack.qml)
set_source_files_properties(app/ScenePresetPanel.qml PROPERTIES QT_RESOURCE_ALIAS ScenePresetPanel.qml)
set_source_files_properties(app/BackgroundControls.qml PROPERTIES QT_RESOURCE_ALIAS BackgroundControls.qml)
set_source_files_properties(app/VisualSceneControls.qml PROPERTIES QT_RESOURCE_ALIAS VisualSceneControls.qml)
set_source_files_properties(app/RecentProjectsPanel.qml PROPERTIES QT_RESOURCE_ALIAS RecentProjectsPanel.qml)
set_source_files_properties(app/EditorShortcuts.qml PROPERTIES QT_RESOURCE_ALIAS EditorShortcuts.qml)
set_source_files_properties(app/ShortcutHelp.qml PROPERTIES QT_RESOURCE_ALIAS ShortcutHelp.qml)
set_source_files_properties(app/StudioTheme.qml PROPERTIES QT_RESOURCE_ALIAS StudioTheme.qml QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(app/StudioIcon.qml PROPERTIES QT_RESOURCE_ALIAS StudioIcon.qml)
set_source_files_properties(app/StudioButton.qml PROPERTIES QT_RESOURCE_ALIAS StudioButton.qml)
set_source_files_properties(app/StudioComboBox.qml PROPERTIES QT_RESOURCE_ALIAS StudioComboBox.qml)
set_source_files_properties(app/StudioSlider.qml PROPERTIES QT_RESOURCE_ALIAS StudioSlider.qml)
set_source_files_properties(app/StudioCheckBox.qml PROPERTIES QT_RESOURCE_ALIAS StudioCheckBox.qml)
set_source_files_properties(app/StudioTextField.qml PROPERTIES QT_RESOURCE_ALIAS StudioTextField.qml)
set_source_files_properties(app/StudioCard.qml PROPERTIES QT_RESOURCE_ALIAS StudioCard.qml)
set_source_files_properties(app/StudioSegmented.qml PROPERTIES QT_RESOURCE_ALIAS StudioSegmented.qml)
set_source_files_properties(app/StudioSliderRow.qml PROPERTIES QT_RESOURCE_ALIAS StudioSliderRow.qml)
set_source_files_properties(app/StudioSection.qml PROPERTIES QT_RESOURCE_ALIAS StudioSection.qml)
set_source_files_properties(app/StudioHelp.qml PROPERTIES QT_RESOURCE_ALIAS StudioHelp.qml)
set_source_files_properties(app/StudioDivider.qml PROPERTIES QT_RESOURCE_ALIAS StudioDivider.qml)
set_source_files_properties(app/StudioDialog.qml PROPERTIES QT_RESOURCE_ALIAS StudioDialog.qml)
set_source_files_properties(app/StudioDialogFooter.qml PROPERTIES QT_RESOURCE_ALIAS StudioDialogFooter.qml)
set_source_files_properties(app/StudioPopover.qml PROPERTIES QT_RESOURCE_ALIAS StudioPopover.qml)
set_source_files_properties(app/StudioMenu.qml PROPERTIES QT_RESOURCE_ALIAS StudioMenu.qml)
set_source_files_properties(app/StudioMenuItem.qml PROPERTIES QT_RESOURCE_ALIAS StudioMenuItem.qml)
set_source_files_properties(app/StudioDotGrid.qml PROPERTIES QT_RESOURCE_ALIAS StudioDotGrid.qml)
set_source_files_properties(app/StudioNumberField.qml PROPERTIES QT_RESOURCE_ALIAS StudioNumberField.qml)
set_source_files_properties(app/EffectItemList.qml PROPERTIES QT_RESOURCE_ALIAS EffectItemList.qml)
set_source_files_properties(app/EffectLane.qml PROPERTIES QT_RESOURCE_ALIAS EffectLane.qml)
set_source_files_properties(app/RegionBox.qml PROPERTIES QT_RESOURCE_ALIAS RegionBox.qml)
set_source_files_properties(app/RegionPicker.qml PROPERTIES QT_RESOURCE_ALIAS RegionPicker.qml)
set_source_files_properties(app/InspectorClip.qml PROPERTIES QT_RESOURCE_ALIAS InspectorClip.qml)
set_source_files_properties(app/InspectorAudio.qml PROPERTIES QT_RESOURCE_ALIAS InspectorAudio.qml)
set_source_files_properties(app/InspectorScreen3D.qml PROPERTIES QT_RESOURCE_ALIAS InspectorScreen3D.qml)
set_source_files_properties(app/InspectorFocus.qml PROPERTIES QT_RESOURCE_ALIAS InspectorFocus.qml)
set_source_files_properties(app/InspectorStickers.qml PROPERTIES QT_RESOURCE_ALIAS InspectorStickers.qml)
set_source_files_properties(app/CameraMotionControls.qml PROPERTIES QT_RESOURCE_ALIAS CameraMotionControls.qml)
set_source_files_properties(app/SettingsDialog.qml PROPERTIES QT_RESOURCE_ALIAS SettingsDialog.qml)
set_source_files_properties(app/RecoveryPanel.qml PROPERTIES QT_RESOURCE_ALIAS RecoveryPanel.qml)
qt_add_qml_module(ScreenPilot
    URI ScreenPilotApp
    QML_FILES app/Main.qml app/Editor.qml app/TimelineStrip.qml app/MemoWindow.qml
        app/CropEditor.qml app/ManualFramingControls.qml app/ManualZoomTrack.qml app/ScenePresetPanel.qml
        app/BackgroundControls.qml app/VisualSceneControls.qml
        app/RecentProjectsPanel.qml app/EditorShortcuts.qml app/ShortcutHelp.qml
        app/StudioTheme.qml app/StudioIcon.qml app/StudioButton.qml app/StudioComboBox.qml
        app/StudioSlider.qml app/StudioCheckBox.qml app/StudioTextField.qml
        app/StudioCard.qml app/StudioSegmented.qml app/StudioSliderRow.qml app/StudioSection.qml
        app/StudioHelp.qml app/StudioDivider.qml app/StudioDialog.qml app/StudioDialogFooter.qml
        app/StudioPopover.qml app/StudioMenu.qml app/StudioMenuItem.qml app/StudioDotGrid.qml
        app/StudioNumberField.qml app/EffectItemList.qml app/EffectLane.qml app/RegionBox.qml app/RegionPicker.qml app/InspectorClip.qml app/InspectorAudio.qml app/InspectorScreen3D.qml app/InspectorFocus.qml app/InspectorStickers.qml app/CameraMotionControls.qml app/SettingsDialog.qml app/RecoveryPanel.qml
)
target_link_libraries(ScreenPilot PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia Qt6::Network user32 ole32 uuid mmdevapi)
target_compile_definitions(ScreenPilot PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
