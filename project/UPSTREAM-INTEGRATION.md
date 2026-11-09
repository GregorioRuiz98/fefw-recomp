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
| Tauri launcher and in-launcher updates | Keep the existing standalone Win32 configurator and its tested settings migration. The upstream updater targets a different package layout and repository; copying it would not update our package correctly. |
| Keyboard/mouse gameplay and automatic prompt switching | Keep controller-only gameplay as requested for this project. |
| 120 FPS | Do not expose it without timing, animation and gameplay validation. Keep the existing optional 60 FPS patch. |
| Game render-size/config-table overrides | Separate follow-up: these modify game-specific data and need verification against our supported build. Do not reuse the previously problematic global Vulkan resolution multiplier. Window resolution keeps its current meaning. |
| Vendor SDK upscalers and frame generation | Omitted from this integration. The built-in TAAU runs without these dependencies. |
| Bundled toolchain/package builder | Different packaging and licensing inventory. Keep the current declared development dependencies and source-only distribution. |

## Validation and limits

The adapted temporal/API suite passes all 11 tests on an NVIDIA RTX 4070 Ti SUPER with Vulkan validation. In its synthetic static scene, mean change between the last eight frames falls from 0.04216 with history reset every frame to 0.00094 with history enabled; moving-scene error is 0.0196 with vectors versus 0.0836 without. These numbers describe synthetic tests, not measured in-game FPS or a guarantee about every scene.

Local setup integration passes 23 checks, including both reconstruction choices, reopening the standalone UI, rejecting an unsupported mode without changing either INI, controller-map/save-path preservation and rejection of an older runtime marker. The 46 existing Python tests and native configuration/mod tests pass. Both embedded host shader modules validate for Vulkan 1.3, and upgrading the previously patched engine succeeds without a clean checkout. A 187-second instrumented prologue run reports no GPU out-of-bounds access, guest trap or device loss. These are bounded checks, not a full playthrough.

This does not establish complete playability, eliminate every reported flicker, or prove that missing dialogue glyphs are fixed. Long gameplay, combat, other GPUs and the user's exact missing-letter scene require separate checks. The latest published release may predate this source integration; consult its release notes before assuming it includes TAAU.

## Attribution

The imported API, temporal upscaler and adapted tests are GPL-3.0-or-later code from Goldoire and contributors. Original SPDX headers are retained. The host shader generator is included in the engine patch; the embedded shader words are compiled from that open-source GLSL, not extracted commercial shaders. See `THIRD_PARTY_NOTICES.md` and `LICENSE`.
