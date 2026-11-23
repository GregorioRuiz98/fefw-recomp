namespace NxExtractor;

/// <summary>Textos de la interfaz y del log. Orden de columnas: en, es, fr, ja, de.</summary>
public static class L
{
    public static readonly (string Code, string Name)[] Languages =
    {
        ("en", "English"), ("es", "Español"), ("fr", "Français"), ("ja", "日本語"), ("de", "Deutsch")
    };

    private static int _index;

    public static string Current
    {
        get => Languages[_index].Code;
        set
        {
            int i = Array.FindIndex(Languages, l => l.Code == value);
            _index = i < 0 ? 0 : i;
        }
    }

    public static string CurrentName => Languages[_index].Name;

    public static string T(string key) =>
        Strings.TryGetValue(key, out var s) ? s[_index] : key;

    public static string F(string key, params object[] args) => string.Format(T(key), args);

    private static readonly Dictionary<string, string[]> Strings = new()
    {
        // ---------- Interfaz ----------
        ["language"] = new[] { "Language", "Idioma", "Langue", "言語", "Sprache" },
        ["chooseLanguage"] = new[] { "Choose the interface language:", "Elige el idioma de la interfaz:", "Choisissez la langue de l’interface :", "表示言語を選択してください:", "Sprache der Oberfläche wählen:" },
        ["ok"] = new[] { "OK", "Aceptar", "OK", "OK", "OK" },
        ["cancel"] = new[] { "Cancel", "Cancelar", "Annuler", "キャンセル", "Abbrechen" },
        ["keys"] = new[] { "Keys", "Claves", "Clés", "キー", "Schlüssel" },
        ["titleKeysOpt"] = new[] { "title.keys (optional)", "title.keys (opcional)", "title.keys (facultatif)", "title.keys (任意)", "title.keys (optional)" },
        ["filterKeys"] = new[] { "Keys (*.keys)|*.keys|All files (*.*)|*.*", "Keys (*.keys)|*.keys|Todos (*.*)|*.*", "Clés (*.keys)|*.keys|Tous les fichiers (*.*)|*.*", "キー (*.keys)|*.keys|すべてのファイル (*.*)|*.*", "Schlüssel (*.keys)|*.keys|Alle Dateien (*.*)|*.*" },
        ["filterRoms"] = new[]
        {
            "Switch ROMs (*.nsp;*.nsz;*.xci;*.xcz;*.nup;*.nx)|*.nsp;*.nsz;*.xci;*.xcz;*.nup;*.nx|All files (*.*)|*.*",
            "Roms de Switch (*.nsp;*.nsz;*.xci;*.xcz;*.nup;*.nx)|*.nsp;*.nsz;*.xci;*.xcz;*.nup;*.nx|Todos (*.*)|*.*",
            "ROM Switch (*.nsp;*.nsz;*.xci;*.xcz;*.nup;*.nx)|*.nsp;*.nsz;*.xci;*.xcz;*.nup;*.nx|Tous les fichiers (*.*)|*.*",
            "Switch ROM (*.nsp;*.nsz;*.xci;*.xcz;*.nup;*.nx)|*.nsp;*.nsz;*.xci;*.xcz;*.nup;*.nx|すべてのファイル (*.*)|*.*",
            "Switch-ROMs (*.nsp;*.nsz;*.xci;*.xcz;*.nup;*.nx)|*.nsp;*.nsz;*.xci;*.xcz;*.nup;*.nx|Alle Dateien (*.*)|*.*"
        },
        ["tabExtract"] = new[] { "Extract", "Extraer", "Extraire", "展開", "Extrahieren" },
        ["tabConvert"] = new[] { "Convert", "Convertir", "Convertir", "変換", "Konvertieren" },
        ["game"] = new[] { "Game", "Juego", "Jeu", "ゲーム", "Spiel" },
        ["baseGame"] = new[] { "Base game", "Juego base", "Jeu de base", "ベースゲーム", "Basisspiel" },
        ["update"] = new[] { "Update", "Update", "Mise à jour", "アップデート", "Update" },
        ["dlc"] = new[] { "DLC", "DLC", "DLC", "DLC", "DLC" },
        ["add"] = new[] { "Add...", "Añadir...", "Ajouter...", "追加...", "Hinzufügen..." },
        ["remove"] = new[] { "Remove", "Quitar", "Retirer", "削除", "Entfernen" },
        ["clearList"] = new[] { "Clear", "Vaciar", "Vider", "クリア", "Leeren" },
        ["output"] = new[] { "Output", "Salida", "Sortie", "出力", "Ausgabe" },
        ["folder"] = new[] { "Folder", "Carpeta", "Dossier", "フォルダー", "Ordner" },
        ["nacpIcons"] = new[] { "control.nacp + icons", "control.nacp + iconos", "control.nacp + icônes", "control.nacp + アイコン", "control.nacp + Symbole" },
        ["extractBtn"] = new[] { "Extract", "Extraer", "Extraire", "展開", "Extrahieren" },
        ["filesGroup"] = new[] { "Files (NSP / NSZ / XCI / XCZ) — drag here", "Archivos (NSP / NSZ / XCI / XCZ) — arrastra aquí", "Fichiers (NSP / NSZ / XCI / XCZ) — glissez-les ici", "ファイル (NSP / NSZ / XCI / XCZ) — ここにドラッグ", "Dateien (NSP / NSZ / XCI / XCZ) — hierher ziehen" },
        ["toNsp"] = new[] { "to NSP (XCI → NSP, NSZ → NSP)", "a NSP (XCI → NSP, NSZ → NSP)", "en NSP (XCI → NSP, NSZ → NSP)", "NSP へ (XCI → NSP, NSZ → NSP)", "nach NSP (XCI → NSP, NSZ → NSP)" },
        ["toNsz"] = new[] { "to NSZ (NSP → NSZ, XCI → NSZ)", "a NSZ (NSP → NSZ, XCI → NSZ)", "en NSZ (NSP → NSZ, XCI → NSZ)", "NSZ へ (NSP → NSZ, XCI → NSZ)", "nach NSZ (NSP → NSZ, XCI → NSZ)" },
        ["zstdLevel"] = new[] { "zstd level:", "Nivel zstd:", "Niveau zstd :", "zstd レベル:", "zstd-Stufe:" },
        ["conversion"] = new[] { "Conversion", "Conversión", "Conversion", "変換設定", "Konvertierung" },
        ["outputFolder"] = new[] { "Output folder", "Carpeta de salida", "Dossier de sortie", "出力フォルダー", "Ausgabeordner" },
        ["convertBtn"] = new[] { "Convert", "Convertir", "Convertir", "変換", "Konvertieren" },

        // ---------- Mensajes ----------
        ["errProd"] = new[] { "Select a valid prod.keys file.", "Selecciona un prod.keys válido.", "Sélectionnez un fichier prod.keys valide.", "有効な prod.keys を選択してください。", "Wähle eine gültige prod.keys-Datei." },
        ["errTitle"] = new[] { "The title.keys file does not exist.", "El title.keys no existe.", "Le fichier title.keys n’existe pas.", "title.keys が見つかりません。", "Die title.keys-Datei existiert nicht." },
        ["errNoInput"] = new[] { "Add at least a game, update or DLC.", "Añade al menos un juego, update o DLC.", "Ajoutez au moins un jeu, une mise à jour ou un DLC.", "ゲーム、アップデート、DLC のいずれかを追加してください。", "Füge mindestens ein Spiel, Update oder DLC hinzu." },
        ["errMissing"] = new[] { "One of the input files does not exist.", "Algún archivo de entrada no existe.", "Un des fichiers d’entrée n’existe pas.", "存在しない入力ファイルがあります。", "Eine der Eingabedateien existiert nicht." },
        ["errOut"] = new[] { "Select an output folder.", "Selecciona una carpeta de salida.", "Sélectionnez un dossier de sortie.", "出力フォルダーを選択してください。", "Wähle einen Ausgabeordner." },
        ["errNoFiles"] = new[] { "Add at least one file.", "Añade al menos un archivo.", "Ajoutez au moins un fichier.", "ファイルを 1 つ以上追加してください。", "Füge mindestens eine Datei hinzu." },
        ["error"] = new[] { "Error", "Error", "Erreur", "エラー", "Fehler" },
        ["extractDone"] = new[] { "Extraction finished. Open the output folder?", "Extracción terminada. ¿Abrir la carpeta de salida?", "Extraction terminée. Ouvrir le dossier de sortie ?", "展開が完了しました。出力フォルダーを開きますか?", "Extraktion abgeschlossen. Ausgabeordner öffnen?" },
        ["convDoneLog"] = new[] { "Finished: {0}/{1} converted.", "Terminado: {0}/{1} convertidos.", "Terminé : {0}/{1} convertis.", "完了: {0}/{1} 件変換しました。", "Fertig: {0}/{1} konvertiert." },
        ["convDoneMsg"] = new[] { "{0} of {1} files converted. Open the output folder?", "{0} de {1} archivos convertidos. ¿Abrir la carpeta de salida?", "{0} fichier(s) sur {1} converti(s). Ouvrir le dossier de sortie ?", "{1} 件中 {0} 件を変換しました。出力フォルダーを開きますか?", "{0} von {1} Dateien konvertiert. Ausgabeordner öffnen?" },

        // ---------- Log de los motores ----------
        ["logOpening"] = new[] { "Opening [{0}] {1}", "Abriendo [{0}] {1}", "Ouverture [{0}] {1}", "開いています [{0}] {1}", "Öffne [{0}] {1}" },
        ["kindBase"] = new[] { "Base", "Base", "Base", "ベース", "Basis" },
        ["kindUpdate"] = new[] { "Update", "Update", "Mise à jour", "アップデート", "Update" },
        ["kindDlc"] = new[] { "DLC", "DLC", "DLC", "DLC", "DLC" },
        ["noSecure"] = new[] { "The XCI has no secure partition.", "El XCI no tiene partición segura.", "Le XCI n’a pas de partition sécurisée.", "XCI にセキュアパーティションがありません。", "Das XCI hat keine Secure-Partition." },
        ["logDecompressing"] = new[] { "  Decompressing {0}...", "  Descomprimiendo {0}...", "  Décompression de {0}...", "  {0} を解凍中...", "  Entpacke {0}..." },
        ["logNczDone"] = new[] { "    NCZ decompressed ({0} MiB), re-encrypting sections...", "    NCZ descomprimido ({0} MiB), re-cifrando secciones...", "    NCZ décompressé ({0} Mio), rechiffrement des sections...", "    NCZ を解凍しました ({0} MiB)。セクションを再暗号化中...", "    NCZ entpackt ({0} MiB), Abschnitte werden neu verschlüsselt..." },
        ["badNcz"] = new[] { "Invalid NCZ: NCZSECTN header missing", "NCZ inválido: falta NCZSECTN", "NCZ invalide : en-tête NCZSECTN manquant", "無効な NCZ: NCZSECTN ヘッダーがありません", "Ungültiges NCZ: NCZSECTN-Header fehlt" },
        ["logGame"] = new[] { "Game {0}", "Juego {0}", "Jeu {0}", "ゲーム {0}", "Spiel {0}" },
        ["logPlusUpdate"] = new[] { " + update", " + update", " + mise à jour", " + アップデート", " + Update" },
        ["logExtracting"] = new[] { "  Extracting {0} -> {1}", "  Extrayendo {0} -> {1}", "  Extraction de {0} -> {1}", "  {0} を展開中 -> {1}", "  Extrahiere {0} -> {1}" },
        ["romfsWithUpdate"] = new[] { "RomFS (update applied)", "RomFS (con update aplicado)", "RomFS (mise à jour appliquée)", "RomFS (アップデート適用済み)", "RomFS (Update angewendet)" },
        ["updateNoBase"] = new[]
        {
            "[!] Update {0} without base game: only ExeFS is extracted (RomFS needs the base game).",
            "[!] Update {0} sin juego base: sólo se extrae ExeFS (el RomFS necesita el base).",
            "[!] Mise à jour {0} sans jeu de base : seul l’ExeFS est extrait (le RomFS nécessite le jeu de base).",
            "[!] アップデート {0} にベースゲームがありません: ExeFS のみ展開します (RomFS にはベースゲームが必要です)。",
            "[!] Update {0} ohne Basisspiel: nur ExeFS wird extrahiert (RomFS benötigt das Basisspiel)."
        },
        ["noSection"] = new[] { "  [!] {0}: the NCA has no such section.", "  [!] {0}: la NCA no tiene esa sección.", "  [!] {0} : la NCA ne contient pas cette section.", "  [!] {0}: NCA にこのセクションがありません。", "  [!] {0}: Die NCA hat diesen Abschnitt nicht." },
        ["infoName"] = new[] { "Name", "Nombre", "Nom", "名前", "Name" },
        ["infoVersion"] = new[] { "Version", "Versión", "Version", "バージョン", "Version" },
        ["logDone"] = new[] { "Done.", "Listo.", "Terminé.", "完了。", "Fertig." },
        ["logCompressing"] = new[] { "  Compressing {0} (level {1})", "  Comprimiendo {0} (nivel {1})", "  Compression de {0} (niveau {1})", "  {0} を圧縮中 (レベル {1})", "  Komprimiere {0} (Stufe {1})" },
        ["logCopying"] = new[] { "  Copying {0}", "  Copiando {0}", "  Copie de {0}", "  {0} をコピー中", "  Kopiere {0}" },
        ["tooManyFiles"] = new[] { "Too many files for the PFS0 header.", "Demasiados archivos para la cabecera PFS0.", "Trop de fichiers pour l’en-tête PFS0.", "PFS0 ヘッダーに対してファイルが多すぎます。", "Zu viele Dateien für den PFS0-Header." },
        ["badNup"] = new[] { "Not a valid NUP package.", "No es un paquete NUP válido.", "Ce n’est pas un paquet NUP valide.", "有効な NUP パッケージではありません。", "Kein gültiges NUP-Paket." },
        ["logNupTitle"] = new[] { "  Title {0}: {1} v{2}", "  Título {0}: {1} v{2}", "  Titre {0} : {1} v{2}", "  タイトル {0}: {1} v{2}", "  Titel {0}: {1} v{2}" },
        ["logNupExtracting"] = new[] { "Game {0} v{1} (Switch 2, already decrypted)", "Juego {0} v{1} (Switch 2, ya descifrado)", "Jeu {0} v{1} (Switch 2, déjà déchiffré)", "ゲーム {0} v{1} (Switch 2、復号済み)", "Spiel {0} v{1} (Switch 2, bereits entschlüsselt)" },
        ["nupBadFrame"] = new[] { "{0}: decompressed to {1} bytes, {2} expected", "{0}: se descomprimió a {1} bytes, se esperaban {2}", "{0} : décompressé en {1} octets, {2} attendus", "{0}: 解凍後 {1} バイト ({2} バイトのはず)", "{0}: auf {1} Bytes entpackt, {2} erwartet" },
        ["unsafePath"] = new[] { "Unsafe path in the package: {0}", "Ruta no segura en el paquete: {0}", "Chemin dangereux dans le paquet : {0}", "パッケージ内に安全でないパスがあります: {0}", "Unsichere Pfadangabe im Paket: {0}" },
        ["nupNoConvert"] = new[] { "{0}: NUP files (Switch 2) can only be extracted, not converted.", "{0}: los NUP (Switch 2) sólo se pueden extraer, no convertir.", "{0} : les NUP (Switch 2) peuvent seulement être extraits, pas convertis.", "{0}: NUP (Switch 2) は展開のみ可能で、変換はできません。", "{0}: NUP-Dateien (Switch 2) können nur extrahiert, nicht konvertiert werden." },
        ["badNpdm"] = new[] { "Invalid main.npdm: {0}", "main.npdm no válido: {0}", "main.npdm invalide : {0}", "無効な main.npdm: {0}", "Ungültige main.npdm: {0}" },
        ["nupNoGame"] = new[] { "{0}: no exefs/ or romfs/ folder found (or base/ and updates/<version>/).", "{0}: no hay carpetas exefs/ ni romfs/ (ni base/ y updates/<versión>/).", "{0} : aucun dossier exefs/ ou romfs/ (ni base/ et updates/<version>/).", "{0}: exefs/ や romfs/ (または base/ と updates/<バージョン>/) が見つかりません。", "{0}: kein exefs/- oder romfs/-Ordner gefunden (oder base/ und updates/<Version>/)." },
        ["nupNoNpdm"] = new[] { "{0}: exefs/main.npdm is missing, so the title ID is unknown.", "{0}: falta exefs/main.npdm, no se puede saber el Title ID.", "{0} : exefs/main.npdm manquant, impossible de connaître le Title ID.", "{0}: exefs/main.npdm がないため Title ID がわかりません。", "{0}: exefs/main.npdm fehlt, daher ist die Title-ID unbekannt." },
        ["logNupPacking"] = new[] { "  Packing {0} title(s), {1} files ({2} MiB)", "  Empaquetando {0} título(s), {1} archivos ({2} MiB)", "  Empaquetage de {0} titre(s), {1} fichiers ({2} Mio)", "  {0} タイトル、{1} ファイル ({2} MiB) をパック中", "  Packe {0} Titel, {1} Dateien ({2} MiB)" },
        ["logNupPkg"] = new[] { "  {0} {1} v{2}: exefs={3} romfs={4}", "  {0} {1} v{2}: exefs={3} romfs={4}", "  {0} {1} v{2} : exefs={3} romfs={4}", "  {0} {1} v{2}: exefs={3} romfs={4}", "  {0} {1} v{2}: exefs={3} romfs={4}" },
        ["nupReadChanged"] = new[] { "{0} changed while it was being read.", "{0} cambió mientras se leía.", "{0} a changé pendant la lecture.", "{0} が読み込み中に変更されました。", "{0} hat sich beim Lesen geändert." },
        ["toNup"] = new[] { "Folder / NX → NUP", "Carpeta / NX → NUP", "Dossier / NX → NUP", "フォルダー / NX → NUP", "Ordner / NX → NUP" },
        ["addFolder"] = new[] { "Add folder...", "Añadir carpeta...", "Ajouter un dossier...", "フォルダーを追加...", "Ordner hinzufügen..." },
        ["errNotFolder"] = new[] { "For Folder / NX → NUP, add extracted game folders (with exefs/ and romfs/) or .nx files.", "Para Carpeta / NX → NUP añade carpetas de juegos extraídos (con exefs/ y romfs/) o archivos .nx.", "Pour Dossier → NUP, ajoutez des dossiers de jeux extraits (avec exefs/ et romfs/), pas des fichiers.", "フォルダー → NUP では、ファイルではなく展開済みゲームのフォルダー (exefs/ と romfs/ を含む) を追加してください。", "Für Ordner → NUP bitte entpackte Spielordner (mit exefs/ und romfs/) hinzufügen, keine Dateien." },
        ["kindBaseNup"] = new[] { "Base", "Base", "Base", "ベース", "Basis" },
        ["kindUpdateNup"] = new[] { "Update", "Update", "Mise à jour", "アップデート", "Update" },
        ["tabFirmware"] = new[] { "Firmware", "Firmware", "Firmware", "ファームウェア", "Firmware" },
        ["fwTarget"] = new[] { "Target firmware", "Firmware destino", "Firmware cible", "対象ファームウェア", "Ziel-Firmware" },
        ["fwTargetItem"] = new[] { "{0} or newer (keygen {1})", "{0} o superior (keygen {1})", "{0} ou plus récent (keygen {1})", "{0} 以降 (keygen {1})", "{0} oder neuer (Keygen {1})" },
        ["fwPatchRsv"] = new[] { "Also lower RequiredSystemVersion", "Bajar también RequiredSystemVersion", "Abaisser aussi RequiredSystemVersion", "RequiredSystemVersion も下げる", "Auch RequiredSystemVersion senken" },
        ["fwPatchBtn"] = new[] { "Patch", "Parchear", "Patcher", "パッチ", "Patchen" },
        ["fwNote"] = new[]
        {
            "Makes games run on older firmware: re-encrypts NCA keys for the chosen key generation, removes titlekey crypto and lowers the minimum system version. Output is an NSP. Needs sigpatches on the console. Games that use newer system features may still crash.",
            "Hace que los juegos corran en firmware más viejo: re-cifra las claves de las NCA para la generación elegida, quita el cifrado por titlekey y baja la versión mínima del sistema. La salida es un NSP. La consola necesita sigpatches. Un juego que use funciones del sistema más nuevas puede cerrarse igual.",
            "Permet de lancer des jeux sur un firmware plus ancien : rechiffre les clés des NCA pour la génération choisie, supprime le chiffrement par titlekey et abaisse la version système minimale. La sortie est un NSP. La console a besoin de sigpatches. Un jeu qui utilise des fonctions système plus récentes peut quand même planter.",
            "古いファームウェアでゲームを動かします: NCA の鍵を選んだ世代で再暗号化し、titlekey 暗号を外し、必要なシステムバージョンを下げます。出力は NSP です。本体に sigpatches が必要です。新しいシステム機能を使うゲームは動かない場合があります。",
            "Lässt Spiele auf älterer Firmware laufen: verschlüsselt die NCA-Schlüssel für die gewählte Generation neu, entfernt die Titlekey-Verschlüsselung und senkt die minimale Systemversion. Ausgabe ist ein NSP. Die Konsole braucht Sigpatches. Spiele, die neuere Systemfunktionen nutzen, können trotzdem abstürzen."
        },
        ["fwMissingKeys"] = new[] { "Your prod.keys has no keys for key generation {0}.", "Tu prod.keys no tiene claves para la generación {0}.", "Votre prod.keys n’a pas de clés pour la génération {0}.", "prod.keys に世代 {0} の鍵がありません。", "Deine prod.keys enthält keine Schlüssel für Generation {0}." },
        ["fwSkipNca"] = new[] { "  {0}: not an NCA3, copied unchanged", "  {0}: no es NCA3, se copia sin cambios", "  {0} : pas un NCA3, copié tel quel", "  {0}: NCA3 ではないため、そのままコピーします", "  {0}: kein NCA3, unverändert kopiert" },
        ["fwNoTitleKey"] = new[] { "{0}: needs a ticket/titlekey that was not found.", "{0}: necesita un ticket/titlekey que no se encontró.", "{0} : nécessite un ticket/titlekey introuvable.", "{0}: 必要なチケット/titlekey が見つかりません。", "{0}: benötigt ein Ticket/einen Titlekey, der nicht gefunden wurde." },
        ["fwLogNca"] = new[] { "  {0}: keygen {1} → {2}{3}", "  {0}: keygen {1} → {2}{3}", "  {0} : keygen {1} → {2}{3}", "  {0}: keygen {1} → {2}{3}", "  {0}: Keygen {1} → {2}{3}" },
        ["fwRightsRemoved"] = new[] { " (titlekey → standard crypto)", " (titlekey → cifrado estándar)", " (titlekey → chiffrement standard)", " (titlekey → 標準暗号)", " (Titlekey → Standardverschlüsselung)" },
        ["fwBadMeta"] = new[] { "Unsupported Meta NCA layout.", "Formato de Meta NCA no soportado.", "Format de NCA Meta non pris en charge.", "未対応の Meta NCA 形式です。", "Nicht unterstütztes Meta-NCA-Format." },
        ["fwLogRsv"] = new[] { "  RequiredSystemVersion {0} → {1}", "  RequiredSystemVersion {0} → {1}", "  RequiredSystemVersion {0} → {1}", "  RequiredSystemVersion {0} → {1}", "  RequiredSystemVersion {0} → {1}" },
        ["fwDoneLog"] = new[] { "Finished: {0}/{1} patched.", "Terminado: {0}/{1} parcheados.", "Terminé : {0}/{1} patchés.", "完了: {0}/{1} 件パッチしました。", "Fertig: {0}/{1} gepatcht." },
        ["fwDoneMsg"] = new[] { "{0} of {1} files patched. Open the output folder?", "{0} de {1} archivos parcheados. ¿Abrir la carpeta de salida?", "{0} fichier(s) sur {1} patché(s). Ouvrir le dossier de sortie ?", "{1} 件中 {0} 件をパッチしました。出力フォルダーを開きますか?", "{0} von {1} Dateien gepatcht. Ausgabeordner öffnen?" },
        ["toNx"] = new[] { "NUP / folder → NX", "NUP / carpeta → NX", "NUP / dossier → NX", "NUP / フォルダー → NX", "NUP / Ordner → NX" },
        ["nxOnlyDecrypted"] = new[] { "{0}: NX can only be made from a NUP or an extracted game folder (NSP/XCI/NSZ are encrypted).", "{0}: el NX solo se puede crear desde un NUP o una carpeta de juego extraído (NSP/XCI/NSZ van cifrados).", "{0} : un NX ne peut être créé qu’à partir d’un NUP ou d’un dossier de jeu extrait (NSP/XCI/NSZ sont chiffrés).", "{0}: NX は NUP か展開済みゲームフォルダーからのみ作成できます (NSP/XCI/NSZ は暗号化されています)。", "{0}: NX kann nur aus einem NUP oder einem entpackten Spielordner erstellt werden (NSP/XCI/NSZ sind verschlüsselt)." },
        ["errOutInsideInput"] = new[] { "The output folder cannot be inside the game folder being packed.", "La carpeta de salida no puede estar dentro de la carpeta del juego que se empaqueta.", "Le dossier de sortie ne peut pas se trouver dans le dossier du jeu à empaqueter.", "出力フォルダーを、パックするゲームフォルダーの中にすることはできません。", "Der Ausgabeordner darf nicht im zu packenden Spielordner liegen." },
        ["errDiskSpace"] = new[] { "Not enough free space on {0}: about {1:0.#} GB needed, {2:0.#} GB free. Choose another output folder or free up space.", "Espacio insuficiente en {0}: se necesitan unos {1:0.#} GB y hay {2:0.#} GB libres. Elige otra carpeta de salida o libera espacio.", "Espace insuffisant sur {0} : environ {1:0.#} Go requis, {2:0.#} Go libres. Choisissez un autre dossier de sortie ou libérez de l'espace.", "{0} の空き容量が不足しています: 約 {1:0.#} GB 必要、空き {2:0.#} GB。別の出力フォルダーを選ぶか、空き容量を確保してください。", "Nicht genug Speicherplatz auf {0}: etwa {1:0.#} GB benötigt, {2:0.#} GB frei. Wähle einen anderen Ausgabeordner oder schaffe Platz." },
        ["logNxVerify"] = new[] { "  Verifying NX...", "  Verificando NX...", "  Vérification du NX...", "  NX を検証中...", "  NX wird geprüft..." },
        ["errNxInputs"] = new[] { "For → NX, add .nup files or extracted game folders.", "Para → NX añade archivos .nup o carpetas de juegos extraídos.", "Pour → NX, ajoutez des fichiers .nup ou des dossiers de jeux extraits.", "→ NX では .nup ファイルか展開済みゲームフォルダーを追加してください。", "Für → NX bitte .nup-Dateien oder entpackte Spielordner hinzufügen." },
        ["converted"] = new[] { "converted", "convertido", "converti", "変換済み", "konvertiert" },
    };
}
