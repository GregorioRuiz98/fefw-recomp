using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;
using ZstdSharp;

namespace NxExtractor;

/// <summary>
/// Formato .nup (dumps ya descifrados de Switch 2). Todo little-endian; mismo layout que lee Lichii/Eglantine.
///   0x00 superbloque (0x80): "NUP\x01", u16 header_size, u16 header_version, u64 toc_offset, u64 toc_size,
///        sha256 del TOC, u64 blob_offset (inicio de datos), u64 file_size, u32 package_count, u32 flags (0), uuid[16]
///   TOC (0x30): "TOC\x01", u32 package_count, u64 dirs, u64 archivos, u64 pool_offset (desde el TOC), u64 pool_size, u64 0
///        luego paquetes (0x40), dirs (0x18), archivos (0x50), lista de hijos (u32) y el pool de nombres
///   paquete: u64 title id, u32 dir raíz, u32 dir exefs, u32 dir romfs (0xFFFFFFFF = ninguno), u32 versión,
///        u8 kind (0 base, 1 update, 2 addon), u8 flags (0x01 = update autónomo), ..., u64 meta_offset, u32 meta_size
///   dir: u32 nombre, u32 largo, u32 primer hijo, u32 nº hijos, u64 0
///   archivo: u32 nombre, u32 largo, u64 tamaño, u64 offset, u64 tamaño guardado, sha256 del contenido, u8 codec (0 raw, 1 zstd)
///   hijo: índice &lt; dirs es un dir; si no, (índice - dirs) es un archivo
///   meta: TLV (u16 tipo, u16 0, u32 tamaño, datos); 1 = NPDM, 3 = icono, 4 = versión, 5 = nombre, 6 = editor
/// </summary>
public static class NupFormat
{
    public const uint None = 0xFFFFFFFF;
    public const int HeaderSize = 0x80, TocHeaderSize = 0x30, PackageSize = 0x40, DirSize = 0x18, FileSize = 0x50;
    public const byte KindBase = 0, KindUpdate = 1, KindAddon = 2;
    public const byte CodecRaw = 0, CodecZstd = 1;

    public static long Align16(long v) => (v + 15) & ~15L;
}

public sealed class NupPackage : IDisposable
{
    public sealed record Title(int Index, ulong Id, uint Root, uint ExeFs, uint RomFs, uint Version, byte Kind);
    public sealed record FileRec(uint NameOff, uint NameLen, long Size, long Offset, long StoredSize, byte[] Sha256, byte Codec);

    private readonly FileStream _f;
    private readonly byte[] _pool;
    private readonly (uint nameOff, uint nameLen, uint first, uint count)[] _dirs;
    private readonly FileRec[] _files;
    private readonly uint[] _kids;
    private readonly long _dirCount;

    public List<Title> Titles { get; } = new();
    public long FileCount => _files.Length;

    public NupPackage(string path)
    {
        _f = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read, 1 << 20);
        var head = new byte[NupFormat.HeaderSize];
        _f.ReadExactly(head);
        if (Encoding.ASCII.GetString(head, 0, 4) != "NUP\x01") throw new InvalidDataException(L.T("badNup"));
        long tocOffset = (long)BinaryPrimitives.ReadUInt64LittleEndian(head.AsSpan(0x08));
        long tocSize = (long)BinaryPrimitives.ReadUInt64LittleEndian(head.AsSpan(0x10));
        if (tocOffset < NupFormat.HeaderSize || tocSize < NupFormat.TocHeaderSize || tocOffset + tocSize > _f.Length || tocSize > int.MaxValue)
            throw new InvalidDataException(L.T("badNup"));

        var toc = new byte[tocSize];
        _f.Position = tocOffset;
        _f.ReadExactly(toc);
        if (!SHA256.HashData(toc).AsSpan().SequenceEqual(head.AsSpan(0x18, 32)) || Encoding.ASCII.GetString(toc, 0, 4) != "TOC\x01")
            throw new InvalidDataException(L.T("badNup"));

