cmake_minimum_required(VERSION 3.22)

# Must be set before project(). 10.13 is the lowest target JUCE 9 and CLAP both support comfortably.
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
# Universal binaries: Apple Silicon and Intel.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")

project(Astralay VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(libs/JUCE)
add_subdirectory(libs/clap-juce-extensions EXCLUDE_FROM_ALL)

juce_add_plugin(Astralay
    COMPANY_NAME "Iron Labs"
    BUNDLE_ID "me.ironlabs.astralay"
    PLUGIN_MANUFACTURER_CODE Ilbs
    PLUGIN_CODE Alay
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Astralay"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Delay
    AU_MAIN_TYPE kAudioUnitType_Effect
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

clap_juce_extensions_plugin(TARGET Astralay
    CLAP_ID "me.ironlabs.astralay"
    CLAP_FEATURES audio-effect delay glitch stereo)

target_sources(Astralay
    PRIVATE
        source/PluginProcessor.cpp
        source/PluginEditor.cpp)

target_include_directories(Astralay
    PRIVATE
        source)

target_compile_definitions(Astralay
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Astralay
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
