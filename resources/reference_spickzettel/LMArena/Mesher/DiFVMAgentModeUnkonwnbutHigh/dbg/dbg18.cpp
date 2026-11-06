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
int main(){
  TriangleSoup sp; make_sphere_soup(sp,0.5,40,48);
  CutCellOptions o; o.h=0.08; o.report=false; CutCellStats st;
  PolyMeshRaw raw = make_cutcell_mesh(sp,o,&st);
  BuildOptions bo; GraphMesh gm = build_graph_mesh(raw,bo);
  int printed=0;
  for(gidx c=0;c<gm.n_cells && printed<3;++c){
    Vec3 S{0,0,0}; double A=0;
    for(gidx q=gm.cf_offsets[c]; q<gm.cf_offsets[c+1]; ++q){
      const gidx f=gm.cf_faces[q];
      Vec3 s=gm.Sf.get((std::size_t)f);
      if(gm.owner[(std::size_t)f]!=c) s = s*(-1.0);
      S+=s; A+=gm.magSf[(std::size_t)f];
    }
    if(A>0 && norm(S)/A > 1e-6){
      ++printed;
      std::printf("OPEN cell %ld nf=%ld vol=%.3e cl=%.3f centre=(%+.3f %+.3f %+.3f)\n",(long)c,
        (long)gm.n_faces_of_cell(c), gm.vol[(std::size_t)c], norm(S)/A,
        gm.cf.get((std::size_t)c).x, gm.cf.get((std::size_t)c).y, gm.cf.get((std::size_t)c).z);
      for(gidx q=gm.cf_offsets[c]; q<gm.cf_offsets[c+1]; ++q){
        const gidx f=gm.cf_faces[q];
        std::printf("   f%ld own=%d nb=%d nv=%ld |S|=%.3e\n",(long)f,(int)gm.owner[(std::size_t)f],
          (int)gm.neighbour[(std::size_t)f],(long)(gm.fv_offsets[(std::size_t)f+1]-gm.fv_offsets[(std::size_t)f]),
          gm.magSf[(std::size_t)f]);
      }
    }
  }
  std::printf("dropped=%ld cut=%ld cells=%ld\n",st.n_dropped_slivers,st.n_cut_cells,(long)gm.n_cells);
}
