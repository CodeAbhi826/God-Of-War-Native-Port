<div align="center">

# God of War: Recomp

**An unofficial static recompilation of God of War (PlayStation 2, 2005) for PC/Linux and Android**

[License: GPL-3.0](LICENSE) · [Releases](../../releases)

</div>

> [!IMPORTANT]
> This is a non-commercial fan project for interoperability and preservation research. It is **not** affiliated
> with, endorsed by or sponsored by Sony Interactive Entertainment, Santa Monica Studio or any other rights
> holder. **This repository contains no game files, no game assets and no generated game code.** The APKs in
> Releases contain the game's code translated to ARM64, but none of its data, textures, audio, video, models,
> artwork or logos. They do nothing without a disc image that you extract from your own legally obtained copy of
> the game. See [Legal](#legal).

## Table of contents

- [Overview](#overview)
- [How it works](#how-it-works)
- [Project status](#project-status)
- [Requirements](#requirements)
- [Building from source](#building-from-source)
- [Running on Android](#running-on-android)
- [Repository layout](#repository-layout)
- [Known issues and roadmap](#known-issues-and-roadmap)
- [Contributing](#contributing)
- [Credits](#credits)
- [Legal](#legal)
- [License](#license)

## Overview

God of War: Recomp translates the MIPS R5900 code of the PlayStation 2 executable of **God of War (2005)**
into C++ ahead of time and compiles it into native applications for PC/Linux and ARM64 Android. There is no interpreter or JIT
for the game's EE code: the game's own instructions become native code, and the PS2 hardware around them is
provided by a small runtime.

The project is built on [PS2Recomp](https://github.com/ran-j/PS2Recomp) and on the God of War port work by
[Waffle565](https://github.com/Waffle565/God-Of-War-Native-Port). The Android application reads the game directly
from an ISO selected by the user; it never copies the disc image into the app and never bundles game data.

The current test device is a rooted Samsung Galaxy A20 (SM-A205F): 2× Cortex-A73 at 1.6 GHz, Mali-G71 MP2,
3 GB RAM, Android 14 and Vulkan 1.1.

## How it works

| Part | How it runs |
|---|---|
| Game code (EE, R5900) | Recompiled ahead of time: the MIPS instructions of the game ELF become C++, built into a native library. There is no EE interpreter or JIT. |
| VU0 / VU1 | VU0 macro instructions are translated with the EE code. VU1 microprograms are handled by the runtime and are being moved to ahead-of-time, NEON-friendly translations for phone performance. |
| IOP (R3000A) | The original IRX modules run on a small virtual IOP with HLE fallbacks for the CD/DVD, SIF, file and pad services. |
| GPU (GS) | A native Vulkan 1.1 backend is being implemented for the PS2 Graphics Synthesizer. The desktop port uses a native OpenGL backend; Android will not use a GLES compatibility path. |
| Disc | The Android launcher opens the user's ISO through the Storage Access Framework. The native runtime reads it in place through `/proc/self/fd/<fd>` and extracts only the small boot ELF (about 2 MB) into the app's private files directory. |
| Audio, input, video | Pad2 input is bridged to the host; audio and FMV playback are part of the ongoing runtime work. |
| Android shell | A Java launcher (`MainActivity`) checks the ISO9660 volume and starts a `NativeActivity` (`GameActivity`). The native runner lives in `libps2EntryRunner.so`. |

The public source tree contains the recompiler, the runtime, the Android shell and the game-specific hooks. The
game ELF itself is not public. A maintainer's private builder holds only the user's own ELF and the function map
needed to generate the game code; the generated C++ is produced by CI and is never committed.

## Project status

**Work in progress. There is no playable release yet.**

What is already in place:

- An ARM64 Android application that builds entirely in GitHub Actions.
- A Storage Access Framework ISO picker, ISO9660 volume validation, and in-place disc reads.
- Extraction of the boot ELF from the user's ISO into the app's private files directory.
- A native runtime and recompiler that build for `arm64-v8a` with the Android NDK.
- A Vulkan 1.1 device and extension probe on the stock Mali driver, with results in `adb logcat -s ps2x-vk`.
- A verified full USA build on the A20: the APK installs, opens the ISO, extracts `SCUS_973.99` and executes the generated game code. It currently reaches the GS/DMA wait without presenting a frame because the Android GS backend is still missing.
- The USA game configuration and the game-specific override hooks for `SCUS-97399`.

The current blockers are:

- **A GS renderer for Android.** The desktop OpenGL backend is not available on Android, and the Vulkan backend
  is still being implemented. Without it the game cannot present a frame.
- **VU1 performance.** The interpreter is correct enough for bring-up, but the phone needs ahead-of-time
  microprograms or a dedicated VU1 thread to reach a useful frame rate.
- **Audio, FMV and save-game integration.**

The target is a stable 20+ fps on the A20's Mali-G71 MP2 at native resolution, with lower internal resolution and
frame pacing as fallbacks. Performance numbers will be published only after the renderer is complete and the
game is measured on the device.

## Requirements

**To play (when a playable build is published)**

- Android 9.0 (API 28) or later, ARM64 device.
- A GPU and driver with Vulkan 1.1 or later. The stock driver is supported; custom drivers are not required.
- 3 GB of RAM or more (4 GB recommended).
- About 9 GB of free storage for the ISO (the USA disc is 8.5 GB).
- Your own copy of **God of War (USA)**, serial `SCUS-97399`, dumped from your own disc.

The PAL release (`SCES-53133`) is the fallback target; the public configuration currently uses the USA map.

**To build**

- GitHub Actions is the supported build environment. The project is developed without local Android compiles.
- For the public shell APK: the workflow in `.github/workflows/android.yml`.
- For a full game build: a private builder repository containing your own ELF, plus the
  `android-full-usa` workflow in this repository. The private builder is connected with a read-only deploy key
  stored as the `PRIVATE_BUILDER_DEPLOY_KEY` secret.
- Toolchain used by CI: Ubuntu, JDK 17, Android SDK 34, NDK 28.2.13676358, CMake 3.22.1 and Gradle 8.9.

## Building from source

The public repository alone produces the **shell APK**: the Android application, the runtime and an empty
function table. That build is useful for checking the UI, the ISO picker and the Vulkan probe, but it cannot run
the game.

A full build needs the game's ELF and a function map. They are deliberately not in this repository:

1. Create a private copy of the builder layout used by the maintainer. It needs:
   - `game/SCUS_973.99` — the boot ELF extracted from your own USA ISO;
   - `port-usa/gow-usa.toml` and `port-usa/functions-usa.csv` — the USA recompiler configuration;
   - `helpers/gow_pad2_packet.h` — the libpad2 packet helper.
2. Add the public key of a read-only deploy key to the private builder, and store the private key in this
   repository as `PRIVATE_BUILDER_DEPLOY_KEY`.
3. In **Actions → android-full-usa → Run workflow**, start the build. It:
   - checks out this repository's `android` branch and the public `port/` map from `main`;
   - checks out the private builder and uses only its ELF, USA map and pad2 helper;
   - builds `ps2_recomp` on the runner;
   - recompiles the ELF into C++ (the generated code is not committed);
   - syncs the generated code and the USA overrides into the runner;
   - builds the APK with `-Pps2xGameSerial=SCUS_973.99`;
   - verifies that the APK contains no ISO, ELF, IRX or PAK files;
   - uploads the APK as a temporary workflow artifact for device testing.
4. Verify the APK on a device before publishing it. The release notes must include the original
   own-copy notice and the SHA-256 of the APK.

Do not commit generated code, the ELF, the ISO or extracted game files. Do not attach them to issues or pull
requests.

The shell build is:

```sh
cd android
gradle assembleRelease
```

The APK is written to `android/app/build/outputs/apk/release/app-release.apk`.

## Running on Android

A playable build will appear in [Releases](../../releases) when the GS renderer and the boot path pass device
testing. To run a build:

1. Install the APK:

   ```sh
   adb install -r app-release.apk
   ```

   Builds signed with different keys cannot update each other; uninstall the previous copy first.

2. Start **God of War**. The launcher asks for your disc image. Choose your own `God of War (USA).iso` and grant
   the file access the picker requests.
3. The launcher checks the ISO9660 volume, writes the disc descriptor and the boot-ELF path, and starts the game.
   The ISO stays where it is; only the boot ELF is extracted into the app's private files directory.
4. For diagnostics:

   ```sh
   adb logcat -s ps2x ps2x-vk
   ```

The APK is `arm64-v8a` only. It does not include a custom Vulkan driver and does not require root for normal
play. Root is used only on the maintainer's test device for logs and installation.

## Repository layout

| Path | Contents |
|---|---|
| `android/` | The Android application: launcher, `NativeActivity`, manifest and Gradle configuration |
| `ps2xRuntime/` | Guest memory, EE/VU execution, syscalls and hardware stubs, GS frontend and native backends |
| `ps2xRecomp/` | MIPS R5900 to C++ code generator |
| `ps2xAnalyzer/` | ELF analysis and Ghidra function-map workflow |
| `ps2xIOP/` | R3000A IRX execution and IOP HLE services |
| `port/` | Recompiler configuration, function maps and game-specific overrides (on `main`) |
| `.github/workflows/` | Public shell build and the full USA build |
| `tools/` | Disc, analysis and development helpers |

## Known issues and roadmap

- **No GS renderer on Android yet.** This is the main blocker. The backend must cover the PS2's destination-alpha
  test, wrapping/soft blend modes and Z-in-color-memory without sampling an attachment being written; Mali lacks
  `VK_EXT_rasterization_order_attachment_access`, so some effects need two-pass or ping-pong paths.
- **VU1 performance.** The interpreter is the current fallback; AOT microprograms and a dedicated VU1 thread are
  planned. The VU1 is the largest CPU cost in the game's scenes.
- **Audio and FMV.** The IOP/SPU2 path and the MPEG decoder are not complete.
- **Save games and memory cards.** Not implemented on Android yet.
- **Mali driver coverage.** The target is Vulkan 1.1 with no dynamic rendering, no timeline semaphores, no
  descriptor indexing and at most four bound descriptor sets.
- **No custom driver packaging.** The stock driver is the baseline; a Turnip-style package may be added later for
  Adreno devices, not for the A20.

## Contributing

Bug reports and pull requests are welcome. Please:

- never attach or link game files, ISOs, ELFs, IRX modules, generated code or unofficial builds;
- do not ask for game files or builds: those issues are closed;
- describe your device (model, GPU, Android version, Vulkan version) and attach the launcher diagnostics or
  `adb logcat` output when reporting a problem;
- keep changes to the runtime, renderer and Android shell in the public tree; game-specific data stays in the
  private builder.

## Credits

This project exists thanks to the work of:

- **[Waffle565/God-Of-War-Native-Port](https://github.com/Waffle565/God-Of-War-Native-Port)**: the God of War
  recompilation, the runtime and the game-specific hooks this Android port is based on.
- **[PS2Recomp](https://github.com/ran-j/PS2Recomp)** by ran-j and contributors: the PS2 static recompiler and
  runtime.
- **[PCSX2](https://pcsx2.net)**: the VU, GS and IOP research that much of the runtime is derived from.
- **[KIexster/god-of-war-recomp](https://github.com/KIexster/god-of-war-recomp)**: the USA function map and
  recompiler configuration used by the private builder.
- **[raylib](https://www.raylib.com)**: the host window, input and audio layer on desktop and the Android glue.
- **[victorgbd/NFSMW-Recompiled-Mobile](https://github.com/victorgbd/NFSMW-Recompiled-Mobile)**: the public-APK
  distribution model this release follows.
- The authors of the libraries listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

God of War was created by Santa Monica Studio and published by Sony Computer Entertainment. This is an
unofficial fan project; the original game and its trademarks belong to their respective owners.

## Legal

- **No game content in the repository.** This repository contains only original source code and open-source code
  under the licenses listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). It does not contain, and will
  never contain, the game's executable, data files, assets, audio, video, artwork, logos or the C++ generated
  from the game.
- **Releases.** The APKs published in Releases contain the game's code translated to ARM64, but none of its data
  files, assets, audio, video, artwork or logos. They do nothing without a disc image the user extracts from
  their own copy. They are free of charge and are provided only for owners of the game.
- **Own copy required.** You must own a legitimate copy of God of War (USA), serial `SCUS-97399`, and use your
  own dump. This project does not provide, and does not explain how to obtain, copies of the game.
- **Do not redistribute the APK.** The APK contains translated game code. Download it only from this
  repository's Releases for your own devices; do not mirror it, publish it elsewhere or include it in another
  product.
- **No Sony code.** This project does not include code from the PlayStation 2 SDK or any other proprietary Sony
  software.
- **Trademarks.** God of War, PlayStation and PS2 are trademarks of Sony Interactive Entertainment. All other
  trademarks belong to their respective owners. They are used only to identify the game and platform this
  software is compatible with; no endorsement is implied.
- **Not for commercial use or piracy.** Do not sell this project or builds of it, and do not use it to
  distribute or play unauthorized copies of the game.
- **Takedown requests.** If you are a rights holder and believe something in this repository or its Releases
  infringes your rights, please open an issue or contact the maintainer, and it will be reviewed and removed
  promptly.
- **No warranty.** This software is provided "as is", without warranty of any kind, as stated in sections 15
  and 16 of the GPL-3.0. Use it at your own risk.

## License

God of War: Recomp is free software, licensed under the
[GNU General Public License v3.0](LICENSE). Third-party components keep their own licenses, listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
