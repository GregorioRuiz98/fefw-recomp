# Preparar la subida

Usa solo la carpeta de fuentes preparada para GitHub. El ZIP público es un artefacto separado con configurador independiente, fuentes correspondientes y licencias. El paquete local que ejecuta el juego no debe usarse como release.

Desde la carpeta del repositorio:

```powershell
python project/tools/audit_public_repo.py --repo . --history
git status --short
git log --oneline --all
```

Si hay cambios posteriores a la revisión, actualiza el manifiesto y vuelve a comprobarlos según `CONTRIBUTING.md`. `.gitignore` no elimina archivos ya versionados.

Para un repositorio de GitHub vacío, sustituye `OWNER/REPO` por el destino elegido:

```powershell
git remote add origin https://github.com/OWNER/REPO.git
git push -u origin codex/public-release
```

Si ya contiene trabajo, integra mediante una rama y revisa el diff. No fuerces la subida ni guardes tokens en URLs o archivos.

Publica como release únicamente el ZIP público auditado con el configurador y sus fuentes correspondientes. Mantén `LICENSE`, avisos y construcción junto al binario. No adjuntes `fefw.exe`, ExeFS/ROMFS, cachés, guardados, capturas o resultados de compilación del juego.

Estas comprobaciones no constituyen una autorización de sus titulares ni un dictamen sobre la legalidad del proyecto en todas las jurisdicciones.
