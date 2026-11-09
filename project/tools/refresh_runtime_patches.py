"""Add validated native instruction variants to an existing private main translation."""
import argparse
import bisect
import json
from pathlib import Path
import re
import sys


def refresh(work):
    sys.path.insert(0, str(work / 'engine/recomp'))
    from nxrecomp.config import load_game
    from nxrecomp.image import Image
    from nxrecomp.lift import Lifter
    from nxrecomp.program import Env, runtime_patch_stmt
    cfg = load_game('fefw')
    image = Image.from_nso((work / 'prepared/exefs/main').read_bytes(), 0x7100000000, 'main')
    env = Env(image, cfg, full=False)
    folder = work / 'lift/main'
    entries = sorted(set(int(a, 16) for a in re.findall(r'\{ 0x([0-9a-f]+)ULL, f_[0-9a-f]+ \}',
                                                       (folder / 'module_main.cpp').read_text())))
    wanted = {}
    sites = {0x71007bf1c0: 0xb901dd09, 0x710180ef44: 0xbd416a60}
    for pc, (old, new) in cfg.runtime_instruction_patches.items():
        if image.word(pc) != old:
            raise ValueError('Unsupported original instruction for native patch at ' + hex(pc))
        i = bisect.bisect_right(entries, pc) - 1
        if i < 0:
            raise ValueError('No translated function contains patch site')
        entry = entries[i]
        end = entries[i + 1] if i + 1 < len(entries) else image.end
        fn = Lifter(entry, end, env)
        wanted[pc] = (entry, fn.lift(old, pc), runtime_patch_stmt(env, fn, old, pc))
    for pc, expected in sites.items():
        if image.word(pc) != expected:
            raise ValueError('Unsupported original instruction for native hook at ' + hex(pc))
        i = bisect.bisect_right(entries, pc) - 1
        if i < 0:
            raise ValueError('No translated function contains hook site')
        entry = entries[i]
        fn = Lifter(entry, entries[i + 1] if i + 1 < len(entries) else image.end, env)
        old = fn.lift(expected, pc)
        wanted[pc] = (entry, old, '{ nx_site_inline(c, 0x%xULL); %s }' % (pc, old))
    updates, found = {}, set()
    for path in sorted(folder.glob('c_*.cpp')):
        text = path.read_text(encoding='utf-8')
        changed = False
        for pc, (entry, old, new) in wanted.items():
            match = re.search(r'(?m)^void f_%x\(Ctx[^\n]*\) \{' % entry, text)
            if not match:
                continue
            end = text.index('\n}', match.end()) + 2
            body = text[match.start():end]
            if new not in body:
                if body.count(old) != 1:
                    raise ValueError('Existing translation differs; perform a full lift before patching')
                body = body.replace(old, new, 1)
                text = text[:match.start()] + body + text[end:]
                changed = True
            found.add(pc)
        if changed:
            updates[path] = text
        if len(found) == len(wanted):
            break
    if found != set(wanted):
        raise ValueError('Native patch sites are missing from the translated main module')
    # Check every site before touching any shard. These are private generated
    # files; the original dump and normalized modules remain unchanged.
    for path, text in updates.items():
        temporary = path.with_suffix('.cpp.refreshing')
        temporary.write_text(text, encoding='utf-8', newline='\n')
        temporary.replace(path)
    report = work / 'lift/main-lift.json'
    data = json.loads(report.read_text())
    data['runtime_patch_sites'] = [hex(pc) for pc in sorted(wanted)]
    report.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
    return {'native_patch_sites': len(found), 'shards_updated': len(updates), 'original_files_modified': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(refresh(args.work.resolve())))
