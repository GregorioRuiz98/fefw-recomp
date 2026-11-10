# FEFW Android — platform diagnostics

**This APK does not run Fire Emblem yet.** It is the first installable Android porting milestone, built for `arm64-v8a` (the intended Galaxy S23 Ultra) with a separate `x86_64` build for emulator tests. No game executable, game code, shaders or assets are included. The Windows release remains separate.

## Implemented

- A native C++20 library, built with Android NDK r28c, and a small Android application without third-party Java dependencies.
- A real Vulkan Android surface/swapchain test that clears and presents 60 frames. This tests presentation, not game rendering or FPS performance.
- GPU/API/feature diagnostics, including buffer device addresses and host-memory import support.
- Host page-size reporting, a real `memfd` shared-memory alias test and a non-destructive one-page probe at the guest main-module address.
- Android's folder picker with persistent read permission. Validation streams SHA-256 and checks Build IDs for all six supported ExeFS modules, and checks that a RomFS folder exists. It does not extract, copy or modify game files.
- Controller discovery and hot-plug events, live axes/buttons and physical PlayStation button labels. Gameplay mappings still belong to the engine port that remains to be implemented.
- Explicit report export without the selected folder URI, game content or device serial number. No internet permission, background downloads or automatic uploads.
- The project's original Woven Shield icon and credits.

## Build on Windows

Prerequisites: Python 3.12+, a working JDK 17+, CMake, Ninja, Android SDK platform 36/build-tools 36.0.0 and Android NDK **28.2.13676358**. The build performs no downloads and makes no global environment changes. Native compilation is capped at two concurrent jobs. Java 23 was used for the initial local build.

From this repository's root:

```powershell
python android/build.py --sdk <ANDROID_SDK> --ndk <ANDROID_NDK> --java-home <JDK> --project project --work work/android --abi arm64-v8a
```

The output is `work/android/arm64-v8a/FEFW-Android-Diagnostics-arm64-v8a-0.1.0.apk`. The build checks its signature and 16 KB ZIP alignment. Native ELF load segments are also linked with 16 KB alignment. Development signing uses a local test key in `work/android/android-diagnostics.p12`; keep it private and reuse it for updates. This is a development APK, not a production-signed store release.

The build follows the official [AAPT2](https://developer.android.com/tools/aapt2), [D8](https://developer.android.com/tools/d8), [apksigner](https://developer.android.com/tools/apksigner) and [16 KB page-size](https://developer.android.com/guide/practices/page-sizes) documentation. It does not require Gradle or Android Studio.

The x86_64 test build uses `--abi x86_64`. After installing it on a test emulator, the built-in instrumentation can be run with:

```text
adb -s <TEST_DEVICE> install -r <APK>
adb -s <TEST_DEVICE> shell am instrument -w io.github.gregorioruiz98.fefw/.SmokeTest
```

Instrumentation uses synthetic NSO headers and the real native library/window; it does not use game content. Build success, emulator results and actual Galaxy results are separate checks. A compiled or signed APK is not evidence of full-game compatibility.

## What remains before the game can boot

1. Port the Win32-dependent runtime: guest address reservations, shared/private aliases, thread scheduling, synchronization, exception handling, filesystems and audio.
2. Account for Android host pages that may be 16 KB while the guest exposes 4 KB pages. Passing this APK's one-page memory test does not validate the full guest address space or alias semantics.
3. Cross-compile and link all six locally generated game modules for ARM64, with the original ABI and checked hooks. The current Windows executable cannot be loaded on Android.
4. Port SDL/game lifecycle integration and the game's NVN/Vulkan renderer, including mobile-driver feature limits and an appropriate path when host-memory import is unavailable.
5. Prepare shaders and textures locally, integrate the movie/audio path, and test actual GPU memory use, input, saves and lifecycle on the Galaxy. Vulkan 1.3 alone does not guarantee all renderer features.

Native probes are kept separate from the existing Windows runtime so that work on Android does not change the working desktop build.

## Credits and licenses

This project's game/runtime foundation comes from **Goldoire**, **drakolordx7**, **featureJosh** and their upstream contributors; **Kompas9** supplies the referenced patch work. This milestone contains independently written Android platform diagnostics, not a completed port of that runtime. Android integration is maintained by **GregorioRuiz98**, with OpenAI Codex assistance. The Woven Shield icon was generated for this project without game/reference artwork.

Project sources retain GPL-3.0-or-later notices. The APK carries the project GPL text and the NDK's original component/toolchain notices, covering the linked C++ runtime and its dependencies. Full upstream credits remain in the repository README and third-party notices. No rights to the commercial game are granted by these licenses.
