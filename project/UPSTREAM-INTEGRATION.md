# Goldoire integration

Source reviewed: [Goldoire/fefw-recomp at 1d82e428125b66655dfa357c6c15515371b4e703](https://github.com/Goldoire/fefw-recomp/tree/1d82e428125b66655dfa357c6c15515371b4e703), dated 2026-11-08. This is a selective integration into the existing pinned pokopia runtime. It does not replace the launcher, build workflow or game files.

## Changes adopted

| Change | Integration |
| --- | --- |
| Built-in temporal anti-aliasing and upscaling | Uses the game's colour, depth, motion vectors, jitter and reset flag. Keeps two host history images per reconstruction network. Reconstructs the 3D pass before the game's later UI passes. No vendor DLL or external upscaler installation is required. |
| NVN2 reconstruction API | Replaces incomplete parameter handling with 58 implemented builder, network and parameter entry points, including motion scale, output dimensions, subrectangles, history reset and network resizing. Recorded commands retain their source textures and network until replay completes. |
| Reconstruction tests | Adapts upstream GPU regressions for constant images, untouched output borders, resize, static-scene convergence, dim HDR input, motion reprojection and history reset. These tests create their own images and contain no game content. |
| Setup integration | Our standalone `fefw_setup.exe` gains **3D reconstruction**: **Temporal anti-aliasing (TAAU)** or **Bilinear (comparison, no temporal anti-aliasing)**. Saved in `[dlss] upscaler`; restart the game after changing it. The initial profile selects TAAU. |

Adaptations include invalidating the renderer's cached bindings after the host compute pass and connecting resource destruction to our existing shutdown path. The previous spatial scaler remains available internally; the user-facing comparison mode uses the same reconstruction API with history disabled.

## Features retained from this project

- Original folder selection, update/build verification and available-language detection.
- Output resolution, fullscreen, vertical synchronization, optional verified Kompas9 60 FPS integration and skip-setup preference.
- Controller-only gameplay, hotplug/rescan support and existing controller mappings.
- Atomic configuration replacement and preservation of unknown INI entries and save paths.
- The corrected wide constant-buffer load translation and robust handling of short GPU buffer descriptors.
- Local generation of game code and commercial shaders. Public packages remain source/configurator packages with no game executable, ROM, assets, keys or commercial shader cache.

## Other upstream features evaluated

| Upstream feature | Decision |
| --- | --- |
| Launcher and updates | Keep the Win32 configurator. Add an updater for this repository and source-package layout, with preview-version ordering, SHA256, manifest validation, preserved local data, rollback and backups. No Tauri dependency. |
| Keyboard/mouse gameplay and automatic prompt switching | Keep controller-only gameplay as requested for this project. |
| 120 FPS | Adapt the checked native gameplay-target and measured-time hooks; preserve menu timing. The limiter honors the title interval independently of the host ceiling. Experimental; no sustained-FPS guarantee. |
| Game render-size/config-table overrides | Adopt the checked SDK graphics-scale hook to rewrite the title's configurations before selection. Output and internal scale are separate. Retain the stable upstream memory cap above 1440p. |
| Vendor SDK upscalers and frame generation | Omitted from this integration. The built-in TAAU runs without these dependencies. |
| Bundled toolchain/package builder | Different packaging and licensing inventory. Keep the current declared development dependencies and source-only distribution. |

## Validation and limits

The adapted temporal/API suite passes all 11 tests on an NVIDIA RTX 4070 Ti SUPER with Vulkan validation. In its synthetic static scene, mean change between the last eight frames falls from 0.04216 with history reset every frame to 0.00094 with history enabled; moving-scene error is 0.0196 with vectors versus 0.0836 without. These numbers describe synthetic tests, not measured in-game FPS or a guarantee about every scene.

Local setup integration passes 23 checks, including both reconstruction choices, reopening the standalone UI, rejecting an unsupported mode without changing either INI, controller-map/save-path preservation and rejection of an older runtime marker. The 46 existing Python tests and native configuration/mod tests pass. Both embedded host shader modules validate for Vulkan 1.3, and upgrading the previously patched engine succeeds without a clean checkout. A 187-second instrumented prologue run reports no GPU out-of-bounds access, guest trap or device loss. These are bounded checks, not a full playthrough.

This does not establish complete playability, eliminate every reported flicker, or prove that missing dialogue glyphs are fixed. Long gameplay, combat, other GPUs and the user's exact missing-letter scene require separate checks. Preview.6 packages these changes; older releases do not contain the complete integration.

## Attribution

The imported API, temporal upscaler and adapted tests are GPL-3.0-or-later code from Goldoire and contributors. Original SPDX headers are retained. The host shader generator is included in the engine patch; the embedded shader words are compiled from that open-source GLSL, not extracted commercial shaders. See `THIRD_PARTY_NOTICES.md` and `LICENSE`.

## Preview.6 additions (2026-11-09)

Reviewed the later upstream master at `45a1904ca81995bfea39cb7aa1012851d3aa84d8` as well. The announced v0.1.3 features were selectively adapted from the stable `1d82e428` implementation. The later render-pool allocator replacement, vendor upscalers and frame generation are not imported.

- Adapted `fefw_play.cpp` target/timing hooks and `fefw_res.cpp` configuration hook; original module bases and the verified update remain unchanged.
- Adapted the independent game-rate limiter, asynchronous ASTC waiter wakeup/cache-miss handling and last texture-view lookup cache. Existing video handling, descriptor robustness and wide-load shader correction are retained.
- The v0.1.2 black-clothing release fix supplied a missing ASTC converter. Our preparation already copies and checks the ASTC decoder DLLs; it continues to do so. The advertised video and crowd optimizations are not a claim that this project now matches upstream performance in every scene.
- Added setup controls for original/60/120 FPS, windowed/borderless/exclusive modes, custom engine output and internal scale. Gameplay remains controller-only per this project's requirements; keyboard navigation of setup and F1 remains available.
- Added an updater for our own repository. Offline tests exercise semantic version ordering, archive path restrictions, digest/manifest checks, rollback after a locked file, backups and preservation of local game data. Network checks are read-only until the user chooses Install.

Preview.6 validation: 46 Python tests, all 11 native CTest groups, 29 setup integration checks, 76 UI checks on the packaged standalone executable, and 34 updater transaction checks pass. A real preview.5 package was downloaded, verified and installed into an isolated fixture without changing its existing settings. The final ZIP also passes the updater's extraction/manifest checks.

The final game executable ran for about 152 seconds with GPU-assisted validation, 1920x1080 game output, 1280x720 internal rendering and the 120 FPS target. Six captures show prologue/cinematic content; the log confirms the gameplay target and menu interval accounting, with no instrumented out-of-bounds access, guest trap or device loss. A separate original-target run also reports no instrumented out-of-bounds access. These runs do not demonstrate sustained 120 FPS, full-game compatibility or correction of every reported visual issue.

## Preview.7 native configurator

The standalone Win32 configurator now follows the palette and information grouping of Goldoire's launcher: navy surfaces, teal accent, persistent sidebar, and separate Picture, Performance and Window sections. Game files, Startup and Updates retain our local preparation workflow. Field explanations, a derived resolution summary, pending-change state, Revert and discard confirmation make the settings easier to review before saving.

The theme source credits Goldoire's `launcher/src/theme.css`; no game artwork, Tauri runtime or additional vendor upscaler is distributed. Native control roles, keyboard navigation and the existing INI-preserving editor are retained. See `src/SETTINGS-DESIGN.md`. Normal/compact/144-DPI captures and 29 setup integration checks pass, including dirty/revert and custom-resolution preview checks. This release does not alter the game executable, shaders or renderer.
