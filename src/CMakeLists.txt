# Pure logic (no widgets, no X11) — unit tested.
add_library(quickrecord_core STATIC
    core/Annotation.cpp
    core/FfmpegArgs.cpp
    core/Geometry.cpp
    core/HistoryStore.cpp
    core/Hotkey.cpp
    core/TextOverlay.cpp
)
target_include_directories(quickrecord_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(quickrecord_core PUBLIC Qt6::Core Qt6::Gui)

add_executable(quickrecord
    app/main.cpp
    app/App.cpp
    app/Tray.cpp
    app/Settings.cpp
    app/SettingsDialog.cpp
    app/GlobalHotkey.cpp
    app/X11.cpp
    app/RegionOverlay.cpp
    app/RecordFrame.cpp
    app/RecordBar.cpp
    app/Recorder.cpp
    app/Exporter.cpp
    app/Tools.cpp
    app/Clipboard.cpp
    app/Thumbnailer.cpp
    app/EditorWindow.cpp
    app/CaptionBar.cpp
    app/ScreenshotEditor.cpp
    app/TrimSlider.cpp
    app/HistoryWindow.cpp
    app/PreviewPane.cpp
    app/VideoView.cpp
    app/Icons.cpp
    app/Theme.cpp
)
target_include_directories(quickrecord PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(quickrecord PRIVATE
    quickrecord_core
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
    PkgConfig::XCB
    spdlog::spdlog
)
# The X11 bits use the public QNativeInterface API; no private Qt headers needed.

file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/exe_path.txt CONTENT "$<TARGET_FILE:quickrecord>")

add_executable(QuickRecordTests
    tests/AnnotationTest.cpp
    tests/FfmpegArgsTest.cpp
    tests/GeometryTest.cpp
    tests/HistoryStoreTest.cpp
    tests/HotkeyTest.cpp
    tests/TextOverlayTest.cpp
)
target_link_libraries(QuickRecordTests PRIVATE quickrecord_core GTest::gtest_main)
