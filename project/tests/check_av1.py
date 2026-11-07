"""Private AV1 fixture extraction and native decoder/texture/queue regression."""
import argparse
import hashlib
import json
import mmap
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--work', type=Path, required=True)
p.add_argument('--movie', type=Path, required=True)
p.add_argument('--report', type=Path, required=True)
a = p.parse_args()
w = a.work.resolve()
folder = w / 'tests/av1'
folder.mkdir(parents=True, exist_ok=True)
with a.movie.open('rb') as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
    if b'V_AV1' not in data[:65536]:
        raise ValueError('Expected the validated single-video-track AV1 WebM')

    def vint(pos, limit, identifier=False):
        if pos >= limit or not data[pos]:
            raise ValueError('Invalid EBML integer')
        n = 1
        while not data[pos] & (1 << (8 - n)):
            n += 1
        if pos + n > limit:
            raise ValueError('Truncated EBML integer')
        value = int.from_bytes(data[pos:pos + n], 'big')
        return (value if identifier else value & ((1 << (7 * n)) - 1)), n

    def first_packet(lo, hi):
        while lo < hi:
            tag, n = vint(lo, hi, True)
            size, m = vint(lo + n, hi)
            start = lo + n + m
            end = min(start + size, hi)  # also handles unknown-size Segment/Cluster
            if tag in (0x18538067, 0x1f43b675, 0xa0):
                packet = first_packet(start, end)
                if packet is not None:
                    return packet
            elif tag in (0xa3, 0xa1):
                track, t = vint(start, end)
                if track == 1:
                    if start + t + 3 >= end or data[start + t + 2] & 6:
                        raise ValueError('Truncated or laced AV1 block is unsupported')
                    return bytes(data[start + t + 3:end])
            lo = end
        return None

    packet = first_packet(0, len(data))
if not packet:
    raise ValueError('No AV1 packet found')
fixture = folder / 'first.obu'
fixture.write_bytes(packet)
subprocess.run(['cmake', '--build', str(w / 'build/fefw'), '--target', 'fefw_video_tests', '--parallel', '1'], check=True)
env = dict(os.environ, FEFW_AV1_PACKET=str(fixture))
r = subprocess.run([str(w / 'build/fefw/fefw_video_tests.exe'), 'av1'], env=env, capture_output=True, text=True, timeout=90)
a.report.with_suffix('.log').write_text(r.stdout + r.stderr, encoding='utf-8')
passed = r.returncode == 0 and '1 passed, 0 failed, 0 skipped' in r.stdout
a.report.write_text(json.dumps(dict(passed=passed, exit_code=r.returncode, private_packet_bytes=len(packet),
    private_packet_sha256=hashlib.sha256(packet).hexdigest(), checks=['AV1 decode and timestamp',
    'native image snapshots and lifetime', 'NVN native texture contract', 'bounded queue and retry']), indent=2), encoding='utf-8')
print(r.stdout + r.stderr)
raise SystemExit(0 if passed else 1)
