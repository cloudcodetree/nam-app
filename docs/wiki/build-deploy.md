# Build & deploy

## Android (primary target)

```
cd Builds/Android
JAVA_HOME=/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home ./gradlew assembleDebug
```

- Native code builds **RelWithDebInfo** — a -O0 NAM is 5–15× too slow for
  real time. APK: `app/build/outputs/apk/debug/app-debug.apk`.
- Gradle shell wraps the repo-root CMake; every new `.cpp` must be added to
  the CMake target list or it silently never links.
- Emulator: `nam_test` AVD (arm64, 320×640) from
  `/opt/homebrew/share/android-commandlinetools/emulator`; grant
  RECORD_AUDIO (`pm grant com.namplayer.app android.permission.RECORD_AUDIO`)
  or the duplex stream dies. NAM can't run real-time under QEMU — auditions
  pre-render offline there.
- Phone (Samsung S25 Ultra): wireless adb; the connect port changes every
  session — use the `/adb-qr` skill (PNG QR + auto-pair watcher).
  `adb -s <ip:port> install -r <apk>`.

## Desktop + tests

```
cmake --preset default && cmake --build --preset default --target nam_tests
./build/tests/nam_tests
```

- Presets: default / debug / asan / tsan. The headless suite covers the
  JUCE-free core (TDD there — test first; see CLAUDE.md).

## Formatting / linting

- `clang-format` (root config; JUCE-spacing override in `Source/app/ui/`) is
  blocking in the pre-push hook. `clang-tidy`
  (bugprone/concurrency/performance/function-size) runs advisory via the
  Android compile DB. Tree reformat commit is in `.git-blame-ignore-revs`.
