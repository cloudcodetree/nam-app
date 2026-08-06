cmake_minimum_required(VERSION 3.22)
project(NamPlayer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Build the JUCE application, or just the JUCE-free core + headless tests?
# The DSP/model/net layers (Source/dsp, Source/model, Source/net) are JUCE-free
# and fully unit-tested, so `-DNAM_BUILD_APP=OFF` builds and tests them without
# fetching JUCE or needing any platform GUI dependencies (used by CI).
option(NAM_BUILD_APP "Build the JUCE desktop/mobile application (needs JUCE + platform GUI deps)" ON)

# --- NeuralAudio (submodule) --- always built; the tests link it too.
add_subdirectory(extern/NeuralAudio)

if(NAM_BUILD_APP)
    # --- JUCE ---
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15)
    FetchContent_MakeAvailable(JUCE)

    # --- Melatonin Inspector (DevTools-style live UI inspector; debug only) ---
    # Desktop only: it's a developer UI overlay used by MainComponent, which the
    # Android bring-up target does not build. Skipping it on Android avoids a
    # needless network fetch and keeps the mobile target lean.
    # SOURCE_DIR forces the folder name to `melatonin_inspector` so JUCE's
    # juce_add_module derives the module header name correctly (FetchContent's
    # default `-src` suffix would make it look for melatonin_inspector-src.h).
    if(NOT ANDROID)
        FetchContent_Declare(melatonin_inspector
            GIT_REPOSITORY https://github.com/sudara/melatonin_inspector.git
            GIT_TAG main
            SOURCE_DIR ${CMAKE_BINARY_DIR}/melatonin_inspector)
        FetchContent_MakeAvailable(melatonin_inspector)
    endif()

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

    if(ANDROID)
        # --- Android app target (Phase 5a: minimal iRig-validation build) ---
        # JUCE on Android builds the app as a shared library loaded by the Java
        # activity (see Builds/Android). This target reuses the JUCE-free core
        # (dsp/model) + NeuralAudio with a trivial bring-up component; the full
        # desktop app (library, TONE3000, MainComponent) is intentionally NOT
        # ported yet. Task 4 swaps the bring-up component for the audio app.
        juce_add_gui_app(NamPlayer PRODUCT_NAME "NAM Player")
        # JUCE's Android Java bootstrap (Java.java) does
        # System.loadLibrary("juce_jni"), so the shared library MUST be named
        # libjuce_jni.so regardless of the target/product name.
        set_target_properties(NamPlayer PROPERTIES OUTPUT_NAME juce_jni)
        target_compile_features(NamPlayer PRIVATE cxx_std_17)

        # Embed the bundled NAM model + the Hi-Fi UI fonts (Libre Caslon +
        # Work Sans). Android assets aren't real file paths, so everything the
        # app loads is BinaryData. Resource names: model_nam, LibreCaslon_ttf,
        # WorkSans_ttf.
        juce_add_binary_data(NamPlayerData SOURCES
            ${CMAKE_SOURCE_DIR}/Builds/Android/app/src/main/assets/model.nam
            ${CMAKE_SOURCE_DIR}/Builds/Android/app/src/main/assets/fonts/LibreCaslon.ttf
            ${CMAKE_SOURCE_DIR}/Builds/Android/app/src/main/assets/fonts/WorkSans.ttf)

        target_sources(NamPlayer PRIVATE
            Source/app/android/AndroidMain.cpp
            Source/app/android/AndroidAudioApp.cpp
            Source/app/ui/NamLookAndFeel.cpp
            Source/app/ui/PlayScreen.cpp
            Source/app/ui/EditScreen.cpp
            Source/app/ui/RadioScreen.cpp
            Source/app/ui/LibraryScreen.cpp
            Source/app/ui/LiveScreen.cpp
            Source/app/ui/AudioSettingsScreen.cpp
            Source/app/ui/AppShell.cpp
            Source/dsp/ToneEngine.cpp
            Source/dsp/IrCab.cpp
            Source/dsp/ToneEq.cpp
            Source/dsp/Reverb.cpp
            Source/model/NamModel.cpp
            Source/model/ModelHost.cpp
            Source/model/IrLoader.cpp
            Source/model/LibraryStore.cpp
            Source/model/LibraryImporter.cpp
            # TONE3000: OAuth2 PKCE + search/download (same code as desktop; the
            # loopback redirect works on Android — the on-device browser reaches
            # 127.0.0.1, so TONE3000's existing redirect_uri whitelist applies).
            Source/net/Pkce.cpp
            Source/net/Tone3000Api.cpp
            Source/app/Tone3000Auth.cpp
            Source/app/Tone3000Session.cpp)
        target_include_directories(NamPlayer PRIVATE Source
            ${CMAKE_SOURCE_DIR}/extern/dr_wav
            ${CMAKE_SOURCE_DIR}/extern/NeuralAudio/deps/NeuralAmpModelerCore/Dependencies/nlohmann
            ${CMAKE_SOURCE_DIR}/extern/picosha2)
        target_link_libraries(NamPlayer PRIVATE
            NamPlayerData
            NeuralAudio
            juce::juce_audio_utils
            juce::juce_gui_extra)
        target_compile_definitions(NamPlayer PRIVATE
            JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
            JUCE_APPLICATION_NAME_STRING="NAM Player"
            # Publishable key only (parsed from .env above); "" if absent.
            TONE3000_PUBLISHABLE_KEY="${T3K_PUB}")
    else()
        # --- Desktop app target (macOS/Windows/Linux) ---
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
            Source/app/SearchPanel.cpp
            Source/app/Main.cpp
            Source/app/Tone3000Auth.cpp
            Source/app/Tone3000Session.cpp
            Source/dsp/ToneEngine.cpp
            Source/dsp/IrCab.cpp
            Source/dsp/ToneEq.cpp
            Source/dsp/Reverb.cpp
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
    endif()
endif()

# --- Tests --- always built (JUCE-free), except when cross-compiling for
# Android via Gradle (the Gradle externalNativeBuild only builds NamPlayer, and
# the Catch2 host suite is not meaningful in that context).
if(NOT ANDROID)
    enable_testing()
    add_subdirectory(tests)
endif()
