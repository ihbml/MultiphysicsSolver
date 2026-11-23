"""Independent binary64 audit. Run: python3 tests/verify_mesh.py [mesh.json ...]"""

import json
import math
import sys
from collections import Counter
from pathlib import Path

FACES = {
    4: ((0, 2, 1), (0, 1, 3), (1, 2, 3), (2, 0, 3)),
    5: ((0, 3, 2, 1), (0, 1, 4), (1, 2, 4), (2, 3, 4), (3, 0, 4)),
    6: ((0, 2, 1), (3, 4, 5), (0, 1, 4, 3), (1, 2, 5, 4), (2, 0, 3, 5)),
    8: ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)),
}


def sub(a, b):
    return [x - y for x, y in zip(a, b)]


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def audit(path):
    m = json.loads(Path(path).read_text())
    ps, cells = m['points'], m['cells']
    assert len(ps) == len(set(map(tuple, ps)))
    assert len(cells) == len(m['quality']) == len(m['volumes'])
    assert all(math.isfinite(v) and v > .25 for v in m['quality'])
    occurrences, boundary, vols, closures = Counter(), {}, [], []
    for ci, cell in enumerate(cells):
        assert len(cell) == len(set(cell)) and all(0 <= i < len(ps) for i in cell)
        center = [math.fsum(ps[i][k] for i in cell) / len(cell) for k in range(3)]
        areas, tets = [], []
        for face in FACES[len(cell)]:
            f = [cell[i] for i in face]
            pivot = f.index(min(f))
            f = f[pivot:] + f[:pivot]
            key = tuple(sorted(f))
            occurrences[key] += 1
            if key in boundary:
                old, owner = boundary.pop(key)
                assert old == [f[0], *reversed(f[1:])] and owner != ci
            else:
                boundary[key] = (f, ci)
            a = ps[f[0]]
            for j in range(1, len(f) - 1):
                b, c = ps[f[j]], ps[f[j + 1]]
                twice_area = cross(sub(b, a), sub(c, a))
                areas.append([v * .5 for v in twice_area])
                tv = math.fsum(x * y for x, y in zip(sub(a, center), twice_area)) / 6
                assert tv > 0, f'Nonpositive simplex in cell {ci}'
                tets.append(tv)
        volume = math.fsum(tets)
        closure = math.hypot(*(math.fsum(a[k] for a in areas) for k in range(3))) / volume**(2 / 3)
        assert math.isclose(volume, m['volumes'][ci], rel_tol=3e-12)
        assert closure < 1e-14, (ci, closure)
        vols.append(volume)
        closures.append(closure)
    assert max(occurrences.values()) == 2
    assert len(occurrences) == m['stats']['faces']
    assert set(boundary) == {tuple(sorted(b[2:])) for b in m['boundary']}
    owners, wall_edges = set(), Counter()
    for owner, patch, *f in m['boundary']:
        assert 0 <= patch < 6 and boundary[tuple(sorted(f))][1] == owner
        assert owner not in owners
        owners.add(owner)
        for a, b in zip(f, f[1:] + f[:1]):
            wall_edges[tuple(sorted((a, b)))] += 1
    assert set(wall_edges.values()) == {2}
    assert math.isclose(math.fsum(vols), m['stats']['expectedVolume'], rel_tol=1e-12)
    print(f'PASS {path}: {len(cells):,} cells, {len(boundary):,} exterior faces; '
          f'min(V)={min(vols):.9g}, min(JENS)={min(m["quality"]):.9g}, max(closure)={max(closures):.5g}')


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('Do not disable audit assertions with python -O')
    for name in sys.argv[1:] or ['public/meshes/l-channel.json', 'public/meshes/twin-plate.json']:
        audit(name)