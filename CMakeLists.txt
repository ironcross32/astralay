cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
# Universal binaries: Apple Silicon and Intel.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")

project(Astralay VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(libs/JUCE)
add_subdirectory(libs/clap-juce-extensions EXCLUDE_FROM_ALL)

# PFFFT: fast FFTs for the formant shifters. Built from source as its own library so its C code
# isn't held to JUCE's warning flags.
add_library(astralay_pffft STATIC
    libs/pffft/src/pffft.c
    libs/pffft/src/pffft_common.c)

target_include_directories(astralay_pffft
    PUBLIC libs/pffft/include
    PRIVATE libs/pffft/include/pffft libs/pffft/src)

set_target_properties(astralay_pffft PROPERTIES
    C_STANDARD 99
    POSITION_INDEPENDENT_CODE ON)

# Static library: stops the headers declaring the functions as DLL imports on Windows.
target_compile_definitions(astralay_pffft PUBLIC PFFFT_STATIC_DEFINE)

if(MSVC)
    target_compile_definitions(astralay_pffft PRIVATE _USE_MATH_DEFINES)
endif()

juce_add_plugin(Astralay
    COMPANY_NAME "Iron Labs"
    BUNDLE_ID "me.ironlabs.astralay"
    PLUGIN_MANUFACTURER_CODE Ilbs
    PLUGIN_CODE Alay
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Astralay"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Delay
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

clap_juce_extensions_plugin(TARGET Astralay
    CLAP_ID "me.ironlabs.astralay"
    CLAP_FEATURES audio-effect delay glitch stereo)

# Sources shared by the plugin and the test runner: DSP and parameter code with no editor dependencies.
set(ASTRALAY_CORE_SOURCES
    source/dsp/Engine.cpp
    source/dsp/FormantShifter.cpp
    source/dsp/GlitchChain.cpp
    source/dsp/RealFft.cpp
    source/dsp/Smear.cpp
    source/dsp/Tap.cpp
    source/params/NoteValues.cpp
    source/params/Parameters.cpp
    source/params/Units.cpp
    source/ui/Steps.cpp)

# Diagnostic logging is always compiled into Debug builds. This option adds it to the other
# configurations too, for when a Debug build is too slow to reproduce a problem. Either way it
# stays off until the ASTRALAY_LOG environment variable is set.
option(ASTRALAY_DIAGNOSTICS "Compile diagnostic logging into every configuration, not just Debug" OFF)
set(ASTRALAY_DIAGNOSTICS_DEFINITION
    "ASTRALAY_DIAGNOSTICS=$<IF:$<OR:$<CONFIG:Debug>,$<BOOL:${ASTRALAY_DIAGNOSTICS}>>,1,0>")

target_sources(Astralay
    PRIVATE
        ${ASTRALAY_CORE_SOURCES}
        source/PluginProcessor.cpp
        source/PluginEditor.cpp
        source/MidiEditor.cpp
        source/state/DiagnosticLog.cpp
        source/state/History.cpp
        source/state/MidiMappings.cpp
        source/state/MidiSmoothing.cpp
        source/state/Macros.cpp
        source/state/Presets.cpp
        source/state/Settings.cpp
        source/ui/AccessibilitySettings.cpp
        source/ui/Announcer.cpp
        source/ui/ContextMenu.cpp
        source/ui/HelpTags.cpp
        source/ui/HelpText.cpp
        source/ui/KeyLayer.cpp
        source/ui/ParameterControls.cpp
        source/ui/PerformancePad.cpp
        source/ui/Theme.cpp)

target_include_directories(Astralay
    PRIVATE
        source)

target_compile_definitions(Astralay
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        ${ASTRALAY_DIAGNOSTICS_DEFINITION})

target_link_libraries(Astralay
    PRIVATE
        astralay_pffft
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Apple Silicon refuses to load unsigned code, and JUCE writes moduleinfo.json into the VST3 after
# signing it. Ad-hoc sign every bundle as the last post-build step so the seal covers everything.
if(APPLE)
    foreach(format IN ITEMS VST3 CLAP AU Standalone)
        add_custom_command(TARGET Astralay_${format} POST_BUILD
            COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:Astralay_${format}>"
            VERBATIM)
    endforeach()
endif()

# Unit tests for the DSP and parameter code, run with CTest or directly.
option(ASTRALAY_BUILD_TESTS "Build the Astralay test runner" ON)

if(ASTRALAY_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(AstralayTests
        PRODUCT_NAME "AstralayTests")

    target_sources(AstralayTests
        PRIVATE
            ${ASTRALAY_CORE_SOURCES}
            tests/TestMain.cpp
            tests/DspTests.cpp
            tests/FreezeSustainTests.cpp
            tests/ParameterTests.cpp
            tests/StepTests.cpp
            tests/EditorTests.cpp
            tests/GlitchTests.cpp
            tests/ResetTests.cpp
            tests/FormantTests.cpp
            tests/FormantSchedulingTests.cpp
            tests/SmearTests.cpp
            tests/StateTests.cpp
            tests/MacroTests.cpp
            tests/MidiTests.cpp
            tests/Benchmark.cpp
            tests/DiagnosticsTests.cpp
            source/PluginProcessor.cpp
            source/PluginEditor.cpp
            source/MidiEditor.cpp
            source/state/DiagnosticLog.cpp
            source/state/History.cpp
            source/state/MidiMappings.cpp
            source/state/MidiSmoothing.cpp
            source/state/Macros.cpp
            source/state/Presets.cpp
            source/state/Settings.cpp
            source/ui/AccessibilitySettings.cpp
            source/ui/Announcer.cpp
            source/ui/ContextMenu.cpp
            source/ui/HelpTags.cpp
            source/ui/HelpText.cpp
            source/ui/KeyLayer.cpp
            source/ui/ParameterControls.cpp
            source/ui/PerformancePad.cpp
            source/ui/Theme.cpp)

    target_include_directories(AstralayTests
        PRIVATE
            source)

    target_compile_definitions(AstralayTests
        PRIVATE
            # Tests dispatch queued editor updates; plugin builds retain JUCE's default.
            JUCE_MODAL_LOOPS_PERMITTED=1
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JucePlugin_Name="Astralay"
            ${ASTRALAY_DIAGNOSTICS_DEFINITION})

    target_link_libraries(AstralayTests
        PRIVATE
            astralay_pffft
            juce::juce_audio_processors
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME AstralayTests COMMAND AstralayTests)
endif()
