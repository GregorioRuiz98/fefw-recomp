# Windows stability follow-up — 2026-11-21

Two reproducible failures are addressed in preview.14 sources.

- **Partial game content:** omitting `Shaders/Presets/Base.presetb` from an otherwise complete local copy causes the published preview.13 executable to read a null pointer and exit with a host access violation before its first presented frame. The startup guard checks the five small required resources (`Fs.bin.zst` and the Base, Chara, Effect and Map presets) for regular-file status and a readable 16-byte header. Setup performs the same check. Explicit INI and skip-setup launches also pass through the guard. This neither validates every game file nor replaces missing content with a shader cache.
- **Unavailable source repository:** new preparation fails when the original renderer repository returns 404. Acquisition now tries two availability mirrors containing the exact pinned original commit, stages the download, verifies HEAD, then installs it. Failed attempts and timeouts leave no partial checkout. An existing checkout is never reset or replaced. Network acquisition cannot wait for a Git credentials prompt on an unavailable public repository.

The runtime, renderer revision, graphics presets and original/60/120 FPS choices are unchanged by these fixes. The local executable is rebuilt once for the new startup guard; existing shaders and saves are reused.

## Reproduction and checks

Private game replays use the user's own files and isolated settings/saves. No game content is included in the tests or public package.

- The missing-preset access violation was reproduced twice with the published executable. The corrected executable rejects the same fixture with an actionable error and exit code 1, with no host exception or GPU initialization.
- Restoring the original preset allows startup. A 100-second complete-content control reaches 2,899 presented frames without a host exception. These are bounded boot checks, not a complete playthrough.
- The final preview.14 executable completed a separate 255-second new-game run with the Vulkan validation layer confirmed active: 7,427 presented frames, the opening AV1 movie and subsequent Spanish 3D dialogue, with no host exception, guest trap, device loss or validation error. A preceding 255-second run also completed, but its validation request was unavailable and is not counted as validation evidence.
- All 63 Python tests, 21 native CTest groups, 83 configuration checks and 37 standalone setup integration checks pass. The packaged configurator also rejects the incomplete-content fixture and identifies the missing preset.
- Synthetic configuration tests cover each missing, empty and short required file, Unicode paths and an externally locked resource.
- Offline acquisition tests cover fallback, an origin without the required commit, complete failure, timeout, idempotence and preservation of a locally modified or differently pinned existing checkout.
- Fresh network acquisition was tested after the upstream 404: the first mirror supplied the exact pinned commit and the local renderer patch applied successfully.

## Remaining reports

High RAM/VRAM use and scene-specific crashes still need reports from affected installations. The incomplete-content reproduction alone does not establish their cause. No generic out-of-memory, driver or full-game compatibility fix is claimed here.

Before relaunching after a crash, copy `fefw.log` and any `fefw_crash.txt` next to the executable. Include the package version, Windows version, GPU/driver, RAM/VRAM, resolution, internal scale, FPS option and exact steps/scene. Logs can contain personal file paths and game text; review them before posting. The setup diagnostic export intentionally omits free-form logs.
