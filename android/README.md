# FEFW Android — platform diagnostics

**This APK does not run Fire Emblem yet.** It is the first installable Android porting milestone, built for `arm64-v8a` (the intended Galaxy S23 Ultra) with a separate `x86_64` build for emulator tests. No game executable, game code, shaders or assets are included. The Windows release remains separate.

Version **0.1.4** links the [Android memory backend, ELF/NSO loader, ABI 3 module registry, dispatcher, hooks, guest exceptions and timer](runtime/README.md). Inside the app, **33 memory cases and 104 runtime-core cases pass**. The normal instrumentation has **23 checks**; an opt-in local-copy run passes **26 checks**, mapping all six actual game modules and analysing **287,544 relocations**. One TLS descriptor is preserved for the game's own dynamic linker; that linker and game code are **not executed** in this milestone. The memory backend requires **4 KB host pages**. The ARM64 APK is built; physical Galaxy execution remains unverified.

## Implemented

- A native C++20 library, built with Android NDK r28c, and a small Android application without third-party Java dependencies.
- A real Vulkan Android surface/swapchain test that clears and presents 120 frames with a 30 FPS target. It uses monotonic deadlines and reports submission rate, intervals and missed deadlines. These are colour-test measurements, not game FPS or on-screen presentation timestamps.
- GPU/API/feature diagnostics, including buffer device addresses and host-memory import support.
- Host page-size reporting, a real `memfd` shared-memory alias test and a non-destructive one-page probe at the guest main-module address.
- Android's folder picker with persistent read permission. Validation streams SHA-256 and checks Build IDs for all six supported ExeFS modules, and checks that a RomFS folder exists. It does not extract, copy or modify game files.
- **Test selected game modules** opens verified NSOs read-only through Android file descriptors, decompresses both LZ4 and ZBIC, maps the six original guest addresses and analyses relocations. It releases the mappings after testing. Final guest linking, guest services and rendering are still pending.
- **Test loader and dispatcher** exercises real synthetic ELF/NSO loading, TLS data, RELA/RELR, imports, constructors, ABI refusal, hooks, guest exception unwinding/type matching, timer registers, function replacement/cache invalidation and cleanup. Unimplemented imports raise a reported engine error instead of returning a fake success value.
- Controller discovery and hot-plug events, live axes/buttons and physical PlayStation button labels. Gameplay mappings still belong to the engine port that remains to be implemented.
- Explicit report export without the selected folder URI, game content or device serial number. No internet permission, background downloads or automatic uploads.
- The project's original Woven Shield icon and credits.

## Earlier emulator milestone (0.1.1)

Version **0.1.1** installs and opens on a local **Android 15 x86_64 emulator using WHPX** with a Galaxy S23 Ultra-shaped profile. All **16 instrumentation checks** pass, including synthetic NSO header rejection, native shared-memory aliases, the application/JNI bridge, invalid frame-rate rejection and 120 paced Vulkan colour frames. One measured run reached **29.95 submissions/s**, with a mean interval of **33.39 ms** and a p95 interval of **45.89 ms**. The p95 shows timer jitter in the VM; this is not evidence of stable game performance.

