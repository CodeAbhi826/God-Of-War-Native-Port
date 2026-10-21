# Third-party notices

God of War Native Port is licensed under the GNU General Public License v3.0
(see `LICENSE`). The following components are included in, fetched by, or
linked into the project. Each component keeps its own license; the license
text shipped with the component governs.

| Component | Use | License |
|---|---|---|
| [PS2Recomp](https://github.com/ran-j/PS2Recomp) | PS2 static recompiler and runtime base | GPL-3.0 |
| [God-Of-War-Native-Port](https://github.com/Waffle565/God-Of-War-Native-Port) | God of War runtime and game hooks | GPL-3.0 |
| [PCSX2](https://github.com/PCSX2/pcsx2) | VU, GS and IOP reference code | GPL-3.0-or-later |
| [libmpeg2 / mpeg2dec](https://libmpeg2.sourceforge.net/) | MPEG-2 video decoder (vendored) | GPL-2.0-or-later |
| [raylib](https://github.com/raysan5/raylib) | Host window, input, audio and Android glue | zlib |
| [sse2neon](https://github.com/DLTcollab/sse2neon) | SSE to NEON translation on ARM | MIT |
| [ELFIO](https://github.com/serge1/ELFIO) | ELF parsing in the recompiler | MIT |
| [toml11](https://github.com/ToruNiina/toml11) | TOML configuration parser | MIT |
| [fmt](https://github.com/fmtlib/fmt) | String formatting | MIT |
| [Rabbitizer](https://github.com/Decompollaborate/rabbitizer) | MIPS instruction metadata | MIT |
| [KIexster/god-of-war-recomp](https://github.com/KIexster/god-of-war-recomp) | USA function map and recompiler configuration | GPL-3.0 |
| [Android Native App Glue](https://developer.android.com/ndk) | Android `NativeActivity` entry point | Apache-2.0 |

`Dear ImGui` and `rlImGui` are fetched only for the optional desktop debug UI and
are not part of the Android APK. `libdwarf` is used only by the optional
analyzer/studio tools. They are not listed as runtime dependencies of the
Android build.

If you believe a component is missing from this list, please open an issue with
the component name, version and license.
