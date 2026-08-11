# Phase 5a: Minimal Android Audio Test (iRig HD X) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. NOTE: this plan is **interactive + hardware-bound** (toolchain install on the user's Mac, deploys to a physical S24 Ultra, a USB audio interface). It is NOT suited to isolated subagents — execute it inline, with the user at the machine for the install/deploy/hardware steps.

**Goal:** Get the smallest useful NAM Player build running on a Samsung Galaxy S24 Ultra and validate the iRig HD X USB-C interface end to end (guitar → NAM processing → output) at a measurable real-time latency.

**Architecture:** Add a thin `Builds/Android/` Gradle project whose `externalNativeBuild` invokes the existing `CMakeLists.txt`. JUCE's CMake already supports the `Android` system name (Oboe audio, `JUCE_ANDROID=1`, `android`/`log` libs); Gradle supplies only the app shell (manifest, JUCE Java glue, packaging). The JUCE-free core (`Source/dsp|model|net` + NeuralAudio) is reused verbatim and proven on-device before any JUCE/Gradle bring-up.

**Tech Stack:** JUCE 8.0.15 (Oboe backend on Android), Android NDK + CMake, Gradle, `adb`, NeuralAudio (Eigen + RTNeural + xsimd), Catch2 (existing suite, run on-device for arm64 proof).

## Global Constraints

- **Target device:** Samsung Galaxy S24 Ultra, Android 14 (API 34), `arm64-v8a` ABI only for 5a.
- **NDK:** pin `26.3.11579264` (r26d). **Android CMake:** `3.22.1`. **JDK:** 17. **compileSdk/targetSdk:** 34. **minSdk:** 29.
- **JUCE stays pinned at 8.0.15** (desktop crash history; do not bump).
- **The JUCE-free core stays JUCE-free** and is reused unchanged — no Android `#ifdef`s in `Source/dsp|model|net`.
- **Desktop build must remain green** — every CMake change is additive/guarded so `cmake --preset default` and the 97-test suite still pass.
- **No network/auth/library in 5a** — no TONE3000, no OAuth, no LibraryStore. One `.nam` bundled in `assets/`.
- **RT-safety invariant unchanged:** the audio callback never allocates/locks/throws/does I/O.
- Secrets: `.env` stays gitignored; no keys embedded in the APK.

---

### Task 1: Install and verify the Android toolchain

Interactive, on the user's Mac. Deliverable: a working `adb` + NDK + JDK 17 that can see the S24 Ultra. No repo changes.

**Files:** none (environment only).

- [ ] **Step 1: Install JDK 17 (Homebrew)**

Run:
```bash
brew install --cask temurin@17
/usr/libexec/java_home -v 17   # prints the JDK 17 path
```
Expected: a path like `/Library/Java/JavaVirtualMachines/temurin-17.jdk/Contents/Home`.

- [ ] **Step 2: Set JAVA_HOME for this shell and persist it**

Run:
```bash
echo 'export JAVA_HOME=$(/usr/libexec/java_home -v 17)' >> ~/.zshrc
export JAVA_HOME=$(/usr/libexec/java_home -v 17)
java -version   # must report 17.x
```
Expected: `openjdk version "17...`.

- [ ] **Step 3: Install Android Studio + command-line tools**

Run:
```bash
brew install --cask android-studio
brew install --cask android-platform-tools   # adb, fastboot on PATH
adb version
```
Then launch Android Studio once and complete the Setup Wizard (installs the SDK). Expected: `adb` prints a version.

- [ ] **Step 4: Install SDK packages, NDK, and CMake via sdkmanager**

In Android Studio: **Settings → Languages & Frameworks → Android SDK**:
- SDK Platforms tab: check **Android 14 (API 34)**.
- SDK Tools tab: check **NDK (Side by side) 26.3.11579264**, **CMake 3.22.1**, **Android SDK Platform-Tools**, **Android SDK Build-Tools 34.x**. Apply.

