#!/usr/bin/env python3
"""Encode native CBF1 frames for the browser. Full precision metrics are retained."""

import array
import gzip
import struct
import sys
from pathlib import Path


def pack(src: Path, dest: Path, preview=None):
    with src.open('rb') as source, dest.open('wb') as raw, gzip.GzipFile(fileobj=raw, filename='', mode='wb', compresslevel=6, mtime=0) as out:
        head = source.read(48)
        if head[:4] != b'CBF1':
            raise ValueError('Not a native snapshot file')
        nx, ny, count, fields, metrics = struct.unpack_from('<5i', head, 4)
        cells = nx * ny
        out.write(b'CBF2' + struct.pack('<5i', nx, ny, count if preview is None else 1, fields, metrics) + head[24:])
        if preview is not None:
            source.seek(48 + preview * (metrics * 8 + fields * cells * 4))
        for _ in range(count if preview is None else 1):
            out.write(source.read(metrics * 8))
            for _ in range(fields):
                x = array.array('f', source.read(cells*4))
                if sys.byteorder != 'little':
                    x.byteswap()
                lo, hi = min(x), max(x)
                step = (hi - lo) / 8191
                q = array.array('H', (round((v-lo)/step) for v in x)) if step else array.array('H', [0])*cells
                if sys.byteorder != 'little':
                    q.byteswap()
                out.write(struct.pack('<2f', lo, step))
                out.write(q.tobytes())


if __name__ == '__main__':
    pack(Path(sys.argv[1]), Path(sys.argv[2]), int(sys.argv[3]) if len(sys.argv) > 3 else None)