#!/usr/bin/env python3
"""Integration and geometric invariant tests; requires numpy for array analysis."""
import json
import math
import pathlib
import subprocess
import sys
import tempfile
import numpy as np

exe = pathlib.Path(sys.argv[1]).resolve()

def run(out, *args):
    return subprocess.run([str(exe), '--out', str(out), '--no-vtk', *args],
                          check=True, stdout=subprocess.PIPE, text=True).stdout.strip()

def read(out):
    meta = json.loads((out / 'manifest.json').read_text())
    cells, faces = meta['cells'], meta['faces']
    a = {}
    for name in ['owner','neighbour','patch','sx','sy','sz','fx','fy','fz','dx','dy','dz',
                 'kx','ky','kz','angle','skx','sky','skz','beta','cx','cy','cz','volume','fraction','omega']:
        dtype = '<i8' if name in ('owner','neighbour') else '<i4' if name=='patch' else '<f8'
        a[name] = np.fromfile(out / (name + '.bin'), dtype=dtype)
        assert len(a[name]) == (cells if name in ('cx','cy','cz','volume','fraction','omega') else faces), name
    return a

def check(a):
    o,n = a['owner'],a['neighbour']; N=len(a['volume']); internal=n>=0
    assert np.all((o>=0)&(o<N)) and np.all(n[internal]<N) and np.all(n[~internal]==-1)
    assert np.all(o[internal]!=n[internal])
    assert np.all(a['patch'][internal]==0) and np.all(np.isin(a['patch'][~internal],[1,2]))
    assert np.all(a['volume']>0) and np.all((a['fraction']>0)&(a['fraction']<=1+1e-10))
    assert np.all((a['omega']>0)&(a['omega']<=1)) and np.all((a['beta']>=0)&(a['beta']<=1))
    S=np.stack([a['sx'],a['sy'],a['sz']],axis=1)
    F=np.stack([a['fx'],a['fy'],a['fz']],axis=1)
    C=np.stack([a['cx'],a['cy'],a['cz']],axis=1)
    D=np.stack([a['dx'],a['dy'],a['dz']],axis=1)
    K=np.stack([a['kx'],a['ky'],a['kz']],axis=1)
    assert np.allclose(D+K,S,rtol=1e-12,atol=1e-14)
    direction=F-C[o];direction[internal]=C[n[internal]]-C[o[internal]]
    assert np.all(np.einsum('ij,ij->i',S,direction)>0)
    closure=np.zeros((N,3)); np.add.at(closure,o,S); np.add.at(closure,n[internal],-S[internal])
    areas=np.linalg.norm(S,axis=1); perimeter=np.zeros(N);np.add.at(perimeter,o,areas);np.add.at(perimeter,n[internal],areas[internal])
    assert np.max(np.linalg.norm(closure,axis=1)/perimeter)<1e-11
    # Divergence theorem gives each cell's volume with the owner-oriented face vectors.
    vf=np.einsum('ij,ij->i',F,S)/3
    vc=np.zeros(N);np.add.at(vc,o,vf);np.add.at(vc,n[internal],-vf[internal])
    assert np.allclose(vc,a['volume'],rtol=1e-7,atol=2e-15)
    return float(np.sum(a['volume']))

def cube_stl(path, binary=False):
    # A closed oriented cube, with exact +/- 0.4 extents.
    v=[(-.4,-.4,-.4),(.4,-.4,-.4),(.4,.4,-.4),(-.4,.4,-.4),
       (-.4,-.4,.4),(.4,-.4,.4),(.4,.4,.4),(-.4,.4,.4)]
    faces=[(0,2,1),(0,3,2),(4,5,6),(4,6,7),(0,1,5),(0,5,4),
           (1,2,6),(1,6,5),(2,3,7),(2,7,6),(3,0,4),(3,4,7)]
    if binary:
        import struct
        with path.open('wb') as f:
            f.write(b'cube'.ljust(80,b'\0')); f.write(struct.pack('<I',len(faces)))
            for tri in faces:
                coords=sum((v[i] for i in tri),())
                f.write(struct.pack('<12fH',0,0,0,*coords,0))
    else:
        with path.open('w') as f:
            f.write('solid cube\n')
            for tri in faces:
                f.write('facet normal 0 0 0\nouter loop\n')
                for i in tri: f.write('vertex %s %s %s\n'%v[i])
                f.write('endloop\nendfacet\n')
            f.write('endsolid cube\n')

with tempfile.TemporaryDirectory() as tmp:
    root=pathlib.Path(tmp)
    empty=root/'empty'; print(run(empty,'--n','4','--center','10','10','10'));v=check(read(empty));assert abs(v-8)<1e-12
    exact=8-4*math.pi*.5**3/3
    errors=[]
    for n in [8,12,16]:
        path=root/('sphere'+str(n));print(run(path,'--n',str(n)));a=read(path);v=check(a);assert np.any(a['patch']==2);errors.append(abs(v-exact))
    assert errors[2]<errors[1]<errors[0],errors
    for binary in (False, True):
        path=root/('cube_binary.stl' if binary else 'cube_ascii.stl');cube_stl(path,binary)
        out=root/('stl_binary' if binary else 'stl_ascii');print(run(out,'--n','10','--stl',str(path)));a=read(out);v=check(a);assert np.any(a['patch']==2) and abs(v-(8-.8**3))<.12
    bad=subprocess.run([str(exe),'--n','0'],capture_output=True); assert bad.returncode!=0
    print('PASS: closed volumes, connectivity, decomposition, convergence, ASCII/binary STL, invalid input')
