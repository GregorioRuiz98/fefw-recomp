using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;

namespace NxExtractor;

/// <summary>
/// Formato .nx de NXFormat (https://github.com/fab918/nxformat, MIT), usando su librería oficial nxformat.dll.
/// La DLL va incrustada en el exe y se extrae a %LOCALAPPDATA%\NxExtractor\native\&lt;versión&gt; la primera vez.
/// nxformat_run ejecuta dentro del proceso los mismos comandos que su herramienta "nxconvert".
/// </summary>
public static class NxFormat
{
    private const string Lib = "nxformat";
    private const string BundledVersion = "1.0.4";

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void LineFn(IntPtr user, int isError, IntPtr line);

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
    private static extern int nxformat_run(IntPtr[] args, UIntPtr count, LineFn onLine, IntPtr user,
        IntPtr cancel, byte[] error, UIntPtr errorSize);

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr nxformat_version();

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
    private static extern uint nxformat_abi_version();

    private static IntPtr _handle;
    private static readonly object Gate = new();

    /// <summary>Extrae (si hace falta) y carga nxformat.dll con su vcruntime al lado.</summary>
    private static void EnsureLoaded()
    {
        lock (Gate)
        {
            if (_handle != IntPtr.Zero) return;
            string dir = Path.Combine(AppContext.BaseDirectory, "native");
            foreach (var name in new[] { "nxformat.dll" })
            {
                string target = Path.Combine(dir, name);
                if (!File.Exists(target)) throw new FileNotFoundException("The NX format library is missing. Extract the complete release package.", name);
            }
            // The standard Visual C++ x64 runtime is supplied by the required Build Tools.
            _handle = NativeLibrary.Load(Path.Combine(dir, "nxformat.dll"));
            NativeLibrary.SetDllImportResolver(typeof(NxFormat).Assembly,
                (lib, _, _) => lib == Lib ? _handle : IntPtr.Zero);
            if (nxformat_abi_version() != 1) throw new NotSupportedException("nxformat ABI " + nxformat_abi_version());
        }
    }

    public static string Version
    {
        get { EnsureLoaded(); return Marshal.PtrToStringUTF8(nxformat_version()); }
    }

    /// <summary>
    /// Ejecuta un comando de nxconvert. Las líneas van a <paramref name="log"/>; las de progreso
    /// (@TOTAL/@PROGRESS de --frontend) alimentan <paramref name="progress"/>.
    /// </summary>
    public static void Run(IEnumerable<string> args, Action<string> log, LibHac.Common.IProgressReport progress)
    {
        EnsureLoaded();
        var list = args.ToList();
        if (progress != null && !list.Contains("--frontend")) list.Add("--frontend");

        var ptrs = list.Select(Marshal.StringToCoTaskMemUTF8).ToArray();
        long total = 0;
        var sync = new object();
        LineFn onLine = (_, isError, linePtr) =>
        {
            lock (sync) OnLine(isError, linePtr);
        };
        void OnLine(int isError, IntPtr linePtr)
        {
            string line = Marshal.PtrToStringUTF8(linePtr) ?? "";
            if (line.StartsWith("@TOTAL\t"))
            {
                if (long.TryParse(line.Split('\t')[1], out long t)) { total += t; progress?.SetTotal(total); }
                return;
            }
            if (line.StartsWith("@PROGRESS\t"))
            {
                if (long.TryParse(line.Split('\t')[1], out long p)) progress?.Report(p);
                return;
            }
            if (line.StartsWith("@") || line.StartsWith("elapsed_s=")) return;
            log("  " + (isError != 0 ? "[!] " : "") + line);
        }
        try
        {
            var error = new byte[4096];
            int rc = nxformat_run(ptrs, (UIntPtr)ptrs.Length, onLine, IntPtr.Zero, IntPtr.Zero, error, (UIntPtr)error.Length);
            GC.KeepAlive(onLine);
            if (rc != 0)
            {
                int n = Array.IndexOf(error, (byte)0);
                string msg = Encoding.UTF8.GetString(error, 0, n < 0 ? error.Length : n);
                throw new InvalidOperationException($"nxformat ({rc}): {msg}");
            }
        }
        finally
        {
            foreach (var p in ptrs) Marshal.FreeCoTaskMem(p);
        }
    }

    public static bool IsNx(string path) => path.EndsWith(".nx", StringComparison.OrdinalIgnoreCase);

    /// <summary>Plataforma por Title ID: los de Switch 2 empiezan por 04.</summary>
    public static string PlatformFor(ulong titleId) => (titleId >> 56) == 0x04 ? "switch2" : "switch1";

    /// <summary>.nup -> .nx</summary>
    public static void FromNup(string nup, string output, Action<string> log, LibHac.Common.IProgressReport progress)
    {
        ulong id;
        using (var pkg = new NupPackage(nup))
            id = (pkg.Titles.FirstOrDefault(t => t.Kind == NupFormat.KindBase)
                ?? pkg.Titles.FirstOrDefault(t => t.Kind != NupFormat.KindAddon) ?? pkg.Titles[0]).Id;
        Run(new[] { "pack-nup", nup, output, "--platform", PlatformFor(id), "--no-trail" }, log, progress);
    }

    /// <summary>
    /// Carpeta extraída (exefs/romfs, o base/updates/dlc) -> .nx. El modo automático de nxformat para carpetas
    /// rechaza muchos main.npdm de Switch 2 sin un manifiesto explícito; su importador de NUP no, así que la
    /// carpeta pasa primero por un NUP temporal sin comprimir.
    /// </summary>
    public static void FromFolder(string folder, string output, Action<string> log, LibHac.Common.IProgressReport progress)
    {
        string temp = output + ".tmp.nup";
        try
        {
            new NupWriter(log, progress) { Compress = false }.Write(NupWriter.Detect(folder), temp);
            FromNup(temp, output, log, progress);
        }
        finally
        {
            try { File.Delete(temp); } catch { }
        }
    }

    /// <summary>.nx -> carpeta con base/, updates/&lt;versión&gt;/ y dlc/&lt;id&gt;/&lt;versión&gt;/ (la carpeta no debe existir).</summary>
    public static void Extract(string nx, string folder, Action<string> log, LibHac.Common.IProgressReport progress) =>
        Run(new[] { "extract", nx, folder, "--no-restore" }, log, progress); // sin receta .nxrestore (no es parte del juego)

    public static void Verify(string nx, Action<string> log) => Run(new[] { "verify", nx }, log, null);
}
