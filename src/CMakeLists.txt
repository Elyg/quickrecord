# Pure logic (no widgets, no X11) — unit tested.
add_library(quickrecord_core STATIC
    core/Annotation.cpp
    core/FfmpegArgs.cpp
    core/Geometry.cpp
    core/HistoryStore.cpp
    core/InputLog.cpp
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
    app/RegionOverlay.cpp
    app/RecordFrame.cpp
    app/RecordBar.cpp
    app/Recorder.cpp
    app/Exporter.cpp
    app/Tools.cpp
    app/Clipboard.cpp
    app/Thumbnailer.cpp
    app/EditorWindow.cpp
    app/FlowLayout.cpp
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
    spdlog::spdlog
)

if(APPLE)
    target_sources(quickrecord PRIVATE
        # for AUTOMOC: it only pairs headers with same-named sources
        app/GlobalHotkey.h
        app/InputMonitor.h
        app/GlobalHotkeyMac.cpp
        app/InputMonitorMac.mm
        app/PlatformMac.mm
    )
    target_link_libraries(quickrecord PRIVATE "-framework AppKit" "-framework Carbon" "-framework CoreGraphics")
    # QuickRecord.app: a menu bar app (LSUIElement) that may record the microphone.
    set_target_properties(quickrecord PROPERTIES
        OUTPUT_NAME QuickRecord
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/scripts/Info.plist.in
        MACOSX_BUNDLE_GUI_IDENTIFIER com.etitas.quickrecord
        MACOSX_BUNDLE_BUNDLE_NAME QuickRecord
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        INSTALL_RPATH "@executable_path/../Frameworks"
    )
else()
    target_sources(quickrecord PRIVATE
        app/GlobalHotkey.cpp
        app/InputMonitorX11.cpp
        app/PlatformX11.cpp
        app/X11.cpp
    )
    # The X11 bits use the public QNativeInterface API; no private Qt headers needed.
    target_link_libraries(quickrecord PRIVATE PkgConfig::XCB)
    set_target_properties(quickrecord PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
endif()

file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/exe_path.txt CONTENT "$<TARGET_FILE:quickrecord>")

add_executable(QuickRecordTests
    tests/AnnotationTest.cpp
    tests/FfmpegArgsTest.cpp
    tests/GeometryTest.cpp
    tests/HistoryStoreTest.cpp
    tests/InputLogTest.cpp
    tests/HotkeyTest.cpp
    tests/TextOverlayTest.cpp
)
target_link_libraries(QuickRecordTests PRIVATE quickrecord_core GTest::gtest_main)
