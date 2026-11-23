// SPDX-License-Identifier: GPL-3.0-or-later
using System.IO.Compression;
using System.Text;
using System.Text.Json;
using NxExtractor;

internal static class Program
{
    private static readonly string[] Containers = [".nsp", ".xci", ".nsz", ".xcz", ".nup", ".nx"];
    private sealed record Request(string[] Inputs, string Output, string Result, string ProdKeys, string TitleKeys, string Language);

    private static IEnumerable<string> Candidates(string root, int depth = 0)
    {
        if (File.Exists(Path.Combine(root, "exefs", "main")) && Directory.Exists(Path.Combine(root, "romfs")))
        { yield return root; yield break; }
        if (depth >= 5) yield break;
        foreach (string child in Directory.EnumerateDirectories(root))
        {
            if ((File.GetAttributes(child) & FileAttributes.ReparsePoint) != 0) continue;
            if (Path.GetFileName(child) is "romfs" or "exefs" or ".nxextractor_tmp") continue;
            foreach (string candidate in Candidates(child, depth + 1)) yield return candidate;
        }
    }

    private static void ExtractZip(string source, string destination)
    {
        using var zip = ZipFile.OpenRead(source);
        if (zip.Entries.Count > 2000000) throw new InvalidDataException("Too many archive entries.");
        string boundary = Path.GetFullPath(destination) + Path.DirectorySeparatorChar;
        var paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var entry in zip.Entries)
        {
            string name = entry.FullName.Replace('\\', '/');
            if (name.Length == 0 || name.StartsWith('/') || name.Contains(':') ||
                name.TrimEnd('/').Split('/').Any(p => p is "" or "." or ".." || p.EndsWith(' ') || p.EndsWith('.')) ||
                ((entry.ExternalAttributes >> 16) & 0xf000) == 0xa000)
                throw new InvalidDataException("Unsafe ZIP entry.");
            string target = Path.GetFullPath(Path.Combine(destination, name));
            if (!target.StartsWith(boundary, StringComparison.OrdinalIgnoreCase) || !paths.Add(target))
                throw new InvalidDataException("Duplicate or unsafe ZIP entry.");
        }
        zip.ExtractToDirectory(destination);
    }

    private static void Import(Request request)
    {
        if (request.Inputs is not { Length: > 0 } || request.Inputs.Length > 32)
            throw new InvalidDataException("Choose the base game and, when separate, its update.");
        if (Directory.Exists(request.Output)) throw new IOException("Import destination already exists; existing data was preserved.");
        foreach (string path in request.Inputs)
        {
            if (!File.Exists(path)) throw new FileNotFoundException("Input file not found.", path);
            if (!Containers.Contains(Path.GetExtension(path).ToLowerInvariant()) && !path.EndsWith(".zip", StringComparison.OrdinalIgnoreCase))
                throw new NotSupportedException("Choose NSP, XCI, NSZ, XCZ, NUP, NX, ZIP or an extracted folder.");
        }
        bool encrypted = request.Inputs.Any(p => new[] { ".nsp", ".xci", ".nsz", ".xcz" }.Contains(Path.GetExtension(p).ToLowerInvariant()));
        if (encrypted && !File.Exists(request.ProdKeys))
            throw new FileNotFoundException("This container needs your own prod.keys. Choose the keys file, or select an already extracted folder / NUP / NX.");
        L.Current = request.Language == "es" ? "es" : "en";
        Directory.CreateDirectory(request.Output);
        var files = new List<string>();
        foreach (string input in request.Inputs)
        {
            if (input.EndsWith(".zip", StringComparison.OrdinalIgnoreCase))
            {
                string folder = Path.Combine(request.Output, "zip-" + files.Count);
                Directory.CreateDirectory(folder);
                ExtractZip(input, folder);
                files.AddRange(Directory.EnumerateFiles(folder).Where(p => Containers.Contains(Path.GetExtension(p).ToLowerInvariant())));
            }
            else files.Add(input);
        }
        if (files.Count > 0)
        {
            if (files.Any(p => new[] { ".nsp", ".xci", ".nsz", ".xcz" }.Contains(Path.GetExtension(p).ToLowerInvariant())) && !File.Exists(request.ProdKeys))
                throw new FileNotFoundException("The ZIP contains encrypted game files. Provide your own prod.keys or select an extracted folder / NUP / NX.");
            var options = new ExtractOptions { OutputDir = request.Output, ProdKeys = request.ProdKeys,
                TitleKeys = request.TitleKeys, BaseFile = files[0], ExeFs = true, RomFs = true, Nacp = true, DlcRomFs = false };
            options.DlcFiles.AddRange(files.Skip(1)); // Upstream identifies patch sections independently of input order.
            using var extractor = new Extractor(options, Console.WriteLine, null);
            extractor.Run();
        }
        var candidates = Candidates(request.Output).Where(p =>
        {
            try { return NupWriter.ReadProgramId(Path.Combine(p, "exefs", "main.npdm")) == 0x04007EF01F8AE000UL; }
            catch { return false; }
        }).OrderByDescending(p => File.GetLastWriteTimeUtc(Path.Combine(p, "exefs", "main"))).ToList();
        if (candidates.Count == 0)
            throw new InvalidDataException("No complete compatible Fire Emblem title was imported. Select the base game and update 131072 together; an update alone is insufficient.");
        string source = Path.GetFullPath(candidates[0]);
        string temporary = request.Result + ".tmp";
        File.WriteAllText(temporary, "[import]\nsource=" + source + "\n", new UTF8Encoding(false));
        File.Move(temporary, request.Result, true);
        Console.WriteLine("Import complete. The launcher will verify the supported build and prepare shaders.");
    }

    private static int Main(string[] args)
    {
        try
        {
            if (args.Length == 2 && args[0] == "--self-test") return SelfTest.Run(args[1]);
            if (args.Length != 1) throw new ArgumentException("Usage: dotnet FEFW.Importer.dll request.json");
            var request = JsonSerializer.Deserialize<Request>(File.ReadAllText(args[0]), new JsonSerializerOptions { PropertyNameCaseInsensitive = true });
            Import(request);
            return 0;
        }
        catch (Exception e) { Console.Error.WriteLine("Import failed: " + e.Message); return 2; }
    }

    private static class SelfTest
    {
        internal static int Run(string root)
        {
            Directory.CreateDirectory(root);
            string unique = Path.Combine(root, Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(unique);
            string data = Path.Combine(unique, "source");
            Directory.CreateDirectory(Path.Combine(data, "exefs"));
            Directory.CreateDirectory(Path.Combine(data, "romfs"));
            byte[] npdm = new byte[0x460];
            Encoding.ASCII.GetBytes("META").CopyTo(npdm, 0);
            BitConverter.GetBytes(0x80U).CopyTo(npdm, 0x70);
            BitConverter.GetBytes(0x60U).CopyTo(npdm, 0x74);
            BitConverter.GetBytes(0xe0U).CopyTo(npdm, 0x78);
            BitConverter.GetBytes(0x380U).CopyTo(npdm, 0x7c);
            Encoding.ASCII.GetBytes("ACI0").CopyTo(npdm, 0x80);
            BitConverter.GetBytes(0x04007EF01F8AE000UL).CopyTo(npdm, 0x90);
            BitConverter.GetBytes(0x40U).CopyTo(npdm, 0xa0);
            BitConverter.GetBytes(0x1cU).CopyTo(npdm, 0xa4);
            npdm[0xc0]=1;
            Encoding.ASCII.GetBytes("ACID").CopyTo(npdm,0x3e0);
            BitConverter.GetBytes(0x200U).CopyTo(npdm,0x3e4);
            BitConverter.GetBytes(0x04007EF01F8AE000UL).CopyTo(npdm,0x3f0);
            BitConverter.GetBytes(0x04007EF01F8AE000UL).CopyTo(npdm,0x3f8);
            BitConverter.GetBytes(0x340U).CopyTo(npdm,0x400);
            BitConverter.GetBytes(0x2cU).CopyTo(npdm,0x404);
            npdm[0x420]=1;
            File.WriteAllBytes(Path.Combine(data, "exefs", "main.npdm"), npdm);
            byte[] nso=new byte[0x104];Encoding.ASCII.GetBytes("NSO0").CopyTo(nso,0);Array.Fill(nso,(byte)0xaa,0x40,32);
            BitConverter.GetBytes(0x100U).CopyTo(nso,0x10);BitConverter.GetBytes(4U).CopyTo(nso,0x18);BitConverter.GetBytes(4U).CopyTo(nso,0x60);
            BitConverter.GetBytes(0xd65f03c0U).CopyTo(nso,0x100);
            File.WriteAllBytes(Path.Combine(data, "exefs", "main"), nso);
            File.WriteAllText(Path.Combine(data, "romfs", "fixture.txt"), "own synthetic data");
            string archive = Path.Combine(unique, "fixture.zip");
            ZipFile.CreateFromDirectory(data, archive);
            string result = Path.Combine(unique, "result.ini");
            Import(new Request([archive], Path.Combine(unique, "zip-result"), result, null, null, "en"));
            if (!File.ReadAllText(result).Contains("source=")) throw new Exception("ZIP import failed");
            string nup = Path.Combine(unique, "fixture.nup");
            var source = new NupWriter.PackageSource { TitleId = 0x04007EF01F8AE000UL, ExeFsDir = Path.Combine(data,"exefs"), RomFsDir = Path.Combine(data,"romfs") };
            new NupWriter(Console.WriteLine, null).Write([source], nup);
            Import(new Request([nup], Path.Combine(unique, "nup-result"), result, null, null, "en"));
            string nx = Path.Combine(unique, "fixture.nx");
            NxFormat.FromNup(nup, nx, Console.WriteLine, null);
            NxFormat.Verify(nx, Console.WriteLine);
            Import(new Request([nx], Path.Combine(unique, "nx-result"), result, null, null, "en"));
            string bad = Path.Combine(unique,"bad.zip");
            using(var z=ZipFile.Open(bad,ZipArchiveMode.Create)) { using var s=new StreamWriter(z.CreateEntry("../escape").Open()); s.Write("bad"); }
            bool rejected=false;
            try { ExtractZip(bad,Path.Combine(unique,"bad-result")); } catch(InvalidDataException){rejected=true;}
            if(!rejected || File.Exists(Path.Combine(unique,"escape"))) throw new Exception("Unsafe ZIP was accepted");
            foreach(string extension in new[]{".nsp",".xci",".nsz",".xcz"})
            {
                string input=Path.Combine(unique,"empty"+extension);File.WriteAllBytes(input,[]);
                bool missing=false;
                try{Import(new Request([input],Path.Combine(unique,extension[1..]),result,null,null,"en"));}
                catch(FileNotFoundException){missing=true;}
                if(!missing)throw new Exception("Missing-key detection failed for "+extension);
            }
            foreach(bool block in new[]{false,true})
            {
                byte[] content=Encoding.UTF8.GetBytes(new string('z',4096));
                string input=Path.Combine(unique,block?"block.ncz":"solid.ncz"), output=input+".decoded";
                using(var stream=File.Create(input))using(var w=new BinaryWriter(stream))
                {
                    w.Write(new byte[0x4000]);w.Write(Encoding.ASCII.GetBytes("NCZSECTN"));w.Write(1L);
                    w.Write(0x4000L);w.Write((long)content.Length);w.Write(1L);w.Write(0L);w.Write(new byte[32]);
                    using var compressor=new ZstdSharp.Compressor();byte[] compressed=compressor.Wrap(content).ToArray();
                    if(block){w.Write(Encoding.ASCII.GetBytes("NCZBLOCK"));w.Write((byte)2);w.Write((byte)1);w.Write((byte)0);w.Write((byte)12);w.Write(1);w.Write((long)content.Length);w.Write(compressed.Length);}
                    w.Write(compressed);
                }
                using(var stream=File.OpenRead(input))Ncz.Decompress(stream,output,Console.WriteLine);
                if(!File.ReadAllBytes(output).AsSpan(0x4000).SequenceEqual(content))throw new Exception("NCZ roundtrip failed");
            }
            Console.WriteLine("Importer: ZIP/NUP/NX roundtrips, solid/block NCZ, traversal rejection and four missing-key checks passed.");
            return 0;
        }
    }
}
