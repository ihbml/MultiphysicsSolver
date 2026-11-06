#include "hybridmesh/hybridmesh.hpp"

#include <chrono>
#include <iostream>
#include <string_view>

using namespace hybridmesh;
int main(int argc,char** argv) {
    using Clock=std::chrono::steady_clock;
    const bool three=argc>1 && std::string_view(argv[1])=="3d";
    const auto start=Clock::now();
    auto mesh=three ? make_box3d(32,32,32,1,1,1) : make_box2d(160,160,1,1);
    const auto mesh_done=Clock::now();
    auto graph=make_graph(mesh);
    const auto graph_done=Clock::now();
    const double spacing=three ? 1.0/32 : 1.0/160;
    auto region=Region::sphere({.5,.5,three ? .5 : 0},three ? .21 : .20,mesh.dimension);
    auto points=discretize(mesh,region,{spacing,2.6,2});
    const auto sample_done=Clock::now();
    auto plan=make_transfer_plan(mesh,points);
    const auto plan_done=Clock::now();
    auto elapsed=[](auto a,auto b) {return std::chrono::duration<double>(b-a).count();};
    std::cout<<(three ? "3D" : "2D")<<" cells="<<mesh.cells.size()
             <<" faces="<<mesh.faces.size()<<" graph_links="<<graph.neighbor_ids.size()
             <<" active_particles="<<points.particles.size()
             <<" directed_sph_links="<<points.neighbor_ids.size()<<'\n'
             <<"seconds build_mesh="<<elapsed(start,mesh_done)
             <<" graph="<<elapsed(mesh_done,graph_done)
             <<" sample_and_neighbors="<<elapsed(graph_done,sample_done)
             <<" coupling_plan="<<elapsed(sample_done,plan_done)<<'\n'
             <<"fallbacks p2m="<<plan.p2m_fallbacks<<" m2p="<<plan.m2p_fallbacks
             <<" singular_moments="<<plan.singular_moments<<'\n';
}
