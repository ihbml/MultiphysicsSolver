#include "difvm/generate.hpp"
#include <cstdio>
#include <vector>
using namespace difvm;
int main(){
  TriangleSoup cube; const Vec3 sh{0.037,0.011,0.023};
  std::vector<Vec3> v; for(int i=0;i<8;++i) v.push_back(Vec3{(i&1?0.5:-0.5),(i&2?0.5:-0.5),(i&4?0.5:-0.5)}+sh);
  const int idx[36]={0,3,2,0,2,1,4,5,6,4,6,7,0,1,5,0,5,4,3,7,6,3,6,2,0,4,7,0,7,3,1,2,6,1,6,5};
  for(int t=0;t<12;++t){ cube.verts.push_back(v[idx[3*t]]); cube.verts.push_back(v[idx[3*t+1]]); cube.verts.push_back(v[idx[3*t+2]]); }
  cube.finalise();
  CutCellOptions o; o.h=0.055; o.report=false;
  CutCellStats st;
  make_cutcell_mesh(cube, o, &st);
  std::printf("grid %dx%dx%d cut=%ld solid=%ld dropped=%ld exact=%g\n",st.nx,st.ny,st.nz,st.n_cut_cells,st.n_solid_cells,st.n_dropped_slivers,st.exact_volume);
}
