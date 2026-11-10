# Android runtime port: execution core and bootstrap SVCs

`nxrt_android` integrates the real `nxrt::mem` Android implementation with the ELF/NSO loader, ABI 3 registry, call/import dispatcher, site hooks, guest exception unwinder and timer, plus bootstrap memory/process SVCs and calling-thread TLS. The JNI application links these components and opens user-selected modules through read-only Android descriptors. **It is not a complete game engine.** No lifted game code is included in the APK or public source. All six game modules compile separately into private ARM64 archives; all six link into a private ARM64 integration library without missing symbols. Worker-thread scheduling, synchronization, IPC and game services remain unimplemented.

## Contract and provenance

The public API header and the original memory test cases come from **Goldoire/fefw-recomp**, revision `a538375a80070e8a6bf5476b03b3dd877a13ea9d`, retaining GPL-3.0-or-later notices. That runtime credits drakolordx7, featureJosh, smo-recomp and SuperMarioWonderRecomp. The Android backend and Android-specific integration/regression tests were written for this project. The VM record split/gap behavior follows the original implementation.

The 26 adapted contract cases retain the original expectations. Android child-process access probes and `/proc/self/maps` replace SEH/VirtualQuery in their OS oracles; the direct Win32 alias commit becomes `nxrt::mem::commit`. The Wine-only configurable private-heap case is excluded. Seven additional Android cases cover all six lifted module bases, foreign mapping ownership, private views, overflow and adjacent protection ranges, sparse 12 GiB backing, more than 64 aliases and concurrent kernel allocations.

The loader, module registry, dispatcher, site-hook registry and its twelve unchanged tests, configuration, logging, gauge registry, nxrecomp ABI/helper headers and the loader/registry tests also come from that same pinned revision. The synthetic `fixture.c` is rebuilt with the NDK compiler/linker into build-directory-only test data. No compiled ELF fixture is committed. Android uses `/proc/self/maps` for loader protection checks, `mmap`/`dladdr` for the flat dispatch table, monotonic logging clocks and logcat. Optional Windows frame tracing/soak sampling is not implemented by this port. The call/import ABI restores state on engine traps; absent HLE handlers raise an explicit `AndroidGuestTrap` instead of the upstream logging stub's zero return. The two affected upstream tests assert that refusal and state restoration. The remaining expectations are retained.

NSO loading handles LZ4 and the ZBIC format found in the supported copy. ZBIC uses the unmodified decoder documented [here](third_party/zbic/README.md). Tests additionally reject malformed streams, overlapping/overflowing segments and negative MOD0 dynamic pointers. The inherited loader preserves relocation slots for SDK 20+ `rtld` to relink. Android now explicitly records `R_AARCH64_TLSDESC` as deferred only in that mode, preserves both descriptor words, and refuses it when no relinking `rtld` is present. It never fabricates a TLS resolver/offset.

## Implemented

- Exact-address reservations without overwriting occupied host mappings. Anonymous decommit replaces only a range already owned by this backend.
- Anonymous commit/decommit/protection and aligned kernel allocations.
- A sparse `memfd` heap, shared aliases and separately owned private views. Committing previously untouched backing makes it visible in all existing views. Decommitting the primary view preserves alias contents, as in the original section contract.
- Heap commit accounting, page-state queries, epoch changes and Horizon VM block records.
- Ownership and cleanup of every mapping and descriptor, with serialized metadata changes. Protection changes validate the whole range before applying and restore prior protection if an OS operation fails.

The Switch 2 default heap reservation is 12 GiB of **virtual** address space. This does not allocate or require 12 GiB of resident RAM on startup. It is not evidence that the full game's eventual memory use will fit a 12 GB phone.

## Verified and pending

All **33 memory cases** and **140 runtime-core cases** pass on the Android 15 x86_64 virtual phone through the installed application's JNI/UI worker. Default application instrumentation has **24 checks**. The opt-in local-copy run passes **27 checks**, including read-only loading of all six original ZBIC NSOs, analysis of **287,544 relocations**, one explicitly deferred TLS descriptor, and complete cleanup. That test does not execute the guest dynamic linker or any game code. The same sources compile/link into the ARM64 APK; physical Galaxy execution remains unverified.

