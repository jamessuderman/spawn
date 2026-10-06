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

    target_sources(SpawnStandalone
        PRIVATE
            src/Main.cpp)

    target_compile_definitions(SpawnStandalone
        PRIVATE
            JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SpawnStandalone,JUCE_PRODUCT_NAME>"
            JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:SpawnStandalone,JUCE_VERSION>")

    target_link_libraries(SpawnStandalone
        PRIVATE
            juce::juce_gui_basics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()

# VST3 plugin (skeleton wired but off by default)
if(SPAWN_BUILD_VST3)
    # Populated in the VST3 milestone. See PROGRESS.md.
endif()