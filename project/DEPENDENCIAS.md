# Dependencias preparadas en esta sesión

La construcción se ha ejecutado en Windows x64. El manifiesto exacto está en `reports/toolchain.json`; los logs se conservan en `reports`.

| Dependencia | Versión/revisión | Ubicación relativa a `work` |
| --- | --- | --- |
| Visual C++ Build Tools 2022 | paquete 17.14.44, instalación 17.14.37827.9 | instalación del sistema |
| Windows SDK | 10.0.26100.0 | instalación del sistema |
| LLVM clang-cl | 22.1.8 | `C:/Program Files/LLVM/bin` |
| .NET SDK | 10.0.403 | `toolchains/dotnet` |
| Ryubing | `a82350bb774f70fcbd41c9987bf67a3775409963` | `refs/ryubing` |
| SPIRV-Tools | `ef96ed763b43b59b33b31b362f09a02b729fa1c9` | `refs/spirv-tools` |
| SPIRV-Headers | `496543121ce6419f23d6fa5d7194ba66c36212d2` | `refs/spirv-tools/external/spirv-headers` |
| astc-encoder | 5.7.0 | `engine/tools/textures/third_party/astc-encoder` |
| bc7enc_rdo | `b9438627eef73a1157e84201b6fa6eb2ffd6d9f0` | `engine/tools/textures/third_party/bc7enc_rdo` |
| ISPC | 1.32.0 | `engine/tools/textures/third_party/ispc` |
| NVIDIA nvdisasm | 13.3.29, archivo verificado con SHA oficial | `toolchains/nvdisasm` |

El motor descarga sus dependencias Vulkan, SDL e ImGui mediante su configuración CMake. Las bibliotecas de texturas se construyeron siguiendo `engine/tools/textures/THIRD_PARTY.md` y su CMakeLists. La herramienta nvdisasm se usó para diagnóstico, sin instalar CUDA ni sustituir el controlador gráfico.

`tools/Initialize-BuildEnv.ps1` localiza Visual Studio mediante vswhere y activa su entorno. No se necesita abrir manualmente una consola especial para las acciones Configure/Build/NativeTest/FloatingPointTest.

El traductor está construido en `engine/tools/shaders/build/bin/Release/net10.0/spirv_harness.dll`. Sus fuentes de Ryubing se prepararon en `engine/tools/shaders/third_party/ryubing` con los tres proyectos que requiere su harness. El validador independiente está en `build/spirv-tools/tools/spirv-val.exe`. Son rutas que las acciones TranslateShaders/ValidateShaders comprueban.

`tools/Prepare-Game.ps1` automatiza la preparación local a partir de una carpeta original compatible. `Bootstrap` prepara Python y el motor; `tools/Prepare-Dependencies.ps1` descarga y construye las dependencias fijadas, incluyendo .NET SDK 10.0.403 local si falta. Se verifica el SHA-512 del SDK y el SHA-256 de ISPC. No se instalan servicios ni se modifica el PATH permanente. La compilación del traductor fija `Platform=AnyCPU` para que el entorno C++ x64 no cambie su ruta de salida.

La traducción generada ocupa unos 637 MB; los objetos, bibliotecas, mapas y EXE consumen espacio adicional. No se ha medido el tiempo de una reconstrucción optimizada completa. `/Od` se eligió para comprobar la integración inicial; `/O1` se puede solicitar con `FEFW_LIFT_OPT` en CMake cuando el arranque sea funcional.

## Vídeo del host (2026-11-06)

Se añadieron libvpx 1.16.0#3 y dav1d 1.5.4, ambas `x64-windows-static`, con vcpkg `661e42cd1600603016ced1d76a15359e7dade9f5`. La instalación privada está en `work/toolchains/vcpkg/installed/x64-windows-static`; CMake la detecta automáticamente. Para reproducirla, usar ese commit de microsoft/vcpkg, preparar su ejecutable con `bootstrap-vcpkg.bat -disableMetrics` e instalar `libvpx:x64-windows-static dav1d:x64-windows-static --disable-metrics`. Después ejecutar Configure y Build. Sus avisos están en `licenses/`.

El decodificador AV1 admite salida CPU de ocho bits y 4:2:0; las imágenes nativas que pide el juego ya se conectan a texturas del host. La reproducción completa conserva una incidencia de pérdida del dispositivo pendiente de investigar.
