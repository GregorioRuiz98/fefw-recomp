"""Run nxrecomp on the six validated modules, saving honest coverage/lift reports."""
import argparse
import json
from pathlib import Path
import sys
import time

from prepare import BASES


def main():
    p = argparse.ArgumentParser()
    p.add_argument('mode', choices=['cover', 'lift'])
    p.add_argument('--engine', required=True, type=Path)
    p.add_argument('--exefs', required=True, type=Path)
    p.add_argument('--out', required=True, type=Path)
    p.add_argument('--module', choices=list(BASES))
    p.add_argument('--jobs', type=int, default=1)
    a = p.parse_args()
    sys.path.insert(0, str(a.engine.resolve() / 'recomp'))
    from nxrecomp.config import load_game
    from nxrecomp.image import Image
    from nxrecomp.program import cover, lift_module
    a.out.mkdir(parents=True, exist_ok=True)
    # Start with the smallest module, then the game itself and the libraries.
    order = [a.module] if a.module else ['rtld', 'main', 'sdk', 'subsdk0', 'subsdk1', 'subsdk2']
    for name in order:
        start = time.time()
        log_path = a.out / f'{name}-{a.mode}.log'
        with log_path.open('w', encoding='utf-8') as log_file:
            def log(message):
                print(message, flush=True)
                log_file.write(message + '\n')
                log_file.flush()
            source = a.exefs.resolve() / name
            log(f'{a.mode}: {name}')
            if a.mode == 'cover':
                image = Image.from_nso(source.read_bytes(), BASES[name], name)
                ok, bad, reasons = cover(image, load_game('fefw'), log=log)
                result = {'translated_words': ok, 'unsupported_words': bad, 'reasons': dict(reasons),
                          'scope': 'All .text words, including padding or embedded data; not proof of executability'}
            else:
                spec = {'path': str(source), 'kind': 'nso', 'name': name, 'base': BASES[name],
                        'game': 'fefw', 'index': list(BASES).index(name)}
                if name == 'main':
                    spec['sites'] = str(Path(__file__).resolve().parents[1] / 'src/main.sites')
                shards = max(1, min(160, (source.stat().st_size + 999999) // 1000000))
                result = lift_module(spec, str(a.out / name), jobs=a.jobs, shards=shards, log=log)
            result.update(module=name, seconds=round(time.time() - start, 2))
            (a.out / f'{name}-{a.mode}.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
        print(f'{name}: report saved', flush=True)


if __name__ == '__main__':
    main()
