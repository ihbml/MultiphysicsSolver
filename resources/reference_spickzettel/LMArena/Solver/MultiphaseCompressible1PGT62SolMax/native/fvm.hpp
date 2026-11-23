#pragma once

#include "hybrid.hpp"
#include <stdexcept>
#include <utility>

namespace cb {

enum class Boundary { Open, Slip };
struct Cell { V x{}; double volume{}; };
struct Face {
    int l{}, r{-1}; V n{}, dl{}, dr{}; double area{}; Boundary bc{Boundary::Open};
};
struct Mesh {
    int dim{3}, nx{}, ny{}; double lx{}, ly{};
    std::vector<Cell> cells;
    std::vector<Face> faces;
};
inline Mesh box(int nx, int ny, double lx, double ly, bool periodic=false) {
    Mesh m{ny==1?1:2,nx,ny,lx,ly,{},{}};
    const double dx=lx/nx, dy=ly/ny;
    for (int j=0;j<ny;++j) for (int i=0;i<nx;++i)
        m.cells.push_back({{(i+.5)*dx,(j+.5)*dy,0},dx*dy});
    const V x{1,0,0}, y{0,1,0}, hx=x*(.5*dx), hy=y*(.5*dy);
    for (int j=0;j<ny;++j) for (int i=0;i<nx;++i) {
        const int k=j*nx+i;
        if (i>0) m.faces.push_back({k-1,k,x,hx,-hx,dy});
        if (j>0) m.faces.push_back({k-nx,k,y,hy,-hy,dx});
        if (i==0&&!periodic) m.faces.push_back({k,-1,-x,-hx,{},dy});
        if (i==nx-1) m.faces.push_back({k,periodic?j*nx:-1,x,hx,-hx,dy});
        if (ny>1&&j==0) m.faces.push_back({k,-1,-y,-hy,{},dx,Boundary::Slip});
        if (ny>1&&j==ny-1) m.faces.push_back({k,-1,y,hy,{},dx,Boundary::Slip});
    }
    return m;
}

struct Entry { int j; V d, coefficient; };
using Stencils=std::vector<std::vector<Entry>>;
struct Linear { Fields lo{}, hi{}; std::array<V,8> g{}; };
inline void correct_stencil(std::vector<Entry>& entries, int dims) {
    Mat m{}, inv{};
    for (const auto& j:entries) m.outer(j.d,j.coefficient);
    if (!m.inverse(inv,dims)) { entries.clear(); return; }
    for (auto& j:entries) j.coefficient=inv.transposed(j.coefficient);
}
inline Stencils least_squares(const Mesh& mesh) {
    Stencils list(mesh.cells.size());
    for (const auto& f:mesh.faces) if (f.r>=0) {
        const V d=f.dl-f.dr; const double r2=dot(d,d);
        if (!(r2>0)) throw std::invalid_argument("coincident mesh centers");
        list[f.l].push_back({f.r,d,d/r2}); list[f.r].push_back({f.l,-d,-d/r2});
    }
    for (auto& entries:list) correct_stencil(entries,mesh.dim);
    return list;
}
inline Stencils kernel_grid(const Mesh& m, double h) {
    Stencils stencils(m.cells.size());
    for (int y=0;y<m.ny;++y) for (int x=0;x<m.nx;++x) {
        const int i=y*m.nx+x;
        for (int v=-2;v<=2;++v) for (int u=-2;u<=2;++u) {
            if ((!v&&!u)||x+u<0||x+u>=m.nx||y+v<0||y+v>=m.ny) continue;
            const int j=(y+v)*m.nx+x+u;
            const V d=m.cells[j].x-m.cells[i].x;
            if (dot(d,d)>=h*h) continue;
            stencils[i].push_back({j,d,wendland_gradient(d,h,2)*m.cells[j].volume});
        }
        correct_stencil(stencils[i],2);
    }
    return stencils;
}
inline Linear reconstruct_gradient(const Fields* q, std::size_t i, const std::vector<Entry>& stencil) {
    Linear l{};
    for (const auto& entry:stencil) for (int k=0;k<8;++k) {
        const double d=q[entry.j][k]-q[i][k];
        l.g[k]+=entry.coefficient*d;
        l.lo[k]=smin(d,l.lo[k],1.e-12); l.hi[k]=smax(d,l.hi[k],1.e-12);
    }
    return l;
}
inline Fields trace(const Fields& base, const Linear& linear, V d, double eps2) {
    auto q=base;
    for (int k=0;k<8;++k) {
        const double dv=dot(linear.g[k],d);
        q[k]+=dv*venkat(dv,linear.lo[k],linear.hi[k],eps2*(1.e-6+sq(base[k])));
    }
    return q;
}

struct Patch {
    V center{}, shift{};
    double area{}, radius{};
    int com{}, trn{}, imt{};
};
struct Account { long double mass{}, energy{}; };
struct Options { Scheme scheme{Scheme::AllMach}; double cfl{.42}; bool hybrid{true}, cavitation{true}; };
struct FaceTask { Q l, r; V n; };
using FluxBatch=void (*)(Scheme,std::span<const FaceTask>,std::span<Flux>);
inline FluxBatch face_flux_batch=nullptr;

class Flow {
    Stencils wls_, kernel_;
    std::vector<Q> q_;
    std::vector<Fields> primitive_, cloud_;
    std::vector<Linear> fvm_, sph_;
    std::vector<double> epsilon_, weights_;
    std::vector<U> stage_, rate_, new_;
    std::vector<FaceTask> tasks_;
    std::vector<Flux> gpu_results_;
    bool closed_{};
    double spacing_{};
    V original_center_{};

