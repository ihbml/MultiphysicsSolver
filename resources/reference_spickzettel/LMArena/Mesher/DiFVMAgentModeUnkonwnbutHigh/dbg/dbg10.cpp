#include "difvm/generate.hpp"
#include <cstdio>
#include <cmath>
#include <vector>
using namespace difvm;
int main(){
  TriangleSoup soup;
  const Vec3 sh{0.037,0.011,0.023};
  std::vector<Vec3> v;
  for(int i=0;i<8;++i) v.push_back(Vec3{(i&1?0.5:-0.5),(i&2?0.5:-0.5),(i&4?0.5:-0.5)}+sh);
  const int idx[36]={0,3,2,0,2,1,4,5,6,4,6,7,0,1,5,0,5,4,3,7,6,3,6,2,0,4,7,0,7,3,1,2,6,1,6,5};
  for(int t=0;t<12;++t){ soup.verts.push_back(v[idx[3*t]]); soup.verts.push_back(v[idx[3*t+1]]); soup.verts.push_back(v[idx[3*t+2]]); }
  soup.finalise();
  CutCellOptions o; o.report=false;
  CutCellStats st;
  PolyMeshRaw raw = make_cutcell_mesh(soup, o, &st);
  std::printf("grid %dx%dx%d cells=%ld faces=%ld cut=%ld pairs=%ld\n", st.nx,st.ny,st.nz,(long)raw.n_cells,(long)raw.owner.size(),st.n_cut_cells,st.n_conformity_pairs);
  int open=0, first=0;
  for(long c=0;c<raw.n_cells;++c){
    Vec3 S{0,0,0}; double A=0,V=0; int nf=0;
    for(std::size_t f=0;f<raw.owner.size();++f){
      if(raw.owner[f]!=c) continue; ++nf;
      gidx b=raw.face_offsets[f], e=raw.face_offsets[f+1];
      Vec3 s{0,0,0}, xc{0,0,0};
      for(gidx k=b;k<e;++k){ Vec3 a=raw.points[raw.face_verts[k]], bb=raw.points[raw.face_verts[(k+1<e)?k+1:b]]; s+=cross(a,bb); xc+=a; }
      s=s*0.5; xc=xc/double(e-b); S+=s; A+=norm(s); V+=dot(xc,s)/3;
    }
    double cl = A>0?norm(S)/A:1;
    if(cl>1e-6){ ++open; if(first<3){ ++first;
      std::printf("cell %ld nf=%d cl=%.3f V=%.5f\n",c,nf,cl,V);
      for(std::size_t f=0;f<raw.owner.size();++f){ if(raw.owner[f]!=c) continue;
        gidx b=raw.face_offsets[f], e=raw.face_offsets[f+1];
        Vec3 s{0,0,0};
        std::printf("  f%zu n=%ld:",f,(long)(e-b));
        for(gidx k=b;k<e;++k){ Vec3 a=raw.points[raw.face_verts[k]]; std::printf(" (%+.3f %+.3f %+.3f)",a.x,a.y,a.z);}
        for(gidx k=b;k<e;++k){ Vec3 a=raw.points[raw.face_verts[k]], bb=raw.points[raw.face_verts[(k+1<e)?k+1:b]]; s+=cross(a,bb);}
        s=s*0.5; std::printf(" S=(%+.4f %+.4f %+.4f)\n",s.x,s.y,s.z);
      }
    }}
  }
  std::printf("open=%d / %ld\n",open,(long)raw.n_cells);
}
