#include "internal.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace hybridmesh {

double& Vec3::operator[](std::size_t axis) {
    switch (axis) { case 0: return x; case 1: return y; case 2: return z;
                    default: throw std::out_of_range("Vec3 axis"); }
}
const double& Vec3::operator[](std::size_t axis) const {
    switch (axis) { case 0: return x; case 1: return y; case 2: return z;
                    default: throw std::out_of_range("Vec3 axis"); }
}
Vec3 operator+(Vec3 a,Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
Vec3 operator-(Vec3 a,Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
Vec3 operator*(Vec3 a,double s) { return {a.x*s,a.y*s,a.z*s}; }
Vec3 operator/(Vec3 a,double s) { return a*(1.0/s); }
Vec3& operator+=(Vec3& a,Vec3 b) { a=a+b; return a; }
double dot(Vec3 a,Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
double norm(Vec3 a) { return std::sqrt(dot(a,a)); }
const char* name(MeshRole r) {
    switch (r) { case MeshRole::common: return "common";
                 case MeshRole::interpolation: return "interpolation";
                 case MeshRole::sleeping: return "sleeping"; }
    return "invalid";
}
const char* name(ParticleRole r) {
    switch (r) { case ParticleRole::common: return "common";
                 case ParticleRole::transition: return "transition";
                 case ParticleRole::interpolation: return "interpolation"; }
    return "invalid";
}

namespace {
using namespace detail;

std::vector<std::vector<Index>> facets(const RawCell& cell,int dim) {
    const auto& v=cell.vertices;
    auto f=[&](std::initializer_list<std::size_t> indices) {
        std::vector<Index> ids;
        ids.reserve(indices.size());
        for (std::size_t i : indices) ids.push_back(v.at(i));
        return ids;
    };
    if (dim==2 && cell.vtk_type==5 && v.size()==3)
        return {f({0,1}),f({1,2}),f({2,0})};
    if (dim==2 && cell.vtk_type==9 && v.size()==4)
        return {f({0,1}),f({1,2}),f({2,3}),f({3,0})};
    if (dim==3 && cell.vtk_type==10 && v.size()==4)
        return {f({0,2,1}),f({0,1,3}),f({1,2,3}),f({2,0,3})};
    if (dim==3 && cell.vtk_type==12 && v.size()==8)
        return {f({0,3,2,1}),f({4,5,6,7}),f({0,1,5,4}),f({1,2,6,5}),
                f({2,3,7,6}),f({3,0,4,7})};
    if (dim==3 && cell.vtk_type==13 && v.size()==6)
        return {f({0,2,1}),f({3,4,5}),f({0,1,4,3}),f({1,2,5,4}),f({2,0,3,5})};
    if (dim==3 && cell.vtk_type==14 && v.size()==5)
        return {f({0,3,2,1}),f({0,1,4}),f({1,2,4}),f({2,3,4}),f({3,0,4})};
    throw std::runtime_error("unsupported cell type or wrong number of vertices");
}

void finish_geometry(Mesh& mesh) {
    if (mesh.dimension!=2 && mesh.dimension!=3) throw std::runtime_error("mesh must be 2D or 3D");
    if (mesh.cells.empty() || mesh.points.empty()) throw std::runtime_error("mesh contains no cells or points");
    const int dim=mesh.dimension;
    const auto inf=std::numeric_limits<double>::infinity();
    mesh.bounds_min={inf,inf,inf}; mesh.bounds_max={-inf,-inf,-inf};
    for (Vec3 point : mesh.points) {
        if (!finite(point)) throw std::runtime_error("non-finite mesh coordinate");
        if (dim==2 && std::abs(point.z-mesh.points.front().z)>1e-9)
            throw std::runtime_error("2D meshes must lie in an XY plane");
        mesh.bounds_min=minimum(mesh.bounds_min,point);
        mesh.bounds_max=maximum(mesh.bounds_max,point);
    }
    double span=0;
    for (int a=0; a<dim; ++a) {
        const double extent=mesh.bounds_max[static_cast<std::size_t>(a)]-
                            mesh.bounds_min[static_cast<std::size_t>(a)];
        if (!(extent>0)) throw std::runtime_error("mesh has zero extent");
        span=std::max(span,extent);
    }
    for (auto& cell : mesh.cells) {
        if (cell.vertices.size() < static_cast<std::size_t>(dim+1) || cell.faces.empty())
            throw std::runtime_error("cell has too few vertices or faces");
        auto sorted=cell.vertices;
        std::sort(sorted.begin(),sorted.end());
        if (std::adjacent_find(sorted.begin(),sorted.end()) != sorted.end())
            throw std::runtime_error("cell has repeated vertices");
        Vec3 mean{}; cell.bounds_min={inf,inf,inf}; cell.bounds_max={-inf,-inf,-inf};
        for (Index vi : cell.vertices) {
            if (vi>=mesh.points.size()) throw std::runtime_error("cell has invalid vertex ID");
            const Vec3 p=mesh.points[vi];
            mean+=p;
            cell.bounds_min=minimum(cell.bounds_min,p);
            cell.bounds_max=maximum(cell.bounds_max,p);
        }
        cell.centroid=mean/static_cast<double>(cell.vertices.size()); // temporary interior reference
    }
    const double tiny=1e-14*std::pow(span,dim-1);
    for (auto& face : mesh.faces) {
        if (face.owner>=mesh.cells.size() ||
            (face.neighbor!=no_index && (face.neighbor>=mesh.cells.size() || face.neighbor==face.owner)))
            throw std::runtime_error("invalid face owner/neighbor");
        if (face.vertices.size() < static_cast<std::size_t>(dim))
            throw std::runtime_error("face has too few vertices");
        face_key(face.vertices); // reject duplicate vertex IDs
        auto calculate=[&]() {
            if (dim==2) {
                if (face.vertices.size()!=2) throw std::runtime_error("2D face must be an edge");
                const Vec3 a=mesh.points.at(face.vertices[0]), b=mesh.points.at(face.vertices[1]);
                face.centroid=(a+b)*0.5;
                face.area_vector={b.y-a.y,a.x-b.x,0};
            } else {
                const Vec3 origin=mesh.points.at(face.vertices[0]);
                Vec3 area{}, weighted_center{};
                double triangle_area_sum=0;
                for (std::size_t i=1; i+1<face.vertices.size(); ++i) {
                    const Vec3 a=mesh.points.at(face.vertices[i]);
                    const Vec3 b=mesh.points.at(face.vertices[i+1]);
                    const Vec3 triangle_area=cross(a-origin,b-origin)*0.5;
                    const double magnitude=norm(triangle_area);
                    area+=triangle_area;
                    weighted_center+=(origin+a+b)*(magnitude/3.0);
                    triangle_area_sum+=magnitude;
                }
                if (!(triangle_area_sum>tiny)) throw std::runtime_error("degenerate 3D face");
                face.area_vector=area;
                face.centroid=weighted_center/triangle_area_sum;
            }
        };
        calculate();
        if (!(norm(face.area_vector)>tiny)) throw std::runtime_error("zero-area face");
        if (dot(face.area_vector,face.centroid-mesh.cells[face.owner].centroid)<0) {
            std::reverse(face.vertices.begin(),face.vertices.end());
            calculate();
        }
        if (!(dot(face.area_vector,face.centroid-mesh.cells[face.owner].centroid)>tiny*span*1e-8))
            throw std::runtime_error("face does not point out of owner cell");
        if (face.neighbor!=no_index) {
            if (face.patch!=no_index) throw std::runtime_error("internal face has a boundary patch");
            if (!(dot(face.area_vector,
                      mesh.cells[face.neighbor].centroid-mesh.cells[face.owner].centroid)>0))
                throw std::runtime_error("neighbor is on wrong side of a shared face");
        } else if (face.patch>=mesh.patches.size()) throw std::runtime_error("missing face boundary patch");
        if (dim==3) {
            const double tol=1e-8*norm(face.area_vector)*span;
            for (Index vi : face.vertices)
                if (std::abs(dot(face.area_vector,mesh.points[vi]-face.centroid))>tol)
                    throw std::runtime_error("non-planar face; planar convex faces are required");
        }
    }
    for (std::size_t ci=0; ci<mesh.cells.size(); ++ci) {
        auto& c=mesh.cells[ci];
        const Vec3 reference=c.centroid;
        if (dim==2) {
            double twice_area=0, xsum=0, ysum=0;
            for (std::size_t i=0; i<c.vertices.size(); ++i) {
                const Vec3 a=mesh.points[c.vertices[i]];
                const Vec3 b=mesh.points[c.vertices[(i+1)%c.vertices.size()]];
                const double z=a.x*b.y-b.x*a.y;
                twice_area+=z; xsum+=(a.x+b.x)*z; ysum+=(a.y+b.y)*z;
            }
            if (std::abs(twice_area)<=2e-14*span*span)
                throw std::runtime_error("degenerate or unordered 2D polygon");
            c.volume=std::abs(twice_area)*0.5;
            c.centroid={xsum/(3*twice_area),ysum/(3*twice_area),reference.z};
        } else {
            double volume=0; Vec3 weighted_center{};
            for (Index fi : c.faces) {
                const Face& f=mesh.faces[fi];
                const Vec3 origin=mesh.points[f.vertices[0]];
                for (std::size_t i=1; i+1<f.vertices.size(); ++i) {
                    Vec3 a=mesh.points[f.vertices[i]], b=mesh.points[f.vertices[i+1]];
                    if (f.neighbor==ci) std::swap(a,b);
                    const double tetra=dot(origin-reference,cross(a-reference,b-reference))/6.0;
                    if (tetra < -1e-12*span*span*span)
                        throw std::runtime_error("inverted cell or non-convex polyhedron");
                    volume+=tetra;
                    weighted_center+=(reference+origin+a+b)*(tetra*0.25);
                }
            }
            if (!(volume>1e-14*span*span*span)) throw std::runtime_error("zero-volume cell");
            c.volume=volume;
            c.centroid=weighted_center/volume;
        }
        Vec3 closure{}; double total_area=0;
        for (Index fi : c.faces) {
            const Face& f=mesh.faces[fi];
            const double sign=(f.owner==ci) ? 1.0 : -1.0;
            closure+=f.area_vector*sign;
            total_area+=norm(f.area_vector);
            // Convexity also guarantees correctness of the particle half-space test.
            for (Index vi : c.vertices) {
                if (dot(f.area_vector*sign,mesh.points[vi]-f.centroid) >
                    1e-8*norm(f.area_vector)*span)
                    throw std::runtime_error("non-convex cell or invalid cell face");
            }
        }
        if (norm(closure)>1e-8*total_area)
            throw std::runtime_error("cell is not topologically/geometrically closed");
    }
}
} // anonymous namespace

namespace detail {
Mesh assemble_elements(int dimension,std::vector<Vec3> nodes,
                       const std::vector<RawCell>& raw,BoundaryTags tags,
                       std::vector<Patch> patches) {
    if (dimension!=2 && dimension!=3) throw std::runtime_error("2D or 3D required");
    if (patches.empty()) patches.push_back({"default"});
    Mesh mesh;
    mesh.dimension=dimension;
    mesh.points=std::move(nodes);
    mesh.patches=std::move(patches);
    mesh.cells.reserve(raw.size());
    std::unordered_map<FaceKey,Index,FaceKeyHash> lookup;
    lookup.reserve(raw.size()*static_cast<std::size_t>(dimension==2 ? 3 : 5));
    for (const auto& rc : raw) {
        const Index ci=checked_index(mesh.cells.size());
        Cell c;
        c.vertices=rc.vertices;
        c.vtk_type=rc.vtk_type;
        for (auto face_nodes : facets(rc,dimension)) {
            const auto key=face_key(face_nodes);
            auto it=lookup.find(key);
            if (it==lookup.end()) {
                const Index fi=checked_index(mesh.faces.size());
                lookup.emplace(key,fi);
                Face face;
                face.vertices=std::move(face_nodes);
                face.owner=ci;
                mesh.faces.push_back(std::move(face));
                c.faces.push_back(fi);
            } else {
                Face& f=mesh.faces[it->second];
                if (f.neighbor!=no_index || f.owner==ci)
                    throw std::runtime_error("non-manifold/duplicated face (more than two cells)");
                f.neighbor=ci;
                c.faces.push_back(it->second);
            }
        }
        mesh.cells.push_back(std::move(c));
    }
    for (auto& face : mesh.faces) {
        auto it=tags.find(face_key(face.vertices));
        if (it!=tags.end()) {
            if (face.neighbor!=no_index) throw std::runtime_error("physical boundary tag on internal face");
            if (it->second>=mesh.patches.size()) throw std::runtime_error("invalid patch index");
            face.patch=it->second;
            tags.erase(it);
        } else if (face.neighbor==no_index) face.patch=0;
    }
    if (!tags.empty()) throw std::runtime_error("tagged boundary facet does not match a boundary face");
    finish_geometry(mesh);
    return mesh;
}
Mesh assemble_faces(std::vector<Vec3> nodes,std::vector<RawFace> raw,
                    std::vector<Patch> patches) {
    if (patches.empty()) patches.push_back({"default"});
    Mesh mesh;
    mesh.dimension=3;
    mesh.points=std::move(nodes);
    mesh.patches=std::move(patches);
    Index max_cell=0;
    for (const auto& r:raw) {
        if (r.owner==no_index) throw std::runtime_error("missing owner in polyMesh");
        max_cell=std::max(max_cell,r.owner);
        if (r.neighbor!=no_index) max_cell=std::max(max_cell,r.neighbor);
    }
    if (raw.empty()) throw std::runtime_error("polyMesh has no faces");
    mesh.cells.resize(static_cast<std::size_t>(max_cell)+1);
    mesh.faces.reserve(raw.size());
    std::unordered_map<FaceKey,Index,FaceKeyHash> unique;
    for (auto& r : raw) {
        const auto key=face_key(r.vertices);
        if (!unique.emplace(key,checked_index(mesh.faces.size())).second)
            throw std::runtime_error("duplicate polyMesh face");
        const Index fi=checked_index(mesh.faces.size());
        Face f;
        f.vertices=std::move(r.vertices);
        f.owner=r.owner;
        f.neighbor=r.neighbor;
        f.patch=r.patch;
        if (f.owner>=mesh.cells.size() || (f.neighbor!=no_index && f.neighbor>=mesh.cells.size()))
            throw std::runtime_error("polyMesh face has invalid owner/neighbor");
        mesh.cells[f.owner].faces.push_back(fi);
        mesh.cells[f.owner].vertices.insert(mesh.cells[f.owner].vertices.end(),f.vertices.begin(),f.vertices.end());
        if (f.neighbor!=no_index) {
            mesh.cells[f.neighbor].faces.push_back(fi);
            mesh.cells[f.neighbor].vertices.insert(mesh.cells[f.neighbor].vertices.end(),f.vertices.begin(),f.vertices.end());
        }
        mesh.faces.push_back(std::move(f));
    }
    for (auto& cell : mesh.cells) {
        std::sort(cell.vertices.begin(),cell.vertices.end());
        cell.vertices.erase(std::unique(cell.vertices.begin(),cell.vertices.end()),cell.vertices.end());
    }
    finish_geometry(mesh);
    return mesh;
}
} // namespace detail

Mesh make_box2d(Index nx,Index ny,double lx,double ly) {
    using namespace detail;
    if (nx==0 || ny==0 || !std::isfinite(lx) || !std::isfinite(ly) || lx<=0 || ly<=0)
        throw std::invalid_argument("box2: positive resolution and lengths required");
    checked_index(checked_product({static_cast<std::size_t>(nx)+1,static_cast<std::size_t>(ny)+1}));
    checked_index(checked_product({nx,ny}));
    std::vector<Vec3> points;
    points.reserve(checked_product({static_cast<std::size_t>(nx)+1,static_cast<std::size_t>(ny)+1}));
    auto vertex=[nx](Index i,Index j) { return j*(nx+1)+i; };
    for (Index j=0;j<=ny;++j) for (Index i=0;i<=nx;++i)
        points.push_back({lx*static_cast<double>(i)/nx,ly*static_cast<double>(j)/ny,0});
    std::vector<RawCell> cells;
    cells.reserve(checked_product({nx,ny}));
    for (Index j=0;j<ny;++j) for (Index i=0;i<nx;++i)
        cells.push_back({{vertex(i,j),vertex(i+1,j),vertex(i+1,j+1),vertex(i,j+1)},9});
    auto mesh=assemble_elements(2,std::move(points),cells,{},
                                {{"default"},{"xmin"},{"xmax"},{"ymin"},{"ymax"}});
    for (auto& f:mesh.faces) if (f.neighbor==no_index) {
        const double tol=1e-10*std::max(lx,ly);
        if (std::abs(f.centroid.x)<tol) f.patch=1;
        else if (std::abs(f.centroid.x-lx)<tol) f.patch=2;
        else if (std::abs(f.centroid.y)<tol) f.patch=3;
        else if (std::abs(f.centroid.y-ly)<tol) f.patch=4;
        else throw std::runtime_error("unexpected box2 boundary face");
    }
    return mesh;
}
Mesh make_box3d(Index nx,Index ny,Index nz,double lx,double ly,double lz) {
    using namespace detail;
    if (!nx || !ny || !nz || !std::isfinite(lx) || !std::isfinite(ly) ||
        !std::isfinite(lz) || lx<=0 || ly<=0 || lz<=0)
        throw std::invalid_argument("box3: positive resolution and lengths required");
    checked_index(checked_product({static_cast<std::size_t>(nx)+1,
                                   static_cast<std::size_t>(ny)+1,static_cast<std::size_t>(nz)+1}));
    checked_index(checked_product({nx,ny,nz}));
    auto vertex=[nx,ny](Index i,Index j,Index k) {
        return (static_cast<std::size_t>(k)*(ny+1)+j)*(nx+1)+i;
    };
    std::vector<Vec3> points;
    points.reserve(checked_product({static_cast<std::size_t>(nx)+1,
                                    static_cast<std::size_t>(ny)+1,static_cast<std::size_t>(nz)+1}));
    for (Index k=0;k<=nz;++k) for (Index j=0;j<=ny;++j) for (Index i=0;i<=nx;++i)
        points.push_back({lx*static_cast<double>(i)/nx,ly*static_cast<double>(j)/ny,
                          lz*static_cast<double>(k)/nz});
    std::vector<RawCell> cells;
    cells.reserve(checked_product({nx,ny,nz}));
    for (Index k=0;k<nz;++k) for (Index j=0;j<ny;++j) for (Index i=0;i<nx;++i) {
        std::vector<Index> v;
        for (auto [a,b,c]:{std::array<Index,3>{i,j,k}, {i+1,j,k}, {i+1,j+1,k}, {i,j+1,k},
                          {i,j,k+1}, {i+1,j,k+1}, {i+1,j+1,k+1}, {i,j+1,k+1}})
            v.push_back(checked_index(vertex(a,b,c)));
        cells.push_back({std::move(v),12});
    }
    auto mesh=assemble_elements(3,std::move(points),cells,{},
                                {{"default"},{"xmin"},{"xmax"},{"ymin"},{"ymax"},
                                 {"zmin"},{"zmax"}});
    for (auto& f:mesh.faces) if (f.neighbor==no_index) {
        const double tol=1e-10*std::max({lx,ly,lz});
        if (std::abs(f.centroid.x)<tol) f.patch=1;
        else if (std::abs(f.centroid.x-lx)<tol) f.patch=2;
        else if (std::abs(f.centroid.y)<tol) f.patch=3;
        else if (std::abs(f.centroid.y-ly)<tol) f.patch=4;
        else if (std::abs(f.centroid.z)<tol) f.patch=5;
        else if (std::abs(f.centroid.z-lz)<tol) f.patch=6;
        else throw std::runtime_error("unexpected box3 boundary face");
    }
    return mesh;
}

Graph make_graph(const Mesh& mesh) {
    Graph g;
    g.face_owner.reserve(mesh.faces.size()); g.face_neighbor.reserve(mesh.faces.size());
    g.neighbor_offsets.assign(mesh.cells.size()+1,0);
    for (const Face& f:mesh.faces) {
        g.face_owner.push_back(f.owner); g.face_neighbor.push_back(f.neighbor);
        if (f.neighbor!=no_index) {
            ++g.neighbor_offsets[static_cast<std::size_t>(f.owner)+1];
            ++g.neighbor_offsets[static_cast<std::size_t>(f.neighbor)+1];
        }
    }
    std::partial_sum(g.neighbor_offsets.begin(),g.neighbor_offsets.end(),g.neighbor_offsets.begin());
    g.neighbor_ids.resize(g.neighbor_offsets.back());
    g.neighbor_face_ids.resize(g.neighbor_offsets.back());
    auto current=g.neighbor_offsets;
    for (std::size_t fi=0;fi<mesh.faces.size();++fi) {
        const Face& f=mesh.faces[fi];
        if (f.neighbor==no_index) continue;
        auto i=current[f.owner]++,j=current[f.neighbor]++;
        g.neighbor_ids[i]=f.neighbor; g.neighbor_face_ids[i]=detail::checked_index(fi);
        g.neighbor_ids[j]=f.owner; g.neighbor_face_ids[j]=detail::checked_index(fi);
    }
    return g;
}
std::vector<double> scatter_face_flux(const Mesh& mesh,std::span<const double> flux,bool divide_by_volume) {
    if (flux.size()!=mesh.faces.size()) throw std::invalid_argument("one flux required for every face");
    std::vector<double> result(mesh.cells.size(),0.0);
    for (std::size_t fi=0;fi<flux.size();++fi) {
        if (!std::isfinite(flux[fi])) throw std::invalid_argument("non-finite face flux");
        const Face& f=mesh.faces[fi];
        result[f.owner]+=flux[fi];
        if (f.neighbor!=no_index) result[f.neighbor]-=flux[fi];
    }
    if (divide_by_volume) for (std::size_t ci=0;ci<result.size();++ci)
        result[ci]/=mesh.cells[ci].volume;
    return result;
}

Region Region::box(Vec3 low,Vec3 high,int dim) {
    if (dim!=2 && dim!=3) throw std::invalid_argument("box dimension must be 2 or 3");
    for (int axis=0;axis<dim;++axis)
        if (!(low[static_cast<std::size_t>(axis)]<high[static_cast<std::size_t>(axis)]) ||
            !std::isfinite(low[static_cast<std::size_t>(axis)]) ||
            !std::isfinite(high[static_cast<std::size_t>(axis)]))
            throw std::invalid_argument("box core requires finite increasing bounds");
    Region region; region.kind=Kind::box; region.low=low; region.high=high;
    return region;
}
Region Region::sphere(Vec3 center,double radius,int dim) {
    if ((dim!=2 && dim!=3) || !detail::finite(center) || !std::isfinite(radius) || radius<=0)
        throw std::invalid_argument("core sphere requires finite center and positive radius");
    Region region; region.kind=Kind::sphere; region.low=center; region.radius=radius;
    return region;
}
double Region::signed_distance(Vec3 p,int dim) const {
    if (kind==Kind::sphere) {
        if (dim==2) p.z=low.z;
        return norm(p-low)-radius;
    }
    double outside=0, inside=std::numeric_limits<double>::infinity();
    for (int a=0;a<dim;++a) {
        const double lo=low[static_cast<std::size_t>(a)],hi=high[static_cast<std::size_t>(a)];
        const double x=p[static_cast<std::size_t>(a)];
        const double gap=std::max({lo-x,x-hi,0.0});
        outside+=gap*gap;
        inside=std::min(inside,std::min(x-lo,hi-x));
    }
    return outside>0 ? std::sqrt(outside) : -inside;
}
bool Region::overlaps(Vec3 a,Vec3 b,double padding,int dim) const {
    if (kind==Kind::sphere) {
        double dist2=0;
        for (int i=0;i<dim;++i) {
            const std::size_t j=static_cast<std::size_t>(i);
            const double gap=std::max({a[j]-low[j],low[j]-b[j],0.0});
            dist2+=gap*gap;
        }
        return dist2 <= (radius+padding)*(radius+padding);
    }
    double dist2=0;
    for (int i=0;i<dim;++i) {
        const std::size_t j=static_cast<std::size_t>(i);
        const double gap=std::max({low[j]-b[j],a[j]-high[j],0.0});
        dist2+=gap*gap;
    }
    return dist2 <= padding*padding;
}

bool adapt_box_to_interface(Region& core,std::span<const Vec3> points,const Mesh& mesh,
                            double margin,double step) {
    if (core.kind!=Region::Kind::box || points.empty() || !std::isfinite(margin) || margin<0 ||
        !std::isfinite(step) || step<=0) throw std::invalid_argument("invalid adaptive box request");
    Vec3 low{INFINITY,INFINITY,INFINITY},high{-INFINITY,-INFINITY,-INFINITY};
    for (Vec3 p:points) {
        if (!detail::finite(p)) throw std::invalid_argument("non-finite interface point");
        for (int a=0;a<mesh.dimension;++a) {
            const std::size_t j=static_cast<std::size_t>(a);
            if (p[j]<mesh.bounds_min[j]-1e-9 || p[j]>mesh.bounds_max[j]+1e-9)
                throw std::invalid_argument("interface point outside the FVM mesh");
        }
        low=detail::minimum(low,p); high=detail::maximum(high,p);
    }
    bool changed=false;
    for (int a=0;a<mesh.dimension;++a) {
        const std::size_t j=static_cast<std::size_t>(a);
        const double lo=std::max(mesh.bounds_min[j],low[j]-margin);
        const double hi=std::min(mesh.bounds_max[j],high[j]+margin);
        // At most one step per call. A full step constitutes the hysteresis threshold.
        if (lo>core.low[j]+step) { core.low[j]=std::min(core.low[j]+step,lo);changed=true; }
        else if (lo<core.low[j]-step) { core.low[j]=std::max(core.low[j]-step,lo);changed=true; }
        if (hi>core.high[j]+step) { core.high[j]=std::min(core.high[j]+step,hi);changed=true; }
        else if (hi<core.high[j]-step) { core.high[j]=std::max(core.high[j]-step,hi);changed=true; }
        core.low[j]=detail::clamp(core.low[j],mesh.bounds_min[j],mesh.bounds_max[j]);
        core.high[j]=detail::clamp(core.high[j],mesh.bounds_min[j],mesh.bounds_max[j]);
        if (!(core.low[j]<core.high[j]))
            throw std::runtime_error("adaptive core collapsed: reduce step or increase margin");
    }
    return changed;
}

} // namespace hybridmesh