    Q face_value(int i, V d, bool second) const {
        if (!second) return q_[i];
        auto b=trace(primitive_[i],fvm_[i],d,epsilon_[i]);
        if (weights_[i]>0) {
            const auto p=trace(cloud_[i],sph_[i],d-patch.shift,epsilon_[i]);
            for (int k=0;k<8;++k) b[k]+=(p[k]-b[k])*weights_[i];
        }
        auto p=primitive(material,b[0],{b[1],b[2],b[3]},b[4],clip(b[5],0.,1.),clip(b[6],0.,1.));
        if (!p.ok) return q_[i];
        if (weights_[i]>0) {
            const auto u=conserved(p);
            auto ue=u;
            ue.E=b[7];
            const auto pe=decode(material,ue);
            // At a material discontinuity, preserve the pressure trace rather
            // than imposing incompatible independently interpolated energy.
            if (pe.ok && ::fabs(pe.p-p.p)<.04*max(::fabs(p.p),1.e-3)) {
                p=primitive(material,p.rho,p.u,p.p+.25*weights_[i]*(pe.p-p.p),p.y,p.a);
            }
        }
        return p.ok ? p : q_[i];
    }

    Account rhs(const std::vector<U>& u, bool second, Scheme scheme) {
        for (std::size_t i=0;i<u.size();++i) { q_[i]=decode(material,u[i]); rate_[i]={}; }
        if (second) {
            for (std::size_t i=0;i<u.size();++i) primitive_[i]=values(q_[i]);
            for (std::size_t i=0;i<u.size();++i) fvm_[i]=reconstruct_gradient(primitive_.data(),i,wls_[i]);
            if (patch.com || patch.trn) {
                for (std::size_t i=0;i<u.size();++i) cloud_[i]=weights_[i]>=0
                    ? trace(primitive_[i],reconstruct_gradient(primitive_.data(),i,kernel_[i]),patch.shift,epsilon_[i]) : primitive_[i];
                for (std::size_t i=0;i<u.size();++i) if (weights_[i]>0)
                    sph_[i]=reconstruct_gradient(cloud_.data(),i,kernel_[i]);
            }
        }
        const auto assemble=[&](const Face& f) {
            const auto l=face_value(f.l,f.dl,second);
            auto r=f.r>=0 ? face_value(f.r,f.dr,second) : l;
            if (f.r<0&&f.bc==Boundary::Slip) r.u-=f.n*(2*dot(l.u,f.n));
            return FaceTask{l,r,f.n};
        };
        if (face_flux_batch) {
            tasks_.resize(mesh.faces.size()); gpu_results_.resize(mesh.faces.size());
            for (std::size_t i=0;i<tasks_.size();++i) tasks_[i]=assemble(mesh.faces[i]);
            face_flux_batch(scheme,tasks_,gpu_results_);
        }
        Account boundary{};
        for (std::size_t i=0;i<mesh.faces.size();++i) {
            const auto& f=mesh.faces[i];
            const auto F=face_flux_batch ? gpu_results_[i] : [&]{ const auto a=assemble(f); return flux(scheme,a.l,a.r,a.n); }();
            const U a=F.f*f.area;
            const double vl=1./mesh.cells[f.l].volume;
            rate_[f.l]=rate_[f.l]-a*vl;
            rate_[f.l].a+=q_[f.l].a*F.v*f.area*vl;
            if (f.r>=0) {
                const double vr=1./mesh.cells[f.r].volume;
                rate_[f.r]=rate_[f.r]+a*vr;
                rate_[f.r].a-=q_[f.r].a*F.v*f.area*vr;
            } else { boundary.mass-=a.rho; boundary.energy-=a.E; }
        }
        return boundary;
    }

