#include "hybridmesh/hybridmesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

using namespace hybridmesh;
namespace {
int checks=0;
void check(bool condition,const char* expression,int line) {
    ++checks;
    if (!condition) throw std::runtime_error(std::string("line ")+std::to_string(line)+
                                              ": assertion failed: "+expression);
}
#define CHECK(x) check((x),#x,__LINE__)
void close(double a,double b,double tolerance=1e-10) {
    if (std::abs(a-b)>tolerance)
        throw std::runtime_error("expected "+std::to_string(b)+", got "+std::to_string(a));
    ++checks;
}
std::filesystem::path fixture(const std::string& file) {
    #ifdef HYBRIDMESH_SOURCE_DIR
    return std::filesystem::path(HYBRIDMESH_SOURCE_DIR)/"tests"/"fixtures"/file;
    #else
    return std::filesystem::path("tests")/"fixtures"/file;
    #endif
}
template<class F> void throws(F&& call) {
    bool caught=false;
    try {call();} catch (const std::exception&) {caught=true;}
    CHECK(caught);
}
void validate_geometry(const Mesh& mesh) {
    for (const auto& c:mesh.cells) CHECK(c.volume>0);
    for (std::size_t ci=0;ci<mesh.cells.size();++ci) {
        Vec3 sum{};
        for (Index fi:mesh.cells[ci].faces) {
            const auto& f=mesh.faces[fi];
            sum+=f.area_vector*(f.owner==ci ? 1.0 : -1.0);
        }
        CHECK(norm(sum)<1e-8);
    }
}
void box_geometry() {
    const auto two=make_box2d(2,3,2.0,3.0);
    CHECK(two.dimension==2);
    CHECK(two.cells.size()==6);
    CHECK(two.faces.size()==17);
    validate_geometry(two);
    for (const auto& c:two.cells) close(c.volume,1.0);
    std::array<int,5> patches{};
    for (const auto& f:two.faces) if (f.neighbor==no_index) ++patches[f.patch];
    CHECK((patches==std::array<int,5>{0,3,3,2,2}));
    auto graph=make_graph(two);
    CHECK(graph.face_owner.size()==two.faces.size());
    CHECK(graph.neighbor_offsets.size()==two.cells.size()+1);
    CHECK(graph.neighbor_ids.size()==14);
    std::vector<double> flux(two.faces.size());
    double boundary_flux=0;
    for (std::size_t i=0;i<flux.size();++i) {
        flux[i]=std::sin(static_cast<double>(i)+1);
        if (two.faces[i].neighbor==no_index) boundary_flux+=flux[i];
    }
    auto summed=scatter_face_flux(two,flux);
    close(std::accumulate(summed.begin(),summed.end(),0.0),boundary_flux);
    // Constant vector field has exactly zero flux divergence in closed cells.
    for (std::size_t i=0;i<flux.size();++i)
        flux[i]=dot(two.faces[i].area_vector,Vec3{0.3,-2,0});
    for (double v:scatter_face_flux(two,flux,true)) close(v,0.0);

    const auto three=make_box3d(2,2,2,2,2,2);
    CHECK(three.dimension==3);
    CHECK(three.cells.size()==8);
    CHECK(three.faces.size()==36);
    validate_geometry(three);
    for (const auto& c:three.cells) close(c.volume,1.0);
    for (std::size_t i=0;i<three.faces.size();++i)
        if (three.faces[i].neighbor!=no_index)
            CHECK(norm(three.faces[i].area_vector)>0);
}
void gmsh_and_foam() {
    auto sq=read_gmsh22(fixture("square.msh"));
    CHECK(sq.cells.size()==2);
    CHECK(sq.faces.size()==5);
    CHECK(sq.patches.size()==5);
    validate_geometry(sq);
    for (const auto& c:sq.cells) close(c.volume,0.5);
    std::array<int,5> counts{};
    for (const auto& f:sq.faces) if (f.neighbor==no_index) ++counts[f.patch];
    CHECK((counts==std::array<int,5>{0,1,1,1,1}));
    CHECK(sq.patches[1].name=="inlet");
    CHECK(sq.patches[3].name=="top wall");
    auto mixed=read_gmsh22(fixture("mixed3d.msh"));
    CHECK(mixed.cells.size()==4);
    validate_geometry(mixed);
    close(mixed.cells[0].volume,1.0/6.0);
    close(mixed.cells[1].volume,1.0);
    close(mixed.cells[2].volume,0.5);
    close(mixed.cells[3].volume,1.0/3.0);
    auto foam=read_openfoam(fixture("foam"));
    CHECK(foam.cells.size()==2);
    CHECK(foam.faces.size()==11);
    validate_geometry(foam);
    for (const auto& c:foam.cells) close(c.volume,1.0);
    CHECK(foam.patches.size()==5);
    CHECK(foam.patches[1].name=="left");
    CHECK(foam.patches[3].name=="right");
    CHECK(foam.faces[0].owner==0 && foam.faces[0].neighbor==1);
    auto coarse=discretize(foam,Region::sphere({1,.5,.5},.3,3),{.4,2.6,2});
    CHECK(make_transfer_plan(foam,coarse).m2p_fallbacks>0); // rank-deficient FV centroids
    auto poly=read_openfoam(fixture("poly_prism"));
    CHECK(poly.cells.size()==1);
    CHECK(poly.cells[0].vtk_type==0); // arbitrary, not a standard VTK element
    CHECK(poly.faces.size()==7);
    validate_geometry(poly);
    close(poly.cells[0].volume,3.0);
    const auto pset=discretize(poly,Region::box(poly.bounds_min,poly.bounds_max,3),{.35,2.6,2});
    CHECK(!pset.particles.empty());
}
void sph_boundary_and_volume() {
    auto mesh=make_box2d(20,20,1,1);
    auto set=discretize(mesh,Region::box(mesh.bounds_min,mesh.bounds_max,2),{.05,2.6,2});
    CHECK(set.particles.size()==400);
    CHECK(set.neighbor_offsets.size()==401);
    CHECK(set.neighbor_ids.size()>400*10);
    CHECK(set.cell_particle_offsets.back()==set.particles.size());
    double total_volume=0;
    for (const auto& p:set.particles) total_volume+=p.volume;
    close(total_volume,1.0,1e-10);
    std::size_t boundary=0,interior=0;
    const double h=set.support_radius/2;
    const double alpha=7.0/(4*std::numbers::pi*h*h);
    for (std::size_t i=0;i<set.particles.size();++i) {
        const auto& p=set.particles[i];
        close(p.volume,.0025,1e-12);
        CHECK(p.owner_cell<mesh.cells.size());
        CHECK(p.role==ParticleRole::common);
        if (p.physical_boundary) {
            ++boundary;
            CHECK(p.wall_patch!=no_index);
            CHECK(p.wall_distance<=set.support_radius+1e-12);
            Vec3 neighbor_area{};
            for (std::size_t k=set.neighbor_offsets[i];k<set.neighbor_offsets[i+1];++k) {
                const auto& q=set.particles[set.neighbor_ids[k]];
                const Vec3 dx=q.position-p.position;
                const double t=1-norm(dx)/set.support_radius;
                neighbor_area+=dx*(q.volume*5*alpha/(h*h)*t*t*t);
            }
            CHECK(norm(neighbor_area+p.closure_area)<1e-10);
        } else if (p.position.x>.3 && p.position.x<.7 && p.position.y>.3 && p.position.y<.7) {
            ++interior;
            CHECK(p.position_divergence>=1.5);
        }
    }
    CHECK(boundary>0 && interior>0);
    // No physically tagged closure on the outer SPH/FVM overlap halo.
    auto partial=discretize(mesh,Region::box({.3,.3,0},{.7,.7,0},2),{.05,2.6,2});
    for (const auto& p:partial.particles)
        if (p.role!=ParticleRole::common) CHECK(!p.physical_boundary);
}
double field(Vec3 p) { return 1.4+2.0*p.x-3.0*p.y+0.7*p.z; }
void interpolation_2d() {
    auto mesh=make_box2d(30,30,1,1);
    auto set=discretize(mesh,Region::box({.27,.3,0},{.73,.7,0},2),{1.0/30,2.6,2});
    auto plan=make_transfer_plan(mesh,set);
    std::vector<double> particle_values,cell_values;
    for (auto& p:set.particles) particle_values.push_back(field(p.position));
    for (auto& c:mesh.cells) cell_values.push_back(field(c.centroid));
    auto p2m=particle_to_mesh_opmis(mesh,set,plan,particle_values);
    auto m2p=mesh_to_halo_particles(mesh,set,plan,cell_values);
    std::size_t p2m_count=0,m2p_count=0,valid=0;
    for (std::size_t i=0;i<mesh.cells.size();++i)
        if (set.cell_roles[i]==MeshRole::interpolation) {
            ++p2m_count;
            if (plan.valid_moments[plan.p2m_particles[plan.p2m_offsets[i]]]) ++valid;
            close(p2m[i],cell_values[i],1e-8);
        } else CHECK(std::isnan(p2m[i]));
    for (std::size_t i=0;i<set.particles.size();++i)
        if (set.particles[i].role!=ParticleRole::common) {
            ++m2p_count;
            close(m2p[i],particle_values[i],1e-8);
            double weights=0;
            for (std::size_t j=plan.m2p_offsets[i];j<plan.m2p_offsets[i+1];++j) {
                CHECK(set.cell_roles[plan.m2p_cells[j]]==MeshRole::common);
                weights+=plan.m2p_weights[j];
            }
            close(weights,1.0,1e-9);
        } else CHECK(std::isnan(m2p[i]));
    CHECK(p2m_count>0 && m2p_count>0 && valid>0);
    for (const auto& p:set.particles) if (p.role==ParticleRole::transition) {
        const double mix=blend_transition(p,10,20,set.support_radius);
        CHECK(mix>=10 && mix<=20);
    }
}
void unstructured_interpolation() {
    auto mesh=read_gmsh22(fixture("jittered_triangles.msh"));
    CHECK(mesh.cells.size()==288);
    validate_geometry(mesh);
    auto set=discretize(mesh,Region::sphere({.5,.5,0},.22,2),{.055,2.6,2});
    auto plan=make_transfer_plan(mesh,set);
    std::size_t centroid_fallbacks=0;
    for (const auto& p:set.particles) centroid_fallbacks+=static_cast<std::size_t>(p.centroid_fallback);
    CHECK(centroid_fallbacks>0); // small cut triangles are sampled at their centroids
    std::vector<double> ps,ms;
    for (const auto& p:set.particles) ps.push_back(field(p.position));
    for (const auto& c:mesh.cells) ms.push_back(field(c.centroid));
    const auto p2m=particle_to_mesh_opmis(mesh,set,plan,ps);
    const auto m2p=mesh_to_halo_particles(mesh,set,plan,ms);
    std::size_t n=0;
    for (std::size_t i=0;i<mesh.cells.size();++i)
        if (set.cell_roles[i]==MeshRole::interpolation) {
            close(p2m[i],ms[i],1e-8);++n;
        }
    CHECK(n>0);
    n=0;
    for (std::size_t i=0;i<set.particles.size();++i)
        if (set.particles[i].role!=ParticleRole::common) {
            close(m2p[i],ps[i],1e-8);++n;
        }
    CHECK(n>0);
}
void interpolation_3d() {
    auto mesh=make_box3d(10,10,10,1,1,1);
    auto set=discretize(mesh,Region::sphere({.5,.5,.5},.23,3),{.1,2.6,2});
    auto plan=make_transfer_plan(mesh,set);
    std::vector<double> values,cell_values;
    for (const auto& p:set.particles) values.push_back(field(p.position));
    for (const auto& c:mesh.cells) cell_values.push_back(field(c.centroid));
    auto p2m=particle_to_mesh_opmis(mesh,set,plan,values);
    auto m2p=mesh_to_halo_particles(mesh,set,plan,cell_values);
    std::size_t n=0;
    for (std::size_t i=0;i<mesh.cells.size();++i)
        if (set.cell_roles[i]==MeshRole::interpolation) {
            close(p2m[i],cell_values[i],1e-7);++n;
        }
    CHECK(n>0);
    n=0;
    for (std::size_t i=0;i<set.particles.size();++i)
        if (set.particles[i].role!=ParticleRole::common) {
            close(m2p[i],values[i],1e-7);++n;
        }
    CHECK(n>0);
}
void adaptive_and_export() {
    auto mesh=make_box2d(10,10,1,1);
    auto region=Region::box({.2,.2,0},{.4,.4,0},2);
    const std::vector<Vec3> interface{{.75,.75,0},{.8,.8,0}};
    CHECK(adapt_box_to_interface(region,interface,mesh,.05,.1));
    close(region.low.x,.3);
    close(region.high.x,.5);
    for (int i=0;i<6;++i) adapt_box_to_interface(region,interface,mesh,.05,.1);
    CHECK(!adapt_box_to_interface(region,interface,mesh,.05,.1));
    auto set=discretize(mesh,region,{.1,2.6,2});
    auto plan=make_transfer_plan(mesh,set);
    auto output=std::filesystem::temp_directory_path()/"hybridmesh_unit_output";
    std::filesystem::remove_all(output);
    write_csv(output,mesh,set,plan,true);
    CHECK(std::filesystem::exists(output/"sph_neighbors.csv"));
    write_csv(output,mesh,set,plan,false);
    CHECK(std::filesystem::exists(output/"mesh.vtk"));
    CHECK(std::filesystem::exists(output/"particles.vtk"));
    CHECK(std::filesystem::exists(output/"faces.csv"));
    CHECK(!std::filesystem::exists(output/"sph_neighbors.csv"));
    std::filesystem::remove_all(output);
    throws([&] {discretize(mesh,region,{0,2.6,2});});
    throws([&] {scatter_face_flux(mesh,std::vector<double>{1.0});});
    throws([&] {Region::box({.5,.5,0},{.2,.6,0},2);});
    const auto bad=std::filesystem::temp_directory_path()/"hybridmesh_bad_format.msh";
    { std::ofstream file(bad); file<<"$MeshFormat\n4.1 0 8\n$EndMeshFormat\n"; }
    throws([&] {read_gmsh22(bad);});
    std::filesystem::remove(bad);
}
}
int main() {
    try {
        box_geometry();gmsh_and_foam();sph_boundary_and_volume();
        interpolation_2d();unstructured_interpolation();interpolation_3d();adaptive_and_export();
        std::cout<<"All "<<checks<<" checks passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr<<"TEST FAILED: "<<e.what()<<"\n";
        return 1;
    }
}