Then set env vars:
```bash
echo 'export ANDROID_HOME=$HOME/Library/Android/sdk' >> ~/.zshrc
export ANDROID_HOME=$HOME/Library/Android/sdk
ls "$ANDROID_HOME/ndk/26.3.11579264/build/cmake/android.toolchain.cmake"
```
Expected: the toolchain file path exists (this is the file Task 2 uses).

- [ ] **Step 4b (optional): Create an arm64 emulator for build/launch iteration**

Lets you iterate on Tasks 3–4 (APK builds/launches, basic audio) without the phone tethered. On Apple Silicon the image is `arm64-v8a`, matching the phone's CPU. **Cannot test the iRig** (no USB-audio passthrough) — Task 5 still requires the physical S24.

Run:
```bash
sdkmanager --install "system-images;android-34;google_apis;arm64-v8a" "emulator"
avdmanager create avd -n nam_test -k "system-images;android-34;google_apis;arm64-v8a" --device pixel_7
"$ANDROID_HOME"/emulator/emulator -avd nam_test -no-snapshot -gpu host &
adb wait-for-device && adb devices
```
Expected: the emulator boots and appears in `adb devices` as `emulator-5554  device`. (`sdkmanager`/`avdmanager` live in `$ANDROID_HOME/cmdline-tools/latest/bin`; add to PATH if not found.)

- [ ] **Step 5: Enable USB debugging on the S24 Ultra and confirm the connection**

On the phone: **Settings → About phone → Software information → tap "Build number" 7×** to unlock Developer options, then **Settings → Developer options → enable "USB debugging"**. Plug the phone into the Mac via USB-C, accept the "Allow USB debugging?" RSA prompt.

Run:
```bash
adb devices -l
```
Expected: the S24 Ultra appears as `device` (not `unauthorized`/`offline`). **Gate:** do not proceed until this shows the device.

---

### Task 2: Prove the JUCE-free core + NeuralAudio build AND run on the phone's arm64

Highest-value, lowest-cost de-risk: cross-compile the existing test suite for `arm64-v8a` with the NDK toolchain (no JUCE, no Gradle) and **run it on the device via adb**. This answers "does the DSP/model core work on the phone's CPU?" before any app machinery. Uses the existing `-DNAM_BUILD_APP=OFF` path.

**Files:**
- Create: `scripts/android-core-check.sh` (convenience wrapper; keeps the exact invocation reproducible)

**Interfaces:**
- Consumes: `$ANDROID_HOME/ndk/26.3.11579264/build/cmake/android.toolchain.cmake` (from Task 1); the existing `NAM_BUILD_APP` option and `nam_tests` target from `CMakeLists.txt`/`tests/CMakeLists.txt`.
- Produces: a runnable `arm64-v8a` `nam_tests` binary and a repeatable script proving the core on-device.

- [ ] **Step 1: Configure the core for arm64-v8a with the NDK toolchain**

Run:
```bash
cd /Users/chris.harper/Development/nam_app
cmake -S . -B build-android-core \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_HOME/ndk/26.3.11579264/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 \
  -DCMAKE_BUILD_TYPE=Release -DNAM_BUILD_APP=OFF
```
Expected: configuration succeeds (this is the first proof the toolchain file + our CMake agree). If NeuralAudio/xsimd/RTNeural error here, that is the arm64 risk surfacing — fix by selecting the portable backend (e.g. ensure RTNeural uses its Eigen backend and xsimd resolves to NEON) rather than forking submodules.

- [ ] **Step 2: Build the arm64 test binary**

Run:
```bash
cmake --build build-android-core --target nam_tests -j
file build-android-core/tests/nam_tests
```
Expected: build succeeds; `file` reports `ELF 64-bit LSB ... ARM aarch64`.

- [ ] **Step 3: Push the binary + fixtures to the device and run the suite**

