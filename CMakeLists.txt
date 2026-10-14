cmake_minimum_required(VERSION 3.24)
project(ScreenPilot VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "This development build currently supports Windows only.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 Multimedia Network)
qt_standard_project_setup(REQUIRES 6.8)

# Application code and the ScreenPilotApp QML module, shared by the product
# executable and the verification probe.
qt_add_library(screenpilot_core STATIC
    app/AppRuntime.cpp
    app/AppRuntime.h
    app/AppSettings.cpp
    app/AppSettings.h
    app/AudioMix.cpp
    app/AudioMix.h
    app/AudioPacketTimeline.h
    app/AudioTimeStretch.cpp
    app/AudioTimeStretch.h
    app/AutoFraming.cpp
    app/AutoFraming.h
    app/BackgroundVideo.cpp
    app/BackgroundVideo.h
    app/CameraController.cpp
    app/CaptureExclusion.cpp
    app/CaptureExclusion.h
    app/CameraController.h
    app/CameraMotion.cpp
    app/CameraMotion.h
    app/CameraOverlay.cpp
    app/CameraOverlay.h
    app/CanvasPreview.cpp
    app/CanvasPreview.h
    app/CanvasRenderer.cpp
    app/CanvasRenderer.h
    app/CropPreview.cpp
    app/CropPreview.h
    app/CursorEffects.cpp
    app/CursorEffects.h
    app/EffectController.cpp
    app/EffectController.h
    app/ExportController.cpp
    app/ExportController.h
    app/FrameConversion.cpp
    app/FrameConversion.h
    app/MemoController.cpp
    app/MemoController.h
    app/PcmWaveFile.cpp
    app/PcmWaveFile.h
    app/PreviewAudioProcessor.cpp
    app/PreviewAudioProcessor.h
    app/ProjectController.cpp
    app/ProjectController.h
    app/ProjectPackage.h
    app/RecentProjectsController.cpp
    app/RecentProjectsController.h
    app/RecordingController.cpp
    app/RecordingController.h
    app/RecordingProfile.h
    app/RecoveryController.cpp
    app/RecoveryController.h
    app/SceneEffects.cpp
    app/SceneEffects.h
    app/ScenePresetController.cpp
    app/ScenePresetController.h
    app/ScreenEffects.cpp
    app/ScreenEffects.h
    app/TimelineMedia.cpp
    app/TimelineMedia.h
    app/UpdateController.cpp
    app/UpdateController.h
    app/WindowsAudioCapture.cpp
    app/WindowsAudioCapture.h
)
target_include_directories(screenpilot_core PUBLIC app)
target_link_libraries(screenpilot_core PUBLIC Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia Qt6::Network user32 ole32 uuid mmdevapi)
target_compile_definitions(screenpilot_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
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
set_source_files_properties(app/RecordingBar.qml PROPERTIES QT_RESOURCE_ALIAS RecordingBar.qml)
qt_add_qml_module(screenpilot_core
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
        app/StudioNumberField.qml app/EffectItemList.qml app/EffectLane.qml app/RegionBox.qml app/RegionPicker.qml app/InspectorClip.qml app/InspectorAudio.qml app/InspectorScreen3D.qml app/InspectorFocus.qml app/InspectorStickers.qml app/CameraMotionControls.qml app/SettingsDialog.qml app/RecoveryPanel.qml app/RecordingBar.qml
)

# Icon and version resource for the product executable.
set(SCREENPILOT_ICON "${CMAKE_CURRENT_SOURCE_DIR}/app/resources/screenpilot.ico")
configure_file(app/resources/screenpilot.rc.in "${CMAKE_CURRENT_BINARY_DIR}/screenpilot.rc" @ONLY)
qt_add_executable(ScreenPilot WIN32 app/main.cpp "${CMAKE_CURRENT_BINARY_DIR}/screenpilot.rc")
target_link_libraries(ScreenPilot PRIVATE screenpilot_core screenpilot_coreplugin)

# Smoke tests, probes and benchmarks (--*-test, --*-benchmark). Not shipped.
qt_add_executable(ScreenPilotProbe WIN32
    tests/AudioCaptureContinuitySmokeTest.cpp
    tests/AudioCaptureContinuitySmokeTest.h
    tests/AudioSmokeTest.cpp
    tests/AudioSmokeTest.h
    tests/AudioTimeStretchSmokeTest.cpp
    tests/AudioTimeStretchSmokeTest.h
    tests/BackendBatchSmokeTest.cpp
    tests/BackendBatchSmokeTest.h
    tests/CameraSmokeTest.cpp
    tests/CameraSmokeTest.h
    tests/CropSmokeTest.cpp
    tests/CropSmokeTest.h
    tests/CursorSmokeTest.cpp
    tests/CursorSmokeTest.h
    tests/DynamicBackgroundSmokeTest.cpp
    tests/DynamicBackgroundSmokeTest.h
    tests/EditorShortcutSmokeTest.cpp
    tests/EditorShortcutSmokeTest.h
    tests/EffectControllerSmokeTest.h
    tests/EncoderBenchmark.cpp
    tests/EncoderBenchmark.h
    tests/ExportDeliverySmokeTest.cpp
    tests/ExportDeliverySmokeTest.h
    tests/ExportOptionsSmokeTest.cpp
    tests/ExportOptionsSmokeTest.h
    tests/FrameConversionSmokeTest.cpp
    tests/FrameConversionSmokeTest.h
    tests/ManualFramingSmokeTest.cpp
    tests/ManualFramingSmokeTest.h
    tests/MarkerSmokeTest.cpp
    tests/MarkerSmokeTest.h
    tests/MemoSmokeTest.cpp
    tests/MemoSmokeTest.h
    tests/NativeExportSmokeTest.cpp
    tests/NativeExportSmokeTest.h
    tests/PerformanceProbe.h
    tests/PresetSmokeTest.cpp
    tests/PresetSmokeTest.h
    tests/PreviewAudioSmokeTest.cpp
    tests/PreviewAudioSmokeTest.h
    tests/ProbeMain.cpp
    tests/ProjectFlowSmokeTest.cpp
    tests/ProjectFlowSmokeTest.h
    tests/RecordingBarSmokeTest.cpp
    tests/RecordingBarSmokeTest.h
    tests/RecentProjectsSmokeTest.cpp
    tests/RecentProjectsSmokeTest.h
    tests/RecentUiSmokeTest.cpp
    tests/RecentUiSmokeTest.h
    tests/RecordingControlSmokeTest.cpp
    tests/RecordingControlSmokeTest.h
    tests/RecordingProfileSmokeTest.cpp
    tests/RecordingProfileSmokeTest.h
    tests/RenderBenchmark.cpp
    tests/RenderBenchmark.h
    tests/ScenesUiSmokeTest.cpp
    tests/ScenesUiSmokeTest.h
    tests/ThirdEffectsSmokeTest.h
    tests/ThirdUiSmokeTest.h
    tests/TimelineInteractionSmokeTest.cpp
    tests/TimelineInteractionSmokeTest.h
    tests/TimelineSmokeTest.cpp
    tests/TimelineSmokeTest.h
    tests/VisualEffectsSmokeTest.cpp
    tests/VisualEffectsSmokeTest.h
    tests/VisualProjectSmokeTest.cpp
    tests/VisualProjectSmokeTest.h
    tests/VisualUiSmokeTest.cpp
    tests/VisualUiSmokeTest.h
)
target_link_libraries(ScreenPilotProbe PRIVATE screenpilot_core screenpilot_coreplugin)