    bool valid(std::vector<U>& v) const {
        for (auto& u:v) {
            // The volume fraction is nonconservative; mass is never clipped.
            u.a=volume_fraction(material,u.rho,u.gas,u.a);
            if (!decode(material,u).ok) return false;
        }
        return true;
    }

public:
    Mesh mesh; Material material; Options options; Cavitation nuclei{};
    std::vector<U> state;
    Patch patch{};
    Account initial{}, exchanged{};
    double t{}, dt{};
    int steps{}, repairs{};

    Flow(Mesh m, Material material_, Options opts, std::vector<U> ic) : mesh(std::move(m)),
        material(material_), options(opts), state(std::move(ic)) {
        const auto n=mesh.cells.size();
        if (state.size()!=n||n==0||mesh.dim<1||mesh.dim>3||!valid(state)||!(options.cfl>0.))
            throw std::invalid_argument("invalid flow initialization");
        for (const auto& cell:mesh.cells) if (!(cell.volume>0)) throw std::invalid_argument("invalid cell measure");
        for (const auto& f:mesh.faces) {
            if (f.l<0||static_cast<std::size_t>(f.l)>=n||f.r<-1||(f.r>=0&&static_cast<std::size_t>(f.r)>=n)
                || !(f.area>0.) || !(::fabs(dot(f.n,f.n)-1)<1.e-8)) throw std::invalid_argument("invalid face geometry");
        }
        wls_=least_squares(mesh);
        q_.resize(n); primitive_.resize(n); cloud_.resize(n); fvm_.resize(n); sph_.resize(n);
        rate_.resize(n); stage_.resize(n); new_.resize(n); epsilon_.resize(n); weights_.resize(n);
        closed_=mesh.nx>0&&mesh.ny>0;
        spacing_=closed_ ? mesh.lx/mesh.nx : 0.;
        if (options.hybrid&&material.mixture&&mesh.ny>1) kernel_=kernel_grid(mesh,2.12*spacing_);
        for (std::size_t i=0;i<n;++i) {
            const double h=mesh.dim==1&&closed_ ? spacing_ : std::pow(mesh.cells[i].volume,1./mesh.dim);
            epsilon_[i]=std::pow(.35*h,3);
        }
        initial=integrals();
        update_patch();
    }

