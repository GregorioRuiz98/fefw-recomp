# FE: Fortune’s Weave Recomp — local preparation tools

An experimental static recompilation project for Windows x64, based on [pokopia-recomp](https://github.com/drakolordx7/pokopia-recomp/tree/2c29515b23a6d78d967775fd7e9f24f50402cca7). These tools translate and compile code and shaders locally from a folder supplied by the user.

**This distribution includes tools, source code and patches. It does not include the game.** You need a lawfully obtained copy and the rights required for the copying, extraction, transformation and use you perform. Owning the game does not guarantee that every such activity is permitted in your jurisdiction. See [LEGAL.md](LEGAL.md).

This project is not affiliated with, sponsored by or approved by Nintendo, Intelligent Systems or any other rights holder. Fire Emblem and Nintendo are named to identify compatibility; their trademarks and other rights belong to their respective holders.

## Download and open the setup tool

**[Download FEFW Recomp for Windows x64 — v0.1.0-preview.5](https://github.com/GregorioRuiz98/fefw-recomp/releases/download/v0.1.0-preview.5/FEFW-Recomp-Windows-x64-v0.1.0-preview.5.zip)** · [Release notes and downloads](https://github.com/GregorioRuiz98/fefw-recomp/releases/tag/v0.1.0-preview.5)

Extract **the entire ZIP** and open `fefw_setup.exe`. That download includes the compiled setup tool. GitHub’s **Code → Download ZIP** and **Source code** archives contain source files only. While the repository is private, you must sign in with an account that has access to download the release.

Select either the parent folder containing `updates/131072`, or `updates/131072` itself if it contains `exefs` and `romfs`. The selector detects the supported update and available languages. You do not need to move or rename the original game files to match the package.

Initial preparation requires the tools listed below and compiles `fefw.exe` and the shaders on your PC. **Selecting a folder does not make a new installation immediately ready to play.** Once preparation finishes, **Save and play** starts the game. You can then open `fefw.exe` directly if **Skip setup on startup** is enabled; `fefw_setup.exe` remains available to change your settings.

## Package contents

- A standalone Win32 setup tool and its corresponding source code.
- Title configuration, preparation scripts and a patch for a pinned revision of the open-source engine.
- Original synthetic tests, publication checks and license notices.

The package does not distribute ROMs, keys, firmware, proprietary SDKs, game updates, audiovisual assets, commercial shaders, generated game C++ or the recompiled game executable. It provides no game download links or instructions for circumventing technical protection measures. The tools accept an already extracted folder and download only the declared development dependencies.

## Status

The setup tool, preparation from an empty workspace and 3D prologue scenes have been checked. The previously validated runtime passed 15 executable checks. A 160-second run finished without Vulkan errors, and a sample of 157 consecutive frames showed no isolated flashes. A fresh libvpx build has also been verified in an installation path containing spaces, including both Release and Debug libraries.

These checks do not establish complete playability or equivalence with the console. Combat, saving/loading, extended stability and physical controller testing remain outstanding. REV16 audio and the spatial DLSS substitute are experimental; the latter does not implement temporal reconstruction. The severe flicker reported by the user still needs confirmation in other gameplay sequences.

## Requirements

- Windows x64 and a GPU/driver supporting Vulkan 1.3.
- Python 3.12 or later, Git, CMake, Ninja and LLVM with clang-cl.
- Visual C++ Build Tools and the Windows SDK installed beforehand.
- A connection for preparing dependencies and space for generated code, objects and caches.
- A controller recognized by Windows and SDL.
- A compatible folder already extracted with the necessary rights, containing `exefs` and `romfs`.

If .NET SDK 10 is unavailable, version 10.0.403 is prepared locally and its SHA-512 is verified. No services are installed and the permanent PATH is not changed. Initial preparation can take a considerable amount of time.

The supported Title ID is `04007EF01F8AE000`, with main Build ID `DB93B0125F4850601D61C3EDDE012F8EB2ECFE77`. Fingerprints are recorded in `project/supported-build.json`; a folder name alone does not establish compatibility. An update-only folder may require base-game resources, which these tools do not download.

## Using the setup ZIP

1. Extract the release into a local folder.
2. Open `fefw_setup.exe` and select the folder containing `exefs` and `romfs`, or its parent folder containing supported updates.
3. Choose the game language, output resolution, windowed or fullscreen mode, and vertical synchronization. Only languages found in your copy are offered.
4. Allow preparation to finish, then use **Save and play**. You can cancel and resume; **Open log** opens `preparation.log` for details.

If preview.3 stopped while building `libvpx` in a folder containing spaces, close setup and extract the current release into the same package folder. Keep the existing `work` folder, reopen setup and retry **Save and play**. The corrected dependency build uses relative source paths; the original game folder can stay where it is. Preparation resumes, but a first installation still needs to compile the game and shaders.

**Skip setup on startup** allows subsequent launches through `fefw.exe`. The setup tool remains available and opens again if required resources are missing. Existing controller mappings and local saves in `save` are preserved.

Output resolution controls the window size; the game’s internal resolution keeps its original value. Fullscreen uses your desktop resolution. The initial configuration enables vertical synchronization. See the [controller guide](project/CONTROLES-MANDO.md).

The runtime searches for controllers at startup and checks again every second. To recover a DualShock 4 or another device, use **F1 → Settings → Controls → Gamepad → Rescan controllers**. Gameplay requires a controller. Keyboard and mouse are used only for the setup tool, host settings and shortcuts; they do not send game actions, even with old bindings in `controls.ini`. Close F1 to return controller input to the game.

## Optional 60 FPS patch

In `fefw_setup.exe`, enable **60 FPS (Kompas9, experimental)**, save and restart the game. It is disabled by default and can be turned off in the same setup tool. The integration reproduces both changes from [Kompas9’s 60FPS v1.0 patch](https://github.com/Kompas9/Switch-Game-Patches/tree/9e553571c986fbd45ef55202cb8ec0e1179a3db9/games/Fire-Emblem-Fortunes-Weave): presentation interval and timing rate. The Build ID and both original instructions are checked before anything is applied.

Both instruction variants are compiled natively. The option acts on the private image in memory without modifying the original game folder or requiring an emulator or an IPS file in the program folder. It is stored as `[mods] fps60` in `fefw.ini` and sets the host frame limit to 60 when enabled. It does not guarantee sustained 60 FPS or correct timing throughout the game; combat and longer cinematics need further validation.

To upgrade from preview.1–preview.4, close the game, extract the new ZIP into the same folder and open the setup tool. Saving updates the local preparation and reuses existing shaders. Settings and saves are preserved. Changing an INI value in an old executable does not add the new functionality: the updated version must be compiled.

## Building the setup tool from source

Building the standalone setup tool does not require game files. From the repository root, in PowerShell:

```powershell
. .\project\tools\Initialize-BuildEnv.ps1
cmake -S project -B work/setup -G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_BUILD_TYPE=Release -DFEFW_BUILD_GAME=OFF
cmake --build work/setup --target fefw_setup fefw_config_tests fefw_mods_tests --parallel 1
ctest --test-dir work/setup --output-on-failure
Copy-Item work/setup/fefw_setup.exe ./fefw_setup.exe
.\fefw_setup.exe
```

Continue to the next step only after the previous one succeeds. Sources and resources are in `project/src/`, and the build is defined in `project/CMakeLists.txt`. The standalone setup tool does not link the runtime or translated game code.

The game executable and its caches are generated later on your own machine. Do not attach them to issues, repositories, releases or CI artifacts.

## Licenses, credits and contributions

The base project’s [GNU GPL v3](LICENSE) and each file’s license notices are retained, including the later-version option where applicable. Third-party components retain their licenses: see [provenance](NOTICE.md) and [attributions](THIRD_PARTY_NOTICES.md). These licenses cover the identified software and do not grant rights to the game.

When reporting an issue, describe the behavior, system, GPU and revision without attaching game material. Remove personal paths, identifiers and credentials from shared messages. See [CONTRIBUTING.md](CONTRIBUTING.md).

The repository contains source files only, with a manifest and an auditor for files and Git history. The release ZIP adds the standalone setup binary alongside its corresponding source. See [publication instructions](GITHUB-PUBLISH.md). These checks reduce the risk of accidental disclosure; they do not certify the legality of every use or replace legal advice.
