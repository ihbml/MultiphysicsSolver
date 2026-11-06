#include "difvm/graph_mesh.hpp"
#include "difvm/generate.hpp"
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
    if(i>0){ soup.verts.push_back(soup.verts[b]); soup.verts.push_back(soup.verts[a]); soup.verts.push_back(soup.verts[d]); }
    if(i<nlat-1){ soup.verts.push_back(soup.verts[a]); soup.verts.push_back(soup.verts[b]); soup.verts.push_back(soup.verts[c]); } }
}
static double vol(const TriangleSoup& s){ double v=0; for(std::size_t t=0;t<s.n_tris();++t) v+=dot(s.verts[3*t],cross(s.verts[3*t+1],s.verts[3*t+2]))/6.0; return v; }
int main(){
  TriangleSoup sp; make_sphere_soup(sp,0.5,40,48);
  std::printf("sphere tris=%zu vol_before=%+.6f area=%g\n", sp.n_tris(), vol(sp), sp.area());
  sp.finalise();
  std::printf("sphere        vol_after =%+.6f area=%g exact=%.6f\n", vol(sp), sp.area(), 4.0/3.0*M_PI*pow(0.5,3));
  // cube from explicit quads
  TriangleSoup cb; const Vec3 sh{0.037,0.011,0.023};
  std::vector<Vec3> v; for(int i=0;i<8;++i) v.push_back(Vec3{(i&1?0.5:-0.5),(i&2?0.5:-0.5),(i&4?0.5:-0.5)}+sh);
  // quads with outward CCW: z-, z+, y-, y+, x-, x+
  const int q[6][4]={{0,2,3,1},{4,5,7,6},{0,1,5,4},{2,6,7,3},{0,4,6,2},{1,3,7,5}};
  for(int f=0;f<6;++f){ const int*a=q[f]; int t1[3]={a[0],a[1],a[2]}, t2[3]={a[0],a[2],a[3]};
    for(int k=0;k<3;++k) cb.verts.push_back(v[t1[k]]);
    for(int k=0;k<3;++k) cb.verts.push_back(v[t2[k]]); }
  std::printf("cube   tris=%zu vol_before=%+.6f area=%g\n", cb.n_tris(), vol(cb), cb.area());
  cb.finalise();
  std::printf("cube          vol_after =%+.6f area=%g exact=1\n", vol(cb), cb.area());
  CutCellOptions o; o.h=0.055; o.report=true; CutCellStats st;
  PolyMeshRaw raw = make_cutcell_mesh(cb, o, &st);
  std::printf("%s\n", st.to_string().c_str());
  BuildOptions bo; GraphMesh gm = build_graph_mesh(raw, bo); MeshAudit au = audit_mesh(gm);
  std::printf("audit cells=%ld vol=%.6f min=%.3e max=%.3e closure=%.1e neg=%ld\n",(long)gm.n_cells,au.total_volume,au.min_volume,au.max_volume,au.max_closure_residual,(long)au.n_negative_cells);
  // volume histogram of cells
  double mn=1e9,mx=-1e9; for(gidx i=0;i<gm.n_cells;++i){ mn=std::min(mn,gm.vol[i]); mx=std::max(mx,gm.vol[i]); }
  std::printf("faces: internal=%ld boundary=%ld  boundary_area=%.4f (cube surface=6)\n",
      (long)gm.n_internal,(long)gm.n_boundary,au.boundary_area);
}