    Account integrals() const {
        Account out{};
        for (std::size_t i=0;i<state.size();++i) {
            const long double v=mesh.cells[i].volume;
            out.mass+=v*state[i].rho; out.energy+=v*state[i].E;
        }
        return out;
    }

    void update_patch() {
        if (!material.mixture) return;
        Patch next{};
        for (std::size_t i=0;i<state.size();++i) {
            const double a=state[i].a, amount=a*a*mesh.cells[i].volume;
            next.area+=amount;
            next.center+=mesh.cells[i].x*amount;
        }
        if (next.area<=1.e-14) { patch={}; return; }
        next.center=next.center/next.area;
        next.radius=::sqrt(next.area/3.14159265358979323846);
        if (!kernel_.empty()) {
            if (steps==0) original_center_=next.center;
            const V dx=(next.center-original_center_)/spacing_;
            next.shift=V{dx.x-::round(dx.x),dx.y-::round(dx.y),0}*spacing_;
            const double r=max(.25,next.radius*1.40);
            for (std::size_t i=0;i<state.size();++i) {
                const double d=norm(mesh.cells[i].x-next.center);
                const double fade=clip((d-r)/.14,0.,1.);
                weights_[i]=.78*(1.-fade*fade*(3-2*fade));
                if (d<r) ++next.com;
                else if (d<r+.14) ++next.trn;
                else if (d<r+.14+3*spacing_) { ++next.imt; weights_[i]=0.; }
                else weights_[i]=-1.;
            }
        }
        patch=next;
    }

    void step(double stop) {
        if (stop<=t) return;
        double inverse=0., mach=0.;
        for (std::size_t i=0;i<state.size();++i) {
            q_[i]=decode(material,state[i]);
            if (options.scheme==Scheme::Ausm) mach=max(mach,norm(q_[i].u)/q_[i].c);
        }
        if (closed_) {
            for (const auto& q:q_) inverse=max(inverse,(::fabs(q.u.x)+q.c)/spacing_
                +(mesh.dim==2 ? (::fabs(q.u.y)+q.c)/(mesh.ly/mesh.ny) : 0.));
        } else {
            std::vector<double> s(state.size());
            for (const auto& f:mesh.faces) {
                s[f.l]+=f.area*(::fabs(dot(q_[f.l].u,f.n))+q_[f.l].c);
                if (f.r>=0) s[f.r]+=f.area*(::fabs(dot(q_[f.r].u,f.n))+q_[f.r].c);
            }
            for (std::size_t i=0;i<s.size();++i) inverse=max(inverse,.5*s[i]/mesh.cells[i].volume);
        }
        const double safety=options.scheme==Scheme::Ausm ? .35+.19*clip(mach/.5,0.,1.) : 1.;
        dt=min(stop-t,options.cfl*safety/inverse);
        for (int attempt=0;attempt<14;++attempt) {
            const bool second=attempt==0;
            const auto scheme=second ? options.scheme : Scheme::Hlle;
            auto a=rhs(state,second,scheme);
            for (std::size_t i=0;i<state.size();++i) stage_[i]=state[i]+rate_[i]*dt;
            if (valid(stage_)) {
                auto b=rhs(stage_,second,scheme);
                for (std::size_t i=0;i<state.size();++i) new_[i]=(state[i]+stage_[i]+rate_[i]*dt)*.5;
                if (valid(new_)) {
                    if (options.cavitation&&material.mixture) for (auto& u:new_) u=phase_change(material,nuclei,u,dt);
                    exchanged.mass+=(a.mass+b.mass)*(.5*dt);
                    exchanged.energy+=(a.energy+b.energy)*(.5*dt);
                    t+=dt; ++steps; state.swap(new_); update_patch(); return;
                }
            }
            ++repairs;
            if (attempt>0) dt*=.5;
        }
        throw std::runtime_error("could not find an admissible step");
    }
    void advance(double target) {
        while (t+2.e-14<target) step(target);
    }
};

} // namespace cb