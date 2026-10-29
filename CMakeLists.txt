cmake_minimum_required(VERSION 3.21)
project(WordLens VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Sql)

# ---------------------------------------------------------------------------
# Core library: screen-word pipeline pieces that do not depend on widgets
# ---------------------------------------------------------------------------
set(CORE_SOURCES
    src/Dictionary.cpp
    src/Settings.cpp
    src/TesseractOcr.cpp
    src/WordFilter.cpp
    src/WordSession.cpp
    src/WindowUtils.cpp
    src/WordStore.cpp
)

add_library(wordlens_core STATIC ${CORE_SOURCES})
target_link_libraries(wordlens_core PUBLIC Qt6::Gui Qt6::Sql dwmapi)
target_include_directories(wordlens_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(wordlens_core PUBLIC
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    WORLDLENS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_options(wordlens_core PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra>)

# ---------------------------------------------------------------------------
# GUI application
# ---------------------------------------------------------------------------
set(GUI_SOURCES
    src/main.cpp
    src/AppController.cpp
    src/DefinitionCard.cpp
    src/OverlayBar.cpp
    src/WordChip.cpp
    src/DetailsPanel.cpp
    src/SettingsDialog.cpp
    src/HotkeyFilter.cpp
    src/PetWidget.cpp
    src/RegionSelector.cpp
)

qt_add_executable(WordLens WIN32 ${GUI_SOURCES})
target_link_libraries(WordLens PRIVATE Qt6::Widgets wordlens_core)
target_compile_options(WordLens PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra>)

# Keep the build output runnable when it is opened directly from Explorer. The
# app links against Qt and the MinGW C++ runtime dynamically, so merely
# building WordLens.exe is not enough on a machine without those directories in
# PATH.
if(WIN32)
    get_filename_component(QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
    find_program(WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt.exe
        HINTS "${QT_PREFIX}/bin"
        REQUIRED)
    get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)

    add_custom_command(TARGET WordLens POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations
                "$<TARGET_FILE:WordLens>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${MINGW_BIN_DIR}/libgcc_s_seh-1.dll"
                "$<TARGET_FILE_DIR:WordLens>/libgcc_s_seh-1.dll"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${MINGW_BIN_DIR}/libstdc++-6.dll"
                "$<TARGET_FILE_DIR:WordLens>/libstdc++-6.dll"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${MINGW_BIN_DIR}/libwinpthread-1.dll"
                "$<TARGET_FILE_DIR:WordLens>/libwinpthread-1.dll"
        # windeployqt only deploys the platform plugin the GUI app needs. The
        # console tools (wlocr) run with QT_QPA_PLATFORM=offscreen, so ship that
        # plugin too - otherwise they abort before printing anything.
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${QT_PREFIX}/plugins/platforms/qoffscreen.dll"
                "$<TARGET_FILE_DIR:WordLens>/platforms/qoffscreen.dll"
        VERBATIM
        COMMENT "Deploying Qt and MinGW runtime DLLs")
endif()

# ---------------------------------------------------------------------------
# Console tool used for testing the OCR pipeline
# ---------------------------------------------------------------------------
add_executable(wlocr src/main_cli.cpp)
target_link_libraries(wlocr PRIVATE Qt6::Gui wordlens_core)

# ---------------------------------------------------------------------------
# Offscreen renderer for the hover card (development only)
# ---------------------------------------------------------------------------
add_executable(wlcard tools/card-preview.cpp src/DefinitionCard.cpp)
target_link_libraries(wlcard PRIVATE Qt6::Widgets wordlens_core)

# ---------------------------------------------------------------------------
# Headless checks for the word-inbox rules (development only)
# ---------------------------------------------------------------------------
add_executable(wltest tools/session-check.cpp)
target_link_libraries(wltest PRIVATE Qt6::Core wordlens_core)

# ---------------------------------------------------------------------------
# Runtime data (word lists, bundled OCR engine)
# ---------------------------------------------------------------------------
# The word lists and the dictionary are regenerated outside the build, so sync
# them on every build instead of only when a target happens to relink.
add_custom_target(sync_data ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/data"
            "${CMAKE_CURRENT_BINARY_DIR}/data"
    COMMENT "Syncing word lists and dictionary")

foreach(target WordLens wlocr)
    add_dependencies(${target} sync_data)

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/tesseract/tesseract.exe")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tesseract"
                    "$<TARGET_FILE_DIR:${target}>/tools/tesseract"
            COMMENT "Copying bundled tesseract")
    endif()
endforeach()

install(TARGETS WordLens RUNTIME DESTINATION .)
qt_generate_deploy_app_script(
    TARGET WordLens
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
