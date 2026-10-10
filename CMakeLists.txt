# ==============================================================================
#
#  Kitbox
#
#  A 16-pad drum sampler: one sample per pad, a short mono voice chain behind
#  each hit, and three shared effects fed by per-pad sends.
#
#  The signal path is mono on purpose, right up to the pan knob. A drum hit is a
#  point source, and a stereo sample path would double the cost of every voice
#  to carry information that pan then throws away. So a sample is summed to mono
#  once, when it is loaded; the voice chain - pitch, filter, filter envelope,
#  amp envelope - runs on one channel; pan places the result. The send buses are
#  mono as well. Only the reverb widens again: it takes the mono send and
#  returns stereo, because a plate that answers in mono sounds like a box.
#
#  Three build products come out of this file:
#
#    AU          the classic v2 component (aumu). The deliverable for Logic.
#    VST3        the cross-host deliverable.
#    Standalone  the development build - no component cache to fight.
#
#  No AUv3, deliberately. FX Analyzer shipped one and took it back: dragging it
#  to another slot in Logic deadlocked the host through JUCE's callSync in the
#  AUv3 wrapper, with no fix reachable from plugin code. See FX Analyzer's
#  CMakeLists.txt and logic-hang.txt before adding it here.
#
#  Two console targets do the verifying. KitboxCheck measures the engine
#  against signals whose answer is known by arithmetic - a filter's response at
#  its cutoff, an octave of pitch, a decay reaching -60 dB where it says it
#  will. EditorShot renders the panel to a PNG and round-trips the plugin state,
#  samples included, through a fresh processor.
#
# ==============================================================================

cmake_minimum_required(VERSION 3.22)

# 0.2.0: the send effects became slots (new parameters). Logic does not
# re-validate an AU whose version is unchanged, so every change to the
# parameter list or the channel layouts bumps this. 0.3.0: the Mod slot's Type
# gained a fifth value, Rackbox's frequency Shifter. 0.4.x had Comp, a one-knob
# compressor on the Main sum with a gain-reduction meter; 0.5.0 took it out
# again (the parameter is gone; a kit that set it loads without it).
# 0.5.1: a send effect switched away from keeps its own A/B through the fade.
# 0.5.2: the Shifter's Shift prints whole hertz above 100 Hz (not 123.4567 Hz).
# 0.6.0: Humanize's range doubled, 50 % at start (a parameter default changed).
# 0.6.1: pads can be renamed (right-click > Rename...).
# 0.7.0: Transmute inside - a pad plays its sample or Transmute's synth of it
# (right-click > Sample / Auto / a model; SYN for every pad); the kit container
# became a ZIP holding originals, synths and their .drumparams.
# 0.7.1: right-click > Export Samples... writes them into a folder.
project(Kitbox VERSION 0.7.1)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Apple Silicon only, matching the other plugin bundles on this machine.
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET "26.5" CACHE STRING "" FORCE)

# JUCE lives outside the project tree rather than as a submodule, because it is
# shared with every other plugin on this machine.
set(JUCE_PATH "$ENV{HOME}/JUCE" CACHE PATH "Path to the JUCE framework")

if(NOT EXISTS "${JUCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found at ${JUCE_PATH}. Pass -DJUCE_PATH=/path/to/JUCE.")
endif()

add_subdirectory("${JUCE_PATH}" JUCE)

# ------------------------------------------------------------------------------
# The plugin
# ------------------------------------------------------------------------------

juce_add_plugin(Kitbox
    # The vendor a host lists this under - display only. The identity is the
    # pair of four-character codes below: the AU is aumu/Ktbx/Tzor and the VST3
    # class id is a hash of the same two codes. Changing either after a session
    # has been saved makes a different plugin, and that session loses it.
    COMPANY_NAME                "T'Zorr"
    BUNDLE_ID                   TZorr.Kitbox
    PLUGIN_MANUFACTURER_CODE    Tzor
    PLUGIN_CODE                 Ktbx
    FORMATS                     AU VST3 Standalone
    PRODUCT_NAME                "Kitbox"
    DESCRIPTION                 "16-pad drum sampler"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE    # Scripts/install.sh does this, cache flush and all
    VST3_CATEGORIES             Instrument Drum Sampler
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    HARDENED_RUNTIME_ENABLED    TRUE
    NEEDS_WEB_BROWSER           FALSE
    NEEDS_CURL                  FALSE)

