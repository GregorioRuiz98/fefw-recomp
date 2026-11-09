# Native settings design

Mode: Operate. Reference: Goldoire/fefw-recomp launcher/src/theme.css, layout.css and pages/settings.js at 45a1904ca81995bfea39cb7aa1012851d3aa84d8 (GPL-3.0-or-later). Adaptation retains native Win32 controls and this project's local setup workflow; no web runtime, vendor upscaler or game art is added.

Use the reference's navy surfaces (#14152a / #1c1d38 / #0f1020), pale text (#e6ebf3), muted labels (#98a3b6) and teal accent (#40ccba). Sidebar lists Game files, Picture, Performance, Window, Startup and Updates. Highlight the current section. Separate engine output/internal resolution from window presentation, and separate frame-rate changes from image quality. Give each choice a nearby explanation and restart guidance.

Keep navigation, status and Save/Save and play visible while the content pane scrolls. The sidebar narrows below 850 logical pixels. Minimum window follows the available desktop work area; controls use DPI-aware layout. Native edit, combo and checkbox roles and keyboard operation are retained. Use system colours if Windows high contrast is enabled.

Pending changes remain across navigation, appear in the footer, and can be reverted without writing files. Closing asks before discarding pending changes. Preparation still permits stopping and resuming completed work. Network updates remain opt-in and preserve local files. The game remains controller-only; setup navigation uses the normal keyboard and mouse.

Verification: normal, compact and 144-DPI native captures; all six sections, empty-folder and preparing states; dirty/revert and custom-resolution preview; existing config-preservation integration. High-contrast colour fallback exists but has not been visually tested under an OS high-contrast theme.