        var t = toc.AsSpan();
        uint np = BinaryPrimitives.ReadUInt32LittleEndian(t[4..]);
        long nd = (long)BinaryPrimitives.ReadUInt64LittleEndian(t[8..]);
        long nf = (long)BinaryPrimitives.ReadUInt64LittleEndian(t[16..]);
        long poolOff = (long)BinaryPrimitives.ReadUInt64LittleEndian(t[24..]);
        long poolSize = (long)BinaryPrimitives.ReadUInt64LittleEndian(t[32..]);

        long pkgsAt = NupFormat.TocHeaderSize;
        long dirsAt = pkgsAt + NupFormat.PackageSize * (long)np;
        long filesAt = dirsAt + NupFormat.DirSize * nd;
        long kidsAt = filesAt + NupFormat.FileSize * nf;
        if (np == 0 || kidsAt > poolOff || poolOff + poolSize > tocSize) throw new InvalidDataException(L.T("badNup"));

        for (int i = 0; i < np; i++)
        {
            var p = t[(int)(pkgsAt + NupFormat.PackageSize * i)..];
            Titles.Add(new Title(i,
                BinaryPrimitives.ReadUInt64LittleEndian(p),
                BinaryPrimitives.ReadUInt32LittleEndian(p[8..]),
                BinaryPrimitives.ReadUInt32LittleEndian(p[12..]),
                BinaryPrimitives.ReadUInt32LittleEndian(p[16..]),
                BinaryPrimitives.ReadUInt32LittleEndian(p[20..]),
                p[24]));
        }

        _dirCount = nd;
        _dirs = new (uint, uint, uint, uint)[nd];
        for (long i = 0; i < nd; i++)
        {
            var d = t[(int)(dirsAt + NupFormat.DirSize * i)..];
            _dirs[i] = (BinaryPrimitives.ReadUInt32LittleEndian(d), BinaryPrimitives.ReadUInt32LittleEndian(d[4..]),
                BinaryPrimitives.ReadUInt32LittleEndian(d[8..]), BinaryPrimitives.ReadUInt32LittleEndian(d[12..]));
        }

        _files = new FileRec[nf];
        for (long i = 0; i < nf; i++)
        {
            var r = t[(int)(filesAt + NupFormat.FileSize * i)..];
            _files[i] = new FileRec(
                BinaryPrimitives.ReadUInt32LittleEndian(r), BinaryPrimitives.ReadUInt32LittleEndian(r[4..]),
                (long)BinaryPrimitives.ReadUInt64LittleEndian(r[8..]), (long)BinaryPrimitives.ReadUInt64LittleEndian(r[16..]),
                (long)BinaryPrimitives.ReadUInt64LittleEndian(r[24..]), r.Slice(32, 32).ToArray(), r[64]);
        }

        long nk = (poolOff - kidsAt) / 4;
        _kids = new uint[nk];
        for (long i = 0; i < nk; i++) _kids[i] = BinaryPrimitives.ReadUInt32LittleEndian(t[(int)(kidsAt + 4 * i)..]);

