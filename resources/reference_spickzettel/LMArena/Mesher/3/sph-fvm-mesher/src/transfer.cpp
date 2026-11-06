#include "internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace hybridmesh {
namespace {
using namespace detail;

// Global multiplicative factors cancel out in the corrected-gradient moment.
double moment_weight(double distance,double cutoff,double neighbor_volume) {
    if (distance>=cutoff) return 0;
    const double t=1.0-distance/cutoff;
    return neighbor_volume*t*t*t;
}
std::array<double,9> corrected_moment(const ParticleSet& set,std::size_t i,
                                      int dim,bool& valid) {
    std::array<double,16> moment{},inv{};
    const Vec3 x=set.particles[i].position;
    for (std::size_t e=set.neighbor_offsets[i];e<set.neighbor_offsets[i+1];++e) {
        const auto& neighbor=set.particles[set.neighbor_ids[e]];
        const Vec3 dx=neighbor.position-x;
        const double w=moment_weight(norm(dx),set.support_radius,neighbor.volume);
        for (int a=0;a<dim;++a) for (int b=0;b<dim;++b)
            moment[static_cast<std::size_t>(a*4+b)]+=w*dx[static_cast<std::size_t>(a)]*
                                                     dx[static_cast<std::size_t>(b)];
    }
    valid=inverse(moment,dim,inv);
    std::array<double,9> output{};
    if (valid) for (int a=0;a<dim;++a) for (int b=0;b<dim;++b)
        output[static_cast<std::size_t>(a*3+b)]=inv[static_cast<std::size_t>(a*4+b)];
    return output;
}
std::vector<double> affine_weights(Vec3 x,const Mesh& mesh,const std::vector<Index>& ids,
                                   double scale,int dim,bool& fallback) {
    const int n=dim+1;
    std::array<double,16> matrix{},inverse_matrix{};
    std::vector<double> bases(ids.size());
    for (std::size_t i=0;i<ids.size();++i) {
        const Vec3 delta=(mesh.cells[ids[i]].centroid-x)/scale;
        std::array<double,4> features{1.0,delta.x,delta.y,delta.z};
        const double w=1.0/(0.04+norm2(delta));
        bases[i]=w;
        for (int a=0;a<n;++a) for (int b=0;b<n;++b)
            matrix[static_cast<std::size_t>(a*4+b)]+=w*features[static_cast<std::size_t>(a)]*
                                                     features[static_cast<std::size_t>(b)];
    }
    std::vector<double> result(ids.size(),0);
    if (inverse(matrix,n,inverse_matrix)) {
        double max_weight=0;
        for (std::size_t i=0;i<ids.size();++i) {
            const Vec3 d=(mesh.cells[ids[i]].centroid-x)/scale;
            const std::array<double,4> features{1.0,d.x,d.y,d.z};
            double value=0;
            for (int a=0;a<n;++a)
                value+=features[static_cast<std::size_t>(a)]*
                       inverse_matrix[static_cast<std::size_t>(a*4)]; // M^-1 e_0
            result[i]=bases[i]*value;
            max_weight=std::max(max_weight,std::abs(result[i]));
        }
        if (max_weight<=50 && std::all_of(result.begin(),result.end(),
                                          [](double w) {return std::isfinite(w);})) {
            fallback=false;
            return result;
        }
    }
    fallback=true;
    double total=0;
    for (std::size_t i=0;i<ids.size();++i) total+=bases[i];
    for (std::size_t i=0;i<ids.size();++i) result[i]=bases[i]/total;
    return result;
}
} // anonymous namespace

