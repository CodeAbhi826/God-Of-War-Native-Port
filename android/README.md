# Android runner

The Android application is a Java launcher (`MainActivity`) plus a
`NativeActivity` (`GameActivity`). The launcher uses the Storage Access
Framework to open the user's ISO, checks the ISO9660 primary volume descriptor,
and passes the open file descriptor and the boot-ELF path to the native runner.
The ISO is read in place; only the boot ELF is extracted into the app's private
files directory.

The project-level [README](../README.md) documents the build model, the
requirements and the release policy. The short version:

- The public tree builds a shell APK. It contains no game data and no generated
  game code.
- A full build needs the user's own `SCUS_973.99` ELF in the private builder and
  is produced by the `android-full-usa` GitHub Actions workflow.
- The APK is `arm64-v8a` only, targets Android 9+ and Vulkan 1.1.

## Build

The CI uses JDK 17, Android SDK 34, NDK 28.2.13676358, CMake 3.22.1 and Gradle
8.9. The shell APK is built by `.github/workflows/android.yml`; the full USA
build is `.github/workflows/android-full.yml`.

For reference, the Gradle command is:

```sh
cd android
gradle assembleRelease \
  -Pps2xGameSerial=SCUS_973.99 \
  -Pps2xBootElf=/storage/emulated/0/Android/data/com.ps2x.runner/files/boot.elf
```

APK output: `android/app/build/outputs/apk/release/app-release.apk`.

## Logs

The native runtime redirects its standard output and error to logcat. Use:

```sh
adb logcat -s ps2x ps2x-vk
```

The app does not require root to run. Root on the maintainer's test device is
used only for installation and diagnostics.