        _pool = toc[(int)poolOff..(int)(poolOff + poolSize)];
    }

    private string Name(uint off, uint len) => Encoding.UTF8.GetString(_pool, (int)off, (int)len);

    /// <summary>(ruta relativa con '/', registro) de cada archivo bajo el dir indicado (nada si es None).</summary>
    public IEnumerable<(string path, FileRec rec)> Walk(uint dir, string prefix = "", int depth = 0)
    {
        if (dir == NupFormat.None) yield break;
        if (dir >= _dirCount || depth > 128) throw new InvalidDataException(L.T("badNup"));
        var (_, _, first, count) = _dirs[dir];
        if ((long)first + count > _kids.Length) throw new InvalidDataException(L.T("badNup"));
        for (long k = first; k < (long)first + count; k++)
        {
            uint node = _kids[k];
            if (node < _dirCount)
            {
                var d = _dirs[node];
                foreach (var x in Walk(node, prefix + Name(d.nameOff, d.nameLen) + "/", depth + 1)) yield return x;
            }
            else if (node - _dirCount < _files.Length)
            {
                var r = _files[node - _dirCount];
                yield return (prefix + Name(r.NameOff, r.NameLen), r);
            }
            else throw new InvalidDataException(L.T("badNup"));
        }
    }

    public void CopyTo(FileRec r, string name, Stream dst, Action<long> progress)
    {
        if (r.Size == 0) return;
        var sink = new ProgressStream(dst, progress);
        if (r.Codec == NupFormat.CodecZstd)
        {
            using var zs = new DecompressionStream(new BoundedStream(_f, r.Offset, r.StoredSize), leaveOpen: false);
            var buf = new byte[1 << 20];
            long got = 0;
            int n;
            while (got < r.Size && (n = zs.Read(buf, 0, (int)Math.Min(buf.Length, r.Size - got))) > 0)
            {
                sink.Write(buf, 0, n);
                got += n;
            }
            if (got != r.Size) throw new InvalidDataException(L.F("nupBadFrame", name, got, r.Size));
            return;
        }
        var src = new BoundedStream(_f, r.Offset, r.Size);
        if (src.Length < r.Size) throw new EndOfStreamException(name);
        src.CopyTo(sink, 4 << 20);
    }

    public void Dispose() => _f.Dispose();
}

/// <summary>
/// Empaqueta carpetas extraídas (exefs/ + romfs/) en un .nup. Cada paquete es un título (base o update).
/// Los datos se escriben primero; el TOC (de tamaño conocido de antemano) y el superbloque, al final.
/// </summary>
public sealed class NupWriter
{
    public sealed class PackageSource
    {
        public ulong TitleId;
        public uint Version;
        public byte Kind;
        public byte Flags;
        public string ExeFsDir;   // null = sin exefs
        public string RomFsDir;   // null = sin romfs
    }

    private sealed class Dir
    {
        public string Name;
        public List<Dir> SubDirs = new();
        public List<FileEntry> Files = new();
        public int Index;
    }

    private sealed class FileEntry
    {
        public string Name, SourcePath;
        public long Size, Offset, StoredSize;
        public byte[] Sha256 = new byte[32];
        public byte Codec;
        public int Index;
    }

    public int Level { get; set; } = 3;
    public bool Compress { get; set; } = true;

    private readonly Action<string> _log;
    private readonly LibHac.Common.IProgressReport _progress;

    public NupWriter(Action<string> log, LibHac.Common.IProgressReport progress)
    {
        _log = log;
        _progress = progress;
    }

    /// <summary>Lee el program ID del ACI0 de un main.npdm.</summary>
    public static ulong ReadProgramId(string npdmPath)
    {
        var b = File.ReadAllBytes(npdmPath);
        if (b.Length < 0x80 || Encoding.ASCII.GetString(b, 0, 4) != "META") throw new InvalidDataException(L.F("badNpdm", npdmPath));
        int aci = (int)BinaryPrimitives.ReadUInt32LittleEndian(b.AsSpan(0x70));
        if (aci + 0x18 > b.Length || Encoding.ASCII.GetString(b, aci, 4) != "ACI0") throw new InvalidDataException(L.F("badNpdm", npdmPath));
        return BinaryPrimitives.ReadUInt64LittleEndian(b.AsSpan(aci + 0x10));
    }

