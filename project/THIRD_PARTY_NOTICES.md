# Components and attributions

These notices accompany the software and patches. References to the engine and its dependencies describe components obtained for local builds; they do not mean that every component is linked into the public setup tool.

| Component | Source and license | Preserved notice |
| --- | --- | --- |
| pokopia-recomp / nxrecomp / nxrt | [Pinned revision](https://github.com/drakolordx7/pokopia-recomp/tree/2c29515b23a6d78d967775fd7e9f24f50402cca7); GPL v3 and file-specific headers | `LICENSE`; `project/licenses/upstream-runtime-NOTICES.md`; `project/licenses/upstream-recomp-NOTICES.md` |
| SuperMarioWonderRecomp | Adapted modules; MIT, BelmanteGu | `project/licenses/SuperMarioWonderRecomp-MIT.txt` |
| Ryujinx / Ryubing | Translator and patch context; MIT, Ryujinx Team and Contributors | `project/licenses/Ryubing-MIT.txt` |
| Mesa NAK | Decoder references; MIT, the identified rights holders | `project/licenses/mesa-nak-MIT.txt` |
| smo-recomp and the engine’s GPL references | GNU GPL v3; scope described in engine notices | `project/licenses/smo-recomp-GPL-3.0.txt` |
| nxnative | Translator adaptations; MIT, seedds | `project/licenses/nxnative-MIT.txt` |
| Goldoire/fefw-recomp | [v0.1.5, a538375a80070e8a6bf5476b03b3dd877a13ea9d](https://github.com/Goldoire/fefw-recomp/tree/a538375a80070e8a6bf5476b03b3dd877a13ea9d); GPL-3.0-or-later. Complete current runtime, memory pools, Vulkan renderer, texture converter, shader extractor/translator, temporal reconstruction and title-specific movie/FPS/controller-glyph work; launcher design reference | Current notices retained in `project/licenses/goldoire-runtime-THIRD-PARTY-NOTICES.md` and `project/licenses/goldoire-textures-THIRD-PARTY.md`; original source headers retained in the fetched checkout; local runtime adaptations in `project/patches/renderer.patch`; integration scope in `project/UPSTREAM-INTEGRATION.md` |
| featureJosh/pokopia-recomp | [Upstream project](https://github.com/featureJosh/pokopia-recomp); additional runtime, tools and launcher lineage credited by Goldoire | Acknowledgment in the README; applicable file-specific notices remain in the fetched Goldoire checkout |
| Kompas9 60 FPS patch | [Switch-Game-Patches, revision 9e553571c986fbd45ef55202cb8ec0e1179a3db9](https://github.com/Kompas9/Switch-Game-Patches/tree/9e553571c986fbd45ef55202cb8ec0e1179a3db9/games/Fire-Emblem-Fortunes-Weave); v1.0 patch locations, original native integration. Original documentation and tools are MIT-licensed | `project/licenses/Kompas9-MIT.txt`; scope and limitations in the source README |
| SDL 3.4.18 | Controller detection and testing, statically linked into the setup tool; zlib license, Sam Lantinga and contributors | `project/licenses/SDL-zlib.txt`; verified source pin in `project/CMakeLists.txt` |
| libvpx | Host video library; BSD and ISC as specified in its files | `project/licenses/libvpx-copyright.txt` |
| dav1d and incorporated code | Host video library; licenses listed in the full notice | `project/licenses/dav1d-copyright.txt` |

The `upstream-*-NOTICES.md` documents are retained from the base revision and include additional attributions for MIT, GPL, Apache with LLVM exceptions and graphics dependencies. Licenses for other downloaded dependencies, such as ASTC/BC7, Vulkan and build tools, remain in their source trees and apply to each component. Their binaries are not included in this setup package.

The setup tool links SDL for controller input, Windows libraries and its C++ standard library without linking the game or graphics runtime. Its sources and build files are in `project/src/` and `project/CMakeLists.txt`. System development tools are not included in this distribution.

Report incomplete attribution by identifying the component, source and missing notice, without attaching proprietary material. This inventory does not claim ownership of third-party components or change their licenses.

## Complete graphics/runtime source (preview.10)

Goldoire/fefw-recomp v0.1.5, commit a538375a80070e8a6bf5476b03b3dd877a13ea9d, GPL-3.0-or-later: runtime, memory pools, Vulkan rendering, texture converter integration and shader extraction/translation tools. Sources are fetched at this exact revision by bootstrap; local adaptations are provided in patches/renderer.patch. Original notices and third-party source licences remain in that checkout. No proprietary upscaler DLLs, extracted game shaders or game code are included in our public package.

Copies of that revision's runtime and texture notices are included in `project/licenses/goldoire-runtime-THIRD-PARTY-NOTICES.md` and `project/licenses/goldoire-textures-THIRD-PARTY.md`. The earlier `upstream-*-NOTICES.md` files remain preserved for the pinned compiler/base lineage. These notices describe upstream components; features or optional binaries mentioned there are not automatically distributed or enabled by this package.

## Android application services and RomFS (0.1.7)

Android service adapters derive from Goldoire nxrt at a538375a80070e8a6bf5476b03b3dd877a13ea9d: am.cpp, common.cpp, time.cpp, timezone.cpp, selected network.cpp/misc.cpp contracts, fs.cpp and fs_romfs.cpp. Existing SPDX and source-specific credits are retained. Their lineage includes ScarletNX (GPL-3.0-or-later), Ryujinx/Ryubing (MIT) and SuperMarioWonderRecomp (MIT). The RomFS layout/builder also credits yuzu fsmitm_romfsbuild (GPL-2.0-or-later), as stated in its source. Android replaces host platform operations; this attribution does not claim full Android game/rendering compatibility. No commercial game code, game files or extracted shaders are part of these adaptations.
