#include "difvm/generate.hpp"
#include "difvm/graph_mesh.hpp"
#include <cstdio>
#include <cmath>
#include <vector>
using namespace difvm;
static void make_sphere_soup(TriangleSoup& soup, double r, int nlat, int nlon) {
  std::vector<std::vector<int>> idx(nlat+1, std::vector<int>(nlon));
  for (int i=0;i<=nlat;++i){ double th=M_PI*i/nlat; for(int j=0;j<nlon;++j){ double ph=2*M_PI*j/nlon;
    soup.verts.push_back(Vec3{r*std::sin(th)*std::cos(ph), r*std::sin(th)*std::sin(ph), r*std::cos(th)});
    idx[i][j]=(int)soup.verts.size()-1; } }
  for (int i=0;i<nlat;++i) for(int j=0;j<nlon;++j){ int j2=(j+1)%nlon;
    int a=idx[i][j],b=idx[i][j2],c=idx[i+1][j2],d=idx[i+1][j];
    if(i>0){ soup.verts.push_back(soup.verts[a]); soup.verts.push_back(soup.verts[c]); soup.verts.push_back(soup.verts[d]); }
    if(i<nlat-1){ soup.verts.push_back(soup.verts[a]); soup.verts.push_back(soup.verts[b]); soup.verts.push_back(soup.verts[c]); } }
  soup.finalise();
}
static void report(const char* tag, const TriangleSoup& soup, const CutCellOptions& o){
  CutCellStats st;
  PolyMeshRaw raw = make_cutcell_mesh(soup, o, &st);
  BuildOptions bo;
  GraphMesh gm = build_graph_mesh(raw, bo);
  MeshAudit au = audit_mesh(gm);
  const double target = st.grid_volume - st.exact_volume;
  std::printf("%-7s h=%.3f grid=%dx%dx%d cells=%-6ld cut=%-5ld vol=%.6f tgt=%.6f err=%+.3f%% closure=%.1e open=%ld neg=%ld wetted=%.4f time=%.0fms\n",
    tag,o.h,st.nx,st.ny,st.nz,(long)gm.n_cells,st.n_cut_cells,au.total_volume,target,
    100.0*(au.total_volume-target)/target, au.max_closure_residual, (long)au.n_open_cells,(long)au.n_negative_cells,
    st.surface_area, st.time_ms);
}
int main(){
  TriangleSoup cube; const Vec3 sh{0.037,0.011,0.023};
  std::vector<Vec3> v; for(int i=0;i<8;++i) v.push_back(Vec3{(i&1?0.5:-0.5),(i&2?0.5:-0.5),(i&4?0.5:-0.5)}+sh);
  const int qd[6][4]={{0,2,3,1},{4,5,7,6},{0,1,5,4},{2,6,7,3},{0,4,6,2},{1,3,7,5}};
  for(int f=0;f<6;++f){ const int*a=qd[f]; int t1[3]={a[0],a[1],a[2]}, t2[3]={a[0],a[2],a[3]};
    for(int k=0;k<3;++k) cube.verts.push_back(v[t1[k]]);
    for(int k=0;k<3;++k) cube.verts.push_back(v[t2[k]]); }
  cube.finalise();
  { CutCellOptions o; o.h=0.055; o.report=false; report("cube",cube,o); }
  TriangleSoup sph; make_sphere_soup(sph,0.5,40,48);
  for(double h:{0.08,0.04,0.02}){ CutCellOptions o; o.h=h; o.report=false; report("sphere",sph,o); }
}