    /// <summary>
    /// Detecta los títulos de una carpeta: "base/" + "updates/&lt;versión&gt;/" (cada uno con exefs/ y/o romfs/),
    /// o una sola carpeta con exefs/ (y romfs/) directamente.
    /// </summary>
    public static List<PackageSource> Detect(string folder)
    {
        var list = new List<PackageSource>();
        PackageSource FromDir(string d, uint version, byte kind)
        {
            string exe = Path.Combine(d, "exefs"), rom = Path.Combine(d, "romfs");
            if (!Directory.Exists(exe) && !Directory.Exists(rom)) return null;
            return new PackageSource
            {
                Version = version, Kind = kind,
                ExeFsDir = Directory.Exists(exe) ? exe : null,
                RomFsDir = Directory.Exists(rom) ? rom : null
            };
        }

        var basePkg = FromDir(Path.Combine(folder, "base"), 0, NupFormat.KindBase);
        if (basePkg != null) list.Add(basePkg);
        string upd = Path.Combine(folder, "updates");
        if (Directory.Exists(upd))
        {
            foreach (var d in Directory.GetDirectories(upd))
            {
                if (!uint.TryParse(Path.GetFileName(d), out uint v)) continue;
                var p = FromDir(d, v, NupFormat.KindUpdate);
                if (p != null) list.Add(p);
            }
        }
        if (list.Count == 0)
        {
            var single = FromDir(folder, 0, NupFormat.KindBase);
            if (single != null) list.Add(single);
        }
        if (list.Count == 0) throw new InvalidDataException(L.F("nupNoGame", folder));

        // Title ID: el del main.npdm de cualquier exefs (base y update comparten el mismo).
        string npdm = list.Select(p => p.ExeFsDir == null ? null : Path.Combine(p.ExeFsDir, "main.npdm")).FirstOrDefault(File.Exists);
        if (npdm == null) throw new InvalidDataException(L.F("nupNoNpdm", folder));
        ulong id = ReadProgramId(npdm);
        foreach (var p in list) p.TitleId = id;

        // Un update sin base tiene que poder arrancar solo.
        if (!list.Any(p => p.Kind == NupFormat.KindBase))
            foreach (var p in list) p.Flags = 0x01;

        // DLC: "dlc/<title id en hex>/<versión>/romfs". El Title ID sale del nombre de la carpeta.
        string dlc = Path.Combine(folder, "dlc");
        if (Directory.Exists(dlc))
        {
            foreach (var idDir in Directory.GetDirectories(dlc))
            {
                if (!ulong.TryParse(Path.GetFileName(idDir), System.Globalization.NumberStyles.HexNumber, null, out ulong dlcId)) continue;
                foreach (var verDir in Directory.GetDirectories(idDir))
                {
                    if (!uint.TryParse(Path.GetFileName(verDir), out uint v)) continue;
                    var p = FromDir(verDir, v, NupFormat.KindAddon);
                    if (p == null) continue;
                    p.TitleId = dlcId;
                    list.Add(p);
                }
            }
        }

        return list.OrderBy(p => p.Kind).ThenBy(p => p.Version).ToList();
    }

