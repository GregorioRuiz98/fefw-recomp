# Importer source

The extraction, NCZ decompression, NUP and NX adapters and their log strings come from
[siriusgoldsmith/NxExtractor](https://github.com/siriusgoldsmith/NxExtractor/tree/9cd476ee21fcb5b9bc4f91c869a7363c52187238),
commit `9cd476ee21fcb5b9bc4f91c869a7363c52187238` (MIT).
The license is in `licenses/NxExtractor-LICENSE.txt`.

This console integration compiles only Extractor.cs, Ncz.cs, Nup.cs, NxFormat.cs,
Lang.cs and our Program.cs. It does not compile the GUI, converter or firmware patcher.
Our NX adapter loads the release's adjacent native libraries instead of extracting
embedded libraries into another application's local data folder.

LibHac 0.19.0 and ZstdSharp.Port 0.8.8 are pinned NuGet dependencies.
The NX adapter uses NXFormat 1.0.4 (MIT); see `licenses/NXFormat-LICENSE.txt`.
Build-Importer.ps1 verifies the pinned native library before packaging it. Its
standard Visual C++ x64 runtime dependency is supplied by the required Build Tools.
No game content or keys are included. Encrypted inputs require keys provided by the user.
