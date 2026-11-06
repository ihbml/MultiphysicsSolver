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
    if(i>0){ soup.verts.push_back(soup.verts[a]); soup.verts.push_back(soup.verts[c]); soup.verts.push_back(soup.verts[d]); }
    if(i<nlat-1){ soup.verts.push_back(soup.verts[a]); soup.verts.push_back(soup.verts[b]); soup.verts.push_back(soup.verts[c]); } }
  soup.finalise();
}
int main(){
  TriangleSoup sp; make_sphere_soup(sp,0.5,40,48);
  double v=0; for(std::size_t t=0;t<sp.n_tris();++t) v+=dot(sp.verts[3*t],cross(sp.verts[3*t+1],sp.verts[3*t+2]))/6.0;
  std::printf("sphere soup: tris=%zu vol=%+.6f (analytic %.6f)\n", sp.n_tris(), v, 4.0/3.0*M_PI*pow(0.5,3));
  CutCellOptions o; o.h=0.08; o.report=true; CutCellStats st;
  make_cutcell_mesh(sp, o, &st);
  std::printf("%s\n", st.to_string().c_str());
}
