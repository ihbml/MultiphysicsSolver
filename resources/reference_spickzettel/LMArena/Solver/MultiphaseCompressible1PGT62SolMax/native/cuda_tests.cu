#include "cases.hpp"
#include "cuda_api.hpp"
#include <cstdio>

int main() {
    using namespace cb;
    if (!cuda_available()) {
        std::puts("SKIP CUDA device tests: no compatible device/driver is available.");
        return 77;
    }
    try {
        const Material gas{eos(Kind::Ideal),{},false}, mix{eos(Kind::Stiffened),eos(Kind::PengRobinson),true};
        const V n{.6,.8,0};
        std::vector<FaceTask> faces;
        std::vector<SourceTask> local;
        for (int i=0;i<385;++i) {
            const double x=i/385.;
            const auto& m=i%2 ? mix : gas;
            faces.push_back({primitive(m,1.1,{4*x-.35,-.002,.05},.2+x,.062,.2),
                primitive(m,.8,{1.1-x,.035,.001},.07+x,.18,.4),n});
            local.push_back({mix,{},conserved(primitive(mix,.9,{.3,.1,0},.09+x,.045,.23)),
                {.035,{.01,0,0},.035*1.3e5},{{.32,.04,0},{-2.,0,0},{1.,0,0},.001,550.},
                {0,0,1},{x,.03,-.24},{{{.9,.1,.2},{.1,1.1,.1},{.2,.1,.96}}},.0008,x*20+1.,85.e-6});
        }
        bool ok=true;
        double worst=0.;
        const auto measure=[&](double host,double device) {
            const double e=::fabs(host-device)/(1+::fabs(host));
            ok=ok&&cb::finite(e)&&e<5.e-12; worst=cb::max(worst,e);
        };
        for (auto s:{Scheme::AllMach,Scheme::Ausm,Scheme::Hllc,Scheme::Hlle}) {
            std::vector<Flux> device(faces.size()); cuda_flux(s,faces,device);
            for (std::size_t i=0;i<faces.size();++i) {
                const auto h=flux(s,faces[i].l,faces[i].r,n);
                const auto d=device[i];
                measure(h.f.rho,d.f.rho); measure(h.f.E,d.f.E); measure(h.f.gas,d.f.gas); measure(h.f.a,d.f.a); measure(h.v,d.v);
                for (int k=0;k<3;++k) measure(h.f.m[k],d.f.m[k]);
            }
        }
        std::vector<SourceResult> out(local.size()); cuda_sources(local,out);
        for (std::size_t i=0;i<local.size();++i) {
            const auto h=local_sources(local[i]), d=out[i];
            measure(h.state.gas,d.state.gas); measure(h.state.a,d.state.a); measure(h.evaporated,d.evaporated);
            measure(h.film.enthalpy,d.film.enthalpy); measure(h.film.mass,d.film.mass);
            measure(h.ghost.radial_times_volume,d.ghost.radial_times_volume); measure(h.limiter,d.limiter);
            measure(h.impact.we,d.impact.we); measure(h.impact.la,d.impact.la);
            for (int k=0;k<3;++k) {
                measure(h.ghost.direction[k],d.ghost.direction[k]); measure(h.film.momentum[k],d.film.momentum[k]);
                for (int j=0;j<3;++j) measure(h.renormalization.a[k][j],d.renormalization.a[k][j]);
            }
            ok=ok&&h.full_rank==d.full_rank&&h.impact.regime==d.impact.regime;
        }
        auto cpu=flow_case(Case::Sod,Kind::Ideal,{Scheme::AllMach,.42,false,false},120);
        auto gpu=flow_case(Case::Sod,Kind::Ideal,{Scheme::AllMach,.42,false,false},120);
        cpu.advance(.1); face_flux_batch=&cuda_flux; gpu.advance(.1); face_flux_batch=nullptr;
        for (std::size_t i=0;i<cpu.state.size();++i) measure(cpu.state[i].rho,gpu.state[i].rho);
        std::printf("%s CPU/CUDA parity, maximum scaled difference %.4g\n",ok?"PASS":"FAIL",worst);
        return ok?0:1;
    } catch (const std::exception& e) { std::fprintf(stderr,"CUDA test: %s\n",e.what()); return 1; }
}