Run:
```bash
adb shell mkdir -p /data/local/tmp/namtest/fixtures
adb push build-android-core/tests/nam_tests /data/local/tmp/namtest/
adb push tests/fixtures/. /data/local/tmp/namtest/fixtures/
adb shell chmod 755 /data/local/tmp/namtest/nam_tests
adb shell "cd /data/local/tmp/namtest && ./nam_tests"
```
Expected: the Catch2 suite runs on the phone and reports **all tests passed** (97 cases). This is on-device proof the DSP + NAM inference work on arm64. (Note: the test binary reads fixtures via the `NAM_FIXTURE_A2` compile-def path baked at configure time — if it can't find the fixture on-device, re-run passing `-DNAM_FIXTURE_A2=/data/local/tmp/namtest/fixtures/example_a2.nam` at Step 1 configure, rebuild, re-push.)

- [ ] **Step 4: Capture the invocation in a script**

Create `scripts/android-core-check.sh`:
```bash
#!/usr/bin/env bash
# Cross-compiles the JUCE-free core + tests for arm64-v8a and runs them on a
# connected Android device via adb. Proves the DSP/model core on the phone's CPU.
set -euo pipefail
: "${ANDROID_HOME:?set ANDROID_HOME}"
NDK="$ANDROID_HOME/ndk/26.3.11579264"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cmake -S "$ROOT" -B "$ROOT/build-android-core" \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 \
  -DCMAKE_BUILD_TYPE=Release -DNAM_BUILD_APP=OFF
cmake --build "$ROOT/build-android-core" --target nam_tests -j
adb shell mkdir -p /data/local/tmp/namtest/fixtures
adb push "$ROOT/build-android-core/tests/nam_tests" /data/local/tmp/namtest/
adb push "$ROOT/tests/fixtures/." /data/local/tmp/namtest/fixtures/
adb shell chmod 755 /data/local/tmp/namtest/nam_tests
adb shell "cd /data/local/tmp/namtest && ./nam_tests"
```

- [ ] **Step 5: Make executable, verify desktop still green, commit**

Run:
```bash
chmod +x scripts/android-core-check.sh
cmake --build --preset default --target nam_tests -j && ctest --test-dir build --output-on-failure | tail -3
git add scripts/android-core-check.sh
git commit -m "chore: android-core-check — build+run the JUCE-free core on-device (arm64)"
```
Expected: desktop suite still 97/97; commit lands. **Gate:** the core must pass on-device before investing in the Gradle/JUCE app bring-up.

---

### Task 3: Stand up the Android app shell (Gradle + JUCE) and launch a trivial app on the device

Build the `Builds/Android/` Gradle wrapper that packages the JUCE app compiled from our CMake, and get a minimal JUCE window to launch on the S24. This is the highest-uncertainty task; verification is "the APK installs and launches," not a unit test. Keep `MainComponent` out of it — use a throwaway trivial component so a failure here is unambiguously a build/shell problem, not an app-logic problem.

**Files:**
- Create: `Builds/Android/settings.gradle`, `Builds/Android/build.gradle`, `Builds/Android/gradle.properties`, `Builds/Android/app/build.gradle`, `Builds/Android/app/src/main/AndroidManifest.xml`, `Builds/Android/app/src/main/res/values/strings.xml`
- Create: `Source/app/android/AndroidMain.cpp` (trivial JUCE app + component for bring-up)
- Modify: `CMakeLists.txt` (an Android branch that builds a shared-library JUCE target from the core + a trivial component, guarded by `if(ANDROID)`)

**Interfaces:**
- Consumes: the NDK toolchain + `ANDROID_HOME` (Task 1); the `NeuralAudio` target and `Source/` includes from the existing CMake; JUCE's `juce_add_gui_app` Android support.
- Produces: an installable debug APK (`app-debug.apk`) that launches a JUCE window on the device — the shell that Task 4 fills with the real audio app.

- [ ] **Step 1: Add the Android CMake branch (trivial component, shared lib)**

In `CMakeLists.txt`, inside the `if(NAM_BUILD_APP)` block, add an Android-specific branch that reuses the JUCE fetch but builds a shared library from a trivial component (JUCE on Android loads the app as a shared lib from the Java activity). Add near the app target:
```cmake
    if(ANDROID)
        # Android: JUCE app is a shared library loaded by the Java activity.
        # Use a trivial component for bring-up (Task 3); Task 4 swaps in the
        # real audio app. Reuses the JUCE-free core sources + NeuralAudio.
        juce_add_gui_app(NamPlayer PRODUCT_NAME "NAM Player")
        target_sources(NamPlayer PRIVATE
            Source/app/android/AndroidMain.cpp
            Source/dsp/ToneEngine.cpp Source/dsp/IrCab.cpp
            Source/dsp/ToneEq.cpp Source/dsp/Reverb.cpp
            Source/model/NamModel.cpp Source/model/ModelHost.cpp
            Source/model/IrLoader.cpp)
        target_include_directories(NamPlayer PRIVATE Source
            ${CMAKE_SOURCE_DIR}/extern/dr_wav
            ${CMAKE_SOURCE_DIR}/extern/NeuralAudio/deps/NeuralAmpModelerCore/Dependencies/nlohmann)
        target_link_libraries(NamPlayer PRIVATE NeuralAudio
            juce::juce_audio_utils juce::juce_gui_extra)
        target_compile_definitions(NamPlayer PRIVATE
            JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
            JUCE_APPLICATION_NAME_STRING="NAM Player")
        return()  # skip the desktop target definition below on Android
    endif()
```
(The existing desktop `juce_add_gui_app(...)` stays below, unreached on Android.)

- [ ] **Step 2: Write the trivial bring-up component**

Create `Source/app/android/AndroidMain.cpp`:
```cpp
#include <juce_gui_extra/juce_gui_extra.h>

// Minimal JUCE app for Android bring-up (Task 3): proves the Gradle -> CMake ->
// APK -> launch path works before wiring real audio (Task 4).
class BringUpComponent : public juce::Component {
public:
    BringUpComponent() { setSize(400, 200); }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colours::black);
        g.setColour(juce::Colours::limegreen);
        g.setFont(24.0f);
        g.drawText("NAM Player - Android bring-up OK",
                   getLocalBounds(), juce::Justification::centred);
    }
};

class BringUpWindow : public juce::DocumentWindow {
public:
    BringUpWindow() : DocumentWindow("NAM Player", juce::Colours::black,
                                     DocumentWindow::allButtons) {
        setUsingNativeTitleBar(true);
        setContentOwned(new BringUpComponent(), true);
        setVisible(true);
        setFullScreen(true);
    }
    void closeButtonPressed() override {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class NamPlayerApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "NAM Player"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const juce::String&) override { window_.reset(new BringUpWindow()); }
    void shutdown() override { window_ = nullptr; }
private:
    std::unique_ptr<BringUpWindow> window_;
};

START_JUCE_APPLICATION(NamPlayerApplication)
```

- [ ] **Step 3: Write the Gradle project files**

Create `Builds/Android/settings.gradle`:
```gradle
pluginManagement {
    repositories { google(); mavenCentral(); gradlePluginPortal() }
}
dependencyResolutionManagement {
    repositories { google(); mavenCentral() }
}
rootProject.name = "NamPlayer"
include ":app"
```

Create `Builds/Android/build.gradle`:
```gradle
plugins {
    id "com.android.application" version "8.5.2" apply false
}
```

Create `Builds/Android/gradle.properties`:
```properties
org.gradle.jvmargs=-Xmx4g
android.useAndroidX=true
```

Create `Builds/Android/app/build.gradle`:
```gradle
plugins { id "com.android.application" }

android {
    namespace "com.namplayer.app"
    compileSdk 34
    ndkVersion "26.3.11579264"

    defaultConfig {
        applicationId "com.namplayer.app"
        minSdk 29
        targetSdk 34
        versionCode 1
        versionName "0.1.0"
        ndk { abiFilters "arm64-v8a" }
        externalNativeBuild {
            cmake {
                arguments "-DNAM_BUILD_APP=ON", "-DANDROID_STL=c++_shared"
                targets "NamPlayer"
            }
        }
    }
    externalNativeBuild {
        cmake {
            path "../../../CMakeLists.txt"   // repo root, relative to app/
            version "3.22.1"
        }
    }
    buildTypes {
        debug { debuggable true }
    }
    compileOptions {
        sourceCompatibility JavaVersion.VERSION_17
        targetCompatibility JavaVersion.VERSION_17
    }
}
```

- [ ] **Step 4: Write the manifest (USB host + audio permission) and strings**

Create `Builds/Android/app/src/main/AndroidManifest.xml`:
```xml
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android">
    <uses-permission android:name="android.permission.RECORD_AUDIO"/>
    <uses-feature android:name="android.hardware.usb.host"/>
    <uses-feature android:name="android.hardware.audio.output"/>

    <application
        android:label="@string/app_name"
        android:allowBackup="false"
        android:hardwareAccelerated="true">
        <activity
            android:name="com.rmsl.juce.JuceActivity"
            android:label="@string/app_name"
            android:configChanges="keyboard|keyboardHidden|orientation|screenSize"
            android:screenOrientation="userLandscape"
            android:exported="true">
            <intent-filter>
                <action android:name="android.intent.action.MAIN"/>
                <category android:name="android.intent.category.LAUNCHER"/>
            </intent-filter>
        </activity>
        <meta-data android:name="android.app.lib_name" android:value="NamPlayer"/>
    </application>
</manifest>
```
Create `Builds/Android/app/src/main/res/values/strings.xml`:
```xml
<resources><string name="app_name">NAM Player</string></resources>
```

- [ ] **Step 5: Generate the Gradle wrapper and build the APK**

Run:
```bash
cd /Users/chris.harper/Development/nam_app/Builds/Android
gradle wrapper --gradle-version 8.9   # uses the Homebrew gradle once to create ./gradlew
./gradlew :app:assembleDebug
```
Expected: `BUILD SUCCESSFUL`; an APK at `app/build/outputs/apk/debug/app-debug.apk`.

**Risk/fallback:** if Gradle cannot wire JUCE's Java activity (`com.rmsl.juce.JuceActivity` unresolved) — JUCE's module Java sources are normally injected by Projucer — resolve by pointing the app `sourceSets` at JUCE's module Java (`build/_deps/juce-src/modules/juce_gui_basics/native/javacore` and peers) OR, as a bounded fallback, use Projucer **once** to generate a reference `Builds/Android` and copy its `java`/`res` glue (keeping our `externalNativeBuild → CMakeLists`). Record whichever worked in `.notes`.

- [ ] **Step 6: Install and launch on the device**

Run:
```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.namplayer.app/com.rmsl.juce.JuceActivity
adb logcat -d | grep -iE "namplayer|juce|AndroidRuntime" | tail -30
```
Expected: the phone shows the green "Android bring-up OK" screen; no fatal `AndroidRuntime` exception in logcat. **Gate:** a launching APK before wiring audio.

- [ ] **Step 7: Verify desktop unaffected, commit**

Run:
```bash
cd /Users/chris.harper/Development/nam_app
cmake --preset default >/dev/null && cmake --build --preset default --target NamPlayer -j 2>&1 | tail -2
git add Builds/Android Source/app/android/AndroidMain.cpp CMakeLists.txt
git commit -m "feat: Android app shell (Gradle -> CMake), trivial JUCE bring-up launches on device"
```
Expected: desktop app still links; commit lands.

---

### Task 4: Wire the real audio path — bundled model + ToneEngine + minimal UI

Replace the trivial bring-up with an audio app: open the audio device, run `dsp::ToneEngine` on the callback with one bundled `.nam`, and show device status / measured latency / in-out gain. Verification is on-device behavior + logcat (no unit test — the DSP is already suite-covered).

**Files:**
- Create: `Source/app/android/AndroidAudioApp.h/.cpp` (an `AudioAppComponent` subclass: device I/O + ToneEngine + minimal UI)
- Create: `Builds/Android/app/src/main/assets/model.nam` (copy of `tests/fixtures/example_a2.nam`)
- Modify: `Source/app/android/AndroidMain.cpp` (host `AndroidAudioApp` instead of the bring-up component)
- Modify: `CMakeLists.txt` (add `AndroidAudioApp.cpp` to the Android target sources)

**Interfaces:**
- Consumes: `dsp::ToneEngine` (`prepare(sampleRate,maxBlock)`, `setModel(shared_ptr<nam::NamModel>)`, `render(const float* in,float* out,int n)`, telemetry getters `cpuLoad()/outputPeak()`); `nam::NamModel::load(path,sampleRate,maxBlock)`; JUCE `AudioAppComponent`.
- Produces: the deployable minimal test app used for on-device iRig validation in Task 5.

- [ ] **Step 1: Bundle the model asset**

Run:
```bash
mkdir -p Builds/Android/app/src/main/assets
cp tests/fixtures/example_a2.nam Builds/Android/app/src/main/assets/model.nam
```

- [ ] **Step 2: Write the audio app component**

Create `Source/app/android/AndroidAudioApp.h`:
```cpp
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "dsp/ToneEngine.h"
#include "model/NamModel.h"

// Minimal Android audio app: device in -> ToneEngine (bundled model + FX) ->
// device out, with a status/latency/gain UI. No library/network (Phase 5a).
class AndroidAudioApp : public juce::AudioAppComponent,
                        private juce::Timer {
public:
    AndroidAudioApp();
    ~AndroidAudioApp() override;
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override;
    void releaseResources() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
private:
    void timerCallback() override;
    std::string copyBundledModelToFile();   // assets/model.nam -> internal storage path
    dsp::ToneEngine engine_;
    juce::Slider inGain_, outGain_;
    juce::Label  status_;
    double sampleRate_ = 48000.0;
    int    blockSize_  = 256;
    std::vector<float> mono_;   // preallocated in prepareToPlay
};
```

Create `Source/app/android/AndroidAudioApp.cpp`:
```cpp
#include "app/android/AndroidAudioApp.h"

AndroidAudioApp::AndroidAudioApp() {
    addAndMakeVisible(status_);
    status_.setColour(juce::Label::textColourId, juce::Colours::limegreen);
    status_.setJustificationType(juce::Justification::topLeft);
    for (auto* s : { &inGain_, &outGain_ }) {
        s->setSliderStyle(juce::Slider::LinearHorizontal);
        s->setRange(-24.0, 24.0, 0.1); s->setValue(0.0);
        s->setTextValueSuffix(" dB");
        addAndMakeVisible(*s);
    }
    inGain_.onValueChange  = [this]{ engine_.setInputDb((float) inGain_.getValue()); };
    outGain_.onValueChange = [this]{ engine_.setOutputDb((float) outGain_.getValue()); };

    // RECORD_AUDIO is requested by JUCE when opening input; 1 in / 2 out.
    setAudioChannels(1, 2);
    setSize(800, 400);
    startTimerHz(10);
}

AndroidAudioApp::~AndroidAudioApp() { stopTimer(); shutdownAudio(); }

std::string AndroidAudioApp::copyBundledModelToFile() {
    // Bundled assets aren't real filesystem paths; copy to internal storage so
    // NamModel::load() (which reads a path) can open it.
    auto dest = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("model.nam");
    if (! dest.existsAsFile()) {
        int sz = 0;
        if (const char* data = BinaryData::getNamedResource("model_nam", sz)) {
            dest.replaceWithData(data, (size_t) sz);
        }
    }
    return dest.getFullPathName().toStdString();
}

void AndroidAudioApp::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    sampleRate_ = sampleRate;
    blockSize_  = samplesPerBlockExpected;
    mono_.assign((size_t) samplesPerBlockExpected, 0.0f);
    engine_.prepare((int) sampleRate, samplesPerBlockExpected);
    if (auto m = nam::NamModel::load(copyBundledModelToFile(), (int) sampleRate, samplesPerBlockExpected))
        engine_.setModel(std::move(m));
}

void AndroidAudioApp::getNextAudioBlock(const juce::AudioSourceChannelInfo& info) {
    auto* buf = info.buffer;
    const int n = info.numSamples;
    const float* in = buf->getReadPointer(0, info.startSample);
    for (int i = 0; i < n && i < (int) mono_.size(); ++i) mono_[(size_t) i] = in[i];
    engine_.render(mono_.data(), mono_.data(), std::min(n, (int) mono_.size()));
    for (int ch = 0; ch < buf->getNumChannels(); ++ch) {
        float* out = buf->getWritePointer(ch, info.startSample);
        for (int i = 0; i < n && i < (int) mono_.size(); ++i) out[i] = mono_[(size_t) i];
    }
}

void AndroidAudioApp::releaseResources() {}

void AndroidAudioApp::timerCallback() {
    const double lat = sampleRate_ > 0 ? (blockSize_ / sampleRate_) * 1000.0 : 0.0;
    juce::String dev = "no device";
    if (auto* d = deviceManager.getCurrentAudioDevice())
        dev = d->getName() + " (" + d->getTypeName() + ")";
    status_.setText("Device: " + dev
        + "\nSR: " + juce::String(sampleRate_, 0) + " Hz  block: " + juce::String(blockSize_)
        + "\n~buffer latency: " + juce::String(lat, 1) + " ms/dir"
        + "\nload: " + juce::String(engine_.cpuLoad() * 100.0f, 0) + "%"
        + "  peak: " + juce::String(engine_.outputPeak(), 2),
        juce::dontSendNotification);
}

void AndroidAudioApp::paint(juce::Graphics& g) { g.fillAll(juce::Colours::black); }

void AndroidAudioApp::resized() {
    auto r = getLocalBounds().reduced(16);
    status_.setBounds(r.removeFromTop(160));
    r.removeFromTop(16);
    inGain_.setBounds(r.removeFromTop(48));
    outGain_.setBounds(r.removeFromTop(48));
}
```

- [ ] **Step 3: Host the audio app in the window**

In `Source/app/android/AndroidMain.cpp`, replace `#include <juce_gui_extra/juce_gui_extra.h>` with `#include "app/android/AndroidAudioApp.h"`, delete the `BringUpComponent` class, and change `setContentOwned(new BringUpComponent(), true);` to `setContentOwned(new AndroidAudioApp(), true);`.

- [ ] **Step 4: Add the source + enable BinaryData for the asset**

In `CMakeLists.txt` Android branch, add `Source/app/android/AndroidAudioApp.cpp` to `target_sources`, and bundle the model as BinaryData so `BinaryData::getNamedResource("model_nam", ...)` resolves:
```cmake
        juce_add_binary_data(NamPlayerData SOURCES
            ${CMAKE_SOURCE_DIR}/Builds/Android/app/src/main/assets/model.nam)
        target_link_libraries(NamPlayer PRIVATE NamPlayerData)
```
(Place before the `target_link_libraries(NamPlayer PRIVATE NeuralAudio ...)` call, and add `NamPlayerData` there too.)

- [ ] **Step 5: Rebuild, install, launch, check audio device opens**

Run:
```bash
cd Builds/Android && ./gradlew :app:assembleDebug \
  && adb install -r app/build/outputs/apk/debug/app-debug.apk \
  && adb shell am start -n com.namplayer.app/com.rmsl.juce.JuceActivity
adb logcat -c; sleep 2; adb logcat -d | grep -iE "juce|oboe|AAudio|namplayer" | tail -40
```
Expected: app launches, grants the mic permission prompt, shows the status screen with a device name, SR, block size, and a latency figure; logcat shows Oboe/AAudio opening a stream, no fatal error. (Built-in mic/speaker is fine for this step — the iRig comes in Task 5.)

- [ ] **Step 6: Commit**

Run:
```bash
cd /Users/chris.harper/Development/nam_app
git add Source/app/android CMakeLists.txt Builds/Android/app/src/main/assets/model.nam
git commit -m "feat: Android minimal audio app — bundled model through ToneEngine + status/latency/gain UI"
```

---

### Task 5: On-device iRig HD X validation

Manual hardware validation — the actual goal. No code deliverable unless a fix is needed; deliverable is a **recorded latency/behavior finding**.

**Files:**
- Create: `docs/superpowers/notes/2026-08-04-irig-android-findings.md` (results)

- [ ] **Step 1: Connect the iRig HD X and confirm the OS sees it**

Plug the iRig HD X into the S24 Ultra's USB-C port; connect guitar to the iRig input, headphones/amp to its output. On the phone, confirm a USB-audio indication. Run:
```bash
adb shell dumpsys media.audio_flinger | grep -iE "usb|device" | head
```
Expected: a USB audio device is present in the audio HAL.

- [ ] **Step 2: Launch and confirm routing through the iRig**

Relaunch the app. In the status readout, confirm the current device reflects USB/AAudio and that plucking the guitar moves the `peak` readout. Run:
```bash
adb logcat -c; adb shell am start -n com.namplayer.app/com.rmsl.juce.JuceActivity
adb logcat -d | grep -iE "oboe|AAudio|usb|routed|deviceId" | tail -40
```
Expected: Oboe/AAudio opens streams routed to the USB device; guitar input registers on the meter; processed sound comes out the iRig output. **If input stays on the built-in mic:** try forcing a lower buffer / exclusive AAudio mode (Step 3), and note Samsung routing behavior.

- [ ] **Step 3: Measure round-trip latency and tune buffer/sample-rate**

Using the on-screen buffer-latency figure as a baseline, iterate: in `AndroidAudioApp` set the device's preferred sample rate (48 kHz) and try successively smaller buffer sizes via `deviceManager.getAudioDeviceSetup()` / `setAudioDeviceSetup()`; rebuild/redeploy; note the smallest buffer that runs xrun-free (watch `load` and audible dropouts). Optionally measure true round-trip by recording the iRig output looped to its input and comparing an impulse.

- [ ] **Step 4: Record findings and commit**

Create `docs/superpowers/notes/2026-08-04-irig-android-findings.md` capturing: whether the iRig routed for I/O, the device name AAudio reported, the smallest xrun-free buffer + resulting latency (ms/dir and round-trip if measured), CPU load through the bundled model, and any Samsung/OneUI quirks. Then:
```bash
git add docs/superpowers/notes/2026-08-04-irig-android-findings.md
git commit -m "docs: iRig HD X on S24 Ultra — Android latency/routing findings"
```
**This is the phase's answer:** a concrete judgment on whether the iRig path is usable, which unblocks (or reshapes) the full mobile port.

---

## Self-Review

- **Spec coverage:** Build approach A (Gradle→CMake) → Task 3. arm64 core cross-compile de-risk → Task 2. USB-audio routing/latency de-risk → Task 5. Minimal APK contents (bundled model, ToneEngine, minimal UI, no library/OAuth) → Task 4. Toolchain install → Task 1. Success criteria (core on arm64, APK launches, guitar through NAM in real time, latency figure) → Tasks 2/3/4/5. ✅
- **Non-goals honored:** no TONE3000/OAuth/library/iOS/CI in any task. ✅
- **Placeholder scan:** concrete commands + file contents in every code step; the one genuine unknown (JUCE Java glue wiring in Task 3 Step 5) is called out with a bounded fallback, not left vague. ✅
- **Type consistency:** `ToneEngine` methods used in Task 4 (`prepare`, `setModel`, `render`, `cpuLoad`, `outputPeak`, `setInputDb`, `setOutputDb`) match the existing header; `NamModel::load(path,sr,maxBlock)` matches the existing signature. ✅
- **Risk honesty:** Task 3 is flagged as highest-uncertainty with a fallback; the whole plan is marked interactive/hardware-bound (not for isolated subagents). ✅
