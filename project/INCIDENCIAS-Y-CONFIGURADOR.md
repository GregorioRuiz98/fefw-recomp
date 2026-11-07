# Incidencias y configuración — 7 de noviembre de 2026

Se han implementado el configurador y el empaquetado solicitado. Las correcciones gráficas pasan sus regresiones y el prólogo funciona con validación Vulkan activa. La eliminación del parpadeo intenso en la escena concreta del usuario todavía requiere su comprobación.

## Corregido

- El escalado espacial conservaba solo rectángulos enteros e ignoraba el desplazamiento fraccionario de cámara. Ahora utiliza los parámetros NVN2 registrados por valor y un shader propio con muestreo bilineal. La regresión falla antes de la corrección y pasa después. La primera configuración activa sincronización vertical.
- Se liberaban imágenes y vistas que la GPU todavía utilizaba al presentar la pantalla. El retiro de estas imágenes espera también las entregas del presentador. Una prueba con la cola bloqueada reproduce el error anterior y pasa con la corrección.
- Un shader de cálculo declaraba lectura flotante sobre una textura de enteros de ocho bits. Se especializa su tipo y se conservan los bits extendidos del resultado. Se comprueban enteros con y sin signo, cambio de textura y otra lectura normalizada que comparte el tipo original.
- Las vistas 2D de imágenes 3D se utilizaban sin habilitar la capacidad y las banderas necesarias. Ahora se consulta y habilita el soporte real del dispositivo y se crea la imagen con las banderas correspondientes.
- Al cerrar el backend quedaban dos buffers temporales sin liberar. La prueba comprueba que el contador de asignaciones vuelve a cero.
- La preparación local podía depender de un SDK .NET 10 ya instalado, generar rutas C++ con barras incorrectas o buscar el traductor en una ruta distinta de la creada por el entorno x64. El SDK se prepara localmente cuando falta y las rutas y plataforma de compilación quedan fijadas. La preparación rechaza una caché incompleta de shaders.

## Configuración implementada

`fefw.exe` abre el asistente cuando falta una configuración válida. `fefw_setup.exe` permite abrirlo siempre y comparte los mismos ajustes.

El asistente selecciona la carpeta con `exefs` y `romfs`, reconoce las actualizaciones compatibles, verifica las huellas de los seis módulos y ofrece únicamente idiomas presentes en la copia. Permite elegir resolución de salida, ventana o pantalla completa, idioma y sincronización vertical. La resolución interna del juego conserva su valor original; pantalla completa utiliza el escritorio.

«No mostrar la configuración al iniciar» solo se aplica después de guardar una configuración válida. Si faltan recursos o se mueve la carpeta, se vuelve al asistente. Se conservan asignaciones de mando y teclado, otros ajustes y partidas. Cancelar una preparación detiene sus procesos sin marcarla como terminada.

## Comprobado

Pasan los 15 checks ejecutables, 32 pruebas Python, 32 comprobaciones nativas de configuración, siete grupos CTest, cuatro casos GPU de constantes/escalado y tres regresiones gráficas adicionales. La integración del configurador pasa 14 comprobaciones, incluido arranque en español a 1600 × 900 y sincronización vertical.

En el recorrido de 160 segundos, la validación Vulkan pasa de 32 mensajes de error de cinco tipos a cero. Se conservan 157 fotogramas consecutivos válidos de dos intervalos del diálogo 3D, sin destellos aislados según el detector utilizado. Un PNG vacío corresponde al corte del diagnóstico mientras se escribía; no se trata como imagen negra. Estas mediciones no demuestran equivalencia completa con la consola ni ausencia de todo temblor temporal.

La preparación completa desde una carpeta de trabajo vacía genera el código de los seis módulos y los 11.977 shaders; los 85 códigos con varios perfiles pasan la comparación de salidas validadas. Las herramientas del sistema estaban instaladas y vcpkg reutilizó su caché de bibliotecas. Las regresiones gráficas también pasan en esa compilación.

## Distribución

El paquete privado del usuario contiene el ejecutable nativo y datos generados a partir de su copia. El ZIP público incluye únicamente el configurador independiente, fuentes, parches, herramientas y licencias. Cada usuario aporta su carpeta original y genera sus componentes localmente.

Se excluyen originales, ExeFS/ROMFS normalizados, claves, texturas, audio, vídeos, shaders comerciales extraídos o traducidos, C++ generado y el EXE privado. Publicar esos datos en formato carpeta también sería publicar contenido del juego. Se revisan carpeta y ZIP mediante una lista explícita de archivos permitidos y hashes. No se ha subido nada a GitHub.

## Límites pendientes

Confirmar el parpadeo en el recorrido del usuario, probar mando físico, combate, guardar/cargar y estabilidad prolongada. El audio REV16 sigue siendo experimental. El sustituto espacial de DLSS no realiza reconstrucción temporal. El reporte detallado y las huellas de los binarios están en `reports/setup-and-flicker.md` y `reports/setup-delivery.json`.