    public void Write(List<PackageSource> packages, string outputPath)
    {
        // ---------- Árbol: por paquete, una raíz con "exefs" y "romfs" ----------
        var allDirs = new List<Dir>();
        var allFiles = new List<FileEntry>();
        var roots = new List<(Dir root, Dir exe, Dir rom)>();
        foreach (var p in packages)
        {
            var root = new Dir { Name = "" };
            Dir exe = p.ExeFsDir != null ? Scan(p.ExeFsDir, "exefs") : null;
            Dir rom = p.RomFsDir != null ? Scan(p.RomFsDir, "romfs") : null;
            if (exe != null) root.SubDirs.Add(exe);
            if (rom != null) root.SubDirs.Add(rom);
            roots.Add((root, exe, rom));
        }

        // Índices en anchura, para que los hijos de cada dir queden contiguos.
        var kids = new List<uint>();
        var firstChild = new Dictionary<Dir, (uint first, uint count)>();
        var queue = new Queue<Dir>();
        foreach (var r in roots) { r.root.Index = allDirs.Count; allDirs.Add(r.root); queue.Enqueue(r.root); }
        var order = new List<Dir>();
        while (queue.Count > 0)
        {
            var d = queue.Dequeue();
            order.Add(d);
            foreach (var s in d.SubDirs.OrderBy(s => s.Name, StringComparer.Ordinal))
            {
                s.Index = allDirs.Count; allDirs.Add(s); queue.Enqueue(s);
            }
            foreach (var f in d.Files.OrderBy(f => f.Name, StringComparer.Ordinal)) { f.Index = allFiles.Count; allFiles.Add(f); }
        }
        uint nd = (uint)allDirs.Count;
        foreach (var d in order)
        {
            var children = d.SubDirs.OrderBy(s => s.Name, StringComparer.Ordinal).Select(s => (uint)s.Index)
                .Concat(d.Files.OrderBy(f => f.Name, StringComparer.Ordinal).Select(f => nd + (uint)f.Index)).ToList();
            firstChild[d] = ((uint)kids.Count, (uint)children.Count);
            kids.AddRange(children);
        }

        // Pool de nombres.
        var pool = new MemoryStream();
        var nameAt = new Dictionary<string, (uint off, uint len)>(StringComparer.Ordinal);
        (uint, uint) Intern(string s)
        {
            if (nameAt.TryGetValue(s, out var v)) return v;
            var b = Encoding.UTF8.GetBytes(s);
            v = ((uint)pool.Length, (uint)b.Length);
            pool.Write(b);
            nameAt[s] = v;
            return v;
        }
        foreach (var d in allDirs) Intern(d.Name);
        foreach (var f in allFiles) Intern(f.Name);

        long poolOff = NupFormat.TocHeaderSize + NupFormat.PackageSize * packages.Count + NupFormat.DirSize * nd
                       + NupFormat.FileSize * allFiles.Count + 4L * kids.Count;
        long tocSize = NupFormat.Align16(poolOff + pool.Length);
        long tocOffset = NupFormat.HeaderSize;
        long blobOffset = NupFormat.Align16(tocOffset + tocSize);
        if (tocSize > int.MaxValue) throw new InvalidDataException(L.T("tooManyFiles"));

        long totalBytes = allFiles.Sum(f => f.Size);
        _log(L.F("logNupPacking", packages.Count, allFiles.Count, totalBytes / (1024 * 1024)));
        _progress?.SetTotal(Math.Max(1, totalBytes));

        using var output = new FileStream(outputPath, FileMode.Create, FileAccess.ReadWrite, FileShare.None, 1 << 20);
        output.SetLength(blobOffset);
        output.Position = blobOffset;

        // ---------- Datos ----------
        // Los archivos pequeños se leen y comprimen en paralelo por lotes; se escriben en orden.
        long done = 0;
        var batch = new List<FileEntry>();
        long batchBytes = 0;
        using var compressors = new ThreadLocal<Compressor>(() => new Compressor(Level), trackAllValues: true);

        void FlushBatch()
        {
            if (batch.Count == 0) return;
            var payloads = new byte[batch.Count][];
            Parallel.For(0, batch.Count, i => payloads[i] = PrepareSmallFile(batch[i], compressors.Value));
            for (int i = 0; i < batch.Count; i++)
            {
                var f = batch[i];
                output.Position = NupFormat.Align16(output.Position);
                f.Offset = output.Position;
                output.Write(payloads[i]);
                done += f.Size;
            }
            _progress?.Report(done);
            batch.Clear();
            batchBytes = 0;
        }

        foreach (var f in allFiles)
        {
            if (f.Size == 0) { f.Offset = 0; f.StoredSize = 0; f.Codec = NupFormat.CodecRaw; f.Sha256 = SHA256.HashData(Array.Empty<byte>()); continue; }
            if (f.Size <= SmallFileLimit)
            {
                batch.Add(f);
                batchBytes += f.Size;
                if (batchBytes >= BatchLimit || batch.Count >= 4096) FlushBatch();
                continue;
            }
            FlushBatch();
            output.Position = NupFormat.Align16(output.Position);
            f.Offset = output.Position;
            f.Codec = WantsCompression(f) && SampleCompresses(f) ? NupFormat.CodecZstd : NupFormat.CodecRaw;
            WriteLargeFile(f, output, ref done);
        }
        FlushBatch();
        foreach (var c in compressors.Values) c.Dispose();

        // ---------- Metadatos (TLV con el NPDM) ----------
        var meta = new (long off, uint size)[packages.Count];
        for (int i = 0; i < packages.Count; i++)
        {
            string npdm = packages[i].ExeFsDir == null ? null : Path.Combine(packages[i].ExeFsDir, "main.npdm");
            if (npdm == null || !File.Exists(npdm)) continue;
            var data = File.ReadAllBytes(npdm);
            output.Position = NupFormat.Align16(output.Position);
            meta[i] = (output.Position, (uint)(8 + data.Length));
            var tlv = new byte[8];
            BinaryPrimitives.WriteUInt16LittleEndian(tlv, 1);
            BinaryPrimitives.WriteUInt32LittleEndian(tlv.AsSpan(4), (uint)data.Length);
            output.Write(tlv);
            output.Write(data);
        }
        long fileSize = output.Position;

        // ---------- TOC ----------
        var toc = new byte[tocSize];
        var t = toc.AsSpan();
        Encoding.ASCII.GetBytes("TOC\x01").CopyTo(t);
        BinaryPrimitives.WriteUInt32LittleEndian(t[4..], (uint)packages.Count);
        BinaryPrimitives.WriteUInt64LittleEndian(t[8..], nd);
        BinaryPrimitives.WriteUInt64LittleEndian(t[16..], (ulong)allFiles.Count);
        BinaryPrimitives.WriteUInt64LittleEndian(t[24..], (ulong)poolOff);
        BinaryPrimitives.WriteUInt64LittleEndian(t[32..], (ulong)pool.Length);

        int at = NupFormat.TocHeaderSize;
        for (int i = 0; i < packages.Count; i++, at += NupFormat.PackageSize)
        {
            var p = packages[i];
            var (root, exe, rom) = roots[i];
            var r = t[at..];
            BinaryPrimitives.WriteUInt64LittleEndian(r, p.TitleId);
            BinaryPrimitives.WriteUInt32LittleEndian(r[8..], (uint)root.Index);
            BinaryPrimitives.WriteUInt32LittleEndian(r[12..], exe != null ? (uint)exe.Index : NupFormat.None);
            BinaryPrimitives.WriteUInt32LittleEndian(r[16..], rom != null ? (uint)rom.Index : NupFormat.None);
            BinaryPrimitives.WriteUInt32LittleEndian(r[20..], p.Version);
            r[24] = p.Kind;
            r[25] = p.Flags;
            BinaryPrimitives.WriteUInt64LittleEndian(r[32..], (ulong)meta[i].off);
            BinaryPrimitives.WriteUInt32LittleEndian(r[40..], meta[i].size);
        }
        foreach (var d in allDirs)
        {
            var r = t[at..];
            var (no, nl) = nameAt[d.Name];
            var (first, count) = firstChild[d];
            BinaryPrimitives.WriteUInt32LittleEndian(r, no);
            BinaryPrimitives.WriteUInt32LittleEndian(r[4..], nl);
            BinaryPrimitives.WriteUInt32LittleEndian(r[8..], first);
            BinaryPrimitives.WriteUInt32LittleEndian(r[12..], count);
            at += NupFormat.DirSize;
        }
        foreach (var f in allFiles)
        {
            var r = t[at..];
            var (no, nl) = nameAt[f.Name];
            BinaryPrimitives.WriteUInt32LittleEndian(r, no);
            BinaryPrimitives.WriteUInt32LittleEndian(r[4..], nl);
            BinaryPrimitives.WriteUInt64LittleEndian(r[8..], (ulong)f.Size);
            BinaryPrimitives.WriteUInt64LittleEndian(r[16..], (ulong)f.Offset);
            BinaryPrimitives.WriteUInt64LittleEndian(r[24..], (ulong)f.StoredSize);
            f.Sha256.CopyTo(r[32..]);
            r[64] = f.Codec;
            at += NupFormat.FileSize;
        }
        foreach (var k in kids) { BinaryPrimitives.WriteUInt32LittleEndian(t[at..], k); at += 4; }
        pool.ToArray().CopyTo(t[(int)poolOff..]);

        // ---------- Superbloque ----------
        var head = new byte[NupFormat.HeaderSize];
        var h = head.AsSpan();
        Encoding.ASCII.GetBytes("NUP\x01").CopyTo(h);
        BinaryPrimitives.WriteUInt16LittleEndian(h[4..], NupFormat.HeaderSize);
        BinaryPrimitives.WriteUInt16LittleEndian(h[6..], 1);
        BinaryPrimitives.WriteUInt64LittleEndian(h[8..], (ulong)tocOffset);
        BinaryPrimitives.WriteUInt64LittleEndian(h[16..], (ulong)tocSize);
        SHA256.HashData(toc).CopyTo(h[24..]);
        BinaryPrimitives.WriteUInt64LittleEndian(h[56..], (ulong)blobOffset);
        BinaryPrimitives.WriteUInt64LittleEndian(h[64..], (ulong)fileSize);
        BinaryPrimitives.WriteUInt32LittleEndian(h[72..], (uint)packages.Count);
        Guid.NewGuid().ToByteArray().CopyTo(h[80..]);

        output.Position = 0;
        output.Write(head);
        output.Position = tocOffset;
        output.Write(toc);
        output.SetLength(fileSize);
    }

