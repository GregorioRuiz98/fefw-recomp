# FE: Fortune’s Weave Recomp — local preparation tools

An experimental static recompilation project for Windows x64, using nxrecomp from [pokopia-recomp](https://github.com/drakolordx7/pokopia-recomp/tree/2c29515b23a6d78d967775fd7e9f24f50402cca7) and the complete runtime and shader tools from [Goldoire/fefw-recomp v0.1.5](https://github.com/Goldoire/fefw-recomp/tree/a538375a80070e8a6bf5476b03b3dd877a13ea9d). These tools translate and compile code and shaders locally from a folder supplied by the user.

**Credits:** [Goldoire](https://github.com/Goldoire/fefw-recomp) provides the current Fire Emblem runtime and graphics/shader implementation; [drakolordx7](https://github.com/drakolordx7/pokopia-recomp) provides the nxrecomp/Pokopia foundation, with additional Pokopia lineage from [featureJosh](https://github.com/featureJosh/pokopia-recomp). [Kompas9](https://github.com/Kompas9/Switch-Game-Patches) provides the referenced 60 FPS patch. [GregorioRuiz98](https://github.com/GregorioRuiz98/fefw-recomp) maintains this integration, its Win32 configurator and public packages. See the detailed credits below.

**This distribution includes tools, source code and patches. It does not include the game.** You need a lawfully obtained copy and the rights required for the copying, extraction, transformation and use you perform. Owning the game does not guarantee that every such activity is permitted in your jurisdiction. See [LEGAL.md](LEGAL.md).

This project is not affiliated with, sponsored by or approved by Nintendo, Intelligent Systems or any other rights holder. Fire Emblem and Nintendo are named to identify compatibility; their trademarks and other rights belong to their respective holders.

The game and setup tool use the project's original **Woven Shield** application icon. It contains no game artwork; its provenance and complete Windows resource source are included in the package.

**Android development:** the separate [Android diagnostics app](https://github.com/GregorioRuiz98/fefw-recomp/tree/codex/public-release/android) now integrates application services, Android focus events, clocks, coherent shared memory and read-only RomFS storage. **33 memory tests and 253 core tests pass**. A private six-module run completes SDK startup and reaches the game's actual `nnMain`, then stops at filesystem mounting. **The APK does not run the playable game yet**: selected-folder integration, saves, audio and game rendering remain pending. No game code or files are included; the Windows download below is separate.

## Download and open the setup tool

**[Download FEFW Recomp for Windows x64 — v0.1.0-preview.16](https://github.com/GregorioRuiz98/fefw-recomp/releases/download/v0.1.0-preview.16/FEFW-Recomp-Windows-x64-v0.1.0-preview.16.exe)** · [Release notes and downloads](https://github.com/GregorioRuiz98/fefw-recomp/releases/tag/v0.1.0-preview.16)

Download and run **FEFW-Recomp-Windows-x64-v0.1.0-preview.16.exe** from the release assets. This small launcher installs the reviewed setup, importer and sources in `%LOCALAPPDATA%/FEFW-Recomp`. It contains no game or commercial shaders. The ZIP remains available for a portable installation: extract the complete ZIP and open `fefw_setup.exe`.

1. Open **Browse → Import game file(s)** and select the base game together with its update, or choose **Browse → Choose extracted folder**.
2. Inputs: **NSP, XCI, NSZ, XCZ, NUP, NX and ZIP**, plus extracted folders containing `exefs` and `romfs`. ZIP archives must contain an extracted folder or supported container files. Only the supported title/build listed below is accepted; a file extension alone does not prove compatibility.
3. NSP/XCI/NSZ/XCZ require your own `prod.keys`; setup detects `%USERPROFILE%/.switch/prod.keys` or asks you to select it. An optional `title.keys` is read from the same folder. NUP/NX and extracted folders do not require keys. No keys are supplied or downloaded. Encrypted game-container end-to-end compatibility has not been validated with a real input in this release; missing-key handling is tested.
4. Import runs locally into a separate work folder, then setup verifies the title and supported build before preparing code and shaders. Imported files continue automatically to preparation and play. For an extracted folder, press **Save and play**.
5. Later starts reuse the generated executable and caches. Settings and saves remain in the installation; there is no first-run recompilation every time.

**A lightweight download is not a complete precompiled game.** The first preparation still requires Windows x64, Vulkan 1.3, the development tools listed below, disk space and an internet connection. It can take around 30–40 minutes or more depending on the PC and caches. The launcher prepares the shared .NET SDK locally when required; it does not silently install Visual C++ Build Tools or the Windows SDK. If a prerequisite is absent, Open log explains what must be installed.

The importer is an integration of [NxExtractor](https://github.com/siriusgoldsmith/NxExtractor/tree/9cd476ee21fcb5b9bc4f91c869a7363c52187238), using LibHac, ZstdSharp and NXFormat. Firmware patching and the converter GUI are not included. ZIP/NUP/NX import roundtrips use synthetic fixtures; original encrypted game containers are not represented as tested.

Preview.15 also fixes the update package allowlist so its stability document and importer files can be installed. When upgrading from preview.13 or earlier, download this release manually: the older updater cannot accept the new package layout. For an existing portable installation, extract the complete ZIP into the same folder after closing the game and setup; retain your existing saves, configuration and work folder. The single EXE uses its own stable AppData installation by default. The current public ZIP is tested through the actual standalone launcher, including preservation of game builds, saves and settings during installation and reinstallation. This does not certify compatibility with every future game/runtime version.

## Saved games from another build (preview.16)

In **Game files → Saved games → Import save from another build**, choose the other installation folder or its complete `save` folder. The importer reads the other build's configured `save_dir`, including custom paths, and copies all accounts, slots and metadata. Selecting one `Auto` file alone is insufficient.

Both games must be closed. The current saves and INI are backed up under `save-backups`; an independent imported copy goes under `save-imports` and becomes active only after verification. The source build is never modified. **Save and play** then continues using that copy. **Open active saved-game folder** shows its location. Future progress in the two builds is independent; importing does not synchronize them.

The game runtime and its cache marker remain unchanged from preview.15, so this save selector does not require a shader rebuild. Tests cover custom/relative paths, complete metadata, existing-save backups, atomic failure and long Windows paths. These checks do not guarantee compatibility with every future game version.

## Graphics runtime replacement

### Windows stability follow-up (preview.14 source)

The startup check now rejects missing, empty, very short or unreadable required startup resources before entering the game, including when setup is skipped. An incomplete RomFS copy could previously close the process with an access violation instead of explaining which file was missing. This is a limited startup check, not a complete integrity scan of the game.

Fresh preparation can also recover when the original Goldoire repository is unavailable. It tries [metrist38's mirror](https://github.com/metrist38/fefw-recomp) and [may22es's mirror](https://github.com/may22es/fefw-recomp), requires the same original commit `a538375a80070e8a6bf5476b03b3dd877a13ea9d`, and installs the checkout only after verification. Existing checkouts are preserved. The mirrors do not change the runtime's authorship or attribution.

These fixes do not establish full-game stability or resolve every crash report. For an unexpected closure, preserve `fefw.log` and `fefw_crash.txt` before launching again, and report the tools version, GPU/driver, selected options and exact scene. Review logs for personal paths before sharing them. See [PC stability notes](project/PC-STABILITY.md).

Preview.10 uses Goldoire’s complete v0.1.5 graphics and memory implementation, matching ASTC/BC7 converter, shader extractor and shader translator. The standalone configurator, controller-only gameplay, reconnection support and local settings workflow remain ours. This replaces the earlier collection of individual graphics patches.

**Preview.11 fixes the crash before the opening battle cinematic when starting a new game.** The host AV1 decoder now receives CPU-backed frames, and the player allocates all four buffers before registering them. Existing installations rebuild the local executable once; the shader cache and saves are reused.

An upgrade requires a local rebuild and a new shader cache. Preparation writes it to `shader_cache/goldoire-v015`; the older cache is preserved. The first run can still pause while textures and driver pipelines are prepared. Full-game rendering compatibility has not been established by this project. See [integration details](project/UPSTREAM-INTEGRATION.md).

## Package contents

- A standalone Win32 setup tool and its corresponding source code.
- Title configuration, preparation scripts and a patch for a pinned revision of the open-source engine.
- Original synthetic tests, publication checks and license notices.

The package does not distribute ROMs, keys, firmware, proprietary SDKs, game updates, audiovisual assets, commercial shaders, generated game C++ or the recompiled game executable. It provides no game download links or instructions for circumventing technical protection measures. The tools import supported user-supplied files or accept an extracted folder, and download only the declared development dependencies.

## Status

Preview.11 retains the complete Goldoire v0.1.5 runtime and matching shader tools introduced in preview.10, and adds its required movie-player hooks. The private local build has loaded an existing save and reached the prologue scene with readable dialogue. Automated checks also verify movie-player buffer selection/count and hook generation for fresh and resumed local builds, alongside the existing configuration, controller, audio, memory and filesystem checks.

CUDA reconstruction kernels found in the dump are handled by the runtime’s native reconstruction path. They are not required Vulkan shader programs. The cache preparation verifies all 11,977 NVN shaders separately and records its upstream source revision before allowing setup to finish.

**Reported save-reset issue:** deleting data through title screen → Extras → Delete save data may leave **Continue** visible; selecting it has been reported to close the game. The examined local crash log stopped in texture untiling during scene loading; preview.9 included a local unreadable-source upload guard; preview.10 replaces that renderer with the upstream implementation. The title-menu state is still being investigated. If the stale option appears after deletion, close and reopen the game before proceeding. Do not use this release as evidence that the menu issue is fixed.

These checks do not establish complete playability or equivalence with the console. Combat, saving/loading, extended stability and physical controller reconnection during gameplay remain outstanding. A USB DualShock 4 has been detected and its live axes read by the standalone configurator; automated virtual-device tests cover button input and reconnection. REV16 audio is experimental. Low internal resolution, reported text errors and flicker in other gameplay sequences remain under investigation. The memory and reconstruction changes are not a claim that all rendering issues are resolved.

## Requirements

- Windows x64 and a GPU/driver supporting Vulkan 1.3.
- Python 3.12 or later, Git, CMake, Ninja and LLVM with clang-cl.
- Visual C++ Build Tools and the Windows SDK installed beforehand.
- A connection for preparing dependencies and space for generated code, objects and caches.
- A controller recognized by Windows and SDL.
- A supported user-supplied game container, or a compatible extracted folder containing `exefs` and `romfs`. Encrypted containers require user-supplied keys.

If .NET SDK 10 is unavailable, version 10.0.403 is prepared locally and its SHA-512 is verified. No services are installed and the permanent PATH is not changed. Initial preparation can take a considerable amount of time.

The supported Title ID is `04007EF01F8AE000`, with main Build ID `DB93B0125F4850601D61C3EDDE012F8EB2ECFE77`. Fingerprints are recorded in `project/supported-build.json`; a folder name alone does not establish compatibility. An update-only folder may require base-game resources, which these tools do not download.

## Using the setup ZIP

1. Extract the release into a local folder.
2. Open `fefw_setup.exe` and select the folder containing `exefs` and `romfs`, or its parent folder containing supported updates.
3. Choose the game language, display mode, frame-rate target, game output, internal scale and 3D reconstruction. Only languages found in your copy are offered.
4. Allow preparation to finish, then use **Save and play**. You can cancel and resume; **Open log** opens `preparation.log` for details.

If preview.3 stopped while building `libvpx` in a folder containing spaces, close setup and extract the current release into the same package folder. Keep the existing `work` folder, reopen setup and retry **Save and play**. The corrected dependency build uses relative source paths; the original game folder can stay where it is. Preparation resumes, but a first installation still needs to compile the game and shaders.

**Skip setup on startup** allows subsequent launches through `fefw.exe`. The setup tool remains available and opens again if required resources are missing. Existing controller mappings and local saves in `save` are preserved.

Setup uses a dark sidebar inspired by Goldoire's launcher, with **Game files**, **Picture**, **Performance**, **Window**, **Controllers**, **Audio**, **Startup**, **Updates** and **Tools** sections. The window can be resized; smaller windows scroll the settings while keeping Save and Save and play visible. Use **Ctrl+Tab** or **Ctrl+Shift+Tab** to switch sections. Hover over the game-folder field to read its full path. **Use recommended graphics** selects windowed 1280 × 720, TAAU, vertical synchronization and the original frame limit; these choices take effect only when saved. Stopping an active preparation asks for confirmation and keeps completed build work for the next attempt.

**Window resolution** controls presentation size. **Game output (WxH)** changes the engine's output independently; enter even dimensions from 640x360 to 3840x2160, or keep **Original game output**. **Internal scale per dimension** controls the 3D resolution before TAAU: 50% of 1920x1080 is 960x540. Above 1440p output, the runtime caps internal rendering to half the output per dimension to stay within the title's existing render-target pool. Larger settings require more GPU memory. These options need a restart.

**Borderless** uses the desktop; **Fullscreen (exclusive)** requests the runtime's SDL fullscreen mode and may fall back to borderless if unavailable. The initial configuration enables vertical synchronization. See the [controller guide](project/CONTROLES-MANDO.md).

**3D reconstruction → Temporal anti-aliasing (TAAU)** combines successive 3D frames using the game's motion and depth data, before the UI is composed. **Bilinear** provides a comparison with temporal history disabled. Save and restart the game after changing it. This is a built-in host upscaler, not NVIDIA DLSS. It does not increase the game's original rendering resolution or promise higher FPS.

The runtime searches for controllers at startup and checks again every second. To recover a DualShock 4 or another device, use **F1 → Settings → Controls → Gamepad → Rescan controllers**. Gameplay requires a controller. Keyboard and mouse are used only for the setup tool, host settings and shortcuts; they do not send game actions, even with old bindings in `controls.ini`. Close F1 to return controller input to the game.

**Preview.12: PlayStation button symbols.** With **F1 → Settings → Controls → Button prompts → Auto**, DualShock 4 and DualSense use Cross/Circle/Square/Triangle, L1/R1 and L2/R2 in the game's font-based prompts. Symbols follow **Gamepad → Buttons by label / Buttons by position** and custom remaps. You can explicitly choose PlayStation, Xbox or the original Switch glyphs. The game fonts are read and patched locally in memory; they are not distributed or modified on disk.

The footer shows whether there are **Unsaved changes**. **Revert** restores the form without changing saved settings. Picture shows the resulting 3D input and output dimensions for a custom resolution; Performance explains the selected target. Pending changes survive section switches. Closing with unsaved changes asks whether to discard them.

## Controller testing, audio and portable preferences

Open **Controllers** to select a connected device, rescan and see raw stick, trigger and button readings. Detection refreshes every second while setup is open. Both stick deadzones can be adjusted independently from 0% to 50%. **Save controller profile** stores the two values for the selected SDL hardware GUID; **Load controller profile** recalls them as pending changes. Press **Save** to use them in the game. Identical controller models can share a GUID and therefore a profile. Profiles do not automatically change the active controller or bindings during gameplay.

**Audio** provides master volume from 0% to 100% and **Mute when the game loses focus**. Save and restart the game to apply these choices. The existing audio device, bindings and unrelated runtime options are preserved.

**Picture → Graphics preset** offers Performance (1280x720 output, 50% internal scale), Balanced (1920x1080, 66.67%) and Quality (2560x1440, 100%). All three use TAAU, VSync and original frame timing. The nearby summary shows the resulting internal resolution. These are starting points; Quality can cost substantially more GPU time and memory. They are not benchmarked FPS guarantees.

In **Tools**, choose **English** or **Español** for setup independently of the game language. **Export settings** writes a versioned INI profile with graphics, audio, deadzones and launcher preferences. **Import settings** validates it and leaves changes pending for review. Neither operation includes game-folder locations, game language, saves or controller bindings. **Reset section** resets the current section only; **Revert** restores the whole unsaved form.

**Export diagnostics** creates a small INI report containing tool version, readiness flags, graphics choices, RAM size, controller count and a display hardware ID when available. It does not collect logs, screenshots, game files, saves, personal paths or controller serial numbers. Review the report before sharing it. Nothing is uploaded automatically.

**Game files → Preparation stages** distinguishes completed or reused stages, the current stage and remaining stages. The indicator describes work stages, not elapsed-time percentage. An interrupted attempt reuses completed work after validation; first preparation can still take around 30–40 minutes.

## Frame-rate targets

In **Performance → Frame-rate target**, choose **Original (30 / 60 FPS)**, **60 FPS** or **120 FPS**, then save and restart. Original is the default. Higher targets are experimental; they do not guarantee sustained performance or correct behavior throughout the game.

The 60 FPS option retains both checked changes from [Kompas9's patch](https://github.com/Kompas9/Switch-Game-Patches/tree/9e553571c986fbd45ef55202cb8ec0e1179a3db9/games/Fire-Emblem-Fortunes-Weave). The 120 FPS option adapts Goldoire's native hooks: gameplay target 120, display interval accounting at 120 Hz, menus retaining their target of 60, and correction of accumulated timing error when actual frame times vary. Movies retain their encoded frame rate. Hook instructions and the supported Build ID are checked before enabling the feature.

The host limiter respects both the selected ceiling and the game's current presentation interval. `fps60` and `fps120` in `[mods]` are mutually exclusive. The options affect the private mapped image and native hooks, preserving the original game folder. An INI change alone cannot add these features to an older executable.

## Project updates

Open **Updates → Check now** to read the changes in a newer compatible package. **Install update and restart setup** closes setup, downloads only from this repository's release assets, checks GitHub's SHA256 digest and the package's file manifest, and applies the tools update with rollback. Save pending settings and close the game first. **Check for updates when setup opens** is optional and off by default; it checks without installing automatically.

Updates preserve the local game executable, game files, INIs, saves, shader caches and build workspace. Backups remain under `work/updates/`. A new runtime can require a local rebuild: the reopened setup detects that and resumes preparation when you save. Interrupted setup reuses completed translation/build work. Network or verification failures leave the existing installation available.

For older packages without the updater, close the game, extract the entire new ZIP into the same folder and open `fefw_setup.exe`. No commercial game data is included in either update method.

## Building the setup tool from source

Building the standalone setup tool does not require game files. From the repository root, in PowerShell:

```powershell
. .\project\tools\Initialize-BuildEnv.ps1
cmake -S project -B work/setup -G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_BUILD_TYPE=Release -DFEFW_BUILD_GAME=OFF
cmake --build work/setup --target fefw_setup fefw_config_tests fefw_mods_tests --parallel 1
ctest --test-dir work/setup --output-on-failure
Copy-Item work/setup/fefw_setup.exe ./fefw_setup.exe
.\fefw_setup.exe
```

Continue to the next step only after the previous one succeeds. Sources and resources are in `project/src/`, and the build is defined in `project/CMakeLists.txt`. The standalone setup tool links the same pinned SDL controller library as the game, built from verified sources. It does not link the game runtime or translated game code. An internet connection or the cached SDL archive is needed for the first build.

The game executable and its caches are generated later on your own machine. Do not attach them to issues, repositories, releases or CI artifacts.

## Licenses, credits and contributions

### Main contributors and upstream projects

| Contributor / project | Contribution used here |
| --- | --- |
| [Goldoire / fefw-recomp](https://github.com/Goldoire/fefw-recomp/tree/a538375a80070e8a6bf5476b03b3dd877a13ea9d) | The complete v0.1.5 runtime, Vulkan renderer, memory-pool implementation, texture converter and shader extraction/translation tools. Also the built-in temporal reconstruction, movie-player and FPS-hook work, font-based controller symbols, and launcher design that informed this configurator. Original code and notices are retained. |
| [drakolordx7 / nxrecomp](https://github.com/drakolordx7/nxrecomp) and [pokopia-recomp](https://github.com/drakolordx7/pokopia-recomp/tree/2c29515b23a6d78d967775fd7e9f24f50402cca7) | The static recompiler, original runtime/tooling foundation and pinned base used for local translation. |
| [featureJosh / pokopia-recomp](https://github.com/featureJosh/pokopia-recomp) | Additional Pokopia runtime, tools and launcher lineage acknowledged by Goldoire's upstream project. |
| [Kompas9 / Switch-Game-Patches](https://github.com/Kompas9/Switch-Game-Patches/tree/9e553571c986fbd45ef55202cb8ec0e1179a3db9/games/Fire-Emblem-Fortunes-Weave) | The checked 60 FPS patch locations and findings used by this integration. The upstream runtime also acknowledges Kompas9's ultrawide patch. |
| [GregorioRuiz98 / this repository](https://github.com/GregorioRuiz98/fefw-recomp) | Title/build validation, local preparation and packaging, the standalone Win32 configurator, controller-only policy and discovery adaptations, settings/update workflow, integration fixes and verification. |

### Dependencies and inherited work

The recompiler and runtime also incorporate or acknowledge work from **BelmanteGu / SuperMarioWonderRecomp**, **smo-recomp**, **ScarletNX**, **ssbu-recomp**, **seedds / nxnative**, **Ryujinx / Ryubing**, **Mesa NAK**, **yuzu / Eden**, **libnx** and **LLVM libc++abi**. Their original attribution and license scope remain in the upstream notices.

Thanks to the maintainers of **SDL**, **Vulkan-Headers**, **volk**, **VulkanMemoryAllocator**, **Dear ImGui**, **Zstandard**, **dav1d**, **libvpx**, **ARM astc-encoder**, **bc7enc_rdo**, **ISPC** and **DejaVu fonts**. The current graphics runtime also carries notices for the AMD and NVIDIA open-source headers or components it uses. This list describes the build's dependency lineage; the standalone setup executable links only its declared subset.

Full component notices, license texts and pinned source references are in [THIRD_PARTY_NOTICES.md](https://github.com/GregorioRuiz98/fefw-recomp/blob/codex/public-release/THIRD_PARTY_NOTICES.md), [NOTICE.md](https://github.com/GregorioRuiz98/fefw-recomp/blob/codex/public-release/NOTICE.md) and [project/licenses](https://github.com/GregorioRuiz98/fefw-recomp/tree/codex/public-release/project/licenses). Current runtime notices are preserved separately from the earlier Pokopia notices.

### Development assistance and original icon

OpenAI Codex assisted with implementation, investigation, documentation and verification in this integration. The **Woven Shield** application icon was generated with OpenAI's image generation tool without game files or reference artwork. Its complete exported source and provenance are in [fefw_icon.json](https://github.com/GregorioRuiz98/fefw-recomp/blob/codex/public-release/project/src/fefw_icon.json) and [APP-ICON.md](https://github.com/GregorioRuiz98/fefw-recomp/blob/codex/public-release/project/src/APP-ICON.md).

The base project’s [GNU GPL v3](LICENSE) and each file’s license notices are retained, including the later-version option where applicable. Third-party components retain their licenses: see [provenance](NOTICE.md) and [attributions](THIRD_PARTY_NOTICES.md). These licenses cover the identified software and do not grant rights to the game.

When reporting an issue, describe the behavior, system, GPU and revision without attaching game material. Remove personal paths, identifiers and credentials from shared messages. See [CONTRIBUTING.md](CONTRIBUTING.md).

The repository contains source files only, with a manifest and an auditor for files and Git history. The release ZIP adds the standalone setup binary alongside its corresponding source. See [publication instructions](GITHUB-PUBLISH.md). These checks reduce the risk of accidental disclosure; they do not certify the legality of every use or replace legal advice.
