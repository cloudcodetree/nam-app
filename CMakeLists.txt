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
    Source/app/Main.cpp
    Source/dsp/ToneEngine.cpp
    Source/model/NamModel.cpp
    Source/model/ModelHost.cpp)
target_include_directories(NamPlayer PRIVATE Source)
target_link_libraries(NamPlayer PRIVATE
    NeuralAudio
    juce::juce_audio_utils
    juce::juce_gui_extra)
target_compile_definitions(NamPlayer PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="NAM Player")

# --- Tests ---
enable_testing()
add_subdirectory(tests)