    private static Dir Scan(string path, string name)
    {
        var d = new Dir { Name = name };
        foreach (var sub in Directory.GetDirectories(path)) d.SubDirs.Add(Scan(sub, Path.GetFileName(sub)));
        foreach (var file in Directory.GetFiles(path))
            if (!IsDumperArtifact(Path.GetFileName(file))) d.Files.Add(new FileEntry { Name = Path.GetFileName(file), SourcePath = file, Size = new FileInfo(file).Length });
        return d;
    }

    // ".listing" son listados de directorio que deja el FTP (wget) al copiar el dump; no son del juego.
    private static bool IsDumperArtifact(string name) => name.Equals(".listing", StringComparison.OrdinalIgnoreCase);

    // Lo que ya viene comprimido (zstd del juego, vídeo, audio) se guarda tal cual.
    private static readonly HashSet<string> RawExtensions = new(StringComparer.OrdinalIgnoreCase)
        { ".zs", ".zst", ".webm", ".mp4", ".bwav", ".bfstm", ".bfwav", ".png", ".jpg", ".jpeg", ".ogg", ".opus" };

    private bool WantsCompression(FileEntry f) =>
        Compress && f.Size >= 64 && !RawExtensions.Contains(Path.GetExtension(f.Name));

    // Hasta 16 MiB un archivo se procesa entero en memoria; los lotes paralelos se limitan a 256 MiB.
    private const int SmallFileLimit = 16 << 20;
    private const long BatchLimit = 256L << 20;
    private readonly byte[] _buf = new byte[4 << 20];

