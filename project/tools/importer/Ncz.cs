using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;
using ZstdSharp;

namespace NxExtractor;

/// <summary>
/// Convierte un NCZ (NCA comprimido de NSZ/XCZ) de vuelta a un NCA cifrado normal.
/// Formato: 0x4000 bytes de cabecera sin tocar, "NCZSECTN" + secciones, y luego
/// un stream zstd sólido o un bloque "NCZBLOCK" con bloques zstd independientes.
/// </summary>
public static class Ncz
{
    private const long UncompressableHeaderSize = 0x4000;

    private sealed class Section
    {
        public long Offset, Size, CryptoType;
        public byte[] Key = new byte[16], Counter = new byte[16];
    }

    public static void Decompress(Stream input, string outputPath, Action<string> log)
    {
        using var output = new FileStream(outputPath, FileMode.Create, FileAccess.ReadWrite, FileShare.None, 1 << 20);
        var br = new BinaryReader(input);

        input.Position = 0;
        CopyExactly(input, output, UncompressableHeaderSize);

        if (Encoding.ASCII.GetString(br.ReadBytes(8)) != "NCZSECTN")
            throw new InvalidDataException(L.T("badNcz"));

        long sectionCount = br.ReadInt64();
        var sections = new List<Section>();
        for (int i = 0; i < sectionCount; i++)
        {
            var s = new Section
            {
                Offset = br.ReadInt64(),
                Size = br.ReadInt64(),
                CryptoType = br.ReadInt64()
            };
            br.ReadInt64(); // padding
            s.Key = br.ReadBytes(16);
            s.Counter = br.ReadBytes(16);
            sections.Add(s);
        }

        long bodyStart = input.Position;
        bool isBlock = Encoding.ASCII.GetString(br.ReadBytes(8)) == "NCZBLOCK";

        if (isBlock)
        {
            br.ReadByte(); // version
            br.ReadByte(); // type
            br.ReadByte(); // unused
            int blockSizeExp = br.ReadByte();
            int blockCount = br.ReadInt32();
            long decompressedSize = br.ReadInt64();
            var sizes = new int[blockCount];
            for (int i = 0; i < blockCount; i++) sizes[i] = br.ReadInt32();

            long blockSize = 1L << blockSizeExp;
            long remaining = decompressedSize;
            using var dec = new Decompressor();
            for (int i = 0; i < blockCount; i++)
            {
                int outSize = (int)Math.Min(blockSize, remaining);
                byte[] comp = br.ReadBytes(sizes[i]);
                if (sizes[i] < outSize)
                {
                    var data = dec.Unwrap(comp).ToArray();
                    output.Write(data, 0, data.Length);
                }
                else
                {
                    output.Write(comp, 0, comp.Length);
                }
                remaining -= outSize;
            }
        }
        else
        {
            input.Position = bodyStart;
            using var zs = new DecompressionStream(input, leaveOpen: true);
            zs.CopyTo(output, 1 << 20);
        }

        output.Flush();
        log(L.F("logNczDone", output.Length / (1024 * 1024)));

        foreach (var s in sections)
        {
            // 3 = AES-CTR, 4 = AES-CTR (BKTR). 1 = sin cifrado.
            if (s.CryptoType != 3 && s.CryptoType != 4) continue;
            long start = Math.Max(s.Offset, UncompressableHeaderSize);
            long end = Math.Min(s.Offset + s.Size, output.Length);
            if (end > start) EncryptCtrInPlace(output, start, end, s.Key, s.Counter);
        }
    }

    private static void EncryptCtrInPlace(FileStream fs, long start, long end, byte[] key, byte[] counterBase)
    {
        using var aes = Aes.Create();
        aes.Key = key;

        const int chunk = 1 << 20;
        var buf = new byte[chunk];
        var ctrBlocks = new byte[chunk];
        var ks = new byte[chunk];
        var ctr = new byte[16];
        Array.Copy(counterBase, ctr, 8);

        long pos = start & ~0xFL;
        while (pos < end)
        {
            int len = (int)Math.Min(chunk, end - pos);
            int alignedLen = (len + 15) & ~15;
            fs.Position = pos;
            int read = ReadFull(fs, buf, alignedLen);

            for (int b = 0; b < alignedLen; b += 16)
            {
                BinaryPrimitives.WriteUInt64BigEndian(ctr.AsSpan(8), (ulong)((pos + b) >> 4));
                Buffer.BlockCopy(ctr, 0, ctrBlocks, b, 16);
            }
            aes.EncryptEcb(ctrBlocks.AsSpan(0, alignedLen), ks.AsSpan(0, alignedLen), PaddingMode.None);

            // Sólo se re-cifra el rango [start, end); los bytes alineados fuera quedan igual.
            int from = (int)Math.Max(0, start - pos);
            int to = (int)Math.Min(read, end - pos);
            for (int i = from; i < to; i++) buf[i] ^= ks[i];

            fs.Position = pos + from;
            fs.Write(buf, from, to - from);
            pos += len;
        }
    }

    /// <summary>Aplica AES-CTR (cifra o descifra) a <paramref name="data"/>, que empieza en el offset <paramref name="ncaOffset"/> de la NCA.</summary>
    public static void CtrXor(Aes aes, byte[] key, byte[] counterBase, Span<byte> data, long ncaOffset)
    {
        aes.Key = key;
        long first = ncaOffset & ~0xFL;
        int skip = (int)(ncaOffset - first);
        int blocks = (skip + data.Length + 15) / 16;
        var ctrs = new byte[blocks * 16];
        for (int b = 0; b < blocks; b++)
        {
            Array.Copy(counterBase, 0, ctrs, b * 16, 8);
            BinaryPrimitives.WriteUInt64BigEndian(ctrs.AsSpan(b * 16 + 8), (ulong)((first >> 4) + b));
        }
        var ks = aes.EncryptEcb(ctrs, PaddingMode.None);
        for (int i = 0; i < data.Length; i++) data[i] ^= ks[skip + i];
    }

    private static int ReadFull(Stream s, byte[] buf, int count)
    {
        int total = 0;
        while (total < count)
        {
            int n = s.Read(buf, total, count - total);
            if (n == 0) break;
            total += n;
        }
        return total;
    }

    private static void CopyExactly(Stream src, Stream dst, long count)
    {
        var buf = new byte[81920];
        while (count > 0)
        {
            int n = src.Read(buf, 0, (int)Math.Min(buf.Length, count));
            if (n == 0) throw new EndOfStreamException();
            dst.Write(buf, 0, n);
            count -= n;
        }
    }
}
