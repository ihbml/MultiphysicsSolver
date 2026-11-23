"""Exact certificates for the binary64 coordinates actually shipped to the UI.

Python's arbitrary-size integers make this independent of C++ evaluation order,
roundoff, coordinate magnitude, and compiler floating-point optimizations.
"""

import hashlib
import json
import sys
from pathlib import Path
from verify_mesh import FACES


def sub(a, b):
    return a[0]-b[0], a[1]-b[1], a[2]-b[2]


def dot(a, b):
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def det(a, b, c):
    return dot(cross(a, b), c)


def squared(v):
    return dot(v, v)


def scaled_gate(a, b, c, opt_n, opt_d):
    # JENS > 1/4 iff the positive raw SJ exceeds opt/4. The reflected
    # above-optimum branch is at least opt, so introduces no false positives.
    d = det(a,b,c)
    assert d > 0 and 16*opt_d*d*d > opt_n*squared(a)*squared(b)*squared(c)


def certify(path):
    raw = Path(path).read_bytes()
    mesh = json.loads(raw)
    ratios = [[float(x).as_integer_ratio() for x in p] for p in mesh['points']]
    scale = max(d for p in ratios for _, d in p)
    points = [tuple(n*(scale//d) for n,d in p) for p in ratios]
    volume_numerator = 0
    simplex_count = 0
    wedge_count = 0
    for cell in mesh['cells']:
        p = [points[i] for i in cell]
        n = len(p)
        total = tuple(map(sum,zip(*p)))
        closure = [0,0,0]
        for local in FACES[n]:
            ids = [cell[i] for i in local]
            k = ids.index(min(ids))
            f = [points[i] for i in ids[k:]+ids[:k]]
            a=f[0]
            origin=tuple(n*a[j]-total[j] for j in range(3))
            for j in range(1,len(f)-1):
                twice_area=cross(sub(f[j],a),sub(f[j+1],a))
                six_n_volume=dot(origin,twice_area)
                assert six_n_volume > 0, 'Nonpositive exact simplex'
                simplex_count += 1
                volume_numerator += six_n_volume*(120//n)
                for k in range(3): closure[k]+=twice_area[k]
        assert closure == [0,0,0], 'Exact face-vector closure failed'

        def corner(i,j,k,l):
            opt_n,opt_d=(1,1) if n==8 else (3,4) if n==6 else (1,2)
            scaled_gate(sub(p[j],p[i]),sub(p[k],p[i]),sub(p[l],p[i]),opt_n,opt_d)

        if n==4:
            for t in ((0,1,2,3),(1,0,3,2),(2,0,1,3),(3,0,2,1)): corner(*t)
        elif n==5:
            tris=[cross(sub(p[j],p[0]),sub(p[j+1],p[0])) for j in (1,2)]
            normal=tuple(map(sum,zip(*tris)))
            for i in range(4):
                a,b=(i+1)%4,(i+3)%4
                corner(i,a,b,4);corner(4,i,b,(i+2)%4)
                # The positive extrusion height cancels in the scaled Jacobian.
                scaled_gate(sub(p[a],p[i]),sub(p[b],p[i]),normal,1,1)
        else:
            m=n//2
            for i in range(m):
                a,b=(i+1)%m,(i+m-1)%m
                corner(i,a,b,i+m);corner(i+m,b+m,a+m,i)
        if n==6:
            wedge_count+=1
            a,b=sub(p[1],p[0]),sub(p[2],p[0])
            at,bt=sub(p[4],p[3]),sub(p[5],p[3])
            da,db=sub(at,a),sub(bt,b)
            for i in range(3):
                d=sub(p[i+3],p[i])
                A,B,C=det(da,db,d),det(da,b,d)+det(a,db,d),det(a,b,d)
                low,den=min(C,A+B+C),1
                if A>0 and -2*A<B<0: low,den=4*A*C-B*B,4*A
                max_norm2=max(squared(a),squared(at))*max(squared(b),squared(bt))*squared(d)
                assert low>0 and 400*low*low>=9*den*den*max_norm2, 'Exact wedge bound below 0.15'
    expected=62464 if mesh['geometry']==0 else 12800
    assert volume_numerator==expected*720*scale**3, 'Exact analytic volume mismatch'
    result={
        'meshId':mesh['id'], 'sha256':hashlib.sha256(raw).hexdigest(),
        'arithmetic':'arbitrary-precision integer predicates on exact binary64 coordinates',
        'cells':len(mesh['cells']), 'positiveSimplices':simplex_count,
        'certifiedPrisms':wedge_count, 'exactClosureResidual':0,
        'exactVolumeMm3':expected,'allJENSAboveOneQuarter':True,
        'allContinuousWedgeBoundsAtLeastThreeTwentieths':True,
    }
    print(f'EXACT PASS {path}: {simplex_count:,} positive simplices, {wedge_count:,} continuous '
          f'wedge bounds; all JENS > 1/4, closure identically zero, V = {expected} mm^3.')
    return result


if __name__=='__main__':
    if not __debug__: raise RuntimeError('Do not disable certificate assertions with python -O')
    args=sys.argv[1:]
    write='--write-report' in args
    if write: args.remove('--write-report')
    results=[certify(p) for p in args or ['public/meshes/l-channel.json','public/meshes/twin-plate.json']]
    if write: Path('public/meshes/certificates.json').write_text(json.dumps(results,indent=2)+'\n')
    print(json.dumps(results,indent=2))