    /// <summary>
    /// Archivo pequeño (se llama en paralelo): se lee entero, se calcula su SHA-256 y se comprime en memoria.
    /// Devuelve los bytes a escribir: comprimidos si ocupan menos, si no el archivo tal cual.
    /// </summary>
    private byte[] PrepareSmallFile(FileEntry f, Compressor compressor)
    {
        var data = File.ReadAllBytes(f.SourcePath);
        if (data.Length != f.Size) throw new IOException(L.F("nupReadChanged", f.SourcePath));
        f.Sha256 = SHA256.HashData(data);
        f.Codec = NupFormat.CodecRaw;
        f.StoredSize = data.Length;
        if (!WantsCompression(f)) return data;

        var packed = compressor.Wrap(data);
        if (packed.Length >= data.Length) return data;
        f.Codec = NupFormat.CodecZstd;
        f.StoredSize = packed.Length;
        return packed.ToArray();
    }

    // Muestra: si el primer MiB casi no comprime, el archivo grande se guarda sin comprimir.
    private bool SampleCompresses(FileEntry f)
    {
        using var src = File.OpenRead(f.SourcePath);
        int n = (int)Math.Min(1 << 20, f.Size);
        src.ReadExactly(_buf, 0, n);
        using var c = new Compressor(1);
        return c.Wrap(_buf.AsSpan(0, n)).Length < n * 0.95;
    }

