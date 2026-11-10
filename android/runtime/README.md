# Android runtime port: memory backend

`nxrt_android` is a real Android implementation of the existing `nxrt::mem` interface. The JNI diagnostics library links this static target; it is also suitable for the next loader/core porting steps. **It is one runtime component, not a complete game engine.** No lifted game code is included.

## Contract and provenance

The public API header and the original memory test cases come from **Goldoire/fefw-recomp**, revision `a538375a80070e8a6bf5476b03b3dd877a13ea9d`, retaining GPL-3.0-or-later notices. That runtime credits drakolordx7, featureJosh, smo-recomp and SuperMarioWonderRecomp. The Android backend and Android-specific integration/regression tests were written for this project. The VM record split/gap behavior follows the original implementation.

The 26 adapted contract cases retain the original expectations. Android child-process access probes and `/proc/self/maps` replace SEH/VirtualQuery in their OS oracles; the direct Win32 alias commit becomes `nxrt::mem::commit`. The Wine-only configurable private-heap case is excluded. Seven additional Android cases cover all six lifted module bases, foreign mapping ownership, private views, overflow and adjacent protection ranges, sparse 12 GiB backing, more than 64 aliases and concurrent kernel allocations.

## Implemented

- Exact-address reservations without overwriting occupied host mappings. Anonymous decommit replaces only a range already owned by this backend.
- Anonymous commit/decommit/protection and aligned kernel allocations.
- A sparse `memfd` heap, shared aliases and separately owned private views. Committing previously untouched backing makes it visible in all existing views. Decommitting the primary view preserves alias contents, as in the original section contract.
- Heap commit accounting, page-state queries, epoch changes and Horizon VM block records.
- Ownership and cleanup of every mapping and descriptor, with serialized metadata changes. Protection changes validate the whole range before applying and restore prior protection if an OS operation fails.

The Switch 2 default heap reservation is 12 GiB of **virtual** address space. This does not allocate or require 12 GiB of resident RAM on startup. It is not evidence that the full game's eventual memory use will fit a 12 GB phone.

## Verified and pending

All **33 native cases** pass on the Android 15 x86_64 virtual phone, both as a standalone executable and through the installed application's JNI/UI worker. The complete application instrumentation has **19 passing checks**, including that native suite and the existing Vulkan presentation test. The same sources compile and link into the ARM64 APK; execution on the physical Galaxy remains unverified.

This first backend requires a **4 KB host page size**. It refuses initialization on 16 KB hosts, where independent guest sub-page permissions/aliases remain unimplemented. ELF/ZIP alignment at 16 KB permits installation; it does not solve guest page semantics. See Android's [page-size documentation](https://developer.android.com/guide/practices/page-sizes).

The Win32 runtime, its write tracker and graphics driver are not replaced by this target yet. Remaining integration includes the module loader, lifted-code dispatch/ABI, guest scheduling and synchronization, SVC/services, exception handling, storage/saves, audio, SDL/lifecycle and NVN/Vulkan rendering. `game_runtime_available` remains false.

The native test executable is `nxrt_android_memory_tests` in the NDK build directory. It runs without game data. The APK also exposes **Test runtime memory**, running the identical backend and contract cases inside the app process. All test mappings are released before returning.
