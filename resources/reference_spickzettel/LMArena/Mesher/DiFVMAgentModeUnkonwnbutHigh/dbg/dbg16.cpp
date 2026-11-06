#include "difvm/generate.hpp"
#include <cstdio>
using namespace difvm;
int main(int argc, char** argv){
  TriangleSoup s = read_stl(argv[1]);
  double v=0, area=0;
  for(std::size_t t=0;t<s.n_tris();++t){
    const Vec3 a=s.verts[3*t], b=s.verts[3*t+1], c=s.verts[3*t+2];
    v += dot(a,cross(b,c))/6.0;
    area += 0.5*norm(cross(b-a,c-a));
    if(t<3) std::printf("tri %zu a=(%g %g %g) b=(%g %g %g) c=(%g %g %g) n=(%g %g %g)\n",t,a.x,a.y,a.z,b.x,b.y,b.z,c.x,c.y,c.z,s.normals[t].x,s.normals[t].y,s.normals[t].z);
  }
  std::printf("tris=%zu vol=%g area=%g bbox=[%g %g %g]..[%g %g %g]\n", s.n_tris(), v, area, s.lo.x,s.lo.y,s.lo.z,s.hi.x,s.hi.y,s.hi.z);
}
