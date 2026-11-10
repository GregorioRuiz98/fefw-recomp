# ZBIC decoder source

The unmodified `zstd.c`, `zstd.h` and `zstd_errors.h` come from the **kinnay/zbic 1.0.1.post1** source distribution, which attributes these files to Atmosphere-NX. The package's Python extension is not compiled or included.

- Project: https://github.com/kinnay/zbic
- Source archive: `zbic-1.0.1.post1.tar.gz` from https://pypi.org/project/zbic/1.0.1.post1/
- Archive SHA-256: `a798c24aea58f81a0b4bb0226f0f383b7e7f07a9a0150732cb45dc65ac541ff3`
- `zstd.c` SHA-256: `6f8e89a93f760ab2e9e48f8bbac766e254aa748a343e495eaa896ac441772e1f`

The source files retain their Meta Platforms/Zstandard copyright and dual BSD/GPLv2 notices. `LICENSE` supplies the BSD text from Zstandard 1.5.7; `COPYING` retains the source package's GPLv2 text. Both notices and this provenance are included in the APK. Android compiles the library with `ZSTD_ZBIC_SUPPORT=1`, as the Python package does; ordinary Zstandard frames are not interchangeable with ZBIC.

This dependency decodes locally supplied NSO segments. It contains no Nintendo code, keys or game files. The added decoder tests use synthetic bytes created for this project.
