cmake_minimum_required(VERSION 3.24)
project(ScreenPilot VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "This development build currently supports Windows only.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 Multimedia)
qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(ScreenPilot WIN32
    app/main.cpp
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
qt_add_qml_module(ScreenPilot
    URI ScreenPilotApp
    QML_FILES app/Main.qml app/Editor.qml
)
target_link_libraries(ScreenPilot PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia user32)
target_compile_definitions(ScreenPilot PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
