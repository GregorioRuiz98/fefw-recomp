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
| Kompas9 60 FPS patch | [Switch-Game-Patches, revision 9e553571c986fbd45ef55202cb8ec0e1179a3db9](https://github.com/Kompas9/Switch-Game-Patches/tree/9e553571c986fbd45ef55202cb8ec0e1179a3db9/games/Fire-Emblem-Fortunes-Weave); v1.0 patch locations, original native integration. Original documentation and tools are MIT-licensed | `project/licenses/Kompas9-MIT.txt`; scope and limitations in the source README |
| libvpx | Host video library; BSD and ISC as specified in its files | `project/licenses/libvpx-copyright.txt` |
| dav1d and incorporated code | Host video library; licenses listed in the full notice | `project/licenses/dav1d-copyright.txt` |

The `upstream-*-NOTICES.md` documents are retained from the base revision and include additional attributions for MIT, GPL, Apache with LLVM exceptions and graphics dependencies. Licenses for other downloaded dependencies, such as ASTC/BC7, SDL, Vulkan and build tools, remain in their source trees and apply to each component. Their binaries are not included in this setup package.

The setup tool links Windows libraries and its C++ standard library without linking the game or graphics runtime. Its sources and build files are in `project/src/` and `project/CMakeLists.txt`. System development tools are not included in this distribution.

Report incomplete attribution by identifying the component, source and missing notice, without attaching proprietary material. This inventory does not claim ownership of third-party components or change their licenses.
