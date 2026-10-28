#include "internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace hybridmesh {
namespace {
using namespace detail;

struct GridKey {
    std::int64_t x=0,y=0,z=0;
    bool operator==(const GridKey&) const = default;
};
struct GridHash {
    std::size_t operator()(GridKey k) const {
        std::size_t h=0x9e3779b97f4a7c15ull;
        for (std::int64_t x:{k.x,k.y,k.z}) {
            const auto v=std::hash<std::int64_t>{}(x);
            h^=v+0x9e3779b97f4a7c15ull+(h<<6)+(h>>2);
        }
        return h;
    }
};
std::int64_t safe_lattice_index(double value) {
    if (!std::isfinite(value) || std::abs(value)>1e12)
        throw std::invalid_argument("lattice index too large; increase particle spacing");
    return static_cast<std::int64_t>(value);
}
GridKey particle_bucket(Vec3 p,double width,int dim) {
    return {safe_lattice_index(std::floor(p.x/width)),
            safe_lattice_index(std::floor(p.y/width)),
            dim==3 ? safe_lattice_index(std::floor(p.z/width)) : 0};
}
// Points on shared faces are accepted in either cell, then deduplicated by lattice ID.
bool contains(const Mesh& mesh,Index cell_id,Vec3 point,double tolerance) {
    const auto& cell=mesh.cells[cell_id];
    for (Index fi:cell.faces) {
        const auto& face=mesh.faces[fi];
        const double side=(face.owner==cell_id ? 1.0 : -1.0)*
                           dot(face.area_vector,point-face.centroid);
        if (side>tolerance*norm(face.area_vector)) return false;
    }
    return true;
}
void make_neighbors(ParticleSet& set,int dim) {
    const auto& p=set.particles;
    const double cutoff=set.support_radius;
    const double cutoff2=cutoff*cutoff;
    std::unordered_map<GridKey,std::vector<Index>,GridHash> buckets;
    buckets.reserve(p.size());
    for (std::size_t i=0;i<p.size();++i)
        buckets[particle_bucket(p[i].position,cutoff,dim)].push_back(checked_index(i));
    const auto& read_only_buckets=buckets;
    auto visit=[&](std::size_t i,auto&& action) {
        const GridKey c=particle_bucket(p[i].position,cutoff,dim);
        for (int dz=(dim==2 ? 0 : -1);dz<=(dim==2 ? 0 : 1);++dz)
            for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) {
                const auto it=read_only_buckets.find({c.x+dx,c.y+dy,c.z+dz});
                if (it==read_only_buckets.end()) continue;
                for (Index j:it->second) {
                    if (j==i) continue;
                    const double d2=norm2(p[j].position-p[i].position);
                    if (d2>0 && d2<cutoff2*(1.0-1e-12)) action(j);
                }
            }
    };
    set.neighbor_offsets.assign(p.size()+1,0);
    #ifdef _OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (std::int64_t i=0;i<static_cast<std::int64_t>(p.size());++i) {
        std::size_t count=0;
        visit(static_cast<std::size_t>(i),[&](Index) { ++count; });
        set.neighbor_offsets[static_cast<std::size_t>(i)+1]=count;
    }
    std::partial_sum(set.neighbor_offsets.begin(),set.neighbor_offsets.end(),set.neighbor_offsets.begin());
    set.neighbor_ids.resize(set.neighbor_offsets.back());
    #ifdef _OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (std::int64_t i=0;i<static_cast<std::int64_t>(p.size());++i) {
        const auto index=static_cast<std::size_t>(i);
        std::size_t cursor=set.neighbor_offsets[index];
        visit(index,[&](Index j) {set.neighbor_ids[cursor++]=j;});
        std::sort(set.neighbor_ids.begin()+static_cast<std::ptrdiff_t>(set.neighbor_offsets[index]),
                  set.neighbor_ids.begin()+static_cast<std::ptrdiff_t>(cursor));
    }
}
// Gradient wrt x_i for Wendland C2 on r<R=2h:
// W(q)=alpha_d (1-q/2)^4 (2q+1); grad_i W = (5 alpha_d/h^2)(1-r/R)^3 (x_j-x_i).
double kernel_gradient_factor(double r,double support,int dim) {
    if (!(r<support)) return 0;
    const double h=support/2.0;
    const double alpha=dim==2 ? 7.0/(4.0*std::numbers::pi*h*h) :
                                 21.0/(16.0*std::numbers::pi*h*h*h);
    const double one_minus=1.0-r/support;
    return 5.0*alpha/(h*h)*one_minus*one_minus*one_minus;
}
double segment_distance2(Vec3 p,Vec3 a,Vec3 b) {
    const Vec3 ab=b-a;
    const double t=clamp(dot(p-a,ab)/norm2(ab),0.0,1.0);
    return norm2(p-(a+ab*t));
}
double triangle_distance2(Vec3 p,Vec3 a,Vec3 b,Vec3 c) {
    // Closest-point-on-triangle algorithm (all Voronoi regions, including edges).
    const Vec3 ab=b-a,ac=c-a,ap=p-a;
    const double d1=dot(ab,ap),d2=dot(ac,ap);
    if (d1<=0 && d2<=0) return norm2(ap);
    const Vec3 bp=p-b;
    const double d3=dot(ab,bp),d4=dot(ac,bp);
    if (d3>=0 && d4<=d3) return norm2(bp);
    const double vc=d1*d4-d3*d2;
    if (vc<=0 && d1>=0 && d3<=0) {
        const double v=d1/(d1-d3);return norm2(p-(a+ab*v));
    }
    const Vec3 cp=p-c;
    const double d5=dot(ab,cp),d6=dot(ac,cp);
    if (d6>=0 && d5<=d6) return norm2(cp);
    const double vb=d5*d2-d1*d6;
    if (vb<=0 && d2>=0 && d6<=0) {
        const double w=d2/(d2-d6);return norm2(p-(a+ac*w));
    }
    const double va=d3*d6-d5*d4;
    if (va<=0 && (d4-d3)>=0 && (d5-d6)>=0) {
        const double w=(d4-d3)/((d4-d3)+(d5-d6));
        return norm2(p-(b+(c-b)*w));
    }
    const double denom=1.0/(va+vb+vc);
    return norm2(p-(a+ab*(vb*denom)+ac*(vc*denom)));
}

