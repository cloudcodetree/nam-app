cmake_minimum_required(VERSION 3.22)
project(NamPlayer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- JUCE ---
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15)
FetchContent_MakeAvailable(JUCE)

# --- NeuralAudio (submodule) ---
add_subdirectory(extern/NeuralAudio)

# --- Melatonin Inspector (DevTools-style live UI inspector; debug only) ---
# SOURCE_DIR forces the folder name to `melatonin_inspector` so JUCE's
# juce_add_module derives the module header name correctly (FetchContent's
# default `-src` suffix would make it look for melatonin_inspector-src.h).
FetchContent_Declare(melatonin_inspector
    GIT_REPOSITORY https://github.com/sudara/melatonin_inspector.git
    GIT_TAG main
    SOURCE_DIR ${CMAKE_BINARY_DIR}/melatonin_inspector)
FetchContent_MakeAvailable(melatonin_inspector)

# --- TONE3000 publishable key (build-time injection) ---
# Reads .env (if present) for ONLY the publishable key. The confidential
# client secret (TONE3000_CLIENT_SECRET) is never parsed or injected here --
# a native OAuth2 PKCE client must not embed it.
set(T3K_PUB "")
if(EXISTS "${CMAKE_SOURCE_DIR}/.env")
    file(STRINGS "${CMAKE_SOURCE_DIR}/.env" _envlines)
    foreach(_l ${_envlines})
        if(_l MATCHES "^TONE3000_PUBLISHABLE_KEY=(.*)$")
            set(T3K_PUB "${CMAKE_MATCH_1}")
            string(STRIP "${T3K_PUB}" T3K_PUB)
        endif()
    endforeach()
endif()

# --- App target ---
juce_add_gui_app(NamPlayer
    PRODUCT_NAME "NAM Player"
    # Required so macOS/iOS grant audio-input (guitar) access; without a
    # microphone usage description the OS terminates the app on input open.
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "NAM Player needs audio input access to process your guitar signal in real time.")
target_compile_features(NamPlayer PRIVATE cxx_std_17)
target_sources(NamPlayer PRIVATE
    Source/app/AudioDeviceCallbackAdapter.cpp
    Source/app/MainComponent.cpp
    Source/app/LibraryPanel.cpp
    Source/app/Main.cpp
    Source/app/Tone3000Auth.cpp
    Source/dsp/ToneEngine.cpp
    Source/dsp/IrCab.cpp
    Source/dsp/ToneEq.cpp
    Source/model/NamModel.cpp
    Source/model/ModelHost.cpp
    Source/model/IrLoader.cpp
    Source/model/LibraryStore.cpp
    Source/model/LibraryImporter.cpp
    Source/net/Pkce.cpp
    Source/net/Tone3000Api.cpp)
target_include_directories(NamPlayer PRIVATE Source ${CMAKE_SOURCE_DIR}/extern/dr_wav
    ${CMAKE_SOURCE_DIR}/extern/NeuralAudio/deps/NeuralAmpModelerCore/Dependencies/nlohmann
    ${CMAKE_SOURCE_DIR}/extern/picosha2)
target_link_libraries(NamPlayer PRIVATE
    NeuralAudio
    melatonin_inspector
    juce::juce_audio_utils
    juce::juce_gui_extra)
target_compile_definitions(NamPlayer PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="NAM Player"
    # Publishable key only (see T3K_PUB above); "" if .env is absent.
    TONE3000_PUBLISHABLE_KEY="${T3K_PUB}")

# --- Tests ---
enable_testing()
add_subdirectory(tests)
