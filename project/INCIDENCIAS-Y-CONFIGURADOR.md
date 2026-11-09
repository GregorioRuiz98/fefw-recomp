# Issues and setup — November 7, 2026

This historical implementation report records the initial setup and graphics delivery before the first GitHub release. Counts below refer to that validation run. See the main README and release notes for later controller, 60 FPS and interface updates.

The setup tool and packaging have been implemented. Graphics corrections pass their regressions and the prologue runs with Vulkan validation enabled. Resolving the severe flicker in the user’s specific scene still requires their confirmation.

## Fixed

- Spatial scaling retained only whole-pixel rectangles and ignored fractional camera offsets. It now uses NVN2 parameters recorded by value and an original shader with bilinear sampling. The regression fails before the correction and passes afterward. Initial setup enables vertical synchronization.
- Images and views could be released while still in use by the GPU for presentation. Retirement now also waits for presenter submissions. A blocked-queue test reproduces the old error and passes with the correction.
- A compute shader declared floating-point reads from an eight-bit integer texture. Its type is specialized and the extended result bits are retained. Checks cover signed/unsigned integers, texture changes and a normalized read sharing the original type.
- 2D views of 3D images were used without enabling the required capability and flags. Actual device support is now queried and enabled, and images use the corresponding flags.
- Two temporary buffers remained allocated when shutting down the backend. A test checks that the allocation counter returns to zero.
- Local preparation could depend on an existing .NET 10 SDK, generate C++ paths with incorrect separators or search for the translator somewhere other than the x64 build output. The SDK is prepared locally when missing, with fixed paths and build platform. Preparation rejects an incomplete shader cache.

## Setup behavior

`fefw.exe` opens the wizard when a valid configuration is missing. `fefw_setup.exe` always opens it and shares the same settings.

The wizard selects a folder containing `exefs` and `romfs`, recognizes supported updates, verifies all six module fingerprints and offers only languages present in the copy. It supports output resolution, windowed/fullscreen mode, language and vertical synchronization. Internal game resolution keeps its original value; fullscreen uses the desktop.

**Skip setup on startup** takes effect only after saving a valid configuration. Missing resources or a moved folder reopen the wizard. Controller/keyboard bindings, other settings and saves are preserved. Canceling preparation stops its processes without marking it complete.

## Validation recorded for the initial delivery

The 15 executable checks, 32 Python tests, 32 native configuration checks, seven CTest groups, four GPU constant/scaling cases and three additional graphics regressions passed. Setup integration passed 14 checks, including a Spanish-language boot at 1600 × 900 with vertical synchronization.

In a 160-second run, Vulkan validation dropped from 32 errors across five categories to zero. There were 157 valid consecutive frames across two intervals of 3D dialogue, with no isolated flashes detected by the tool used. One empty PNG resulted from stopping the diagnostic while it was being written and was not counted as a black frame. These measurements do not establish full console equivalence or the absence of every temporal artifact.

Preparation from an empty workspace generated code for all six modules and all 11,977 shaders. The 85 shader codes with multiple profiles passed comparison of validated outputs. System tools were already installed, and vcpkg reused its library cache. Graphics regressions also passed in that build.

## Distribution

The user’s private package contains the native executable and data generated from their copy. The public ZIP contains only the standalone setup tool, source, patches, tools and licenses. Each user supplies their own original folder and generates components locally.

Original files, normalized ExeFS/ROMFS, keys, textures, audio, video, extracted/translated commercial shaders, generated game C++ and the private executable are excluded. Distributing those files as a folder would still distribute game content. Folder and ZIP contents are checked against an explicit allowlist and hashes. Nothing had been uploaded to GitHub at the time of this initial report.

## Outstanding limitations

Confirm flicker behavior in the user’s sequence, test physical controller reconnection during gameplay, combat, saving/loading and extended stability. REV16 audio remains experimental. Built-in TAAU now performs temporal reconstruction; reported flicker and missing letters in unvisited sequences still require reproduction. Detailed private reports and binary fingerprints are generated locally as `reports/setup-and-flicker.md` and `reports/setup-delivery.json`; they are not included in the public repository.