struct BoundaryTree {
    struct Node { Vec3 lo,hi;std::size_t begin=0,end=0;int left=-1,right=-1; };
    const Mesh& mesh;
    std::vector<Index> ids;
    std::vector<Node> nodes;
    int root=-1;
    explicit BoundaryTree(const Mesh& m):mesh(m) {
        for (std::size_t i=0;i<m.faces.size();++i)
            if (m.faces[i].neighbor==no_index) ids.push_back(checked_index(i));
        nodes.reserve(ids.size()*2);root=build(0,ids.size());
    }
    std::pair<Index,double> nearest(Vec3 position,double radius) const {
        Index best=no_index;
        double best2=radius*radius;
        visit(root,position,best,best2);
        return {best,std::sqrt(best2)};
    }
private:
    std::pair<Vec3,Vec3> face_box(Index id) const {
        Vec3 lo{INFINITY,INFINITY,INFINITY},hi{-INFINITY,-INFINITY,-INFINITY};
        for (Index vertex:mesh.faces[id].vertices) {
            lo=minimum(lo,mesh.points[vertex]);hi=maximum(hi,mesh.points[vertex]);
        }
        return {lo,hi};
    }
    int build(std::size_t begin,std::size_t end) {
        if (begin==end) return -1;
        const int id=static_cast<int>(nodes.size());
        Node current;
        current.begin=begin;current.end=end;
        current.lo={INFINITY,INFINITY,INFINITY};current.hi={-INFINITY,-INFINITY,-INFINITY};
        for (std::size_t i=begin;i<end;++i) {
            auto [lo,hi]=face_box(ids[i]);
            current.lo=minimum(current.lo,lo);current.hi=maximum(current.hi,hi);
        }
        nodes.push_back(current);
        if (end-begin>8) {
            const Vec3 extent=current.hi-current.lo;
            int axis=0;
            if (extent.y>extent.x) axis=1;
            if (extent[2]>extent[static_cast<std::size_t>(axis)]) axis=2;
            const std::size_t middle=(begin+end)/2;
            std::nth_element(ids.begin()+static_cast<std::ptrdiff_t>(begin),
                             ids.begin()+static_cast<std::ptrdiff_t>(middle),
                             ids.begin()+static_cast<std::ptrdiff_t>(end),
                             [&](Index a,Index b) {return mesh.faces[a].centroid[static_cast<std::size_t>(axis)] <
                                                         mesh.faces[b].centroid[static_cast<std::size_t>(axis)];});
            const int left=build(begin,middle),right=build(middle,end);
            nodes[static_cast<std::size_t>(id)].left=left;
            nodes[static_cast<std::size_t>(id)].right=right;
        }
        return id;
    }
    static double box_distance2(Vec3 p,Vec3 a,Vec3 b) {
        return norm2(p-Vec3{clamp(p.x,a.x,b.x),clamp(p.y,a.y,b.y),clamp(p.z,a.z,b.z)});
    }
    double face_distance2(Vec3 p,Index id) const {
        const auto& f=mesh.faces[id];
        const Vec3 a=mesh.points[f.vertices[0]];
        if (mesh.dimension==2) return segment_distance2(p,a,mesh.points[f.vertices[1]]);
        double best=INFINITY;
        for (std::size_t i=1;i+1<f.vertices.size();++i)
            best=std::min(best,triangle_distance2(p,a,mesh.points[f.vertices[i]],
                                                 mesh.points[f.vertices[i+1]]));
        return best;
    }
    void visit(int id,Vec3 p,Index& best,double& best2) const {
        if (id<0) return;
        const Node& n=nodes[static_cast<std::size_t>(id)];
        if (box_distance2(p,n.lo,n.hi)>best2) return;
        if (n.left<0) {
            for (std::size_t i=n.begin;i<n.end;++i) {
                const double d2=face_distance2(p,ids[i]);
                if (d2<=best2) {
                    if (d2<best2 || best==no_index || ids[i]<best) {
                        best2=d2;best=ids[i];
                    }
                }
            }
        } else {
            const Node& l=nodes[static_cast<std::size_t>(n.left)];
            const Node& r=nodes[static_cast<std::size_t>(n.right)];
            if (box_distance2(p,l.lo,l.hi)<box_distance2(p,r.lo,r.hi)) {
                visit(n.left,p,best,best2);visit(n.right,p,best,best2);
            } else { visit(n.right,p,best,best2);visit(n.left,p,best,best2); }
        }
    }
};

} // anonymous namespace