# Transmute's analysis, fit and synth, ported from its Swift engine (see
# Source/Transmute/Dsp.h). No fused multiply-adds: Swift does not contract
# a * b + c into one instruction, clang would, and the fused form rounds
# differently - enough, over a fit's thousands of dependent steps, to land on
# another drum than Transmute does for the same sample.
set(KITBOX_TRANSMUTE_SOURCES
    Source/Transmute/DrumParams.cpp
    Source/Transmute/Dsp.cpp
    Source/Transmute/DrumSynth.cpp
    Source/Transmute/Comparison.cpp
    Source/Transmute/Analyzer.cpp
    Source/Transmute/Fitter.cpp
    Source/Transmute/AnalysisInput.cpp)

# And no -Wfloat-equal there: the port compares exactly where the Swift does.
set_source_files_properties(${KITBOX_TRANSMUTE_SOURCES} PROPERTIES COMPILE_OPTIONS "-ffp-contract=off;-Wno-float-equal")

# The engine is listed separately so the verification target links exactly the
# objects the plugin runs. Testing a reimplementation tests nothing.
set(KITBOX_CORE_SOURCES
    Source/Engine/SampleData.cpp
    Source/Engine/DrumVoice.cpp
    Source/Engine/DrumEngine.cpp
    Source/Engine/Fx/PlateReverb.cpp
    Source/Engine/Fx/EffectCatalog.cpp
    Source/Engine/Fx/EffectBlock.cpp
    Source/Engine/KitFile.cpp
    Source/Engine/AuPreset.cpp
    Source/Engine/PadTransmuter.cpp
    ${KITBOX_TRANSMUTE_SOURCES})

set(KITBOX_UI_SOURCES
    Source/UI/Knob.cpp
    Source/UI/PadButton.cpp
    Source/UI/SampleDisplay.cpp)

target_sources(Kitbox PRIVATE
    ${KITBOX_CORE_SOURCES}
    ${KITBOX_UI_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(Kitbox PRIVATE Source)

target_compile_definitions(Kitbox PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(Kitbox PRIVATE
    "-framework Accelerate"
    "-framework AudioToolbox"
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# KitboxCheck - the engine harness
# ------------------------------------------------------------------------------

juce_add_console_app(KitboxCheck PRODUCT_NAME "KitboxCheck")

target_sources(KitboxCheck PRIVATE
    Verification/KitboxCheck.cpp
    Verification/TransmuteCheck.cpp
    ${KITBOX_CORE_SOURCES})

target_include_directories(KitboxCheck PRIVATE Source)

target_compile_definitions(KitboxCheck PUBLIC
    KITBOX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(KitboxCheck PRIVATE
    "-framework Accelerate"
    "-framework AudioToolbox"
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_data_structures
    juce::juce_events
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# EditorShot - renders the panel to a PNG and round-trips the state
# ------------------------------------------------------------------------------

juce_add_console_app(EditorShot PRODUCT_NAME "EditorShot")

target_sources(EditorShot PRIVATE Verification/EditorShot.cpp)

target_compile_definitions(EditorShot PUBLIC
    JUCE_MODAL_LOOPS_PERMITTED=1)

# Linking the plugin's own shared-code library rather than recompiling its
# sources: that library already carries the JucePlugin_* definitions that
# juce_add_plugin generates.
target_link_libraries(EditorShot PRIVATE
    Kitbox
    juce::juce_gui_basics
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

target_include_directories(EditorShot PRIVATE Source)

# Both sides define JUCE_STANDALONE_APPLICATION and the console app's is the
# one that should win.
target_compile_options(EditorShot PRIVATE -Wno-macro-redefined)
