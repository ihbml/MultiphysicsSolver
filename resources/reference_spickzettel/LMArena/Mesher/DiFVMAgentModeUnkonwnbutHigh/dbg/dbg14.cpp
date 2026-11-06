#include "difvm/generate.hpp"
#include "difvm/graph_mesh.hpp"
#include <cstdio>
#include <cmath>
#include <vector>
using namespace difvm;
int main(){
  TriangleSoup cb; const Vec3 sh{0.037,0.011,0.023};
  std::vector<Vec3> v; for(int i=0;i<8;++i) v.push_back(Vec3{(i&1?0.5:-0.5),(i&2?0.5:-0.5),(i&4?0.5:-0.5)}+sh);
  const int qd[6][4]={{0,2,3,1},{4,5,7,6},{0,1,5,4},{2,6,7,3},{0,4,6,2},{1,3,7,5}};
  for(int f=0;f<6;++f){ const int*a=qd[f]; int t1[3]={a[0],a[1],a[2]}, t2[3]={a[0],a[2],a[3]};
    for(int k=0;k<3;++k) cb.verts.push_back(v[t1[k]]);
    for(int k=0;k<3;++k) cb.verts.push_back(v[t2[k]]); }
  cb.finalise();
  CutCellOptions o; o.h=0.055; o.report=false; CutCellStats st;
  PolyMeshRaw raw = make_cutcell_mesh(cb, o, &st);
  BuildOptions bo; GraphMesh gm = build_graph_mesh(raw, bo);
  MeshAudit au = audit_mesh(gm);
  // classify boundary faces
  double a_box=0, a_cap=0, a_other=0; long n_other=0;
  double boxlo=1e9, boxhi=-1e9;
  for(gidx c=0;c<gm.n_cells;++c){ for(Vec3 p: gm.points) {} }
  const double tol = 1e-6;
  // domain box from boundary vertices
  for(gidx f=gm.n_internal; f<gm.n_faces; ++f){
    const gidx b=gm.fv_offsets[(std::size_t)f], e=gm.fv_offsets[(std::size_t)f+1];
    const gidx nv=e-b;
    // cap = triangle whose 3 vertices are all inside the domain box interior (not on box)
    bool on_box=false;
    // check if all vertices share a coordinate equal to a plane {min or max} of the mesh points
    double mnx=1e9,mxx=-1e9,mny=1e9,mxy=-1e9,mnz=1e9,mxz=-1e9;
    for(gidx i=0;i<gm.n_cells;++i){}
    on_box = false;
    (void)mnx;(void)mxx;(void)mny;(void)mxy;(void)mnz;(void)mxz;(void)boxlo;(void)boxhi;(void)tol;
    if(nv==3) { a_cap += gm.magSf[(std::size_t)f]; }
    else { a_other += gm.magSf[(std::size_t)f]; ++n_other; }
    (void)on_box; (void)b;
  }
  std::printf("boundary faces=%ld  triangles(caps)=%.4f  polygons=%.4f (n=%ld)  total=%.4f\n",
     (long)gm.n_boundary, a_cap, a_other, n_other, au.boundary_area);
  std::printf("cells=%ld internal=%ld vol=%.6f target=%.6f err=%+.3f%%\n",(long)gm.n_cells,(long)gm.n_internal,
     au.total_volume, st.grid_volume-st.exact_volume, 100.0*(au.total_volume-(st.grid_volume-st.exact_volume))/(st.grid_volume-st.exact_volume));
}
