"""Build synthetic GPL nxrt loader fixtures; no commercial game data is used."""
import argparse
from pathlib import Path
import struct
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    compiler = args.compiler.resolve()
    linker = compiler.parent / ('ld.lld.exe' if compiler.suffix == '.exe' else 'ld.lld')
    source = Path(__file__).with_name('fixture.c')
    subprocess.run([str(compiler), '--target=aarch64-linux-gnu', '-x', 'c', '-O1', '-fPIC', '-ffreestanding',
                    '-nostdlib', '-fno-stack-protector', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
                    '-ftls-model=initial-exec', '-c', str(source), '-o', str(output/'fixture.o')], check=True)
    images = {}
    for name, extra in [('full', []), ('relr', ['-z', 'pack-relative-relocs'])]:
        path = output / ('fixture_' + name + '.elf')
        subprocess.run([str(linker), '-shared', '-Bsymbolic', '-z', 'relro', '-z', 'max-page-size=4096',
                        '-z', 'separate-code', '-e', 'fixture_entry', '--hash-style=both', *extra,
                        str(output/'fixture.o'), '-o', str(path)], check=True)
        images[name] = path.read_bytes()
    # The loader's no-section-table path must work independently of .dynsym sections.
    stripped = bytearray(images['full'])
    struct.pack_into('<Q', stripped, 40, 0)
    struct.pack_into('<HHH', stripped, 58, 0, 0, 0)
    images['nosections'] = bytes(stripped)
    text = '// Generated from the synthetic fixture.c; never execute its guest machine code.\n#pragma once\n'
    for name, data in images.items():
        text += 'inline const unsigned char fixture_' + name + '[] = {\n'
        text += '\n'.join(','.join(str(byte) for byte in data[i:i+32])+',' for i in range(0, len(data), 32))
        text += '\n};\n'
    (output/'fixture_data.h').write_text(text, encoding='utf-8')


if __name__ == '__main__':
    main()
