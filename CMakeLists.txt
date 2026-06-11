cmake_minimum_required(VERSION 3.21)

project(GameSaveCloudQt
    VERSION 0.3.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Qml QuickDialogs2 Network Widgets)

qt_add_executable(GameSaveCloudQt
    assets/app.rc
    assets/resources.qrc
    src/main.cpp
    src/cache/GameMetadataCache.h
    src/cache/GameMetadataCache.cpp
    src/logging/AppLogger.h
    src/logging/AppLogger.cpp
    src/manual/GameIdentityResolver.h
    src/manual/GameIdentityResolver.cpp
    src/manual/ManualGameManager.h
    src/manual/ManualGameManager.cpp
    src/manual/PCGamingWikiGameSearchClient.h
    src/manual/PCGamingWikiGameSearchClient.cpp
    src/models/GameInfo.h
    src/models/GameListModel.h
    src/models/GameListModel.cpp
    src/models/LogListModel.h
    src/models/LogListModel.cpp
    src/process/GameProcessMonitor.h
    src/process/GameProcessMonitor.cpp
    src/saves/PCGamingWikiSaveProvider.h
    src/saves/PCGamingWikiSaveProvider.cpp
    src/saves/SavePathResolver.h
    src/saves/SavePathResolver.cpp
    src/saves/SavePathValidator.h
    src/saves/SavePathValidator.cpp
    src/saves/SteamAutoCloudSaveProvider.h
    src/saves/SteamAutoCloudSaveProvider.cpp
    src/saves/SteamCloudDetector.h
    src/saves/SteamCloudDetector.cpp
    src/snapshots/SaveFileScanner.h
    src/snapshots/SaveFileScanner.cpp
    src/snapshots/SnapshotManifestStore.h
    src/snapshots/SnapshotManifestStore.cpp
    src/snapshots/SnapshotPackageWriter.h
    src/snapshots/SnapshotPackageWriter.cpp
    src/snapshots/SnapshotPreprocessor.h
    src/snapshots/SnapshotPreprocessor.cpp
    src/snapshots/SnapshotRestoreManager.h
    src/snapshots/SnapshotRestoreManager.cpp
    src/snapshots/SnapshotSettings.h
    src/snapshots/SnapshotSettings.cpp
    src/storage/AppSettings.h
    src/storage/AppSettings.cpp
    src/storage/SettingsManager.h
    src/storage/SettingsManager.cpp
    src/sync/WebDavSettings.h
    src/sync/WebDavSettings.cpp
    src/sync/ConfigCloudSync.h
    src/sync/ConfigCloudSync.cpp
    src/system/StartupManager.h
    src/system/StartupManager.cpp
    src/sync/WebDavClient.h
    src/sync/WebDavClient.cpp
    src/sync/QuarkGatewayManager.h
    src/sync/QuarkGatewayManager.cpp
    src/tray/TrayController.h
    src/tray/TrayController.cpp
    src/steam/SteamMetadataClient.h
    src/steam/SteamMetadataClient.cpp
    src/SteamManager.h
    src/SteamManager.cpp
)

qt_add_qml_module(GameSaveCloudQt
    URI GameSaveCloud
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/components/ActionButton.qml
        qml/components/AppTitleBar.qml
        qml/components/AutoSyncSettingsPage.qml
        qml/components/CloudSettingsPage.qml
        qml/components/ConfirmDialog.qml
        qml/components/DeleteLocalSnapshotsDialog.qml
        qml/components/EmptyState.qml
        qml/components/FramelessResizeHandles.qml
        qml/components/LogPage.qml
        qml/components/ModernScrollBar.qml
        qml/components/SnapshotList.qml
        qml/components/ThemeToggle.qml
)

target_include_directories(GameSaveCloudQt
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(GameSaveCloudQt
    PRIVATE
        Qt6::Quick
        Qt6::Qml
        Qt6::QuickDialogs2
        Qt6::Network
        Qt6::Widgets
)

target_compile_options(GameSaveCloudQt
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

set(OPENLIST_ENGINE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/engines/openlist")
if(EXISTS "${OPENLIST_ENGINE_SOURCE_DIR}/openlist.exe")
    add_custom_command(TARGET GameSaveCloudQt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:GameSaveCloudQt>/engines/openlist"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${OPENLIST_ENGINE_SOURCE_DIR}"
            "$<TARGET_FILE_DIR:GameSaveCloudQt>/engines/openlist"
        COMMENT "Copying OpenList engine beside GameSaveCloudQt"
        VERBATIM
    )
else()
    message(WARNING "OpenList engine not found at ${OPENLIST_ENGINE_SOURCE_DIR}/openlist.exe")
endif()

if(WIN32)
    target_link_libraries(GameSaveCloudQt
        PRIVATE
            Ole32
            Shell32
            Uuid
    )

    target_compile_definitions(GameSaveCloudQt
        PRIVATE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
    )

    set_target_properties(GameSaveCloudQt PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()