ParticleSet discretize(const Mesh& mesh,const Region& core,const Options& opt) {
    if (mesh.dimension!=2 && mesh.dimension!=3)
        throw std::invalid_argument("invalid FVM mesh dimension");
    if (!(opt.spacing>0) || !std::isfinite(opt.spacing) ||
        !(opt.support_ratio>1) || !std::isfinite(opt.support_ratio) ||
        !(opt.mesh_band>0) || !std::isfinite(opt.mesh_band))
        throw std::invalid_argument("spacing, support ratio and mesh band must be finite and positive");
    const int dim=mesh.dimension;
    ParticleSet set;
    set.spacing=opt.spacing;
    set.support_radius=opt.support_ratio*opt.spacing;
    const double outer=2.0*set.support_radius;
    set.cell_roles.reserve(mesh.cells.size());
    for (const auto& c:mesh.cells) {
        const double signed_distance=core.signed_distance(c.centroid,dim);
        set.cell_roles.push_back(signed_distance>0 ? MeshRole::common :
            signed_distance>=-opt.mesh_band*opt.spacing ? MeshRole::interpolation : MeshRole::sleeping);
    }
    const Vec3 origin=mesh.bounds_min;
    std::unordered_set<GridKey,GridHash> visited;
    visited.reserve(std::min<std::size_t>(mesh.cells.size()*4,5000000));
    const double tol=1e-10*opt.spacing;
    for (std::size_t ci=0;ci<mesh.cells.size();++ci) {
        const auto& c=mesh.cells[ci];
        if (!core.overlaps(c.bounds_min,c.bounds_max,outer,dim)) continue;
        std::array<std::int64_t,3> lower{},upper{};
        for (int axis=0;axis<dim;++axis) {
            const std::size_t j=static_cast<std::size_t>(axis);
            lower[j]=safe_lattice_index(std::ceil((c.bounds_min[j]-origin[j])/opt.spacing-0.5-1e-10));
            upper[j]=safe_lattice_index(std::floor((c.bounds_max[j]-origin[j])/opt.spacing-0.5+1e-10));
        }
        if (upper[0]>=lower[0] && upper[1]>=lower[1] &&
            (dim==2 || upper[2]>=lower[2])) {
            const auto candidates=checked_product({
                static_cast<std::size_t>(upper[0]-lower[0]+1),
                static_cast<std::size_t>(upper[1]-lower[1]+1),
                dim==3 ? static_cast<std::size_t>(upper[2]-lower[2]+1) : std::size_t{1}});
            if (candidates>20000000)
                throw std::runtime_error("too many candidate lattice sites in a cell; increase spacing");
        }
        std::size_t all_owned=0;
        std::vector<Index> active;
        for (std::int64_t k=(dim==3 ? lower[2] : 0);k<=(dim==3 ? upper[2] : 0);++k)
            for (std::int64_t j=lower[1];j<=upper[1];++j)
                for (std::int64_t i=lower[0];i<=upper[0];++i) {
                    const GridKey key{i,j,k};
                    const Vec3 pos{origin.x+(static_cast<double>(i)+0.5)*opt.spacing,
                                   origin.y+(static_cast<double>(j)+0.5)*opt.spacing,
                                   dim==3 ? origin.z+(static_cast<double>(k)+0.5)*opt.spacing : origin.z};
                    if (!contains(mesh,checked_index(ci),pos,tol)) continue;
                    if (!visited.insert(key).second) continue;
                    ++all_owned;
                    const double sd=core.signed_distance(pos,dim);
                    if (sd>outer+tol) continue;
                    if (set.particles.size()>=20000000)
                        throw std::runtime_error("20 million particle safety limit; increase spacing");
                    Particle p;
                    p.position=pos;
                    p.owner_cell=checked_index(ci);
                    p.signed_core_distance=sd;
                    p.role=sd<=0 ? ParticleRole::common :
                               sd<=set.support_radius ? ParticleRole::transition : ParticleRole::interpolation;
                    active.push_back(checked_index(set.particles.size()));
                    set.particles.push_back(p);
                }
        if (!all_owned && core.signed_distance(c.centroid,dim)<=outer+tol) {
            if (set.particles.size()>=20000000)
                throw std::runtime_error("20 million particle safety limit; increase spacing");
            Particle p;
            p.position=c.centroid;
            p.owner_cell=checked_index(ci);
            p.volume=c.volume;
            p.centroid_fallback=true;
            p.signed_core_distance=core.signed_distance(p.position,dim);
            p.role=p.signed_core_distance<=0 ? ParticleRole::common :
                   p.signed_core_distance<=set.support_radius ? ParticleRole::transition :
                                                              ParticleRole::interpolation;
            set.particles.push_back(p);
        } else if (all_owned) {
            for (Index id:active) set.particles[id].volume=c.volume/static_cast<double>(all_owned);
        }
    }
    if (set.particles.empty())
        throw std::runtime_error("core and its SPH halo contain no particles; adjust spacing/region");
    set.cell_particle_offsets.assign(mesh.cells.size()+1,0);
    for (const auto& p:set.particles) ++set.cell_particle_offsets[p.owner_cell+1];
    std::partial_sum(set.cell_particle_offsets.begin(),set.cell_particle_offsets.end(),
                     set.cell_particle_offsets.begin());
    set.cell_particle_ids.resize(set.particles.size());
    auto offsets=set.cell_particle_offsets;
    for (std::size_t i=0;i<set.particles.size();++i)
        set.cell_particle_ids[offsets[set.particles[i].owner_cell]++]=checked_index(i);
    make_neighbors(set,dim);
    const BoundaryTree boundary(mesh);
    const double threshold=0.75*static_cast<double>(dim);
    #ifdef _OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (std::int64_t i=0;i<static_cast<std::int64_t>(set.particles.size());++i) {
        auto& p=set.particles[static_cast<std::size_t>(i)];
        Vec3 sum{}; double divergence=0;
        for (std::size_t n=set.neighbor_offsets[static_cast<std::size_t>(i)];
             n<set.neighbor_offsets[static_cast<std::size_t>(i)+1];++n) {
            const auto& neighbor=set.particles[set.neighbor_ids[n]];
            const Vec3 dx=neighbor.position-p.position;
            const double factor=kernel_gradient_factor(norm(dx),set.support_radius,dim)*neighbor.volume;
            sum+=dx*factor;
            divergence+=dot(dx,dx)*factor;
        }
        p.position_divergence=divergence;
        // The outer halo is an artificial SPH/FVM overlap, not a physical wall.
        // Only core particles can receive the physical FVM-style closure.
        if (p.role==ParticleRole::common && divergence<threshold) {
            const auto [face,distance]=boundary.nearest(p.position,set.support_radius+tol);
            if (face!=no_index) {
                p.physical_boundary=true;
                p.wall_patch=mesh.faces[face].patch;
                p.wall_distance=distance;
                p.closure_area=sum*(-1.0);
                const double magnitude=norm(sum);
                if (magnitude>0) p.closure_normal=p.closure_area/magnitude;
            }
        }
    }
    return set;
}

} // namespace hybridmesh
