#include "hybridmesh/hybridmesh.hpp"

#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace hybridmesh;
namespace {
void help() {
    std::cout<<R"(hybridmesh: portable unstructured FVM / SPH geometry and coupling backend

Usage: hybridmesh SOURCE --spacing DX CORE --output DIRECTORY [options]
SOURCE:
  --box2 NX NY LX LY            make a 2D quad mesh
  --box3 NX NY NZ LX LY LZ      make a 3D hex mesh
  --gmsh PATH                   load ASCII Gmsh MSH 2.2 (first-order)
  --openfoam CASE_OR_POLYMESH   load ASCII OpenFOAM polyMesh
CORE:
  --core-box MIN_X MIN_Y [MIN_Z] MAX_X MAX_Y [MAX_Z]
  --core-ball X Y [Z] R         sphere in 3D / disk in 2D
  --core-all                    SPH covers the entire FVM domain
OPTIONS:
  --support RATIO               kernel cutoff / DX (default 2.6)
  --band CELL_SPACINGS          inner interpolation-mesh band (default 2)
  --adapt-points CSV            adjust a box core to interface points once
  --margin LENGTH              margin around observed interface (requires --adapt-points)
  --step LENGTH                per-call adaptation step/dead band (requires --adapt-points)
  --no-links                   omit potentially large link CSVs (stencils still built)
  --help

Output: mesh_points.csv, patches.csv, cells.csv, faces.csv, particles.csv,
        mesh.vtk (for standard cells), particles.vtk, summary.txt, and optional links.
No CFD time stepping or differentiable solver is provided.
)";
}
std::size_t unsigned_number(const std::string& text) {
    std::size_t offset=0;
    auto value=std::stoull(text,&offset);
    if (offset!=text.size() || !value || value>=no_index)
        throw std::invalid_argument("invalid positive integer: "+text);
    return static_cast<std::size_t>(value);
}
double real_number(const std::string& text) {
    std::size_t offset=0;
    const double x=std::stod(text,&offset);
    if (offset!=text.size() || !std::isfinite(x))
        throw std::invalid_argument("invalid real: "+text);
    return x;
}
std::vector<Vec3> read_interface_points(const std::string& path,int dim) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open interface point CSV: "+path);
    std::vector<Vec3> points;
    std::string line;
    while (std::getline(in,line)) {
        if (line.empty() || line[0]=='#') continue;
        for (char& c:line) if (c==',' || c==';') c=' ';
        std::istringstream values(line);
        Vec3 p;
        if (!(values>>p.x>>p.y)) {
            if (points.empty() && (line.starts_with('x') || line.starts_with('X'))) continue;
            throw std::runtime_error("invalid interface CSV row: "+line);
        }
        if (dim==3 && !(values>>p.z)) throw std::runtime_error("interface CSV requires x,y,z");
        points.push_back(p);
    }
    if (points.empty()) throw std::runtime_error("interface point CSV is empty");
    return points;
}
} // namespace

