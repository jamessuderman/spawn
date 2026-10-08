cmake_minimum_required(VERSION 3.21)
project(Spawn VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SPAWN_BUILD_STANDALONE "Build Spawn as a standalone application" ON)
option(SPAWN_BUILD_VST3 "Build Spawn as a VST3 plugin" OFF)

# JUCE (vendored as a git submodule at third_party/JUCE)
set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE" CACHE PATH "Path to the JUCE source tree")
if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE not found at ${JUCE_DIR}.\n"
        "Run: git submodule update --init --recursive\n"
        "Or set JUCE_DIR to a valid JUCE source tree.")
endif()
add_subdirectory(${JUCE_DIR} "${CMAKE_BINARY_DIR}/_juce" EXCLUDE_FROM_ALL)

# Standalone app
if(SPAWN_BUILD_STANDALONE)
    juce_add_gui_app(SpawnStandalone
        PRODUCT_NAME "Spawn"
        PRODUCT_VERSION 0.1.0
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE)

    juce_generate_juce_header(SpawnStandalone)

    # Embedded assets (fonts, icons, licences).
    # Add new files here as the project grows.
    juce_add_binary_data(SpawnAssets
        SOURCES
            resources/fonts/PublicSans-Regular.ttf
            resources/fonts/PublicSans-SemiBold.ttf
            resources/icons/lucide/settings.svg
            resources/icons/lucide/sliders-horizontal.svg
            resources/icons/lucide/square-stack.svg
            resources/icons/lucide/activity.svg
            resources/icons/lucide/audio-lines.svg
            resources/icons/lucide/x.svg
            resources/icons/lucide/unfold-horizontal.svg
            resources/icons/lucide/chart-gantt.svg
            resources/icons/lucide/volume-2.svg
            resources/icons/lucide/scissors.svg
            resources/icons/lucide/move.svg
            resources/icons/lucide/folder-open.svg
            resources/licences/LICENCE-PublicSans.md
            resources/licences/LICENCE-Lucide.md)

    target_sources(SpawnStandalone
        PRIVATE
            src/Main.cpp
            src/controlheader/ControlHeader.cpp
            src/controlheader/DisplayButton.cpp
            src/display/Display.cpp
            src/sequencer/SequencerView.cpp
            src/toolbar/Toolbar.cpp
            src/toolbar/ToolButton.cpp
            src/settings/PropertyRow.cpp
            src/settings/SettingsView.cpp
            src/settings/ThemeColorSwatch.cpp
            src/stores/SettingsStore.cpp
            src/ui/fonts/Fonts.cpp
            src/ui/icons/Icon.cpp
            src/ui/icons/Icons.cpp
            src/ui/lookandfeel/Palette.cpp
            src/ui/lookandfeel/SpawnLookAndFeel.cpp
            src/waveform/WaveformView.cpp)

    target_compile_definitions(SpawnStandalone
        PRIVATE
            JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SpawnStandalone,JUCE_PRODUCT_NAME>"
            JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:SpawnStandalone,JUCE_VERSION>")

    target_link_libraries(SpawnStandalone
        PRIVATE
            juce::juce_gui_basics
            juce::juce_gui_extra
            juce::juce_audio_basics
            juce::juce_audio_formats
            juce::juce_audio_utils
            SpawnAssets
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()

# VST3 plugin (skeleton wired but off by default)
if(SPAWN_BUILD_VST3)
    # Populated in the VST3 milestone. See PROGRESS.md.
endif()