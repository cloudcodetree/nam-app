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

## Release builds (Task 6, docs/business/play-release-checklist.md)

```
cd Builds/Android
JAVA_HOME=/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home ./gradlew bundleRelease
```

- `versionCode`/`versionName` live in `Builds/Android/app/build.gradle`
  (`defaultConfig`); bump `versionCode` every upload.
- Signing is a `hasProperty` no-op: `signingConfigs.release` only exists (and
  `buildTypes.release` only attaches it) when all four
  `NAMPLAYER_UPLOAD_STORE_FILE/STORE_PASSWORD/KEY_ALIAS/KEY_PASSWORD`
  properties are present in `~/.gradle/gradle.properties` (never in the
  repo). No keystore on the machine → `bundleRelease` still reports
  **BUILD SUCCESSFUL**, producing an *unsigned* `.aab` at
  `app/build/outputs/bundle/release/app-release.aab` — good enough to prove
  the build graph but not uploadable to Play Console. Verified both states
  are reachable; only the unsigned path has been exercised so far (no
  keystore generated yet).
- **RelWithDebInfo applies to release too**, not just debug: the CMake
  arguments (`-DCMAKE_BUILD_TYPE=RelWithDebInfo`) live in `defaultConfig`,
  and neither `buildTypes.debug` nor `buildTypes.release` overrides them —
  confirmed by inspecting `app/.cxx/RelWithDebInfo/*/arm64-v8a/CMakeCache.txt`
  (`CMAKE_BUILD_TYPE:STRING=RelWithDebInfo`) after both an `assembleDebug`
  and a `bundleRelease` run: both land in the same `RelWithDebInfo` `.cxx`
  folder, never a plain `Release`/`Debug` one. (A stale `app/.cxx/Debug/`
  folder from an earlier experiment predates this and is untouched by
  current builds — gitignored, harmless, safe to `rm -rf` if it's ever
  confusing.) Do not add a per-buildType `CMAKE_BUILD_TYPE` override.
- Keystore generation, Play Console steps, and the `pro_unlock` product
  setup are documented end-to-end in
  `docs/business/play-release-checklist.md`.

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
