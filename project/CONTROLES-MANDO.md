# Control con mando

Conecta el mando a Windows y abre **fefw.exe**. La detección es automática; también puedes conectarlo con el juego abierto. No hace falta activar un emulador ni desactivar el teclado.

| Juego | Xbox | PlayStation | Nintendo Pro |
| --- | --- | --- | --- |
| A / confirmar | A | Cruz | A |
| B / volver | B | Círculo | B |
| X | X | Cuadrado | X |
| Y | Y | Triángulo | Y |
| L / R | LB / RB | L1 / R1 | L / R |
| ZL / ZR | LT / RT | L2 / R2 | ZL / ZR |
| PLUS / pausa o saltar vídeo | Menu / Start | Options | + |
| MINUS | View / Back | Create / Share | − |
| Cruceta | Cruceta | Cruceta | Cruceta |
| Sticks y sus pulsaciones | Ambos sticks | Ambos sticks | Ambos sticks |

El mapeo de los cuatro botones principales sigue las letras del mando cuando SDL las identifica. En PlayStation se usan las equivalencias de la tabla. Los avisos que dibuja el juego conservan sus símbolos originales.

Los ajustes están en **controls.ini**, secciones `[pad]` y `[padmap]`; también puedes abrir los ajustes del runtime con **F1**. La zona muerta predeterminada es del 10 % en ambos sticks y los gatillos se activan al 35 % de recorrido. La vibración y los sensores están habilitados cuando el dispositivo los ofrece; no se ha comprobado su comportamiento físico en este juego.

Al desconectar el mando se liberan botones y sticks, y el teclado sigue disponible. Si no responde, comprueba en `fefw.log` que aparezca `gamepad ... added` con su nombre. La compatibilidad depende de que Windows y SDL reconozcan el dispositivo.

Se ha probado la ruta completa SDL → lectura por servicios → HID con mandos virtuales de las familias Xbox, PlayStation y Nintendo Switch, incluidos botones, gatillos, sticks, zona muerta y conexión/desconexión. Falta la prueba con tu mando físico y una partida completa.
