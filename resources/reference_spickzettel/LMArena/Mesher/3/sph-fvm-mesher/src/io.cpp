#include "internal.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace hybridmesh {
namespace {
using namespace detail;

std::string trim(std::string s) {
    const auto first=s.find_first_not_of(" \t\r\n");
    if (first==std::string::npos) return {};
    return s.substr(first,s.find_last_not_of(" \t\r\n")-first+1);
}
std::string next_line(std::istream& in, const std::string& context) {
    std::string line;
    if (!std::getline(in,line)) throw std::runtime_error("unexpected EOF in "+context);
    return trim(line);
}
std::size_t nonnegative(std::string_view s,const std::string& context) {
    std::size_t n=0;
    auto [end,ec]=std::from_chars(s.data(),s.data()+s.size(),n);
    if (ec!=std::errc{} || end!=s.data()+s.size())
        throw std::runtime_error("invalid nonnegative integer in "+context+": "+std::string(s));
    return n;
}
std::size_t count_line(std::istream& in,const std::string& context) {
    return nonnegative(next_line(in,context),context);
}
void require_end(std::istream& in, const std::string& section) {
    const auto end=next_line(in,section);
    if (end!="$End"+section.substr(1))
        throw std::runtime_error("expected $End"+section.substr(1)+", got '"+end+"'");
}
int gmsh_dimension(int type) {
    switch (type) {
        case 15: return 0; case 1: return 1; case 2: case 3: return 2;
        case 4: case 5: case 6: case 7: return 3;
        default: throw std::runtime_error("unsupported Gmsh element type "+std::to_string(type)+
                            " (only first-order line/triangle/quad/tet/hex/prism/pyramid)");
    }
}
std::size_t gmsh_vertices(int type) {
    switch (type) {
        case 15: return 1; case 1: return 2; case 2: return 3; case 3: return 4;
        case 4: return 4; case 5: return 8; case 6: return 6; case 7: return 5;
        default: throw std::runtime_error("unsupported Gmsh element type");
    }
}
std::uint8_t vtk_from_gmsh(int type) {
    switch (type) {
        case 2: return 5; case 3: return 9; case 4: return 10;
        case 5: return 12; case 6: return 13; case 7: return 14;
        default: throw std::runtime_error("Gmsh element is not a supported volume cell");
    }
}
struct GmshElement { int type=0, physical=0; std::vector<Index> vertices; };

// OpenFOAM's ASCII format is tokenized without external dependencies.
// C++-style comments and quoted names are supported; binary and #include are not.
std::vector<std::string> foam_tokens(const std::filesystem::path& file) {
    std::ifstream in(file,std::ios::binary);
    if (!in) throw std::runtime_error("cannot open OpenFOAM file: "+file.string());
    std::string text((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    std::vector<std::string> tokens;
    std::size_t i=0;
    while (i<text.size()) {
        if (std::isspace(static_cast<unsigned char>(text[i]))) { ++i;continue; }
        if (text[i]=='/' && i+1<text.size() && text[i+1]=='/') {
            i=text.find('\n',i+2);
            if (i==std::string::npos) break;
            continue;
        }
        if (text[i]=='/' && i+1<text.size() && text[i+1]=='*') {
            const auto end=text.find("*/",i+2);
            if (end==std::string::npos) throw std::runtime_error("unterminated OpenFOAM comment");
            i=end+2;continue;
        }
        if (text[i]=='#') throw std::runtime_error("OpenFOAM # directives are not supported; expand them first");
        if (text[i]=='"') {
            const std::size_t begin=++i;
            while (i<text.size() && (text[i]!='"' || (i>begin && text[i-1]=='\\'))) ++i;
            if (i==text.size()) throw std::runtime_error("unterminated OpenFOAM string");
            tokens.push_back(text.substr(begin,i-begin)); ++i;continue;
        }
        if (std::string_view("(){};[]").find(text[i])!=std::string_view::npos) {
            tokens.emplace_back(1,text[i++]);continue;
        }
        const std::size_t begin=i;
        while (i<text.size() && !std::isspace(static_cast<unsigned char>(text[i])) &&
               std::string_view("(){};[]\"").find(text[i])==std::string_view::npos &&
               !(text[i]=='/' && i+1<text.size() && (text[i+1]=='/' || text[i+1]=='*'))) ++i;
        if (i==begin) throw std::runtime_error("unexpected OpenFOAM token");
        tokens.push_back(text.substr(begin,i-begin));
    }
    for (std::size_t k=0;k+1<tokens.size();++k)
        if (tokens[k]=="format" && tokens[k+1]!="ascii")
            throw std::runtime_error("only ASCII OpenFOAM polyMesh is supported");
    return tokens;
}
struct FoamReader {
    std::vector<std::string> tokens;
    std::size_t pos=0;
    explicit FoamReader(const std::filesystem::path& path) : tokens(foam_tokens(path)) {
        for (std::size_t i=0;i+1<tokens.size();++i) {
            std::size_t value=0;
            const auto& t=tokens[i];
            auto [last,ec]=std::from_chars(t.data(),t.data()+t.size(),value);
            if (ec==std::errc{} && last==t.data()+t.size() && tokens[i+1]=="(") {
                pos=i; return;
            }
        }
        throw std::runtime_error("OpenFOAM file has no counted list");
    }
    std::string take() {
        if (pos>=tokens.size()) throw std::runtime_error("unexpected EOF in polyMesh list");
        return tokens[pos++];
    }
    void expect(const std::string& t) {
        const auto x=take();
        if (x!=t) throw std::runtime_error("expected '"+t+"' in polyMesh; got '"+x+"'");
    }
    std::size_t number() { return nonnegative(take(),"polyMesh list"); }
    double real() {
        const auto token=take();
        std::size_t n=0; double result=0;
        try { result=std::stod(token,&n); } catch (...) {
            throw std::runtime_error("invalid real in polyMesh: "+token);
        }
        if (n!=token.size() || !std::isfinite(result))
            throw std::runtime_error("non-finite/invalid real in polyMesh: "+token);
        return result;
    }
    std::size_t start() { auto n=number(); expect("("); return n; }
};
std::filesystem::path locate_poly_mesh(std::filesystem::path directory) {
    if (std::filesystem::exists(directory/"constant"/"polyMesh"/"points"))
        directory/=std::filesystem::path("constant")/"polyMesh";
    if (!std::filesystem::exists(directory/"points"))
        throw std::runtime_error("OpenFOAM points not found: pass a polyMesh or case directory");
    return directory;
}
std::string csv_string(const std::string& str) {
    std::string s="\"";
    for (char ch:str) { if (ch=='"') s+='"'; s+=ch; }
    s+='"';return s;
}
std::string ids_string(const std::vector<Index>& ids) {
    std::ostringstream out;
    for (std::size_t i=0;i<ids.size();++i) { if (i) out<<';'; out<<ids[i]; }
    return csv_string(out.str());
}
long long printable(Index id) { return id==no_index ? -1 : static_cast<long long>(id); }
std::ofstream output_file(const std::filesystem::path& dir,const char* file) {
    std::ofstream out(dir/file);
    if (!out) throw std::runtime_error("cannot write output: "+(dir/file).string());
    out<<std::setprecision(17);
    return out;
}

} // anonymous namespace

Mesh read_gmsh22(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) throw std::runtime_error("cannot open Gmsh file: "+file.string());
    bool format=false,have_nodes=false,have_elements=false;
    std::map<std::pair<int,int>,std::string> physical_names;
    std::unordered_map<std::size_t,Index> node_ids;
    std::vector<Vec3> nodes;
    std::vector<GmshElement> elements;
    std::string line;
    while (std::getline(in,line)) {
        line=trim(line);
        if (line.empty()) continue;
        if (line=="$MeshFormat") {
            const auto info=next_line(in,line);
            std::istringstream values(info);
            double version=0; int binary=-1,word_size=0;
            if (!(values>>version>>binary>>word_size) || std::abs(version-2.2)>0.05 ||
                binary!=0 || word_size!=8)
                throw std::runtime_error("only ASCII Gmsh MSH 2.2 is supported (not v4/binary)");
            require_end(in,line); format=true;
        } else if (line=="$PhysicalNames") {
            const auto count=count_line(in,line);
            for (std::size_t i=0;i<count;++i) {
                std::istringstream values(next_line(in,line));
                int dimension=0, tag=0;std::string label;
                if (!(values>>dimension>>tag>>std::quoted(label)))
                    throw std::runtime_error("bad Gmsh PhysicalNames entry");
                physical_names[{dimension,tag}]=std::move(label);
            }
            require_end(in,line);
        } else if (line=="$Nodes") {
            if (have_nodes) throw std::runtime_error("repeated Gmsh $Nodes section");
            const auto count=count_line(in,line);
            checked_index(count);
            nodes.reserve(count);node_ids.reserve(count);
            for (std::size_t i=0;i<count;++i) {
                std::istringstream values(next_line(in,line));
                std::size_t id=0;Vec3 p;
                if (!(values>>id>>p.x>>p.y>>p.z) || !finite(p))
                    throw std::runtime_error("bad Gmsh node entry");
                if (!node_ids.emplace(id,checked_index(nodes.size())).second)
                    throw std::runtime_error("duplicate Gmsh node ID");
                nodes.push_back(p);
            }
            require_end(in,line);have_nodes=true;
        } else if (line=="$Elements") {
            if (have_elements) throw std::runtime_error("repeated Gmsh $Elements section");
            const auto count=count_line(in,line);
            elements.reserve(count);
            for (std::size_t i=0;i<count;++i) {
                std::istringstream values(next_line(in,line));
                std::size_t element_id=0; int type=0,num_tags=0;
                if (!(values>>element_id>>type>>num_tags) || num_tags<0 || num_tags>1000)
                    throw std::runtime_error("bad Gmsh element header");
                (void)element_id;
                gmsh_dimension(type); // fail early for unsupported higher-order types
                GmshElement element; element.type=type;
                for (int k=0;k<num_tags;++k) {
                    int tag=0;if (!(values>>tag)) throw std::runtime_error("bad Gmsh element tags");
                    if (k==0) element.physical=tag;
                }
                for (std::size_t j=0;j<gmsh_vertices(type);++j) {
                    std::size_t id=0;
                    if (!(values>>id)) throw std::runtime_error("Gmsh element has too few vertices");
                    auto it=node_ids.find(id);
                    if (it==node_ids.end()) throw std::runtime_error("unknown Gmsh vertex ID");
                    element.vertices.push_back(it->second);
                }
                values>>std::ws;
                if (!values.eof()) throw std::runtime_error("Gmsh element has trailing data");
                elements.push_back(std::move(element));
            }
            require_end(in,line);have_elements=true;
        } else if (line.starts_with("$End")) {
            throw std::runtime_error("unexpected Gmsh section terminator: "+line);
        } else if (line.starts_with('$')) {
            const std::string end="$End"+line.substr(1);
            bool reached=false;
            while (std::getline(in,line)) if (trim(line)==end) { reached=true;break; }
            if (!reached) throw std::runtime_error("unterminated Gmsh section: "+end);
        } else throw std::runtime_error("unexpected line outside Gmsh section: "+line);
    }
    if (!format || !have_nodes || !have_elements)
        throw std::runtime_error("Gmsh file requires $MeshFormat, $Nodes and $Elements");
    int dim=0;
    for (const auto& e:elements) dim=std::max(dim,gmsh_dimension(e.type));
    if (dim!=2 && dim!=3) throw std::runtime_error("Gmsh mesh has no 2D/3D cells");
    std::vector<RawCell> cells;
    BoundaryTags boundary_tags;
    std::vector<Patch> patches{{"default"}};
    std::map<int,Index> patch_by_physical;
    for (const auto& e:elements) {
        const int entity_dim=gmsh_dimension(e.type);
        if (entity_dim==dim) cells.push_back({e.vertices,vtk_from_gmsh(e.type)});
        else if (entity_dim==dim-1 && e.physical!=0) {
            Index patch=0;
            if (auto it=patch_by_physical.find(e.physical);it!=patch_by_physical.end()) patch=it->second;
            else {
                patch=checked_index(patches.size());
                auto found=physical_names.find({dim-1,e.physical});
                patches.push_back({found==physical_names.end() ?
                    "physical_"+std::to_string(e.physical) : found->second});
                patch_by_physical[e.physical]=patch;
            }
            if (e.vertices.size()!=static_cast<std::size_t>(dim) &&
                !(dim==3 && e.vertices.size()==4))
                throw std::runtime_error("invalid boundary facet in Gmsh mesh");
            if (!boundary_tags.emplace(face_key(e.vertices),patch).second)
                throw std::runtime_error("duplicate physical boundary facet");
        }
    }
    return assemble_elements(dim,std::move(nodes),cells,std::move(boundary_tags),std::move(patches));
}

Mesh read_openfoam(const std::filesystem::path& path) {
    const auto dir=locate_poly_mesh(path);
    FoamReader points_file(dir/"points");
    const auto npoints=points_file.start();
    checked_index(npoints);
    std::vector<Vec3> points;
    points.reserve(npoints);
    for (std::size_t i=0;i<npoints;++i) {
        points_file.expect("(");
        const Vec3 p{points_file.real(),points_file.real(),points_file.real()};
        points_file.expect(")");points.push_back(p);
    }
    points_file.expect(")");
    FoamReader faces_file(dir/"faces");
    const auto nfaces=faces_file.start();
    checked_index(nfaces);
    std::vector<RawFace> raw;
    raw.reserve(nfaces);
    for (std::size_t i=0;i<nfaces;++i) {
        const auto nv=faces_file.number();
        if (nv<3) throw std::runtime_error("polyMesh face must have at least three vertices");
        faces_file.expect("(");
        std::vector<Index> verts;verts.reserve(nv);
        for (std::size_t j=0;j<nv;++j) {
            const auto v=faces_file.number();
            if (v>=points.size()) throw std::runtime_error("polyMesh face has invalid vertex");
            verts.push_back(checked_index(v));
        }
        faces_file.expect(")");
        raw.push_back({std::move(verts),no_index,no_index,no_index});
    }
    faces_file.expect(")");
    FoamReader owner_file(dir/"owner");
    if (owner_file.start()!=nfaces) throw std::runtime_error("owner count differs from face count");
    for (auto& r:raw) r.owner=checked_index(owner_file.number());
    owner_file.expect(")");
    FoamReader neighbor_file(dir/"neighbour");
    const auto internal=neighbor_file.start();
    if (internal>nfaces) throw std::runtime_error("too many internal polyMesh faces");
    for (std::size_t i=0;i<internal;++i) raw[i].neighbor=checked_index(neighbor_file.number());
    neighbor_file.expect(")");
    FoamReader boundary_file(dir/"boundary");
    const auto npatches=boundary_file.start();
    std::vector<Patch> patches{{"default"}};
    std::vector<std::uint8_t> assigned(nfaces,0);
    for (std::size_t p=0;p<npatches;++p) {
        patches.push_back({boundary_file.take()});
        boundary_file.expect("{");
        std::size_t first=std::numeric_limits<std::size_t>::max();
        std::size_t count=std::numeric_limits<std::size_t>::max();
        int depth=1;
        while (depth>0) {
            const auto key=boundary_file.take();
            if (key=="{") { ++depth;continue; }
            if (key=="}") { --depth;continue; }
            if (depth!=1) continue;
            if (key=="nFaces") { count=boundary_file.number();boundary_file.expect(";"); }
            else if (key=="startFace") { first=boundary_file.number();boundary_file.expect(";"); }
        }
        if (first==std::numeric_limits<std::size_t>::max() ||
            count==std::numeric_limits<std::size_t>::max() ||
            first<internal || first>nfaces || count>nfaces-first)
            throw std::runtime_error("bad polyMesh boundary range");
        for (std::size_t fi=first;fi<first+count;++fi) {
            if (assigned[fi]++) throw std::runtime_error("overlapping polyMesh patches");
            raw[fi].patch=checked_index(patches.size()-1);
        }
    }
    boundary_file.expect(")");
    for (std::size_t fi=internal;fi<nfaces;++fi)
        if (!assigned[fi]) throw std::runtime_error("unassigned polyMesh boundary face");
    return assemble_faces(std::move(points),std::move(raw),std::move(patches));
}

void write_csv(const std::filesystem::path& directory,const Mesh& mesh,
               const ParticleSet& particles,const TransferPlan& plan,bool include_links) {
    std::filesystem::create_directories(directory);
    if (!include_links)
        for (const char* old : {"sph_neighbors.csv","fvm_graph.csv",
                                "mesh_to_particle.csv","particle_to_mesh.csv"})
            std::filesystem::remove(directory/old);
    {
        auto out=output_file(directory,"mesh_points.csv");
        out<<"id,x,y,z\n";
        for (std::size_t i=0;i<mesh.points.size();++i) {
            const Vec3 p=mesh.points[i]; out<<i<<','<<p.x<<','<<p.y<<','<<p.z<<'\n';
        }
    }
    {
        auto out=output_file(directory,"patches.csv");
        out<<"id,name\n";
        for (std::size_t i=0;i<mesh.patches.size();++i)
            out<<i<<','<<csv_string(mesh.patches[i].name)<<'\n';
    }
    {
        auto out=output_file(directory,"cells.csv");
        out<<"id,center_x,center_y,center_z,volume,mesh_role,vtk_type,vertices,faces\n";
        for (std::size_t i=0;i<mesh.cells.size();++i) {
            const auto& c=mesh.cells[i];
            out<<i<<','<<c.centroid.x<<','<<c.centroid.y<<','<<c.centroid.z<<','
               <<c.volume<<','<<name(particles.cell_roles.at(i))<<','
               <<static_cast<int>(c.vtk_type)<<','<<ids_string(c.vertices)<<','
               <<ids_string(c.faces)<<'\n';
        }
    }
    {
        auto out=output_file(directory,"faces.csv");
        out<<"id,owner,neighbor,patch,center_x,center_y,center_z,area_x,area_y,area_z,vertices\n";
        for (std::size_t i=0;i<mesh.faces.size();++i) {
            const auto& f=mesh.faces[i];
            out<<i<<','<<f.owner<<','<<printable(f.neighbor)<<','<<printable(f.patch)<<','
               <<f.centroid.x<<','<<f.centroid.y<<','<<f.centroid.z<<','
               <<f.area_vector.x<<','<<f.area_vector.y<<','<<f.area_vector.z<<','
               <<ids_string(f.vertices)<<'\n';
        }
    }
    {
        auto out=output_file(directory,"particles.csv");
        out<<"id,x,y,z,volume,owner_cell,role,centroid_fallback,signed_core_distance,"
              "position_divergence,physical_boundary,closure_x,closure_y,closure_z,"
              "normal_x,normal_y,normal_z,wall_distance,wall_patch\n";
        for (std::size_t i=0;i<particles.particles.size();++i) {
            const auto& p=particles.particles[i];
            out<<i<<','<<p.position.x<<','<<p.position.y<<','<<p.position.z<<','
               <<p.volume<<','<<p.owner_cell<<','<<name(p.role)<<','<<p.centroid_fallback<<','
               <<p.signed_core_distance<<','<<p.position_divergence<<','<<p.physical_boundary<<','
               <<p.closure_area.x<<','<<p.closure_area.y<<','<<p.closure_area.z<<','
               <<p.closure_normal.x<<','<<p.closure_normal.y<<','<<p.closure_normal.z<<','
               <<p.wall_distance<<','<<printable(p.wall_patch)<<'\n';
        }
    }
    if (include_links) {
        auto neighbor_out=output_file(directory,"sph_neighbors.csv");
        neighbor_out<<"particle_id,neighbor_id\n";
        for (std::size_t i=0;i<particles.particles.size();++i)
            for (std::size_t j=particles.neighbor_offsets[i];j<particles.neighbor_offsets[i+1];++j)
                neighbor_out<<i<<','<<particles.neighbor_ids[j]<<'\n';
        auto graph_out=output_file(directory,"fvm_graph.csv");
        graph_out<<"owner,neighbor,face_id\n";
        for (std::size_t i=0;i<mesh.faces.size();++i)
            if (mesh.faces[i].neighbor!=no_index)
                graph_out<<mesh.faces[i].owner<<','<<mesh.faces[i].neighbor<<','<<i<<'\n';
        auto m2p_out=output_file(directory,"mesh_to_particle.csv");
        m2p_out<<"particle_id,cell_id,weight\n";
        for (std::size_t i=0;i<particles.particles.size();++i)
            for (std::size_t k=plan.m2p_offsets[i];k<plan.m2p_offsets[i+1];++k)
                m2p_out<<i<<','<<plan.m2p_cells[k]<<','<<plan.m2p_weights[k]<<'\n';
        auto p2m_out=output_file(directory,"particle_to_mesh.csv");
        p2m_out<<"cell_id,particle_id\n";
        for (std::size_t i=0;i<mesh.cells.size();++i)
            for (std::size_t k=plan.p2m_offsets[i];k<plan.p2m_offsets[i+1];++k)
                p2m_out<<i<<','<<plan.p2m_particles[k]<<'\n';
    }
    {
        auto out=output_file(directory,"summary.txt");
        std::array<std::size_t,3> p_counts{},m_counts{};
        std::size_t physical=0,centroid_fallbacks=0;
        for (const auto& p:particles.particles) {
            ++p_counts[static_cast<std::size_t>(p.role)];
            physical+=static_cast<std::size_t>(p.physical_boundary);
            centroid_fallbacks+=static_cast<std::size_t>(p.centroid_fallback);
        }
        for (auto r:particles.cell_roles) ++m_counts[static_cast<std::size_t>(r)];
        out<<"dimension "<<mesh.dimension<<"\nnodes "<<mesh.points.size()<<"\ncells "
           <<mesh.cells.size()<<"\nfaces "<<mesh.faces.size()<<"\nparticles "
           <<particles.particles.size()<<"\ncommon_mesh "<<m_counts[0]
           <<"\ninterpolation_mesh "<<m_counts[1]<<"\nsleeping_mesh "<<m_counts[2]
           <<"\ncommon_particles "<<p_counts[0]<<"\ntransition_particles "<<p_counts[1]
           <<"\ninterpolation_particles "<<p_counts[2]<<"\nphysical_boundary_particles "
           <<physical<<"\ncentroid_sample_fallbacks "<<centroid_fallbacks
           <<"\nsph_directed_links "<<particles.neighbor_ids.size()
           <<"\nparticle_to_mesh_fallbacks "<<plan.p2m_fallbacks
           <<"\nmesh_to_particle_fallbacks "<<plan.m2p_fallbacks
           <<"\nsingular_particle_moments "<<plan.singular_moments<<'\n';
    }
    // Legacy ASCII VTK exports make conventional cells/particles easy to inspect
    // in ParaView without making visualization libraries a runtime dependency.
    {
        auto out=output_file(directory,"particles.vtk");
        const auto count=particles.particles.size();
        out<<"# vtk DataFile Version 3.0\nHybridMesh SPH particles\nASCII\nDATASET POLYDATA\n"
           <<"POINTS "<<count<<" double\n";
        for (const auto& p:particles.particles)
            out<<p.position.x<<' '<<p.position.y<<' '<<p.position.z<<'\n';
        out<<"VERTICES "<<count<<' '<<count*2<<"\n";
        for (std::size_t i=0;i<count;++i) out<<"1 "<<i<<'\n';
        out<<"POINT_DATA "<<count<<"\nSCALARS role int 1\nLOOKUP_TABLE default\n";
        for (const auto& p:particles.particles) out<<static_cast<int>(p.role)<<'\n';
        out<<"SCALARS physical_boundary int 1\nLOOKUP_TABLE default\n";
        for (const auto& p:particles.particles) out<<static_cast<int>(p.physical_boundary)<<'\n';
        out<<"VECTORS closure_area double\n";
        for (const auto& p:particles.particles)
            out<<p.closure_area.x<<' '<<p.closure_area.y<<' '<<p.closure_area.z<<'\n';
    }
    bool standard=true;
    for (const auto& c:mesh.cells) standard &= c.vtk_type!=0;
    if (standard) {
        auto out=output_file(directory,"mesh.vtk");
        out<<"# vtk DataFile Version 3.0\nHybridMesh FVM cells\nASCII\nDATASET UNSTRUCTURED_GRID\n"
           <<"POINTS "<<mesh.points.size()<<" double\n";
        for (Vec3 p:mesh.points) out<<p.x<<' '<<p.y<<' '<<p.z<<'\n';
        std::size_t total=mesh.cells.size();
        for (const auto& c:mesh.cells) total+=c.vertices.size();
        out<<"CELLS "<<mesh.cells.size()<<' '<<total<<"\n";
        for (const auto& c:mesh.cells) {
            out<<c.vertices.size(); for (Index id:c.vertices) out<<' '<<id;out<<'\n';
        }
        out<<"CELL_TYPES "<<mesh.cells.size()<<"\n";
        for (const auto& c:mesh.cells) out<<static_cast<int>(c.vtk_type)<<'\n';
        out<<"CELL_DATA "<<mesh.cells.size()<<"\nSCALARS role int 1\nLOOKUP_TABLE default\n";
        for (auto r:particles.cell_roles) out<<static_cast<int>(r)<<'\n';
    } else std::filesystem::remove(directory/"mesh.vtk");
}

} // namespace hybridmesh
