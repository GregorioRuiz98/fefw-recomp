# Contribuir

Revisa [LEGAL.md](LEGAL.md), conserva cabeceras de licencia y documenta la procedencia de código ajeno. Aporta solo material para el que dispongas de derechos suficientes; las licencias del software existente no cubren archivos del juego.

No adjuntes ROM, claves, recursos, shaders comerciales, C++ generado, ejecutables locales ni fixtures extraídos. Las pruebas públicas deben usar datos sintéticos propios. Describe los problemas visuales con texto y retira rutas personales, identificadores y credenciales de los mensajes que compartas.

Explica qué cambia, cómo lo comprobaste y qué queda pendiente. Las pruebas privadas con una copia local no deben adjuntar sus datos al repositorio o a GitHub Actions.

Desde la carpeta preparada para GitHub, revisa lo que vas a añadir y actualiza el manifiesto:

```powershell
git add --all
python project/tools/audit_public_repo.py --repo . --write-manifest
git add source-manifest.json
python project/tools/audit_public_repo.py --repo . --history
```

El auditor comprueba el índice de Git, el manifiesto y, con `--history`, los objetos alcanzables por sus referencias. No sustituye la revisión de procedencia o licencias. Eliminar un archivo del último commit no elimina sus copias ni el historial remoto.