TransferPlan make_transfer_plan(const Mesh& mesh,const ParticleSet& set) {
    if (set.cell_roles.size()!=mesh.cells.size() || set.particles.empty() ||
        set.neighbor_offsets.size()!=set.particles.size()+1 ||
        set.cell_particle_offsets.size()!=mesh.cells.size()+1)
        throw std::invalid_argument("particle/mesh data are inconsistent");
    TransferPlan plan;
    const int dim=mesh.dimension;
    const auto count=set.particles.size();
    plan.inverse_moments.resize(count);
    plan.valid_moments.resize(count,0);
    #ifdef _OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (std::int64_t i=0;i<static_cast<std::int64_t>(count);++i) {
        if (set.particles[static_cast<std::size_t>(i)].role!=ParticleRole::common) continue;
        bool valid=false;
        plan.inverse_moments[static_cast<std::size_t>(i)]=
            corrected_moment(set,static_cast<std::size_t>(i),dim,valid);
        plan.valid_moments[static_cast<std::size_t>(i)]=static_cast<std::uint8_t>(valid);
    }
    std::vector<Index> common_particles;
    std::vector<Vec3> positions;
    positions.reserve(count);
    for (std::size_t i=0;i<count;++i) {
        positions.push_back(set.particles[i].position);
        if (set.particles[i].role==ParticleRole::common) {
            common_particles.push_back(detail::checked_index(i));
            if (!plan.valid_moments[i]) ++plan.singular_moments;
        }
    }
    std::vector<Index> common_cells;
    std::vector<Vec3> cell_centers;
    cell_centers.reserve(mesh.cells.size());
    for (std::size_t ci=0;ci<mesh.cells.size();++ci) {
        cell_centers.push_back(mesh.cells[ci].centroid);
        if (set.cell_roles[ci]==MeshRole::common)
            common_cells.push_back(detail::checked_index(ci));
    }
    plan.p2m_offsets.reserve(mesh.cells.size()+1);
    plan.p2m_offsets.push_back(0);
    if (!common_particles.empty()) {
        detail::KDTree particle_tree(positions,common_particles,dim);
        for (std::size_t ci=0;ci<mesh.cells.size();++ci) {
            if (set.cell_roles[ci]==MeshRole::interpolation) {
                std::size_t start=plan.p2m_particles.size();
                for (std::size_t k=set.cell_particle_offsets[ci];
                     k<set.cell_particle_offsets[ci+1];++k) {
                    const Index id=set.cell_particle_ids[k];
                    if (set.particles[id].role==ParticleRole::common)
                        plan.p2m_particles.push_back(id);
                }
                if (start==plan.p2m_particles.size()) {
                    ++plan.p2m_fallbacks;
                    auto nearest=particle_tree.nearest(mesh.cells[ci].centroid,
                                                       static_cast<std::size_t>(dim==2 ? 8 : 16));
                    plan.p2m_particles.insert(plan.p2m_particles.end(),nearest.begin(),nearest.end());
                }
            }
            plan.p2m_offsets.push_back(plan.p2m_particles.size());
        }
    } else {
        for (MeshRole role:set.cell_roles)
            if (role==MeshRole::interpolation)
                throw std::runtime_error("interpolation meshes require common SPH particles");
        plan.p2m_offsets.resize(mesh.cells.size()+1,0);
    }
    plan.m2p_offsets.reserve(count+1);
    plan.m2p_offsets.push_back(0);
    bool has_halo=false;
    for (const auto& p:set.particles) if (p.role!=ParticleRole::common) has_halo=true;
    if (has_halo && common_cells.empty())
        throw std::runtime_error("halo particles require at least one common FVM cell; "
                                 "shrink core or enlarge FVM domain");
    if (!common_cells.empty()) {
        detail::KDTree cell_tree(cell_centers,common_cells,dim);
        for (const auto& p:set.particles) {
            if (p.role!=ParticleRole::common) {
                const auto nearest=cell_tree.nearest(p.position,
                                  static_cast<std::size_t>(dim==2 ? 8 : 16));
                bool fallback=false;
                const auto weights=affine_weights(p.position,mesh,nearest,set.spacing,dim,fallback);
                plan.m2p_fallbacks+=static_cast<std::size_t>(fallback);
                plan.m2p_cells.insert(plan.m2p_cells.end(),nearest.begin(),nearest.end());
                plan.m2p_weights.insert(plan.m2p_weights.end(),weights.begin(),weights.end());
            }
            plan.m2p_offsets.push_back(plan.m2p_cells.size());
        }
    } else plan.m2p_offsets.resize(count+1,0);
    return plan;
}

