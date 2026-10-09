cmake_minimum_required(VERSION 3.22)

project(MegaSynth VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
endif()

# JUCE: use a local checkout if JUCE_PATH is given, otherwise download it.
if(DEFINED JUCE_PATH)
    add_subdirectory(${JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

option(MEGASYNTH_MINIMAL_LINUX "Disable optional Linux system libraries (for headless test builds)" OFF)
option(MEGASYNTH_TESTS "Build the offline render test" OFF)

if(APPLE)
    set(MEGASYNTH_FORMATS VST3 AU Standalone)
else()
    set(MEGASYNTH_FORMATS VST3 Standalone)
endif()

set(MEGASYNTH_SOURCES
    Source/Engine.cpp
    Source/Registry.cpp
    Source/ModMatrix.cpp
    Source/Mut/Mutator.cpp
    Source/Sequencer.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UI/Overview.cpp)

set(MEGASYNTH_DEFINES
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

if(MEGASYNTH_MINIMAL_LINUX)
    list(APPEND MEGASYNTH_DEFINES JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_ALSA=0 JUCE_JACK=0)
endif()

juce_add_plugin(MegaSynth
    COMPANY_NAME "Twisted Genetics"
    COMPANY_WEBSITE "https://github.com/TwistedGenetics/Mega-Synth"
    BUNDLE_ID com.twistedgenetics.megasynth
    PLUGIN_MANUFACTURER_CODE TwGn
    PLUGIN_CODE MgSy
    FORMATS ${MEGASYNTH_FORMATS}
    PRODUCT_NAME "Mega Synth"
    DESCRIPTION "Mega Synth - 6 oscillators, FM/ring matrix, 17 filters, effects and sequencer"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)

juce_generate_juce_header(MegaSynth)
target_sources(MegaSynth PRIVATE ${MEGASYNTH_SOURCES})
target_compile_definitions(MegaSynth PUBLIC ${MEGASYNTH_DEFINES})
target_link_libraries(MegaSynth
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(MEGASYNTH_TESTS)
    juce_add_console_app(MegaSynthTest PRODUCT_NAME "MegaSynthTest")
    juce_generate_juce_header(MegaSynthTest)
    target_sources(MegaSynthTest PRIVATE Tests/RenderTest.cpp ${MEGASYNTH_SOURCES})
    target_include_directories(MegaSynthTest PRIVATE Source)
    target_compile_definitions(MegaSynthTest PRIVATE ${MEGASYNTH_DEFINES}
        JucePlugin_Name="Mega Synth" JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(MegaSynthTest PRIVATE
        juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