    private void WriteLargeFile(FileEntry f, FileStream output, ref long done)
    {
        using var sha = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        using var src = new FileStream(f.SourcePath, FileMode.Open, FileAccess.Read, FileShare.Read, 1 << 20);
        var buf = _buf;
        long start = output.Position;
        int n;
        if (f.Codec == NupFormat.CodecZstd)
        {
            using (var zs = new CompressionStream(output, Level, 1 << 20, leaveOpen: true))
            {
                if (f.Size > (8 << 20))
                    zs.SetParameter(ZstdSharp.Unsafe.ZSTD_cParameter.ZSTD_c_nbWorkers, Math.Max(1, Environment.ProcessorCount - 1));
                zs.SetPledgedSrcSize((ulong)f.Size);
                while ((n = src.Read(buf, 0, buf.Length)) > 0)
                {
                    sha.AppendData(buf, 0, n);
                    zs.Write(buf, 0, n);
                    done += n;
                    _progress?.Report(done);
                }
            }
            f.StoredSize = output.Position - start;
            // Si no salió más pequeño, se reescribe sin comprimir.
            if (f.StoredSize >= f.Size)
            {
                done -= f.Size;
                output.Position = start;
                output.SetLength(start);
                f.Codec = NupFormat.CodecRaw;
                WriteLargeFile(f, output, ref done);
                return;
            }
        }
        else
        {
            while ((n = src.Read(buf, 0, buf.Length)) > 0)
            {
                sha.AppendData(buf, 0, n);
                output.Write(buf, 0, n);
                done += n;
                _progress?.Report(done);
            }
            f.StoredSize = output.Position - start;
        }
        if (f.StoredSize == 0 || (f.Codec == NupFormat.CodecRaw && f.StoredSize != f.Size))
            throw new IOException(L.F("nupReadChanged", f.SourcePath));
        f.Sha256 = sha.GetHashAndReset();
    }
}

/// <summary>Vista de sólo lectura de un rango de un stream.</summary>
internal sealed class BoundedStream : Stream
{
    private readonly Stream _s;
    private readonly long _start, _len;
    private long _pos;
    public BoundedStream(Stream s, long start, long len) { _s = s; _start = start; _len = Math.Max(0, Math.Min(len, s.Length - start)); }
    public override int Read(byte[] buffer, int offset, int count)
    {
        if (_pos >= _len) return 0;
        _s.Position = _start + _pos;
        int n = _s.Read(buffer, offset, (int)Math.Min(count, _len - _pos));
        _pos += n;
        return n;
    }
    public override bool CanRead => true;
    public override bool CanSeek => false;
    public override bool CanWrite => false;
    public override long Length => _len;
    public override long Position { get => _pos; set => throw new NotSupportedException(); }
    public override void Flush() { }
    public override long Seek(long o, SeekOrigin so) => throw new NotSupportedException();
    public override void SetLength(long v) => throw new NotSupportedException();
    public override void Write(byte[] b, int o, int c) => throw new NotSupportedException();
}

internal sealed class ProgressStream : Stream
{
    private readonly Stream _s;
    private readonly Action<long> _p;
    public ProgressStream(Stream s, Action<long> p) { _s = s; _p = p; }
    public override void Write(byte[] b, int o, int c) { _s.Write(b, o, c); _p?.Invoke(c); }
    public override bool CanRead => false;
    public override bool CanSeek => false;
    public override bool CanWrite => true;
    public override long Length => throw new NotSupportedException();
    public override long Position { get => throw new NotSupportedException(); set => throw new NotSupportedException(); }
    public override void Flush() => _s.Flush();
    public override int Read(byte[] b, int o, int c) => throw new NotSupportedException();
    public override long Seek(long o, SeekOrigin so) => throw new NotSupportedException();
    public override void SetLength(long v) => throw new NotSupportedException();
}