This first backend requires a **4 KB host page size**. It refuses initialization on 16 KB hosts, where independent guest sub-page permissions/aliases remain unimplemented. ELF/ZIP alignment at 16 KB permits installation; it does not solve guest page semantics. See Android's [page-size documentation](https://developer.android.com/guide/practices/page-sizes).

The Win32 runtime, its write tracker and game graphics driver are not replaced by this target yet. Remaining integration includes SDK initialization, guest worker scheduling/synchronization, IPC/game services, storage/saves, audio, SDL/lifecycle and NVN/Vulkan rendering. Unknown system registers and untranslated instructions fail explicitly. The full game modules are not linked into this APK. `game_runtime_available` remains false.

The native test executable is `nxrt_android_memory_tests` in the NDK build directory. Its default suite tests memory; `--core` tests the loader/registry/dispatcher/NSO path, without game data. The APK exposes **Test runtime memory**, **Test execution core and bootstrap**, and **Test selected game modules**. All test mappings are released before returning. A shared runtime lock prevents diagnostics from destroying a live future session's state.

## Guest exceptions and hooks

The site-hook registry and its twelve unchanged upstream tests are included. The guest EH implementation and 37 original unwinder/typeinfo/ABI-3 cases come from the same pinned runtime. Android replaces process termination with a reported engine trap. Five additional ABI/platform cases check the real 31.25 MHz monotonic timer, unsupported system registers, untranslated instructions, exclusive-store counters and catch-frame cleanup after a host error. One extra EH regression covers a guest unwind across the import/dispatcher boundary. Cleanup at that boundary is explicit: guest `longjmp` must not skip newly introduced non-trivial C++ destructors. Final cleanup checks include registries, hooks and exception activations. These tests are synthetic; the game's exceptions have not been exercised in gameplay.

## Bootstrap memory and process phase (0.1.5)

The SVC declarations/names, kernel/game contracts and memory/info handler semantics come from the same pinned Goldoire runtime. Android replaces Win32 thread adoption and CPU clocks, and supplies an owned handle table. `kernel.h` retains the upstream contract; only handles, calling-thread adoption/TLS, ticks and sleep are implemented by this backend. Unported worker and IPC APIs are not registered. A missing SVC returns `ResultNotImplemented` and records its first arguments, function and PC; it never silently succeeds. Fatal guest Break/ExitProcess/exception-return paths report a trap to the native boundary rather than terminating the launcher.

The 21 registered handlers cover heap size, memory permissions/attributes, stack MapMemory/UnmapMemory, QueryMemory, eight process/info/resource queries, ticks/sleep, CloseHandle and diagnostic termination/output. RELRO permission locks are irreversible, reported with exact QueryMemory boundaries, and output pointers require writable pages. The memory backend distinguishes committed PROT_NONE pages from unmapped pages so a valid permission change can restore access. Sparse heap zeroing uses memfd hole punching, with no bulk physical allocation. Shrinking a heap refuses live alias/private-view or mapped-stack ranges.

**36 new native cases** exercise those behaviours through the real `guest_svc` ABI, including an 8 GiB sparse heap, read-only output rejection, partial stack unmapping, TLS handle fields, CPU ticks, explicit missing services and repeated sessions. These are included in the 140 core cases and run inside the installed APK. Reset checks now include kernel handles and adopted thread state.

The private rtld test uses six separately supplied original NSOs and only the locally generated rtld code. It starts with x0=0, x1=the adopted thread handle and a real guest stack/TLS, using the production guest-linker boot convention. It executes eight RELR self-relocations and 35 QueryMemory SVCs, then a deliberate host checkpoint stops at `nn::init::Start`. It does not execute SDK initialization, the remaining module linking or game initialization. This test is separate from the APK's public, synthetic tests and from the optional six-module data inspection. No private code or files enter a public artifact.