std::vector<double> particle_to_mesh_opmis(const Mesh& mesh,const ParticleSet& set,
                                           const TransferPlan& plan,
                                           std::span<const double> values) {
    if (values.size()!=set.particles.size() || plan.p2m_offsets.size()!=mesh.cells.size()+1 ||
        plan.inverse_moments.size()!=set.particles.size())
        throw std::invalid_argument("OPMIS input sizes do not match particle set/mesh");
    if (!std::all_of(values.begin(),values.end(),[](double x) {return std::isfinite(x);}))
        throw std::invalid_argument("OPMIS requires finite values on all particles");
    std::vector<Vec3> gradients(set.particles.size());
    #ifdef _OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (std::int64_t i=0;i<static_cast<std::int64_t>(set.particles.size());++i) {
        const auto idx=static_cast<std::size_t>(i);
        if (!plan.valid_moments[idx]) continue; // diagnosed singular: zero-order fallback
        const auto& p=set.particles[idx];
        Vec3 rhs{};
        for (std::size_t n=set.neighbor_offsets[idx];n<set.neighbor_offsets[idx+1];++n) {
            const auto& neighbor=set.particles[set.neighbor_ids[n]];
            const double scalar=values[set.neighbor_ids[n]];
            const Vec3 displacement=neighbor.position-p.position;
            const double weight=moment_weight(norm(displacement),set.support_radius,
                                              neighbor.volume);
            rhs+=displacement*(weight*(scalar-values[idx]));
        }
        const auto& inv=plan.inverse_moments[idx];
        for (int a=0;a<mesh.dimension;++a) {
            double value=0;
            for (int b=0;b<mesh.dimension;++b)
                value+=inv[static_cast<std::size_t>(a*3+b)]*rhs[static_cast<std::size_t>(b)];
            gradients[idx][static_cast<std::size_t>(a)]=value;
        }
    }
    std::vector<double> output(mesh.cells.size(),std::numeric_limits<double>::quiet_NaN());
    #ifdef _OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (std::int64_t ci=0;ci<static_cast<std::int64_t>(mesh.cells.size());++ci) {
        const auto id=static_cast<std::size_t>(ci);
        if (set.cell_roles[id]!=MeshRole::interpolation) continue;
        const auto n=plan.p2m_offsets[id+1]-plan.p2m_offsets[id];
        if (!n) continue;
        double accumulated=0;
        for (std::size_t k=plan.p2m_offsets[id];k<plan.p2m_offsets[id+1];++k) {
            const Index pi=plan.p2m_particles[k];
            accumulated+=values[pi]+dot(mesh.cells[id].centroid-set.particles[pi].position,
                                         gradients[pi]);
        }
        output[id]=accumulated/static_cast<double>(n);
    }
    return output;
}

std::vector<double> mesh_to_halo_particles(const Mesh& mesh,const ParticleSet& set,
                                           const TransferPlan& plan,
                                           std::span<const double> values) {
    if (values.size()!=mesh.cells.size() || plan.m2p_offsets.size()!=set.particles.size()+1)
        throw std::invalid_argument("mesh-to-particle input sizes do not match mesh/particles");
    for (Index cell:plan.m2p_cells)
        if (!std::isfinite(values[cell]))
            throw std::invalid_argument("non-finite common mesh value in M->P interpolation");
    std::vector<double> output(set.particles.size(),std::numeric_limits<double>::quiet_NaN());
    #ifdef _OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (std::int64_t i=0;i<static_cast<std::int64_t>(set.particles.size());++i) {
        const auto id=static_cast<std::size_t>(i);
        if (set.particles[id].role==ParticleRole::common) continue;
        double value=0;
        for (std::size_t k=plan.m2p_offsets[id];k<plan.m2p_offsets[id+1];++k) {
            const Index cell=plan.m2p_cells[k];
            value+=values[cell]*plan.m2p_weights[k];
        }
        output[id]=value;
    }
    return output;
}

double blend_transition(const Particle& particle,double sph_value,double fvm_value,
                        double support_radius) {
    if (particle.role!=ParticleRole::transition || !(support_radius>0))
        throw std::invalid_argument("blend requires a transition particle and positive support radius");
    const double lambda=detail::clamp(1.0-particle.signed_core_distance/support_radius,0,1);
    return lambda*sph_value+(1.0-lambda)*fvm_value;
}

} // namespace hybridmesh
