# Provenance notices

This is a modified adaptation for preparing Fire Emblem: Fortune’s Weave locally. It is not an official game distribution or a release by the original engine maintainer.

Base: [drakolordx7/pokopia-recomp](https://github.com/drakolordx7/pokopia-recomp/tree/2c29515b23a6d78d967775fd7e9f24f50402cca7), commit `2c29515b23a6d78d967775fd7e9f24f50402cca7`.

Modifications are supplied as `project/patches/engine.patch`; the tools download that revision and apply the patch locally. Headers and attributions are retained. The adaptation adds title configuration, preparation, tests and a Win32 setup tool, with changes to ARM64 translation, services, video and NVN2 compatibility.

The GNU GPL v3 text is in `LICENSE`. Each file’s version notice is preserved, including the later-version option where applicable. Third-party MIT, Apache and other notices are not replaced by a generic attribution to this project.

The public setup package includes its corresponding source and original resources. Game-derived components are generated locally and are not part of this distribution. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [LEGAL.md](LEGAL.md).
