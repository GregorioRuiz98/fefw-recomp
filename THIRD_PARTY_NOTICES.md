# Componentes y atribuciones

Estos avisos acompañan al software y los parches. Las referencias al motor y sus dependencias describen componentes obtenidos para la construcción local; no indican que todos estén enlazados en el configurador público.

| Componente | Procedencia y licencia | Aviso conservado |
| --- | --- | --- |
| pokopia-recomp / nxrecomp / nxrt | [Revisión fijada](https://github.com/drakolordx7/pokopia-recomp/tree/2c29515b23a6d78d967775fd7e9f24f50402cca7); GPL v3 y cabeceras específicas | `LICENSE`; `project/licenses/upstream-runtime-NOTICES.md`; `project/licenses/upstream-recomp-NOTICES.md` |
| SuperMarioWonderRecomp | Módulos adaptados; MIT, BelmanteGu | `project/licenses/SuperMarioWonderRecomp-MIT.txt` |
| Ryujinx / Ryubing | Traductor y contexto de parches; MIT, Ryujinx Team and Contributors | `project/licenses/Ryubing-MIT.txt` |
| Mesa NAK | Referencias de decodificación; MIT, titulares indicados | `project/licenses/mesa-nak-MIT.txt` |
| smo-recomp y referencias GPL del motor | GNU GPL v3; alcance en los avisos del motor | `project/licenses/smo-recomp-GPL-3.0.txt` |
| nxnative | Adaptaciones del traductor; MIT, seedds | `project/licenses/nxnative-MIT.txt` |
| libvpx | Biblioteca de vídeo del host; BSD e ISC según sus archivos | `project/licenses/libvpx-copyright.txt` |
| dav1d y código incorporado | Biblioteca de vídeo del host; licencias del aviso completo | `project/licenses/dav1d-copyright.txt` |

Los documentos `upstream-*-NOTICES.md` se conservan desde la revisión base y recogen atribuciones adicionales, incluido código MIT, GPL, Apache con excepciones LLVM y dependencias gráficas. Las licencias de otras dependencias descargadas, como ASTC/BC7, SDL, Vulkan y herramientas de construcción, permanecen en sus fuentes y se aplican según el componente. Sus binarios no se incluyen en este paquete del configurador.

El configurador se enlaza con bibliotecas de Windows y su biblioteca estándar de C++, sin enlazar juego ni runtime gráfico. Sus fuentes y archivos de construcción están en `project/src/` y `project/CMakeLists.txt`. Las herramientas de desarrollo del sistema no se incluyen en esta distribución.

Comunica atribuciones incompletas indicando componente, procedencia y aviso, sin adjuntar material propietario. Este inventario no atribuye al proyecto la titularidad de componentes ajenos ni cambia sus licencias.
