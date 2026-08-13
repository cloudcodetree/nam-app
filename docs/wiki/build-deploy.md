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
- This AVD ships **no Google Play Services/Play Store** — `BillingClient`
  fails to bind and every `juce::InAppPurchases` call self-resolves to
  "not owned" within the same event-loop tick as the activity's first
  frame, independent of `svc wifi/data enable|disable`. So it can't hold
  a simulated "offline returning Pro user" state long enough to eyeball —
  the async correction always wins the race before a screenshot lands.
  To test Pro-entitlement UI in isolation, hand-seed the cache. `adb
  shell` joins ALL of its arguments with plain spaces before handing
  them to the device's `sh -c` — so the whole `run-as ...` script must
  arrive as a single already-quoted argument, or the device shell parses
  your `&&`/`>` itself and the write silently no-ops. Wrap the entire
  thing in one pair of double quotes (verified working verbatim,
  `adb -s emulator-5554` swap for your device):

  ```
  adb shell "run-as com.namplayer.app sh -c 'mkdir -p \"NAM Player\" && echo \"{\\\"pro\\\": true}\" > \"NAM Player/entitlement.json\"'"
  ```

  ...then temporarily short-circuit `AndroidAudioApp::initBilling()`
  right after its cache-seed/refresh (never commit the stub) — real
  offline-Pro billing behavior needs a device with Play Services
  actually installed.
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
