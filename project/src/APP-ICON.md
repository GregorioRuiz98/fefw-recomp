# Woven Shield application icon

Created for this independent project on 2026-11-10 using OpenAI's built-in image generation tool, with no reference images or game files supplied. The design is a navy shield with angular copper and turquoise ribbons. It does not use Nintendo or Fire Emblem artwork, characters, logos or extracted resources, and is not an official franchise emblem.

The original prompt requested a single transparent desktop icon with broad interwoven ribbons, an open diamond-shaped centre, restrained metallic highlights and clear small-size legibility. It expressly excluded text, existing brands, franchise iconography, characters, crowns, swords, dragons and flames.

`fefw_icon.json` contains the complete exported artwork as standard Windows icon resources in 16, 20, 24, 32, 48, 64, 128 and 256 pixel sizes. CMake decodes the embedded ICO using Windows PowerShell, verifies its SHA-256 and compiles it into both executables. No asset download or image-processing dependency is needed to build or run them. Text storage keeps the existing source-only updater compatible. The source PNG's SHA-256 is recorded alongside the ICO data. `tools/export_app_icon.py` is an optional authoring tool for exporting a replacement original PNG (requires Pillow); it is not run during setup.

The project's contribution to these resources is distributed under GPL-3.0-or-later, to the extent copyright applies. This provenance record is not a trademark clearance or a claim of affiliation.
