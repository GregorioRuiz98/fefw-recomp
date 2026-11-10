# Android runtime port: memory, module loading and dispatch

`nxrt_android` integrates the real `nxrt::mem` Android implementation with the ELF/NSO loader, ABI 3 module registry and call/import dispatcher. The JNI application links these components and opens user-selected modules through read-only Android descriptors. **It is not a complete game engine.** No lifted game code is included in the APK or public source.

## Contract and provenance

The public API header and the original memory test cases come from **Goldoire/fefw-recomp**, revision `a538375a80070e8a6bf5476b03b3dd877a13ea9d`, retaining GPL-3.0-or-later notices. That runtime credits drakolordx7, featureJosh, smo-recomp and SuperMarioWonderRecomp. The Android backend and Android-specific integration/regression tests were written for this project. The VM record split/gap behavior follows the original implementation.

The 26 adapted contract cases retain the original expectations. Android child-process access probes and `/proc/self/maps` replace SEH/VirtualQuery in their OS oracles; the direct Win32 alias commit becomes `nxrt::mem::commit`. The Wine-only configurable private-heap case is excluded. Seven additional Android cases cover all six lifted module bases, foreign mapping ownership, private views, overflow and adjacent protection ranges, sparse 12 GiB backing, more than 64 aliases and concurrent kernel allocations.

The loader, module registry, dispatcher, configuration, logging, gauge registry, nxrecomp ABI/helper headers and the loader/registry tests also come from that same pinned revision. The synthetic `fixture.c` is rebuilt with the NDK compiler/linker into build-directory-only test data. No compiled ELF fixture is committed. Android uses `/proc/self/maps` for loader protection checks, `mmap`/`dladdr` for the flat dispatch table, monotonic logging clocks and logcat. Optional Windows frame tracing/soak sampling is not implemented by this port. The call/import ABI restores state on engine traps; absent HLE handlers raise an explicit `AndroidGuestTrap` instead of the upstream logging stub's zero return. The two affected upstream tests assert that refusal and state restoration. The remaining expectations are retained.

NSO loading handles LZ4 and the ZBIC format found in the supported copy. ZBIC uses the unmodified decoder documented [here](third_party/zbic/README.md). Tests additionally reject malformed streams, overlapping/overflowing segments and negative MOD0 dynamic pointers. The inherited loader preserves relocation slots for SDK 20+ `rtld` to relink. Android now explicitly records `R_AARCH64_TLSDESC` as deferred only in that mode, preserves both descriptor words, and refuses it when no relinking `rtld` is present. It never fabricates a TLS resolver/offset.

## Implemented

- Exact-address reservations without overwriting occupied host mappings. Anonymous decommit replaces only a range already owned by this backend.
- Anonymous commit/decommit/protection and aligned kernel allocations.
- A sparse `memfd` heap, shared aliases and separately owned private views. Committing previously untouched backing makes it visible in all existing views. Decommitting the primary view preserves alias contents, as in the original section contract.
- Heap commit accounting, page-state queries, epoch changes and Horizon VM block records.
- Ownership and cleanup of every mapping and descriptor, with serialized metadata changes. Protection changes validate the whole range before applying and restore prior protection if an OS operation fails.

The Switch 2 default heap reservation is 12 GiB of **virtual** address space. This does not allocate or require 12 GiB of resident RAM on startup. It is not evidence that the full game's eventual memory use will fit a 12 GB phone.

## Verified and pending

All **33 memory cases** and **49 loader/core cases** pass on the Android 15 x86_64 virtual phone through the installed application's JNI/UI worker. Default application instrumentation has **23 checks**. The opt-in local-copy run passes **26 checks**, including read-only loading of all six original ZBIC NSOs, analysis of **287,544 relocations**, one explicitly deferred TLS descriptor, and complete cleanup. That test does not execute the guest dynamic linker or any game code. The same sources compile/link into the ARM64 APK; physical Galaxy execution remains unverified.

This first backend requires a **4 KB host page size**. It refuses initialization on 16 KB hosts, where independent guest sub-page permissions/aliases remain unimplemented. ELF/ZIP alignment at 16 KB permits installation; it does not solve guest page semantics. See Android's [page-size documentation](https://developer.android.com/guide/practices/page-sizes).

The Win32 runtime, its write tracker and game graphics driver are not replaced by this target yet. Remaining integration includes the rest of the generated-code ABI (SVC/system registers, hooks and guest EH), guest scheduling/synchronization, SVC/services, storage/saves, audio, SDL/lifecycle and NVN/Vulkan rendering. The full game modules are not linked into this APK. `game_runtime_available` remains false.

The native test executable is `nxrt_android_memory_tests` in the NDK build directory. Its default suite tests memory; `--core` tests the loader/registry/dispatcher/NSO path, without game data. The APK exposes **Test runtime memory**, **Test loader and dispatcher**, and **Test selected game modules**. All test mappings are released before returning. A shared runtime lock prevents diagnostics from destroying a live future session's state.
