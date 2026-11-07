# FE: Fortune’s Weave Recomp — herramientas de preparación local

Proyecto experimental de recompilación estática para Windows x64, basado en [pokopia-recomp](https://github.com/drakolordx7/pokopia-recomp/tree/2c29515b23a6d78d967775fd7e9f24f50402cca7). Las herramientas traducen y compilan localmente código y shaders a partir de una carpeta aportada por el usuario.

**Aquí se distribuyen herramientas, fuentes y parches; el juego no está incluido.** Necesitas una copia obtenida legítimamente y los derechos necesarios para los actos de copia, extracción, transformación y uso que realices. Poseer el juego no garantiza que todos esos actos estén permitidos en tu jurisdicción. Consulta [LEGAL.md](LEGAL.md).

Este proyecto no está afiliado, patrocinado ni aprobado por Nintendo, Intelligent Systems ni otros titulares. Fire Emblem y Nintendo se mencionan para identificar compatibilidad; sus marcas y derechos pertenecen a sus respectivos titulares.

## Descargar y abrir el configurador

**[Descargar FEFW Recomp para Windows x64 — v0.1.0-preview.2](https://github.com/GregorioRuiz98/fefw-recomp/releases/download/v0.1.0-preview.2/FEFW-Recomp-Windows-x64-v0.1.0-preview.2.zip)** · [Notas y archivos del release](https://github.com/GregorioRuiz98/fefw-recomp/releases/tag/v0.1.0-preview.2)

Extrae **todo el ZIP** y abre `fefw_setup.exe`. El configurador está dentro de esa descarga; las opciones **Code → Download ZIP** y **Source code** de GitHub solo contienen fuentes, sin el ejecutable del configurador. Mientras el repositorio sea privado, necesitas acceso e iniciar sesión para descargarlo.

Puedes elegir la carpeta superior que contiene `updates/131072`, o directamente `updates/131072` si esta contiene `exefs` y `romfs`. El selector detecta la actualización compatible y los idiomas presentes. No cambies ni muevas tus archivos originales para ajustarlos al paquete.

La primera preparación requiere las herramientas indicadas abajo y compila en tu PC el ejecutable `fefw.exe` y los shaders. **Seleccionar la carpeta no equivale a iniciar inmediatamente una instalación nueva.** Cuando termine, **Guardar y jugar** abre el juego. Después podrás abrir `fefw.exe` directamente si activaste **No mostrar la configuración al iniciar**; `fefw_setup.exe` seguirá disponible para cambiar idioma y pantalla.

## Contenido de la entrega

- Configurador Win32 independiente y sus fuentes correspondientes.
- Configuración del título, scripts de preparación y parche contra una revisión fijada del motor abierto.
- Pruebas sintéticas propias, comprobaciones de publicación y avisos de licencia.

No se distribuyen ROM, claves, firmware, SDK propietario, actualizaciones, recursos audiovisuales, shaders comerciales, C++ generado ni el ejecutable recompilado del juego. No se proporcionan enlaces a descargas del juego ni instrucciones para eludir medidas técnicas de protección. Las herramientas reciben una carpeta ya extraída; solo descargan las dependencias de desarrollo declaradas.

## Estado

Se han comprobado el configurador, la preparación desde una carpeta de trabajo vacía y escenas 3D del prólogo. En la revisión preparada pasan 15 comprobaciones ejecutables; un recorrido de 160 segundos termina sin errores Vulkan y una muestra de 157 fotogramas consecutivos no muestra destellos aislados. Las bibliotecas de vídeo pudieron reutilizar la caché vcpkg del equipo de prueba.

Esto no demuestra jugabilidad completa ni equivalencia con la consola. Combate, guardar/cargar, estabilidad prolongada y mando físico siguen pendientes. El audio REV16 y el sustituto espacial de DLSS son experimentales; este último no implementa reconstrucción temporal. Confirmar la desaparición del parpadeo intenso en otros recorridos sigue pendiente.

## Requisitos

- Windows x64 y GPU/controlador compatibles con Vulkan 1.3.
- Python 3.12 o posterior, Git, CMake, Ninja y LLVM con clang-cl.
- Visual C++ Build Tools y Windows SDK instalados previamente.
- Conexión para preparar las dependencias y espacio para código generado, objetos y cachés.
- Tu carpeta compatible, ya extraída con los derechos necesarios, con `exefs` y `romfs`.

Si falta .NET SDK 10, se prepara la versión 10.0.403 localmente y se verifica su SHA-512. No se instalan servicios ni se modifica el PATH permanente. La preparación inicial puede tardar bastante.

Se reconoce el Title ID `04007EF01F8AE000`, main Build ID `DB93B0125F4850601D61C3EDDE012F8EB2ECFE77`. Las huellas están en `project/supported-build.json`; un nombre de carpeta no demuestra compatibilidad. Una actualización aislada puede necesitar recursos de la base, que estas herramientas no descargan.

## Usar el ZIP del configurador

1. Extrae la entrega pública en una carpeta local.
2. Abre `fefw_setup.exe` y selecciona tu carpeta con `exefs` y `romfs`, o su carpeta superior con actualizaciones compatibles.
3. Elige idioma, resolución de salida, ventana o pantalla completa y sincronización vertical. Solo se ofrecen idiomas encontrados en tu copia.
4. Deja terminar la preparación y pulsa **Guardar y jugar**. Puedes cancelar y reanudar; `preparation.log` conserva el detalle.

**No mostrar la configuración al iniciar** permite abrir después `fefw.exe` directamente. El configurador permanece disponible y se vuelve a él si faltan archivos. Se conservan partidas locales en `save` y mapeos existentes.

La resolución de salida es el tamaño de la ventana; la resolución interna conserva su valor original. Pantalla completa utiliza el escritorio. La primera configuración activa sincronización vertical. Consulta [los controles de mando y teclado](project/CONTROLES-MANDO.md).

La versión preview.2 busca mandos al arrancar y vuelve a comprobarlos cada segundo. Para recuperar un DualShock 4 u otro dispositivo puedes usar **F1 → Settings → Controls → Gamepad → Detectar mandos**. Mando y teclado funcionan simultáneamente; cierra F1 para devolver el control al juego.

## Parche opcional de 60 FPS

En `fefw_setup.exe`, activa **60 FPS (Kompas9, experimental)**, guarda y reinicia el juego. Está desactivado inicialmente y se puede desactivar desde el mismo configurador. La integración reproduce los dos cambios del [parche 60FPS v1.0 de Kompas9](https://github.com/Kompas9/Switch-Game-Patches/tree/9e553571c986fbd45ef55202cb8ec0e1179a3db9/games/Fire-Emblem-Fortunes-Weave): intervalo de presentación y tasa de temporización. Se comprueban el Build ID y las dos instrucciones antes de aplicar nada.

Ambas variantes están compiladas de forma nativa. La opción actúa sobre la imagen privada en memoria, sin modificar tu carpeta original ni exigir un emulador o copiar un IPS a la carpeta del programa. El ajuste se conserva como `[mods] fps60` en `fefw.ini` y fija el límite del host en 60 al activarlo. No garantiza rendimiento sostenido de 60 FPS ni equivalencia temporal en todo el juego; combate y cinemáticas prolongadas requieren validación adicional.

Para actualizar desde preview.1, extrae el nuevo ZIP sobre la misma carpeta y abre el configurador con el juego cerrado. Al guardar se actualizará la preparación local y se reutilizarán los shaders ya preparados. Se conservan ajustes y partidas. No basta con cambiar un número en el INI de un ejecutable antiguo: la nueva versión tiene que compilarse.

## Construir el configurador desde las fuentes

No necesitas archivos del juego para construir el configurador. Desde la raíz, en PowerShell:

```powershell
. .\project\tools\Initialize-BuildEnv.ps1
cmake -S project -B work/setup -G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_BUILD_TYPE=Release -DFEFW_BUILD_GAME=OFF
cmake --build work/setup --target fefw_setup fefw_config_tests fefw_mods_tests --parallel 1
ctest --test-dir work/setup --output-on-failure
Copy-Item work/setup/fefw_setup.exe ./fefw_setup.exe
.\fefw_setup.exe
```

Continúa al siguiente paso solo si el anterior termina correctamente. Las fuentes y recursos están en `project/src/`; la construcción se define en `project/CMakeLists.txt`. Este configurador no enlaza el runtime ni código traducido del juego.

El ejecutable del juego y sus cachés se generan posteriormente en tu equipo. No los adjuntes a incidencias, repositorios, releases ni artefactos de CI.

## Licencias, créditos y contribuciones

Se conserva [GNU GPL v3](LICENSE) de la base y las indicaciones de cada archivo, incluida la opción de versiones posteriores donde corresponda. Los componentes ajenos mantienen sus licencias: [procedencia](NOTICE.md) y [atribuciones](THIRD_PARTY_NOTICES.md). Estas licencias cubren el software indicado y no conceden derechos sobre el juego.

Para comunicar un problema, describe el comportamiento, sistema, GPU y revisión sin adjuntar material del juego. Retira de cualquier mensaje rutas personales, identificadores y credenciales. Consulta [CONTRIBUTING.md](CONTRIBUTING.md).

El repositorio preparado contiene solo fuentes y tiene un manifiesto y un auditor de archivos e historial. El ZIP público añade el configurador independiente con sus fuentes. [Preparación de la subida](GITHUB-PUBLISH.md). Estos controles reducen riesgos de distribución accidental; no certifican la legalidad de todos los usos ni sustituyen una valoración jurídica.
