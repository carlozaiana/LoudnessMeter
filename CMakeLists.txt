cmake_minimum_required(VERSION 3.22)

project(LoudnessMeter VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# macOS universal binary settings
set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Build architectures")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

# Fix for Xcode 15 linker issue with older deployment targets
add_link_options("-Wl,-ld_classic")

# Fetch JUCE
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 7.0.9
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

# Plugin target
juce_add_plugin(LoudnessMeter
    COMPANY_NAME "AudioDev"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE AdEv
    PLUGIN_CODE Ldmt
    FORMATS VST3
    PRODUCT_NAME "Loudness Meter"
)

juce_generate_juce_header(LoudnessMeter)

target_sources(LoudnessMeter
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/DSP/EBU128LoudnessMeter.cpp
        Source/DSP/EBU128LoudnessMeter.h
        Source/Storage/LoudnessDataStore.cpp
        Source/Storage/LoudnessDataStore.h
        Source/UI/LoudnessHistoryDisplay.cpp
        Source/UI/LoudnessHistoryDisplay.h
)

target_compile_definitions(LoudnessMeter
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_SILENCE_XCODE_15_LINKER_WARNING=1
)

target_link_libraries(LoudnessMeter
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)