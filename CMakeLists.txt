cmake_minimum_required(VERSION 3.22)

# The build sets THOREAU_VERSION (1.0.<build number>); THOREAU compares it with the latest release to offer updates.
set(THOREAU_VERSION "1.0.0" CACHE STRING "Version of this build")
project(THOREAU VERSION ${THOREAU_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static MSVC runtime: the .exe runs without installing the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(THOREAU_BUILD_APP   "Build the THOREAU application" ON)
option(THOREAU_BUILD_TESTS "Build the headless engine tests" OFF)

# ---------------------------------------------------------------------------
# JUCE: use a local checkout if JUCE_DIR is given, otherwise fetch it.
# ---------------------------------------------------------------------------
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")

if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(ENGINE_SOURCES
    Source/DrumVoices.cpp
    Source/AcousticDrum.cpp
    Source/Kits.cpp
    Source/KitBus.cpp
    Source/DrumEngine.cpp
    Source/Project.cpp
    Source/TapeChase.cpp
    Source/HawthorneLink/NetSender.cpp)

set(COMMON_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    THOREAU_VERSION_STRING="${PROJECT_VERSION}")

juce_add_binary_data(ThoreauFonts SOURCES
    Resources/Fonts/CourierPrime-Regular.ttf
    Resources/Fonts/CourierPrime-Bold.ttf
    Resources/Fonts/Roboto-Regular.ttf
    Resources/Fonts/Roboto-Bold.ttf)

if(THOREAU_BUILD_APP)
    juce_add_gui_app(Thoreau
        PRODUCT_NAME "THOREAU"
        COMPANY_NAME "PEQUOD"
        BUNDLE_ID    "com.pequod.thoreau"
        ICON_BIG     "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Icon/THOREAU.png"
        ICON_SMALL   "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Icon/THOREAU-small.png"
        VERSION      ${PROJECT_VERSION})

    juce_generate_juce_header(Thoreau)

    target_sources(Thoreau PRIVATE
        ${ENGINE_SOURCES}
        Source/Main.cpp
        Source/MainComponent.cpp
        Source/StepGrid.cpp
        Source/VoicePanel.cpp
        Source/SongPanel.cpp
        Source/KitPanel.cpp
        Source/SettingsPanel.cpp
        Source/Updater.cpp)

    target_compile_definitions(Thoreau PRIVATE ${COMMON_DEFS}
        JUCE_ASIO=1
        JUCE_WASAPI=1
        JUCE_DIRECTSOUND=1
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Thoreau,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Thoreau,JUCE_VERSION>")

    target_link_libraries(Thoreau
        PRIVATE
            ThoreauFonts
            juce::juce_audio_utils
            juce::juce_audio_formats
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------------------
# Headless tests: voices, sequencer, and tape sync against Hawthorne's receiver
# (HAWTHORNE_SOURCE = a Hawthorne checkout; the sync test is skipped without it).
# ---------------------------------------------------------------------------
if(THOREAU_BUILD_TESTS)
    set(HAWTHORNE_SOURCE "" CACHE PATH "Path to a Hawthorne checkout, for the end-to-end sync test")
    juce_add_console_app(ThoreauTests PRODUCT_NAME "ThoreauTests")
    juce_generate_juce_header(ThoreauTests)
    target_sources(ThoreauTests PRIVATE ${ENGINE_SOURCES} Tests/ThoreauTests.cpp)
    target_compile_definitions(ThoreauTests PRIVATE ${COMMON_DEFS})
    if(HAWTHORNE_SOURCE AND EXISTS "${HAWTHORNE_SOURCE}/Source/Network/NetReceiver.cpp")
        # Hawthorne's own receiver, behind a small bridge (its protocol header can't share a file with ours)
        target_sources(ThoreauTests PRIVATE
            Tests/HawthorneBridge.cpp
            "${HAWTHORNE_SOURCE}/Source/Network/NetReceiver.cpp"
            "${HAWTHORNE_SOURCE}/Source/Engine/Varispeed.cpp")
        set_source_files_properties(Tests/HawthorneBridge.cpp
            "${HAWTHORNE_SOURCE}/Source/Network/NetReceiver.cpp"
            "${HAWTHORNE_SOURCE}/Source/Engine/Varispeed.cpp"
            PROPERTIES INCLUDE_DIRECTORIES "${HAWTHORNE_SOURCE}/Source")
        target_compile_definitions(ThoreauTests PRIVATE THOREAU_HAVE_HAWTHORNE=1)
    endif()
    target_link_libraries(ThoreauTests PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_core
        juce::juce_dsp
        juce::juce_events
        juce::juce_recommended_config_flags)
endif()
