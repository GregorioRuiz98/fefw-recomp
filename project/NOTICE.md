# Provenance notices

This is a modified adaptation for preparing Fire Emblem: Fortune’s Weave locally. It is not an official game distribution or a release by the original engine maintainer.

Base: [drakolordx7/pokopia-recomp](https://github.com/drakolordx7/pokopia-recomp/tree/2c29515b23a6d78d967775fd7e9f24f50402cca7), commit `2c29515b23a6d78d967775fd7e9f24f50402cca7`.

Current runtime and graphics: [Goldoire/fefw-recomp v0.1.5](https://github.com/Goldoire/fefw-recomp/tree/a538375a80070e8a6bf5476b03b3dd877a13ea9d), commit `a538375a80070e8a6bf5476b03b3dd877a13ea9d`. Bootstrap fetches that revision and applies `project/patches/renderer.patch`; the full runtime, memory pools, Vulkan rendering, texture converter and shader tools come from this upstream project. This integration also uses its title-specific controller-glyph implementation and adapts its movie/FPS hooks. Goldoire credits the earlier work of drakolordx7 and featureJosh on Pokopia; that lineage is acknowledged here as well.

Compiler/base modifications are supplied as `project/patches/engine.patch`; the tools download that revision and apply the patch locally. Headers and attributions are retained. The adaptation maintained by GregorioRuiz98 adds title configuration, preparation, tests and a Win32 setup tool, with changes to ARM64 translation and local integration. The current renderer is fetched separately as described above; older selective graphics changes remain documented as historical work in `project/UPSTREAM-INTEGRATION.md`.

Kompas9's MIT-licensed Switch-Game-Patches supplies the referenced 60 FPS patch locations and findings. Its license text is preserved in `project/licenses/Kompas9-MIT.txt`.

The GNU GPL v3 text is in `LICENSE`. Each file’s version notice is preserved, including the later-version option where applicable. Third-party MIT, Apache and other notices are not replaced by a generic attribution to this project.

The public setup package includes its corresponding source and original resources. Game-derived components are generated locally and are not part of this distribution. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [LEGAL.md](LEGAL.md).

The Woven Shield application icon was created for this project with image generation and no game assets or reference images. Its complete resource source and provenance are included in `project/src/fefw_icon.json` and `project/src/APP-ICON.md`.