The profile has **12,288 MB RAM**, **1440 × 3088** pixels, a configured **120 Hz** display and a logical density of 560 dpi. Android reported about 11.7 GiB usable system RAM and the requested screen mode. Eight CPU cores were requested, but this emulator version supports at most six. It uses the host PC's **NVIDIA GPU through Vulkan 1.3**, not the Galaxy's Snapdragon/Adreno hardware or Samsung One UI. See Samsung's [S23 Ultra specifications](https://news.samsung.com/global/take-your-passions-further-with-the-new-samsung-galaxy-s23-series-designed-for-a-premium-experience-today-and-beyond) and Android's [AVD configuration guide](https://developer.android.com/studio/run/managing-avds).

The separate `arm64-v8a` APK is for the physical Galaxy; the accelerated PC emulator uses the `x86_64` APK. A device profile does not make their CPU/GPU performance equivalent. The ARM64 APK's signature, architecture and 16 KB alignment pass static checks; installation, controller behavior, pacing and graphics on the physical Galaxy S23 Ultra remain unverified. The complete game engine remains unavailable in both APKs.

The earlier 0.1.0 diagnostics passed 10 checks using SwiftShader/Vulkan 1.2. Its folder-picker flow, persistent read permission after restart, rejection of an empty folder and sanitized report export were also checked through Android's actual system UI. Those checks did not run the game.

On the test Windows host, the earlier software-CPU emulator crashed before boot; using the available WHPX accelerator allowed Android to boot without changing Windows features. See the official [emulator acceleration guide](https://developer.android.com/studio/run/emulator-acceleration).

## Build on Windows

Prerequisites: Python 3.12+, a working JDK 17+, CMake, Ninja, Android SDK platform 36/build-tools 36.0.0 and Android NDK **28.2.13676358**. The build performs no downloads and makes no global environment changes. Native compilation is capped at two concurrent jobs. Java 23 was used for the initial local build.

From this repository's root:

```powershell
python android/build.py --sdk <ANDROID_SDK> --ndk <ANDROID_NDK> --java-home <JDK> --project project --work work/android --abi arm64-v8a
```

The output is `work/android/arm64-v8a/FEFW-Android-Diagnostics-arm64-v8a-0.1.2.apk`. The build checks its signature and 16 KB ZIP alignment. Native ELF load segments are also linked with 16 KB alignment. Development signing uses a local test key in `work/android/android-diagnostics.p12`; keep it private and reuse it for updates. This is a development APK, not a production-signed store release.

The build follows the official [AAPT2](https://developer.android.com/tools/aapt2), [D8](https://developer.android.com/tools/d8), [apksigner](https://developer.android.com/tools/apksigner) and [16 KB page-size](https://developer.android.com/guide/practices/page-sizes) documentation. It does not require Gradle or Android Studio.

The x86_64 test build uses `--abi x86_64`. After installing it on a test emulator, the built-in instrumentation can be run with:

```text
adb -s <TEST_DEVICE> install -r <APK>
adb -s <TEST_DEVICE> shell am instrument -w io.github.gregorioruiz98.fefw/.SmokeTest
```

Default instrumentation uses synthetic fixtures and the real native library/window; it does not require game content. An optional developer run with `-e test_local_modules 1` reads six separately supplied NSOs from the app's external `files/loader-inputs` folder. Those files are never packaged or uploaded. Build success, emulator results and actual Galaxy results are separate checks. A compiled or signed APK is not evidence of full-game compatibility.

The 30 FPS setting currently controls **only the diagnostic Vulkan loop**. Android's frame-rate hint alone is not a limiter: the native loop also waits on absolute monotonic deadlines. Presentation semaphores are allocated per swapchain image, following [Khronos's synchronization guidance](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html), rather than draining the entire queue on each frame. Once the game engine is ported, its simulation timing and renderer need their own integration and validation at the requested 30 FPS target.

## What remains before the game can boot

1. Port guest thread scheduling, synchronization, SVC/services, guest exception handling, filesystems and audio. The memory backend, module loader and call/import dispatcher are integrated; the graphics write tracker is not ported yet.
2. Implement guest sub-page permissions/aliases on 16 KB hosts. The memory backend currently refuses those hosts explicitly; 16 KB ELF alignment is a separate installation property. Its 4 KB-host contract results do not establish 16 KB-host compatibility.
3. All six locally generated game modules now compile into private ARM64 archives. Complete the Android SVC/kernel services and game runtime to link and execute them. The current integration link stops on `guest_svc`; that one ABI entry represents the unported system-call subsystem, not a single missing game setting. A private archive compilation target is available using CMake `NXRT_ANDROID_LIFT_DIR` and target `nxrt_lifted_modules`; its outputs stay outside the diagnostics APK. An archive is not an executable game. The current Windows executable cannot be loaded on Android.
4. Port SDL/game lifecycle integration and the game's NVN/Vulkan renderer, including mobile-driver feature limits and an appropriate path when host-memory import is unavailable.
5. Prepare shaders and textures locally, integrate the movie/audio path, and test actual GPU memory use, input, saves and lifecycle on the Galaxy. Vulkan 1.3 alone does not guarantee all renderer features.

Android components are kept separate from the existing Windows runtime so that this port does not change the working desktop build. Memory contract tests compare real Android access behavior and kernel mappings; their success does not validate the unported systems above.

## Credits and licenses

This project's game/runtime foundation comes from **Goldoire**, **drakolordx7**, **featureJosh** and their upstream contributors; **Kompas9** supplies the referenced patch work. This milestone includes adapted nxrt memory/loader/registry code and tests, an Android memory backend, Android dispatcher/platform adapters and diagnostics. The ZBIC decoder comes from **kinnay/zbic**, which credits **Atmosphere-NX**, and retains its **Meta Platforms/Zstandard** notices. It is not a completed port of the game runtime. Exact provenance and adaptations are recorded in [runtime/README.md](runtime/README.md) and [ZBIC notices](runtime/third_party/zbic/README.md). Android integration is maintained by **GregorioRuiz98**, with OpenAI Codex assistance. The Woven Shield icon was generated for this project without game/reference artwork.

Project sources retain GPL-3.0-or-later notices. The APK carries the project GPL text, the NDK's component/toolchain notices and the ZBIC decoder's provenance/BSD/GPLv2 notices. Full upstream credits remain in the repository README and third-party notices. No rights to the commercial game are granted by these licenses.