int main(int argc,char** argv) {
    try {
        std::vector<std::string> args(argv+1,argv+argc);
        if (args.empty()) {help();return 1;}
        enum class Input {none,box2,box3,gmsh,foam};
        Input input=Input::none;
        std::vector<std::string> input_args,core_args;
        std::string core_kind,output,interface_file;
        double spacing=0,support=2.6,band=2,margin=-1,step=-1;
        bool include_links=true;
        for (std::size_t i=0;i<args.size();) {
            const auto option=args[i++];
            if (option=="--help" || option=="-h") {help();return 0;}
            auto take=[&]() -> std::string {
                if (i==args.size()) throw std::invalid_argument("missing argument after "+option);
                return args[i++];
            };
            if (option=="--box2" || option=="--box3" || option=="--gmsh" || option=="--openfoam") {
                if (input!=Input::none) throw std::invalid_argument("specify exactly one mesh source");
                input=(option=="--box2" ? Input::box2 : option=="--box3" ? Input::box3 :
                       option=="--gmsh" ? Input::gmsh : Input::foam);
                const std::size_t n=input==Input::box2 ? 4 : input==Input::box3 ? 6 : 1;
                for (std::size_t j=0;j<n;++j) input_args.push_back(take());
            } else if (option=="--core-box" || option=="--core-ball" || option=="--core-all") {
                if (!core_kind.empty()) throw std::invalid_argument("specify exactly one SPH core");
                core_kind=option;
                if (option!="--core-all") while (i<args.size() && !args[i].starts_with("--"))
                    core_args.push_back(take());
            } else if (option=="--spacing") spacing=real_number(take());
            else if (option=="--support") support=real_number(take());
            else if (option=="--band") band=real_number(take());
            else if (option=="--output") output=take();
            else if (option=="--adapt-points") interface_file=take();
            else if (option=="--margin") margin=real_number(take());
            else if (option=="--step") step=real_number(take());
            else if (option=="--no-links") include_links=false;
            else throw std::invalid_argument("unknown option: "+option);
        }
        if (input==Input::none || core_kind.empty() || output.empty() || !(spacing>0))
            throw std::invalid_argument("mesh source, --spacing, core and --output are required; see --help");
        const auto t0=std::chrono::steady_clock::now();
        Mesh mesh;
        switch (input) {
            case Input::box2:
                mesh=make_box2d(static_cast<Index>(unsigned_number(input_args[0])),
                                static_cast<Index>(unsigned_number(input_args[1])),
                                real_number(input_args[2]),real_number(input_args[3]));break;
            case Input::box3:
                mesh=make_box3d(static_cast<Index>(unsigned_number(input_args[0])),
                                static_cast<Index>(unsigned_number(input_args[1])),
                                static_cast<Index>(unsigned_number(input_args[2])),
                                real_number(input_args[3]),real_number(input_args[4]),
                                real_number(input_args[5]));break;
            case Input::gmsh: mesh=read_gmsh22(input_args[0]);break;
            case Input::foam: mesh=read_openfoam(input_args[0]);break;
            default: throw std::invalid_argument("missing mesh source");
        }
        Region core;
        if (core_kind=="--core-all") core=Region::box(mesh.bounds_min,mesh.bounds_max,mesh.dimension);
        else if (core_kind=="--core-box") {
            if (core_args.size()!=static_cast<std::size_t>(2*mesh.dimension))
                throw std::invalid_argument("--core-box needs DIM minima followed by DIM maxima");
            Vec3 min{},max{};
            for (int axis=0;axis<mesh.dimension;++axis) {
                min[static_cast<std::size_t>(axis)]=real_number(core_args[static_cast<std::size_t>(axis)]);
                max[static_cast<std::size_t>(axis)]=real_number(core_args[static_cast<std::size_t>(axis+mesh.dimension)]);
            }
            core=Region::box(min,max,mesh.dimension);
        } else {
            if (core_args.size()!=static_cast<std::size_t>(mesh.dimension+1))
                throw std::invalid_argument("--core-ball needs DIM center coordinates and a radius");
            Vec3 center{};
            for (int axis=0;axis<mesh.dimension;++axis)
                center[static_cast<std::size_t>(axis)]=real_number(core_args[static_cast<std::size_t>(axis)]);
            core=Region::sphere(center,real_number(core_args.back()),mesh.dimension);
        }
        if (!interface_file.empty()) {
            if (core.kind!=Region::Kind::box || margin<0 || step<=0)
                throw std::invalid_argument("adaptation requires --core-box/--core-all, --margin and --step");
            auto points=read_interface_points(interface_file,mesh.dimension);
            const bool moved=adapt_box_to_interface(core,points,mesh,margin,step);
            std::cout<<"Adaptive box moved: "<<(moved ? "yes" : "no (within dead band)")<<'\n';
        }
        const auto t1=std::chrono::steady_clock::now();
        const auto set=discretize(mesh,core,{spacing,support,band});
        const auto t2=std::chrono::steady_clock::now();
        const auto plan=make_transfer_plan(mesh,set);
        const auto t3=std::chrono::steady_clock::now();
        write_csv(output,mesh,set,plan,include_links);
        const auto t4=std::chrono::steady_clock::now();
        auto seconds=[](auto a,auto b) {return std::chrono::duration<double>(b-a).count();};
        std::cout<<"Wrote "<<output<<" | dim="<<mesh.dimension<<" | cells="<<mesh.cells.size()
                 <<" faces="<<mesh.faces.size()<<" particles="<<set.particles.size()
                 <<" SPH links="<<set.neighbor_ids.size()<<"\n"
                 <<"Seconds: load "<<seconds(t0,t1)<<", sample/neighbors "<<seconds(t1,t2)
                 <<", transfer plan "<<seconds(t2,t3)<<", export "<<seconds(t3,t4)<<'\n'
                 <<"P->M fallbacks "<<plan.p2m_fallbacks<<", M->P fallbacks "
                 <<plan.m2p_fallbacks<<", singular SPH gradients "<<plan.singular_moments<<'\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr<<"hybridmesh: "<<e.what()<<"\nTry --help for usage.\n";
        return 2;
    }
}
