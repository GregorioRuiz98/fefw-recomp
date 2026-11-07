# Development dependencies used for validation

Builds were performed on Windows x64. The exact local manifest is generated as `reports/toolchain.json`, with logs in `reports`. These private reports are not part of the public package.

| Dependency | Version/revision | Location relative to `work` |
| --- | --- | --- |
| Visual C++ Build Tools 2022 | Package 17.14.44, installation 17.14.37827.9 | System installation |
| Windows SDK | 10.0.26100.0 | System installation |
| LLVM clang-cl | 22.1.8 | `C:/Program Files/LLVM/bin` |
| .NET SDK | 10.0.403 | `toolchains/dotnet` |
| Ryubing | `a82350bb774f70fcbd41c9987bf67a3775409963` | `refs/ryubing` |
| SPIRV-Tools | `ef96ed763b43b59b33b31b362f09a02b729fa1c9` | `refs/spirv-tools` |
| SPIRV-Headers | `496543121ce6419f23d6fa5d7194ba66c36212d2` | `refs/spirv-tools/external/spirv-headers` |
| astc-encoder | 5.7.0 | `engine/tools/textures/third_party/astc-encoder` |
| bc7enc_rdo | `b9438627eef73a1157e84201b6fa6eb2ffd6d9f0` | `engine/tools/textures/third_party/bc7enc_rdo` |
| ISPC | 1.32.0 | `engine/tools/textures/third_party/ispc` |
| NVIDIA nvdisasm | 13.3.29, archive checked against its official SHA | `toolchains/nvdisasm` |

The engine downloads Vulkan, SDL and ImGui dependencies through CMake. Texture libraries were built using `engine/tools/textures/THIRD_PARTY.md` and its CMake configuration. nvdisasm was used for diagnosis without installing CUDA or replacing the graphics driver.

`tools/Initialize-BuildEnv.ps1` locates Visual Studio through vswhere and activates its environment. Configure/Build/NativeTest/FloatingPointTest do not require a separate developer console.

The translator is built as `engine/tools/shaders/build/bin/Release/net10.0/spirv_harness.dll`. Ryubing sources are prepared in `engine/tools/shaders/third_party/ryubing`, including the three projects required by the harness. The independent validator is `build/spirv-tools/tools/spirv-val.exe`. TranslateShaders/ValidateShaders check these paths.

`tools/Prepare-Game.ps1` automates preparation from a supported original folder. `Bootstrap` prepares Python and the engine; `tools/Prepare-Dependencies.ps1` downloads and builds pinned dependencies, including a local .NET SDK 10.0.403 when needed. The SDK’s SHA-512 and ISPC’s SHA-256 are checked. No services are installed and the permanent PATH is unchanged. The translator build pins `Platform=AnyCPU` so the x64 C++ environment does not change its output path.

Generated translation occupies approximately 637 MB; objects, libraries, maps and executables require additional space. A complete optimized rebuild has not been timed. `/Od` was chosen for initial integration checks; `/O1` can be requested with CMake’s `FEFW_LIFT_OPT` when validating performance builds.

## Host video libraries (2026-11-06)

libvpx 1.16.0#3 and dav1d 1.5.4, both `x64-windows-static`, were added using vcpkg `661e42cd1600603016ced1d76a15359e7dade9f5`. The private installation is `work/toolchains/vcpkg/installed/x64-windows-static`, detected automatically by CMake. To reproduce it, use that microsoft/vcpkg commit, run `bootstrap-vcpkg.bat -disableMetrics`, then install `libvpx:x64-windows-static dav1d:x64-windows-static --disable-metrics`. Run Configure and Build afterward. Notices are in `licenses/`.

The AV1 decoder supports eight-bit 4:2:0 CPU output; native images requested by the game are connected to host textures. A device-loss issue during extended playback remains under investigation.
